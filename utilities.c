#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void print_matrix(double *data, int rows, int cols) {
    for (int i = 0; i < rows; i++) {
        for (int j = 0; j < cols; j++) {
            printf("%5.2f ", data[i * cols + j]);
        }
        printf("\n");
    }
}