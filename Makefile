CC = gcc
CFLAGS = -Wall -Wstrict-prototypes -std=gnu99

UTILS = utilities.c

all: make-2d print-2d stencil-2d stencil-2d-pth

make-2d: make-2d.c $(UTILS)
	$(CC) $(CFLAGS) -o $@ $^

print-2d: print-2d.c $(UTILS)
	$(CC) $(CFLAGS) -o $@ $^

stencil-2d: stencil-2d.c $(UTILS)
	$(CC) $(CFLAGS) -o $@ $^

stencil-2d-pth: stencil-2d-pth.c $(UTILS)
	$(CC) $(CFLAGS) -pthread -o $@ $^

clean:
	rm -f make-2d print-2d stencil-2d stencil-2d-pth *.o *.dat