#include <stdio.h>
#include <stdlib.h>

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "usage: %s <input_file>\n", argv[0]);
        return 1;
    }

    char *infile = argv[1];
    FILE *fp = fopen(infile, "rb");
    if (fp == NULL) {
        perror("fopen");
        return 1;
    }

    int rows, cols;

    if (fread(&rows, sizeof(int), 1, fp) != 1 ||
        fread(&cols, sizeof(int), 1, fp) != 1) {
        fprintf(stderr, "Error reading matrix dimensions.\n");
        fclose(fp);
        return 1;
    }

    double *data = malloc(rows * cols * sizeof(double));
    if (data == NULL) {
        fprintf(stderr, "Memory allocation failed.\n");
        fclose(fp);
        return 1;
    }

    if (fread(data, sizeof(double), rows * cols, fp) != (size_t)(rows * cols)) {
        fprintf(stderr, "Error reading matrix data.\n");
        free(data);
        fclose(fp);
        return 1;
    }

    fclose(fp);

    printf("reading in file: %s\n", infile);
    for (int i = 0; i < rows; i++) {
        for (int j = 0; j < cols; j++) {
            printf("%5.2f ", data[i * cols + j]);
        }
        printf("\n");
    }

    free(data);
    return 0;
}