# heat-transfer

2D heat diffusion with a 9-point stencil (CSCI 473 final project): serial, pthreads, OpenMP, MPI, and MPI+OpenMP hybrid implementations.

## Prerequisites

- **GCC** with C99 support (`gcc`)
- **OpenMP** (`-fopenmp`) for `stencil-2d-omp` and `stencil-2d-hybrid`
- **MPI** (`mpicc`, `mpirun` or `mpiexec`) for `stencil-2d-mpi`, `stencil-2d-hybrid`, and `make check`

Optional: Python 3 with NumPy and Matplotlib (see `requirements.txt`) if you use `build/visualize-2d.py`.

## Build

From the repository root:

```bash
make
```

Executables are written to `build/`. To remove them:

```bash
make clean
```

## Matrix file format

Programs read and write the same binary layout:

1. Two `int` values: `rows`, `cols`
2. `rows * cols` `double` values in row-major order

`make-2d` creates an initial field: left/right columns `1.0`, top/bottom rows `0.0`, interior `0.0`.

## Running the programs

All examples assume you run from the repository root and use `./build/...`.

### 1. Create a grid

```bash
./build/make-2d <rows> <cols> <output_file>
```

Example: `5000 × 5000` matrix in `input.dat`:

```bash
./build/make-2d 5000 5000 input.dat
```

### 2. Serial stencil (`stencil-2d`)

Uses **`-n`** for the number of iterations (time steps).

```bash
./build/stencil-2d -n <num_iters> -i <input> -o <output> -v <debug>
```

- **`-v 0`**: quiet (default if you omit `-v` by only passing `-n`, `-i`, `-o`)
- **`-v 1`**: basic messages (dimensions, file names)
- **`-v 2`**: print the matrix after each iteration

Example:

```bash
./build/stencil-2d -n 100 -i input.dat -o out-serial.dat -v 0
```

On success, the program prints one metadata line (`version=serial ...`) and timing (`T_overall`, `T_computation`, `T_other`) to stdout.

### 3. Pthreads (`stencil-2d-pth`)

Parallel versions use **`-t`** for iterations and **`-p`** for thread count. Optional **`-v`** (same meaning as serial).

```bash
./build/stencil-2d-pth -t <num_iters> -i <input> -o <output> -p <num_threads> [-v <debug>]
```

Example (4 threads):

```bash
./build/stencil-2d-pth -t 100 -i input.dat -o out-pth.dat -p 4 -v 0
```

### 4. OpenMP (`stencil-2d-omp`)

```bash
./build/stencil-2d-omp -t <num_iters> -i <input> -o <output> [-v <debug>]
```

The number of threads is controlled by the **`OMP_NUM_THREADS`** environment variable (OpenMP default applies if unset).

Example:

```bash
export OMP_NUM_THREADS=8
./build/stencil-2d-omp -t 100 -i input.dat -o out-omp.dat -v 0
```

### 5. MPI (`stencil-2d-mpi`)

No **`-p`** flag: process count is the `mpirun` / `mpiexec` **`-np`** value.

```bash
mpirun -np <num_processes> ./build/stencil-2d-mpi -t <num_iters> -i <input> -o <output>
```

Example:

```bash
mpirun -np 4 ./build/stencil-2d-mpi -t 100 -i input.dat -o out-mpi.dat
```

**Constraint:** the implementation requires **`num_processes ≤ rows - 2`** (one rank owns a contiguous block of interior rows plus halos). Choose `rows` and `-np` accordingly.

### 6. Hybrid MPI + OpenMP (`stencil-2d-hybrid`)

**`-p`** is the number of **OpenMP threads per MPI rank**. Total parallel units are **ranks × threads**.

```bash
export OMP_NUM_THREADS=<threads_per_rank>   # should match -p
mpirun -np <num_ranks> ./build/stencil-2d-hybrid -t <num_iters> -i <input> -o <output_stem> -p <threads_per_rank>
```

Example: 2 ranks × 4 threads (set both for clarity):

```bash
export OMP_NUM_THREADS=4
mpirun -np 2 ./build/stencil-2d-hybrid -t 100 -i input.dat -o out-hybrid.dat -p 4
```

### 7. Print a matrix

```bash
./build/print-2d <input_file>
```

### 8. Compare two output files

Binary comparison with a fixed tolerance:

```bash
./build/verify <file1.dat> <file2.dat>
```

Use this to confirm parallel outputs match the serial reference for the same **`-n` / `-t`** and input file.

**Note:** the serial program uses **`-n`** for iterations; all parallel stencils use **`-t`**. Use the **same numeric value** for both when comparing serial vs parallel.

## Smoke tests

With MPI available:

```bash
make check
```

This builds if needed, runs the MPI test harness, then a small hybrid job and is intended as a quick sanity check.

## Slurm / HPC

Example batch scripts (`sbatch_*.bash`) are tuned for a specific cluster account and partition. Copy one, adjust `#SBATCH` directives and `module load` lines for your site, then submit with `sbatch scriptname.bash`.

## Optional visualization

After `make`, a helper script is in `build/visualize-2d.py`:

```bash
python3 build/visualize-2d.py <matrix.dat> [output.png]
```

Install dependencies first, e.g. `pip install -r requirements.txt` (ideally in a virtual environment).

## Repository layout

| Path | Role |
|------|------|
| `src/*.c`, `src/utilities.h` | Source |
| `Makefile` | Build rules; output under `build/` |
| `requirements.txt` | Python deps for plotting |
| `sbatch_*.bash` | Example cluster job scripts |

The course handout refers to a `./code/` folder; in this repo the same sources live under **`src/`** and binaries under **`build/`**.
