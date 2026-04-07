#include <stdio.h>
#include <stdlib.h>

int main(int argc, char *argv[]) {
    if (argc != 4) {
        fprintf(stderr, "usage: %s <rows> <cols> <output_file>\n", argv[0]);
        return 1;
    }

    int rows = atoi(argv[1]);
    int cols = atoi(argv[2]);
    char *outfile = argv[3];

    if (rows <= 0 || cols <= 0) {
        fprintf(stderr, "Error: rows and cols must be positive.\n");
        return 1;
    }

    FILE *fp = fopen(outfile, "wb");
    if (fp == NULL) {
        perror("fopen");
        return 1;
    }

    fwrite(&rows, sizeof(int), 1, fp);
    fwrite(&cols, sizeof(int), 1, fp);

    for (int i = 0; i < rows; i++) {
        for (int j = 0; j < cols; j++) {
            double val;

            if (j == 0 || j == cols - 1) {
                val = 1.0;               // left/right walls
            } else if (i == 0 || i == rows - 1) {
                val = 0.0;               // top/bottom walls
            } else {
                val = 0.0;               // interior
            }

            fwrite(&val, sizeof(double), 1, fp);
        }
    }

    fclose(fp);
    return 0;
}