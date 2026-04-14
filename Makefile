CC = gcc
CFLAGS = -Wall -Wstrict-prototypes -std=gnu99
# MPI helpers in utilities.c: build with e.g. `make utilities-mpi.o` (requires mpicc)
CFLAGS_MPI = $(CFLAGS) -DHAVE_MPI

UTILS = utilities.c

all: make-2d print-2d stencil-2d stencil-2d-pth stencil-2d-mpi test-framework verify

utilities-mpi.o: utilities.c utilities.h
	mpicc $(CFLAGS_MPI) -c utilities.c -o utilities-mpi.o

stencil-2d-mpi: stencil-2d-mpi.c $(UTILS)
	mpicc $(CFLAGS_MPI) -o $@ $^

# Pre-flight: mpirun -np 1 ./test-framework  and  mpirun -np 2 ./test-framework
test-framework: test-framework.c $(UTILS)
	mpicc $(CFLAGS_MPI) -o $@ $^

make-2d: make-2d.c $(UTILS)
	$(CC) $(CFLAGS) -o $@ $^

print-2d: print-2d.c $(UTILS)
	$(CC) $(CFLAGS) -o $@ $^

stencil-2d: stencil-2d.c $(UTILS)
	$(CC) $(CFLAGS) -o $@ $^

stencil-2d-pth: stencil-2d-pth.c $(UTILS)
	$(CC) $(CFLAGS) -pthread -o $@ $^

verify: verify.c $(UTILS)
	$(CC) $(CFLAGS) -o $@ $^

clean:
	rm -f make-2d print-2d stencil-2d stencil-2d-pth stencil-2d-mpi test-framework verify *.o *.dat

# Run unit/smoke tests (requires working mpirun; use -np 2+ for halo checks)
check: test-framework
	mpirun -np 2 ./test-framework && mpirun -np 1 ./test-framework