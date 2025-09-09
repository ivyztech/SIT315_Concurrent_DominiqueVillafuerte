#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>

static void init_vectors(double *a, double *b, long long n) {
    for (long long i = 0; i < n; ++i) {
        a[i] = (double)i;
        b[i] = (double)(n - i);
    }
}

int main(int argc, char **argv) {
    MPI_Init(&argc, &argv);

    int rank, size;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    if (argc < 2) {
        if (rank == 0) fprintf(stderr, "Usage: %s N\n", argv[0]);
        MPI_Abort(MPI_COMM_WORLD, 1);
    }

    long long N = atoll(argv[1]);
    if (N <= 0) {
        if (rank == 0) fprintf(stderr, "N must be > 0\n");
        MPI_Abort(MPI_COMM_WORLD, 1);
    }

    if (N % size != 0) {
        if (rank == 0) fprintf(stderr,
            "For this demo, choose N divisible by number of processes (%d).\n", size);
        MPI_Abort(MPI_COMM_WORLD, 2);
    }

    long long chunk = N / size;

    // Root buffers (only allocated on rank 0)
    double *A = NULL, *B = NULL, *C = NULL;
    if (rank == 0) {
        A = (double*)malloc(N * sizeof(double));
        B = (double*)malloc(N * sizeof(double));
        C = (double*)malloc(N * sizeof(double));
        if (!A || !B || !C) {
            fprintf(stderr, "Allocation failed on root\n");
            MPI_Abort(MPI_COMM_WORLD, 3);
        }
        init_vectors(A, B, N);
    }

    // Local buffers on every rank
    double *a_local = (double*)malloc(chunk * sizeof(double));
    double *b_local = (double*)malloc(chunk * sizeof(double));
    double *c_local = (double*)malloc(chunk * sizeof(double));
    if (!a_local || !b_local || !c_local) {
        fprintf(stderr, "Allocation failed on rank %d\n", rank);
        MPI_Abort(MPI_COMM_WORLD, 4);
    }

    MPI_Barrier(MPI_COMM_WORLD);
    double t0 = MPI_Wtime();

    // Distribute data
    MPI_Scatter(A, (int)chunk, MPI_DOUBLE, a_local, (int)chunk, MPI_DOUBLE, 0, MPI_COMM_WORLD);
    MPI_Scatter(B, (int)chunk, MPI_DOUBLE, b_local, (int)chunk, MPI_DOUBLE, 0, MPI_COMM_WORLD);

    // Local compute
    for (long long i = 0; i < chunk; ++i)
        c_local[i] = a_local[i] + b_local[i];

    // Local -> global sum of C via Reduce
    double local_sum = 0.0;
    for (long long i = 0; i < chunk; ++i) local_sum += c_local[i];

    double global_sum = 0.0;
    MPI_Reduce(&local_sum, &global_sum, 1, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);

    // Gather full C back to root (C is valid only on rank 0)
    MPI_Gather(c_local, (int)chunk, MPI_DOUBLE, C, (int)chunk, MPI_DOUBLE, 0, MPI_COMM_WORLD);

    MPI_Barrier(MPI_COMM_WORLD);
    double t1 = MPI_Wtime();

    if (rank == 0) {
        // Quick correctness check
        int ok = 1;
        for (long long i = 0; i < 5 && i < N; ++i) {
            double expect = (double)i + (double)(N - i); // equals N
            if (C[i] != expect) { ok = 0; break; }
        }
        printf("MPI vector add done: N=%lld, procs=%d, time=%.6f s, sample-ok=%s\n",
               N, size, t1 - t0, ok ? "yes" : "no");
        printf("Total sum of v3 (via MPI_Reduce) = %.3f\n", global_sum);
        // (For this init, global_sum should be N * (double)N)
    }

    free(a_local); free(b_local); free(c_local);
    if (rank == 0) { free(A); free(B); free(C); }

    MPI_Finalize();
    return 0;
}