#include <iostream>
#include <thread>
#include <mutex>
#include <vector>
#include <string>
#include <atomic>
#include <ctime>
#include <chrono>
#include <unistd.h>

//global mutex lock for first lock
std::mutex g_lock;
//native test and set flag
std::atomic_flag tsalock = ATOMIC_FLAG_INIT;

//tsa with backoff
struct lock_tsa_backoff{
    std::atomic_flag tsalock_backoff;
    const int base;
    const int limit;
    const int multiplier;
};
lock_tsa_backoff tsa_b = {ATOMIC_FLAG_INIT,1,2^5,2};

//tick lock
struct tick_lock{
    std::atomic <int> next_ticket=ATOMIC_VAR_INIT(0);
    std::atomic <int> now_serving=ATOMIC_VAR_INIT(0);
    const int base = 1;

    void acquire(){
        int my_ticket =  next_ticket.fetch_add(1,std::memory_order_relaxed);  
        while(true){
            int ns = 
                now_serving.load(std::memory_order_acquire);
                if(ns == my_ticket) break;
        }
    }

    void release(){
        now_serving.fetch_add(1,std::memory_order_release);
    }

    void acquire_b(){
        int my_ticket =  next_ticket.fetch_add
                (1,std::memory_order_relaxed);
        int delay = base*
                (my_ticket -  now_serving.load
                        (std::memory_order_acquire));
        while(delay != 0){
            std::this_thread::sleep_for
                (std::chrono::nanoseconds
                    (delay));
            delay = base*
                (my_ticket -  now_serving.load
                        (std::memory_order_acquire));
        }        
    }


};
tick_lock t_lock;

//mcs lock
struct qnode_m{
    std::atomic <qnode_m*> next;
    std::atomic_bool waiting;
};


struct lock_mcs{

    std::atomic <qnode_m*> tail= ATOMIC_VAR_INIT(NULL);    
    void acquire(qnode_m * p){ 
        p->next = ATOMIC_VAR_INIT(NULL);
        p->waiting = ATOMIC_VAR_INIT(true);
        qnode_m * prev = 
            tail.exchange(p,std::memory_order_acquire);  
        if (prev != NULL){ 
            prev->next.store(p,std::memory_order_relaxed);
            while (p->waiting.load(std::memory_order_acquire)){
            } 
        }
    }

    void release(qnode_m *p){ 
        qnode_m * succ = p->next.load(std::memory_order_relaxed);
        if (succ == NULL){
            qnode_m * temp = p;
            if (tail.compare_exchange_strong(temp,NULL)){ 
                return;
            }
            do{ 
                succ = p->next.load(std::memory_order_relaxed);
            }while (succ == NULL);
        }
        succ->waiting.store(false,std::memory_order_release);
    }
};

lock_mcs m_lock; 
//mcs k42
struct qnode_mk{
    std::atomic <qnode_mk*> tail;
    std::atomic <qnode_mk*> next;
};
qnode_mk * waiting = (qnode_mk *)1;

struct lock_mcsk{

    qnode_mk q = {ATOMIC_VAR_INIT(NULL),ATOMIC_VAR_INIT(NULL)};

    void acquire(){ 
        while(true){
            qnode_mk * prev = 
                q.tail.load(std::memory_order_acquire);
            if (prev == NULL){
                qnode_mk * test = NULL;
                if(q.tail.compare_exchange_strong(test,&q)) 
                    break;
            }else{
                qnode_mk n = 
                    {ATOMIC_VAR_INIT(waiting),
                        ATOMIC_VAR_INIT(NULL)};
                qnode_mk * temp = prev;
                if(q.tail.compare_exchange_strong(temp,&n)){
                    prev->next.store(&n,std::memory_order_relaxed);
                    while (n.tail.load(std::memory_order_acquire) == waiting);
                    qnode_mk * succ = 
                        n.next.load(std::memory_order_relaxed);
                    if(succ == NULL){
                        q.next.store(NULL, std::memory_order_relaxed);
                        qnode_mk * t = &n;
                        if (! q.tail.compare_exchange_strong(t,&q)){
                            while(succ == NULL){ 
                                succ = n.next.load(std::memory_order_acquire);
                            }
                            q.next.store(succ,std::memory_order_relaxed); 
                        }
                        break;
                    }else{
                        q.next.store(succ,std::memory_order_relaxed);
                        break;
                    }
                }
            }
        }
    }
    
