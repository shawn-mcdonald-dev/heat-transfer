#!/bin/bash
#SBATCH --job-name=heat-mini-csv
#SBATCH --output=heat-mini-csv.out
#SBATCH --partition=shared
#SBATCH --nodes=1
#SBATCH --ntasks=4
#SBATCH --cpus-per-task=4
#SBATCH --mem=8G
#SBATCH --time=00:20:00
#SBATCH --account=ccu108

module purge
module load cpu
module load slurm
module load gcc/10.2.0
module load openmpi/4.1.1

echo "Current directory:"
pwd
echo "Files:"
ls -l

echo "Cleaning old data files..."
rm -f *.dat *.csv

echo "Compiling..."
make clean
make

ROWS=5000
COLS=5000
ITERS=20
CSV=mini_results.csv

echo "version,threads,ranks,rows,cols,iters,T_overall,T_computation,T_other" > "$CSV"

run_and_log() {
    local cmd="$1"

    output=$(eval "$cmd")
    status=$?

    echo "$output"

    if [ $status -ne 0 ]; then
        echo "ERROR: command failed -> $cmd"
        exit 1
    fi

    meta=$(echo "$output" | grep '^version=' | tail -n 1)
    timing=$(echo "$output" | grep '^T_overall=' | tail -n 1)

    if [ -z "$meta" ] || [ -z "$timing" ]; then
        echo "ERROR: missing metadata or timing output for command -> $cmd"
        exit 1
    fi

    version=$(echo "$meta"   | sed -n 's/.*version=\([^ ]*\).*/\1/p')
    threads=$(echo "$meta"   | sed -n 's/.*threads=\([^ ]*\).*/\1/p')
    ranks=$(echo "$meta"     | sed -n 's/.*ranks=\([^ ]*\).*/\1/p')
    rows=$(echo "$meta"      | sed -n 's/.*rows=\([^ ]*\).*/\1/p')
    cols=$(echo "$meta"      | sed -n 's/.*cols=\([^ ]*\).*/\1/p')
    iters=$(echo "$meta"     | sed -n 's/.*iters=\([^ ]*\).*/\1/p')

    t_overall=$(echo "$timing" | sed -n 's/.*T_overall=\([^ ]*\).*/\1/p')
    t_compute=$(echo "$timing" | sed -n 's/.*T_computation=\([^ ]*\).*/\1/p')
    t_other=$(echo "$timing"   | sed -n 's/.*T_other=\([^ ]*\).*/\1/p')

    echo "$version,$threads,$ranks,$rows,$cols,$iters,$t_overall,$t_compute,$t_other" >> "$CSV"
}

echo "Creating input matrix..."
./build/make-2d $ROWS $COLS input-5k.dat

echo "Running serial..."
run_and_log "./build/stencil-2d -n $ITERS -i input-5k.dat -o out-serial.dat"

echo "Running pthreads..."
run_and_log "./build/stencil-2d-pth -t $ITERS -i input-5k.dat -o out-pth.dat -p 4"

echo "Running OpenMP..."
export OMP_NUM_THREADS=4
run_and_log "./build/stencil-2d-omp -t $ITERS -i input-5k.dat -o out-omp.dat -v 0"

echo "Running MPI..."
run_and_log "mpirun -np 4 ./build/stencil-2d-mpi -t $ITERS -i input-5k.dat -o out-mpi.dat"

echo "Running hybrid..."
run_and_log "mpirun -np 2 ./build/stencil-2d-hybrid -t $ITERS -i input-5k.dat -o out-hybrid.dat -p 2"

echo "Verifying outputs..."

echo "serial vs pthreads"
./build/verify out-serial.dat out-pth.dat

echo "serial vs openmp"
./build/verify out-serial.dat out-omp.dat

echo "serial vs mpi"
./build/verify out-serial.dat out-mpi.dat

echo "serial vs hybrid"
./build/verify out-serial.dat out-hybrid.dat

echo "CSV contents:"
cat "$CSV"

echo "Done."
