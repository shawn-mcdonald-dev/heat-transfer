#!/bin/bash


iters=3
procs=4

echo Iters: $iters Procs: $procs

./build/make-2d 32 32 out.dat

./build/stencil-2d -n $iters -i out.dat -o sten.dat

mpiexec -n $procs ./build/stencil-2d-mpi -n $iters -i out.dat -o mpi.dat

./build/verify sten.dat mpi.dat

rm -f *.dat.*
rm -f *.dat