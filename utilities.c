#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "utilities.h"

void print_matrix(double *data, int rows, int cols) {
    for (int i = 0; i < rows; i++) {
        for (int j = 0; j < cols; j++) {
            printf("%5.2f ", data[i * cols + j]);
        }
        printf("\n");
    }
}

/**
 * Given nodes, current index, and matrix size
 * return where this specific node should operate
 */
op_range_t get_node_op_range(int num_nodes, int node_idx, int mat_size)
{
    op_range_t range;

    range.start = (int)(mat_size / num_nodes * node_idx);

    if (mat_size % num_nodes != 0 && mat_size % num_nodes >= node_idx) {
        range.stop = (int)(mat_size / num_nodes * (node_idx + 1) + 1);
    }
    else {
        range.stop = (int)(mat_size / num_nodes * (node_idx + 1));
    }
    return range;
}

/**
 * Given threads, current index, node range, and start
 * return where this specific thread should operate
 */
op_range_t get_process_op_range(int num_threads, int thread_idx, int block_size, int node_start)
{
    op_range_t range;

    range.start = (int)(block_size / num_threads * thread_idx) + node_start;

    if (block_size % num_threads != 0 && block_size % num_threads > thread_idx) {
        range.stop = (int)(block_size / num_threads * (thread_idx + 1) + node_start + 1);
    }
    else {
        range.stop = (int)(block_size / num_threads * (thread_idx + 1)) + node_start;
    }
    return range;
}

/**
 * Check that divisions based on nodes and threads are correctly balanced
 * Mainly debug
 */
int check_balance_split(int nodes, int procs, uint64_t size)
{
    // Matrix to check amount of ops for each thread
    int *counts = malloc(sizeof(int) * nodes * procs);
    if (counts == NULL) {
        fprintf(stderr, "Memory allocation failed.\n");
        return 1;
    }
    int small = 1000;
    int large = 0;

    // For each proc, count range
    for (int n = 0; n < nodes; n++)
    {
        op_range_t node_range = get_node_op_range(nodes, n, size * size);
        
        for (int p = 0; p < procs; p++)
        {
            op_range_t thread_range = get_process_op_range(
                procs, 
                p, 
                node_range.stop - node_range.start,
            node_range.start);

            counts[n* nodes + p] = thread_range.stop - thread_range.start;
        }
    }

    // Find max and min of op ranges
    for (int i = 0; i < nodes; i++)
    {
        for (int j = 0; j < procs; j++)
        {
            //printf("[%ld]", counts[i*num_nodes + j]);
            if (counts[i*nodes + j] > large) large = counts[i*nodes + j];
            else if (counts[i*nodes + j] < small) small = counts[i*nodes + j];
        }
    }
    // Free matrix, calc complete
    free(counts);
    
    // If diff between largest and smallest is bigger than 1, not balanced
    if (large - small > 1) return 0;
    else return 1;
}