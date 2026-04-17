#!/bin/bash
#SBATCH --job-name=heat-test
#SBATCH --output=heat-test.out
#SBATCH --partition=shared
#SBATCH --nodes=1
#SBATCH --ntasks=4
#SBATCH --cpus-per-task=4
#SBATCH --mem=4G
#SBATCH --time=00:10:00
#SBATCH --account=ccu108

module purge
module load cpu
module load slurm
module load gcc/10.2.0
module load openmpi/4.1.1

echo "Compiling code..."
make clean
make

ROWS=512
COLS=512
ITERS=10

echo "Creating input matrix..."
./make-2d $ROWS $COLS input-512.dat

echo "Running serial..."
./stencil-2d -n $ITERS -i input-512.dat -o out-serial.dat

echo "Running pthreads..."
./stencil-2d-pth -t $ITERS -i input-512.dat -o out-pth.dat -p 4

echo "Running OpenMP..."
export OMP_NUM_THREADS=4
./stencil-2d-omp -n $ITERS -i input-512.dat -o out-omp.dat

echo "Running MPI..."
mpirun -np 4 ./stencil-2d-mpi -t $ITERS -i input-512.dat -o out-mpi.dat

echo "Running hybrid..."
mpirun -np 2 ./stencil-2d-hybrid -t $ITERS -i input-512.dat -o out-hybrid.dat -p 2

echo "Verifying outputs..."

echo "serial vs pthreads"
./verify out-serial.dat out-pth.dat

echo "serial vs openmp"
./verify out-serial.dat out-omp.dat

echo "serial vs mpi"
./verify out-serial.dat out-mpi.dat

echo "Done."