    void release(){ 
        qnode_mk * succ = q.next.load(std::memory_order_acquire);
        if (succ == NULL){
            qnode_mk * temp = &q;
            if(q.tail.compare_exchange_strong(temp,NULL)){ 
                return;
            }
            while(succ == NULL){ 
                succ = q.next.load(std::memory_order_acquire);
            }
        }
        succ->tail.store(NULL,std::memory_order_release);
    }
};
lock_mcsk mk_lock; 


//clh lock

struct qnode_c{
    std:: atomic <qnode_c *> prev;
    std::atomic_bool succ_must_wait;
};

struct clh_lock{
    qnode_c dummy
        = {ATOMIC_VAR_INIT(NULL),ATOMIC_VAR_INIT(false)};
    std:: atomic <qnode_c*> tail  = ATOMIC_VAR_INIT(&dummy);
    
    void acquire(qnode_c *p){
        p->succ_must_wait.store(true,std::memory_order_relaxed);
        qnode_c * pred 
            = tail.exchange(p,std::memory_order_acquire);
        
        p->prev.store(pred,std::memory_order_relaxed);
        while(pred->succ_must_wait.load(std::memory_order_acquire));
    }

    void release(qnode_c** p){
        qnode_c * pred = (*p)->prev;
        (*p)->succ_must_wait.store(false, std::memory_order_release);
        *p = pred;
    }
};

clh_lock c_lock;

//clh_lock k42
int tt;
struct qnode_ck{
   std::atomic_bool succ_must_wait;
   int t;
};

qnode_ck* inial_thread_qnodes =  new qnode_ck[200];
qnode_ck** thread_qnode_ptrs = new qnode_ck*[200];

struct lock_clhk
{
    qnode_ck dummy = {ATOMIC_VAR_INIT(false)};
    std::atomic<qnode_ck *> tail = ATOMIC_VAR_INIT(&dummy);
    std::atomic <qnode_ck *> head;
    
    void init_nodes(){
        for(int i = 0; i< tt; i++){
            inial_thread_qnodes[i].t = i;
            thread_qnode_ptrs[i] = &inial_thread_qnodes[i];
        }
    }

    void acquire(int threadnum){      
        qnode_ck* p = thread_qnode_ptrs[threadnum];      
        p->succ_must_wait.store(true,std::memory_order_relaxed);
        qnode_ck * pred = tail.exchange(p,std::memory_order_acquire);
        while (pred->succ_must_wait.load(std::memory_order_acquire)){
        }
        head.store(p,std::memory_order_relaxed);
        thread_qnode_ptrs[threadnum] = pred;
    }

    void release(){
        head.load()->succ_must_wait.store(false,std::memory_order_release);
    }

};

lock_clhk ck_lock;

//thread vector and atomic flag to increase paralle
std::atomic_bool start = ATOMIC_VAR_INIT(false);
std::vector<std::thread> threads;


