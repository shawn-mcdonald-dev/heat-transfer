/*
 * Pre-flight / regression checks for heat-transfer utilities and MPI helpers.
 *
 * Build:  make test-framework
 * Run:    mpirun -np 1 ./test-framework
 *         mpirun -np 2 ./test-framework    (exercises halo + multi-rank paths)
 *         mpirun -np 4 ./test-framework
 *
 * Exit 0 if all tests pass, non-zero if any fail (see stderr).
 */

#include <mpi.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "utilities.h"

static int g_failures = 0;
static int g_tf_rank;

static void expect_impl(int ok, const char *msg, const char *file, int line)
{
    if (!ok) {
        fprintf(stderr, "[%s:%d] FAIL: %s\n", file, line, msg);
        g_failures++;
    }
}

#define EXPECT(cond, msg) expect_impl((cond) ? 1 : 0, msg, __FILE__, __LINE__)

static void section(const char *name)
{
    if (g_tf_rank == 0) {
        printf("-- %s\n", name);
        fflush(stdout);
    }
}

static void test_get_node_op_range(void)
{
    section("get_node_op_range");
    /* 10 units, 3 ranks -> [0,3), [3,6), [6,10)  lengths 3,3,4 */
    op_range_t r0 = get_node_op_range(3, 0, 10);
    op_range_t r1 = get_node_op_range(3, 1, 10);
    op_range_t r2 = get_node_op_range(3, 2, 10);
    EXPECT(r0.start == 0 && r0.stop == 3, "rank 0 [0,3)");
    EXPECT(r1.start == 3 && r1.stop == 6, "rank 1 [3,6)");
    EXPECT(r2.start == 6 && r2.stop == 10, "rank 2 [6,10)");
    /* 7 units, 3 ranks -> lengths 2, 2, 3 */
    op_range_t a = get_node_op_range(3, 0, 7);
    op_range_t b = get_node_op_range(3, 1, 7);
    op_range_t c = get_node_op_range(3, 2, 7);
    EXPECT(a.stop - a.start == 2 && b.stop - b.start == 2 && c.stop - c.start == 3,
           "7/3 partition lengths");
}

static void test_get_process_op_range(void)
{
    section("get_process_op_range");
    op_range_t t0 = get_process_op_range(3, 0, 10, 5);
    op_range_t t1 = get_process_op_range(3, 1, 10, 5);
    op_range_t t2 = get_process_op_range(3, 2, 10, 5);
    EXPECT(t0.start == 5 && t0.stop == 8, "thread 0");
    EXPECT(t1.start == 8 && t1.stop == 11, "thread 1");
    EXPECT(t2.start == 11 && t2.stop == 15, "thread 2");
}

static void test_check_balance_split(void)
{
    section("check_balance_split");
    int ok = check_balance_split(2, 2, 4);
    EXPECT(ok == 1, "2x2 nodes/procs on 4x4 interior cells should balance");
}

static void test_recombine_plan_math(void)
{
    section("stencil_mpi_recombine_plan_* / scatter_recvcount");
    int rows = 11;
    int cols = 7;
    int n = 3;
    stencil_mpi_recombine_plan_t plan;
    EXPECT(stencil_mpi_recombine_plan_create(&plan, rows, cols, n) == 0, "create");
    stencil_mpi_recombine_plan_fill(&plan);

    int interior = rows - 2;
    int sum_owned = 0;
    for (int r = 0; r < n; r++) {
        op_range_t ir = get_node_op_range(n, r, interior);
        int owned = ir.stop - ir.start;
        int lr = owned + 2;
        int gfr = 1 + ir.start;
        EXPECT(plan.scatter_sendcounts[r] == lr * cols, "scatter_sendcounts");
        EXPECT(plan.scatter_displs[r] == (gfr - 1) * cols, "scatter_displs");
        EXPECT(plan.gather_recvcounts[r] == owned * cols, "gather_recvcounts");
        EXPECT(plan.gather_displs[r] == gfr * cols, "gather_displs");
        int src = stencil_mpi_recombine_scatter_recvcount(r, rows, cols, n);
        EXPECT(src == lr * cols, "scatter_recvcount matches");
        sum_owned += owned;
    }
    EXPECT(sum_owned == interior, "owned rows cover interior");

    stencil_mpi_recombine_plan_destroy(&plan);
}

static void test_print_matrix_smoke(void)
{
    section("print_matrix (rank 0 only, small grid)");
    if (g_tf_rank == 0) {
        double m[4] = {1.0, 2.0, 3.0, 4.0};
        printf("(expect 2x2 matrix below)\n");
        print_matrix(m, 2, 2);
    }
}

