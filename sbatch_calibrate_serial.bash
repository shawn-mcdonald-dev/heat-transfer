#!/bin/bash
#SBATCH --job-name=calibrate_serial
#SBATCH --output=calibrate_serial_%j.out
#SBATCH --partition=compute
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=1
#SBATCH --mem=48G
#SBATCH --time=00:30:00
#SBATCH --account=ccu108

set -e

echo "Job started at: $(date)"
echo "Host: $(hostname)"

module purge
module load cpu
module load slurm
module load gcc/10.2.0

make

N=40000
ITER_LIST=(5 10 20 40 60 80 100)
INPUT_FILE="input_${N}.dat"

if [ ! -f "$INPUT_FILE" ]; then
    echo "Creating input matrix ${N}x${N}"
    ./make-2d $N $N "$INPUT_FILE"
fi

for T in "${ITER_LIST[@]}"; do
    OUT_FILE="serial_${N}_t${T}.dat"

    echo "========================================"
    echo "Running serial calibration: N=$N, T=$T"
    echo "========================================"

    ./stencil-2d -n "$T" -i "$INPUT_FILE" -o "$OUT_FILE"

    # Delete huge output file after timing to save space
    rm -f "$OUT_FILE"
done

echo "Job finished at: $(date)"