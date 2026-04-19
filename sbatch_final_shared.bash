#!/bin/bash
#SBATCH --job-name=heat-final-shared
#SBATCH --output=heat-final-shared.out
#SBATCH --partition=shared
#SBATCH --nodes=1
#SBATCH --ntasks=16
#SBATCH --cpus-per-task=1
#SBATCH --mem=100G
#SBATCH --time=08:00:00
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

echo "Cleaning old temporary data..."
rm -f *.dat *.csv

echo "Compiling..."
make clean
make

T=13
SIZES=(5000 10000 20000 40000)
P_LIST=(1 2 4 8 16)
CSV=final_results_shared.csv

echo "version,p,threads,ranks,rows,cols,iters,T_overall,T_computation,T_other" > "$CSV"

run_and_log() {
    local pval="$1"
    local cmd="$2"

    output=$(eval "$cmd" 2>&1)
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

    echo "$version,$pval,$threads,$ranks,$rows,$cols,$iters,$t_overall,$t_compute,$t_other" >> "$CSV"
}

hybrid_config() {
    local p="$1"
    case "$p" in
        1)  echo "1 1" ;;
        2)  echo "1 2" ;;
        4)  echo "2 2" ;;
        8)  echo "2 4" ;;
        16) echo "4 4" ;;
        *)  echo "ERROR ERROR" ;;
    esac
}

for N in "${SIZES[@]}"; do
    INPUT="input-${N}.dat"

    echo "=================================================="
    echo "Creating input matrix for ${N} x ${N}"
    echo "=================================================="
    ./build/make-2d "$N" "$N" "$INPUT"

    echo "Running serial for N=${N}"
    OUT="out-serial-${N}.dat"
    run_and_log 1 "./build/stencil-2d -n $T -i $INPUT -o $OUT"
    rm -f "$OUT"

    for P in "${P_LIST[@]}"; do
        echo "--------------------------------------------------"
        echo "Running pthreads for N=${N}, p=${P}"
        echo "--------------------------------------------------"
        OUT="out-pthreads-${N}-p${P}.dat"
        run_and_log "$P" "./build/stencil-2d-pth -t $T -i $INPUT -o $OUT -p $P"
        rm -f "$OUT"

        echo "--------------------------------------------------"
        echo "Running OpenMP for N=${N}, p=${P}"
        echo "--------------------------------------------------"
        OUT="out-openmp-${N}-p${P}.dat"
        export OMP_NUM_THREADS=$P
        run_and_log "$P" "./build/stencil-2d-omp -t $T -i $INPUT -o $OUT -v 0"
        rm -f "$OUT"

        echo "--------------------------------------------------"
        echo "Running MPI for N=${N}, p=${P}"
        echo "--------------------------------------------------"
        OUT="out-mpi-${N}-p${P}.dat"
        run_and_log "$P" "mpirun -np $P ./build/stencil-2d-mpi -t $T -i $INPUT -o $OUT"
        rm -f "$OUT"

        echo "--------------------------------------------------"
        echo "Running hybrid for N=${N}, p=${P}"
        echo "--------------------------------------------------"
        read HRANKS HTHREADS <<< "$(hybrid_config "$P")"
        OUT="out-hybrid-${N}-p${P}.dat"
        run_and_log "$P" "mpirun -np $HRANKS ./build/stencil-2d-hybrid -t $T -i $INPUT -o $OUT -p $HTHREADS"
        rm -f "$OUT"
    done

    rm -f "$INPUT"
done

echo "=================================================="
echo "Final CSV:"
echo "=================================================="
cat "$CSV"

echo "Done."