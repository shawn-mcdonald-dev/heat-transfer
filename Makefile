CC = gcc
CFLAGS = -Wall -Wstrict-prototypes -std=gnu99
# MPI helpers in utilities.c: build with e.g. `make build/utilities-mpi.o` (requires mpicc)
CFLAGS_MPI = $(CFLAGS) -DHAVE_MPI

SRC = src
BUILD = build

UTILS = $(SRC)/utilities.c

BINS = $(BUILD)/make-2d $(BUILD)/print-2d $(BUILD)/stencil-2d $(BUILD)/stencil-2d-pth \
       $(BUILD)/stencil-2d-omp $(BUILD)/stencil-2d-mpi $(BUILD)/stencil-2d-hybrid \
	   $(BUILD)/test-framework $(BUILD)/verify 
PY_SCRIPTS = $(BUILD)/merge-stencil-shards.py $(BUILD)/visualize-2d.py

all: $(BINS) $(PY_SCRIPTS)

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/%.py: $(SRC)/%.py | $(BUILD)
	cp $< $@

$(BUILD)/utilities-mpi.o: $(SRC)/utilities.c $(SRC)/utilities.h | $(BUILD)
	mpicc $(CFLAGS_MPI) -c $(SRC)/utilities.c -o $@

$(BUILD)/stencil-2d-mpi: $(SRC)/stencil-2d-mpi.c $(UTILS) | $(BUILD)
	mpicc $(CFLAGS_MPI) -o $@ $^

$(BUILD)/stencil-2d-hybrid: $(SRC)/stencil-2d-hybrid.c $(UTILS) | $(BUILD)
	mpicc $(CFLAGS_MPI) -fopenmp -o $@ $^

# Pre-flight: mpirun -np 1 ./build/test-framework  and  mpirun -np 2 ./build/test-framework
$(BUILD)/test-framework: $(SRC)/test-framework.c $(UTILS) | $(BUILD)
	mpicc $(CFLAGS_MPI) -o $@ $^

$(BUILD)/make-2d: $(SRC)/make-2d.c $(UTILS) | $(BUILD)
	$(CC) $(CFLAGS) -o $@ $^

$(BUILD)/print-2d: $(SRC)/print-2d.c $(UTILS) | $(BUILD)
	$(CC) $(CFLAGS) -o $@ $^

$(BUILD)/stencil-2d: $(SRC)/stencil-2d.c $(UTILS) | $(BUILD)
	$(CC) $(CFLAGS) -o $@ $^

$(BUILD)/stencil-2d-pth: $(SRC)/stencil-2d-pth.c $(UTILS) | $(BUILD)
	$(CC) $(CFLAGS) -pthread -o $@ $^

$(BUILD)/stencil-2d-omp: $(SRC)/stencil-2d-omp.c $(UTILS) | $(BUILD)
	$(CC) $(CFLAGS) -fopenmp -o $@ $^

$(BUILD)/verify: $(SRC)/verify.c $(UTILS) | $(BUILD)
	$(CC) $(CFLAGS) -o $@ $^

clean:
	rm -rf $(BUILD)

# Run unit/smoke tests (requires working mpirun; use -np 2+ for halo checks)
check: $(BUILD)/test-framework
	mpirun -np 2 $(BUILD)/test-framework && mpirun -np 1 $(BUILD)/test-framework
	# This runs 2 MPI processes, each using 4 OpenMP threads
	$(BUILD)/make-2d 100 100 input.bin
	mpirun -np 2 $(BUILD)/stencil-2d-hybrid -n 10 -i input.bin -o output_stem -p 4