static void test_mpi_domain_consistency(void)
{
    section("stencil_mpi_domain_init (partition sums to interior)");
    int rank, size;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    int rows = 14;
    int cols = 9;
    if (size > rows - 2) {
        if (g_tf_rank == 0) {
            printf("SKIP domain consistency: world_size %d > interior %d\n", size, rows - 2);
        }
        return;
    }

    stencil_mpi_domain_t dom;
    stencil_mpi_domain_init(&dom, rows, cols);
    EXPECT(dom.rank == rank && dom.size == size, "rank/size");
    EXPECT(dom.rows == rows && dom.cols == cols, "rows/cols");
    EXPECT(dom.owned_rows == dom.interior.stop - dom.interior.start, "owned_rows");
    EXPECT(dom.local_rows == dom.owned_rows + 2, "local_rows");
    EXPECT(dom.interior_start_local == 1, "interior_start_local");
    EXPECT(dom.interior_end_local == dom.owned_rows + 1, "interior_end_local");

    int local_owned = dom.owned_rows;
    int sum_owned;
    MPI_Allreduce(&local_owned, &sum_owned, 1, MPI_INT, MPI_SUM, MPI_COMM_WORLD);
    EXPECT(sum_owned == rows - 2, "sum of owned interior rows");

    if (rank == 0) {
        EXPECT(dom.neighbor_north == -1, "rank0 no north");
    } else {
        EXPECT(dom.neighbor_north == rank - 1, "north neighbor");
    }
    if (rank == size - 1) {
        EXPECT(dom.neighbor_south == -1, "last no south");
    } else {
        EXPECT(dom.neighbor_south == rank + 1, "south neighbor");
    }
}

static void test_global_interior_to_local(void)
{
    section("stencil_mpi_global_interior_to_local");
    int rank, size;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    int rows = 10;
    int cols = 6;
    if (size > rows - 2) {
        return;
    }

    stencil_mpi_domain_t dom;
    stencil_mpi_domain_init(&dom, rows, cols);

    for (int g = dom.global_first_row; g <= dom.global_last_row; g++) {
        int li = stencil_mpi_global_interior_to_local(&dom, g);
        EXPECT(li >= 1 && li < dom.local_rows, "local index in interior band");
        EXPECT(li == g - dom.global_first_row + dom.interior_start_local, "formula");
    }
    EXPECT(stencil_mpi_global_interior_to_local(&dom, 0) == -1, "reject row 0");
    EXPECT(stencil_mpi_global_interior_to_local(&dom, rows - 1) == -1,
           "reject last row if not owned");
}

static void test_local_thread_rows(void)
{
    section("stencil_mpi_local_thread_rows");
    int rank, size;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    int rows = 12;
    int cols = 4;
    if (size > rows - 2) {
        return;
    }

    stencil_mpi_domain_t dom;
    stencil_mpi_domain_init(&dom, rows, cols);

    int nt = 3;
    int cov = 0;
    for (int t = 0; t < nt; t++) {
        int sr, er;
        stencil_mpi_local_thread_rows(&dom, nt, t, &sr, &er);
        EXPECT(sr <= er, "non-empty slice");
        EXPECT(sr >= dom.interior_start_local && er < dom.interior_end_local, "bounds");
        cov += (er - sr + 1);
    }
    EXPECT(cov == dom.owned_rows, "thread rows cover owned interior");
}

static void test_exchange_halos(void)
{
    section("stencil_mpi_exchange_halos");
    int rank, size;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    if (size < 2) {
        if (g_tf_rank == 0) {
            printf("SKIP halo exchange (need at least 2 MPI ranks)\n");
        }
        return;
    }

    int rows = 10;
    int cols = 5;
    if (size > rows - 2) {
        if (g_tf_rank == 0) {
            printf("SKIP halo exchange (world too large for grid)\n");
        }
        return;
    }

    stencil_mpi_domain_t dom;
    stencil_mpi_domain_init(&dom, rows, cols);

    int n = dom.local_rows * dom.cols;
    double *curr = calloc((size_t)n, sizeof(double));
    EXPECT(curr != NULL, "calloc curr");

    /* Tag owned + ghost rows with (1000*global_row + col) for checking */
    for (int li = 0; li < dom.local_rows; li++) {
        int global_row;
        if (rank == 0 && li == 0) {
            global_row = 0;
        } else if (rank == size - 1 && li == dom.local_rows - 1) {
            global_row = rows - 1;
        } else if (li >= dom.interior_start_local && li < dom.interior_end_local) {
            global_row = dom.global_first_row + (li - dom.interior_start_local);
        } else {
            global_row = -1;
        }
        for (int j = 0; j < cols; j++) {
            double v = (global_row >= 0) ? (1000.0 * (double)global_row + (double)j) : -1.0;
            curr[(size_t)li * (size_t)cols + (size_t)j] = v;
        }
    }

    /* Stash unique sentinel on rank0 last interior row */
    int last_in = dom.interior_end_local - 1;
    double sentinel = 314159.0;
    for (int j = 0; j < cols; j++) {
        curr[(size_t)last_in * (size_t)cols + (size_t)j] = sentinel;
    }
    /* Rank1 first interior row */
    if (rank == 1) {
        for (int j = 0; j < cols; j++) {
            curr[(size_t)dom.interior_start_local * (size_t)cols + (size_t)j] = 271828.0 + j;
        }
    }

    stencil_mpi_exchange_halos(&dom, curr);

    if (rank == 1) {
        for (int j = 0; j < cols; j++) {
            double got = curr[(size_t)j];
            EXPECT(got == sentinel, "rank1 north ghost == rank0 last interior sentinel");
        }
    }

    if (rank == 0 && size >= 2) {
        int sg = dom.local_rows - 1;
        for (int j = 0; j < cols; j++) {
            double got = curr[(size_t)sg * (size_t)cols + (size_t)j];
            EXPECT(got == 271828.0 + (double)j,
                   "rank0 south ghost == rank1 first interior");
        }
    }

    free(curr);
}

