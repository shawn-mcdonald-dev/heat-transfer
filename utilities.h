#ifndef UTILITIES_H
#define UTILITIES_H

typedef __uint64_t uint64_t;

typedef struct op_range
{
    int start;
    int stop;
} op_range_t;

void print_matrix(double *data, int rows, int cols);

#endif