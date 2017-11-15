#!/bin/bash

for t in 1 2 3 4 6 8 12 16 24 32 48 64 96 128
do
	for m in 'blocked' 'cyclic' 'dynamic'
	do
		echo "m $m t $t"
		./openmp matrix_2000.dat -t $t -m $m >> trial1.out
	done
	echo "cilk t $t"
	./cilk matrix_2000.dat -t $t >> trial1.out
	echo "" >> trial1.out
done

exit 0