static void test_shard_write(void)
{
    section("stencil_mpi_shard_write + header readback");
    int rank, size;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    int rows = 9;
    int cols = 4;
    if (size > rows - 2) {
        if (g_tf_rank == 0) {
            printf("SKIP shard write (world too large)\n");
        }
        return;
    }

    stencil_mpi_domain_t dom;
    stencil_mpi_domain_init(&dom, rows, cols);

    int n = dom.local_rows * dom.cols;
    double *curr = malloc((size_t)n * sizeof(double));
    EXPECT(curr != NULL, "malloc");
    for (int i = 0; i < n; i++) {
        curr[i] = (double)(rank * 10000 + i);
    }

    char stem[256];
    memset(stem, 0, sizeof(stem));
    if (rank == 0) {
        snprintf(stem, sizeof(stem), "heat_fw_shard_%ld", (long)getpid());
    }
    MPI_Bcast(stem, (int)sizeof(stem), MPI_BYTE, 0, MPI_COMM_WORLD);

    EXPECT(stencil_mpi_shard_write(&dom, curr, stem) == 0, "shard_write");

    MPI_Barrier(MPI_COMM_WORLD);

    if (rank == 0) {
        for (int r = 0; r < size; r++) {
            char path[280];
            snprintf(path, sizeof(path), "%s.%d", stem, r);
            FILE *fp = fopen(path, "rb");
            EXPECT(fp != NULL, "open shard");
            if (!fp) {
                continue;
            }
            uint32_t magic;
            int32_t ver, gr, gc, rk, ws, fg, nr;
            EXPECT(fread(&magic, sizeof(magic), 1, fp) == 1, "read magic");
            EXPECT(magic == STENCIL_MPI_SHARD_MAGIC, "magic");
            EXPECT(fread(&ver, sizeof(ver), 1, fp) == 1, "read ver");
            EXPECT(ver == STENCIL_MPI_SHARD_VERSION, "version");
            EXPECT(fread(&gr, sizeof(gr), 1, fp) == 1, "gr");
            EXPECT(fread(&gc, sizeof(gc), 1, fp) == 1, "gc");
            EXPECT(fread(&rk, sizeof(rk), 1, fp) == 1, "rk");
            EXPECT(fread(&ws, sizeof(ws), 1, fp) == 1, "ws");
            EXPECT(fread(&fg, sizeof(fg), 1, fp) == 1, "fg");
            EXPECT(fread(&nr, sizeof(nr), 1, fp) == 1, "nr");
            EXPECT(gr == rows && gc == cols, "global dims in header");
            EXPECT(rk == r && ws == size, "rank/world in header");
            size_t expect_bytes = (size_t)nr * (size_t)gc * sizeof(double);
            fseek(fp, 0, SEEK_END);
            long flen = ftell(fp);
            EXPECT(flen == (long)(sizeof(magic) + 7 * sizeof(int32_t) + expect_bytes),
                   "file length");
            fclose(fp);
            remove(path);
        }
    }

    free(curr);
}

