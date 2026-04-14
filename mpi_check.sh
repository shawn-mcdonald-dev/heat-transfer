#!/bin/bash


iters=8
procs=1

echo Iters: $iters Procs: $procs

./make-2d 32 32 out.dat

./stencil-2d -n $iters -i out.dat -o sten.dat

mpiexec -n $procs ./stencil-2d-mpi -n $iters -i out.dat -o mpi.dat

python merge-stencil-shards.py mpi.dat mpi.dat

./verify sten.dat mpi.dat

rm *.dat.*
rm *.dat