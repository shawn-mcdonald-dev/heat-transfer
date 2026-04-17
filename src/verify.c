#include <stdio.h>
#include <stdlib.h>

int compare_files(const char *f1, const char *f2)
{
    FILE *fp1 = fopen(f1, "rb");
    FILE *fp2 = fopen(f2, "rb");

    if (!fp1 || !fp2)
    {
        perror("fopen");
        return 1;
    }

    int rows1, cols1, rows2, cols2;

    fread(&rows1, sizeof(int), 1, fp1);
    fread(&cols1, sizeof(int), 1, fp1);

    fread(&rows2, sizeof(int), 1, fp2);
    fread(&cols2, sizeof(int), 1, fp2);

    if (rows1 != rows2 || cols1 != cols2)
    {
        printf("Different dimensions: (%d x %d) vs (%d x %d)\n",
               rows1, cols1, rows2, cols2);
        fclose(fp1);
        fclose(fp2);
        return 0;
    }

    int total = rows1 * cols1;
    int diff_count = 0;

    for (int i = 0; i < total; i++)
    {
        double v1, v2;

        fread(&v1, sizeof(double), 1, fp1);
        fread(&v2, sizeof(double), 1, fp2);

        if ((int)(100000 * v1) != (int)(100000 * v2))
        {
            printf("Value 1: %f Value 2: %f\n", v1, v2);
            diff_count++;
        }
    }

    if (diff_count == 0)
    {
        printf("Files are identical (%d cells checked)\n", total);
    }
    else
    {
        printf("Files differ: %d / %d cells mismatch\n", diff_count, total);
    }

    fclose(fp1);
    fclose(fp2);
    return 0;
}

int main(int argc, char *argv[])
{
    if (argc != 3)
    {
        printf("Usage: %s file1.dat file2.dat\n", argv[0]);
        return 1;
    }

    return compare_files(argv[1], argv[2]);
}