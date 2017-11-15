TARGET = gaussian_openmp gaussian_cilk

default: $(TARGET)

gaussian_openmp: 
	g++ -std=c++11 -fopenmp -O2 -o openmp gaussian_openmp.cpp

gaussian_cilk: 
	g++ -std=c++11 -fcilkplus -O2 -lcilkrts -o cilk gaussian_cilk.cpp

clean:
	rm -f cilk openmp
