CC = gcc
CFLAGS = -Wall -Wstrict-prototypes -std=gnu99

# Targets
all: make-2d print-2d stencil-2d stencil-2d-pth

make-2d: make-2d.c
	$(CC) $(CFLAGS) -o make-2d make-2d.c

print-2d: print-2d.c
	$(CC) $(CFLAGS) -o print-2d print-2d.c

stencil-2d: stencil-2d.c
	$(CC) $(CFLAGS) -o stencil-2d stencil-2d.c

stencil-2d-pth: stencil-2d-pth.c
	$(CC) $(CFLAGS) -pthread -o stencil-2d-pth stencil-2d-pth.c

clean:
	rm -f make-2d print-2d stencil-2d stencil-2d-pth *.o *.dat