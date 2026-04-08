CC = gcc
CFLAGS = -Wall -Wstrict-prototypes -std=gnu99

# Targets
all: make-2d print-2d stencil-2d stencil-2d.omp

make-2d: make-2d.c
	$(CC) $(CFLAGS) -o make-2d make-2d.c

print-2d: print-2d.c
	$(CC) $(CFLAGS) -o print-2d print-2d.c

stencil-2d: stencil-2d.c
	$(CC) $(CFLAGS) -o stencil-2d stencil-2d.c

stencil-2d.omp: stencil-2d.omp.c
	$(CC) $(CFLAGS) -fopenmp -o stencil-2d.omp stencil-2d.omp.c

clean:
	rm -f make-2d print-2d stencil-2d stencil-2d.omp *.o *.dat