#!/bin/bash


iters=8
procs=1

echo Iters: $iters Procs: $procs

./build/make-2d 32 32 out.dat

./build/stencil-2d -n $iters -i out.dat -o sten.dat

mpiexec -n $procs ./build/stencil-2d-mpi -n $iters -i out.dat -o mpi.dat

python ./build/merge-stencil-shards.py mpi.dat mpi.dat

./build/verify sten.dat mpi.dat

rm *.dat.*
rm *.dat