static void test_rank_file_rows_contrib_partition(void)
{
    section("stencil_mpi_rank_file_rows_contrib (partition)");
    int rank, size;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    int rows = 19;
    int cols = 5;
    if (size > rows - 2) {
        if (g_tf_rank == 0) {
            printf("SKIP rank_file_rows_contrib (world too large)\n");
        }
        return;
    }

    int sum_nr = 0;
    for (int r = 0; r < size; r++) {
        int ls, le, fg, nr;
        stencil_mpi_rank_file_rows_contrib(rows, cols, r, size, &ls, &le, &fg, &nr);
        sum_nr += nr;
        EXPECT(ls <= le, "slice non-empty");
        EXPECT(fg + nr <= rows, "slice fits in global rows");
    }
    EXPECT(sum_nr == rows, "row contributions sum to global height");

    if (rank == 0) {
        int *seen = calloc((size_t)rows, sizeof(int));
        EXPECT(seen != NULL, "calloc seen");
        if (seen) {
            for (int r = 0; r < size; r++) {
                int ls, le, fg, nr;
                stencil_mpi_rank_file_rows_contrib(rows, cols, r, size, &ls, &le, &fg, &nr);
                (void)ls;
                (void)le;
                for (int i = 0; i < nr; i++) {
                    int g = fg + i;
                    EXPECT(g >= 0 && g < rows, "global row in range");
                    if (g >= 0 && g < rows) {
                        EXPECT(seen[g] == 0, "no duplicate global row");
                        seen[g] = 1;
                    }
                }
            }
            for (int g = 0; g < rows; g++) {
                EXPECT(seen[g] == 1, "every global row covered");
            }
            free(seen);
        }
    }
}

static void test_serial_gather_write_roundtrip(void)
{
    section("stencil_mpi_serial_gather_write (stencil-2d file layout)");
    int rank, size;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    int rows = 13;
    int cols = 8;
    if (size > rows - 2) {
        if (g_tf_rank == 0) {
            printf("SKIP serial_gather_write (world too large)\n");
        }
        return;
    }

    stencil_mpi_domain_t dom;
    stencil_mpi_domain_init(&dom, rows, cols);

    int n = dom.local_rows * dom.cols;
    double *curr = malloc((size_t)n * sizeof(double));
    EXPECT(curr != NULL, "malloc curr");
    if (!curr) {
        return;
    }

    int ls, le, fg, nr;
    stencil_mpi_rank_file_rows_contrib(rows, cols, rank, size, &ls, &le, &fg, &nr);
    for (int li = ls; li <= le; li++) {
        int g = fg + (li - ls);
        for (int j = 0; j < cols; j++) {
            curr[(size_t)li * (size_t)cols + (size_t)j] = 1000.0 * (double)g + (double)j;
        }
    }

    char path[256];
    memset(path, 0, sizeof(path));
    if (rank == 0) {
        snprintf(path, sizeof(path), "heat_fw_gather_%ld.dat", (long)getpid());
    }
    MPI_Bcast(path, (int)sizeof(path), MPI_BYTE, 0, MPI_COMM_WORLD);

    EXPECT(stencil_mpi_serial_gather_write(&dom, curr, path) == 0, "serial_gather_write");

    MPI_Barrier(MPI_COMM_WORLD);

    if (rank == 0) {
        FILE *fp = fopen(path, "rb");
        EXPECT(fp != NULL, "open gathered file");
        if (fp) {
            int gr, gc;
            EXPECT(fread(&gr, sizeof(int), 1, fp) == 1, "read rows");
            EXPECT(fread(&gc, sizeof(int), 1, fp) == 1, "read cols");
            EXPECT(gr == rows && gc == cols, "header dims");
            for (int g = 0; g < rows; g++) {
                for (int j = 0; j < cols; j++) {
                    double v;
                    EXPECT(fread(&v, sizeof(double), 1, fp) == 1, "read cell");
                    double expect = 1000.0 * (double)g + (double)j;
                    EXPECT(v == expect, "cell matches sentinel");
                }
            }
            char extra;
            EXPECT(fread(&extra, 1, 1, fp) == 0, "no trailing bytes");
            fclose(fp);
            remove(path);
        }
    }

    free(curr);
}

int main(int argc, char *argv[])
{
    MPI_Init(&argc, &argv);

    int rank;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    g_tf_rank = rank;

    if (rank == 0) {
        printf("heat-transfer test-framework (utilities + MPI smoke)\n");
    }

    test_get_node_op_range();
    test_get_process_op_range();
    test_check_balance_split();
    test_recombine_plan_math();
    test_print_matrix_smoke();

    MPI_Barrier(MPI_COMM_WORLD);

    test_mpi_domain_consistency();
    test_global_interior_to_local();
    test_local_thread_rows();
    test_exchange_halos();
    test_shard_write();
    test_rank_file_rows_contrib_partition();
    test_serial_gather_write_roundtrip();

    int local = g_failures;
    int total;
    MPI_Allreduce(&local, &total, 1, MPI_INT, MPI_SUM, MPI_COMM_WORLD);

    MPI_Barrier(MPI_COMM_WORLD);

    if (rank == 0) {
        if (total == 0) {
            printf("OK: all tests passed on all ranks.\n");
        } else {
            printf("FAILED: %d assertion(s) failed across ranks.\n", total);
        }
    }

    MPI_Finalize();
    return total == 0 ? 0 : 1;
}