void counter(int phase, int& count, int i,int threadnum){
    if(phase == 1){
        while(!start.load());
        for (int ind = 0; ind < i; ind++){
            g_lock.lock();
            count++;
            //printf("Thread %d with count %d.\n", threadnum,count);
            g_lock.unlock();
        }
    }else if(phase == 2){
        while(!start.load());
        for (int ind = 0; ind < i; ind++){
            while(tsalock.test_and_set(std::memory_order_acquire));
            //printf("Thread %d with count %d.\n", threadnum,count);
            count++;
            tsalock.clear(std::memory_order_release);
        }      
    }else if(phase == 3){
        while(!start.load());
        for (int ind = 0; ind < i; ind++){
            int delay = tsa_b.base;
            while(tsa_b.tsalock_backoff.test_and_set(std::memory_order_acquire)){
                std::this_thread::sleep_for
                    (std::chrono::nanoseconds(delay*10));
                delay = delay * tsa_b.multiplier > tsa_b.limit? 
                        tsa_b.limit : delay * tsa_b.multiplier;
            }
            count++;
            //printf("Thread %d with count %d.\n", threadnum,count);
            tsa_b.tsalock_backoff.clear(std::memory_order_release);
        }              
    }else if(phase == 4){
        while(!start.load());
        for (int ind = 0; ind < i; ind++){
            t_lock.acquire();
            count++;
            //printf("Thread %d with count %d.\n", threadnum,count);
            t_lock.release();
        }              
    }else if(phase == 5){
        while(!start.load());
        for (int ind = 0; ind < i; ind++){
            t_lock.acquire_b();
            count++;
            //printf("Thread %d with count %d.\n", threadnum,count);
            t_lock.release();
        }               
    }else if(phase == 6){
        while(!start.load());
        for (int ind = 0; ind < i; ind++){
            qnode_m p;
            m_lock.acquire(&p);
            count++;
            //printf("Thread %d with count %d.\n", threadnum,count);
            m_lock.release(&p);
        }               
    }else if(phase == 7){
        while(!start.load());
        for (int ind = 0; ind < i; ind++){
            qnode_mk p;
            mk_lock.acquire();
            count++;
            //printf("Thread %d with count %d.\n", threadnum,count);
            mk_lock.release();
        }               
    }else if(phase == 8){
        while(!start.load());
        for (int ind = 0; ind < i; ind++){
            qnode_c * p = new qnode_c;
            c_lock.acquire(p);
            count++;
            //printf("Thread %d with count %d.\n", threadnum,count);
            c_lock.release(&p);
        }               
    }else if(phase == 9){
        while(!start.load());
        for (int ind = 0; ind < i; ind++){
            ck_lock.acquire(threadnum);
            count++;
            //printf("Thread %d with count %d.\n", threadnum,count);
            ck_lock.release();
        }               
    }
}



void thread_calling(int t, int i, int phase){

    int threadnum = 0;
    int count = 0;

    using namespace std::chrono;

    for (int ind =0; ind < t; ind++){
        threads.push_back(std::thread 
            (counter, phase, std::ref(count), i, threadnum));
        threadnum++;
    }

    start = 1;
    high_resolution_clock::time_point t1 = high_resolution_clock::now();
    
    for (std::thread & th : threads){
        th.join();
    }


    threads.clear();
    high_resolution_clock::time_point t2 = high_resolution_clock::now();
    std::cout <<"Phase "<< phase <<" result: " << count << std::endl;
    duration<double> time = duration_cast<duration<double>>(t2 - t1);

    std::cout << "Time: "<<time.count()<<"              "<<std::endl;
    std::cout<<"Thoughput: "<<(t*i/1000)/time.count()<<"               "<<std::endl;
    //printf("%f      ",t*i/time.count());
    start = 0;
}



int main(int argc, char ** argv)
{
    int t = 4;
    int i = 10000;

    for (int ind = 1; ind < argc-1; ind++){
        if(std::string(argv[ind])=="-t"){
            t = std::stol(argv[ind+1]);
        }
        else if(std::string(argv[ind])=="-i"){
            i = std::stol(argv[ind+1]);
        }
    }
    tt = t;
    ck_lock.init_nodes();
    //c++ mutex
    thread_calling(t, i, 1);
    //naive TSA lock
    thread_calling(t, i, 2);
    //TSA with backoff
    thread_calling(t,i,3);
    //naive ticket lock
    thread_calling(t, i, 4);
    //ticket lock with back off
    thread_calling(t,i,5);
    //mcs lock
    thread_calling(t,i,6);
    //mcs k42
    thread_calling(t,i,7);
    //clh lock
    thread_calling(t,i,8);
    //clh lock k42
    thread_calling(t,i,9);
    return 0;
}
