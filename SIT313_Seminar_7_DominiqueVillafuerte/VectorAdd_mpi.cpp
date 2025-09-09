#include <mpi.h>
#include <iostream>
#include <vector>
#include <cstdlib>
#include <chrono>

using namespace std;

static void init_vectors(vector<int>& a, vector<int>& b) {
    // deterministic init so runs are comparable
    srand(0);
    for (size_t i = 0; i < a.size(); ++i) {
        a[i] = rand() % 100;
        b[i] = rand() % 100;
    }
}

int main(int argc, char** argv) {
    MPI_Init(&argc, &argv);

    int rank = 0, size = 1;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    if (argc < 2) {
        if (rank == 0) cerr << "Usage: " << argv[0] << " N\n";
        MPI_Abort(MPI_COMM_WORLD, 1);
    }

    long long N = atoll(argv[1]);
    if (N <= 0) {
        if (rank == 0) cerr << "N must be > 0\n";
        MPI_Abort(MPI_COMM_WORLD, 1);
    }

    // Root allocates full vectors; others allocate none initially
    vector<int> A, B, C;
    if (rank == 0) {
        A.resize(N);
        B.resize(N);
        C.resize(N);
        init_vectors(A, B);
    }

    // Build counts/displacements for arbitrary N
    vector<int> counts(size), displs(size);
    long long base = N / size, rem = N % size;
    for (int p = 0; p < size; ++p) {
        counts[p] = static_cast<int>(base + (p < rem ? 1 : 0));
        displs[p] = (p == 0) ? 0 : displs[p - 1] + counts[p - 1];
    }

    int local_n = counts[rank];
    vector<int> a_local(local_n), b_local(local_n), c_local(local_n);

    MPI_Barrier(MPI_COMM_WORLD);
    double t0 = MPI_Wtime();

    // Distribute A and B
    MPI_Scatterv(A.data(), counts.data(), displs.data(), MPI_INT,
                 a_local.data(), local_n, MPI_INT, 0, MPI_COMM_WORLD);

    MPI_Scatterv(B.data(), counts.data(), displs.data(), MPI_INT,
                 b_local.data(), local_n, MPI_INT, 0, MPI_COMM_WORLD);

    // Local compute C = A + B
    for (int i = 0; i < local_n; ++i) {
        c_local[i] = a_local[i] + b_local[i];
    }

    // Local sum (use 64-bit to avoid overflow)
    long long local_sum = 0;
    for (int x : c_local) local_sum += static_cast<long long>(x);

    long long global_sum = 0;
    MPI_Reduce(&local_sum, &global_sum, 1, MPI_LONG_LONG, MPI_SUM, 0, MPI_COMM_WORLD);

    // Gather full C back to root (for optional checks)
    MPI_Gatherv(c_local.data(), local_n, MPI_INT,
                (rank == 0 ? C.data() : nullptr), counts.data(), displs.data(), MPI_INT,
                0, MPI_COMM_WORLD);

    MPI_Barrier(MPI_COMM_WORLD);
    double t1 = MPI_Wtime();

    if (rank == 0) {
        // simple spot-check on a couple of indices
        bool ok = true;
        if (N >= 1 && C[0] != (A[0] + B[0])) ok = false;
        if (N >= 2 && C[1] != (A[1] + B[1])) ok = false;

        cout << "MPI vector add (C++) done: N=" << N
             << ", procs=" << size
             << ", time=" << (t1 - t0) << " s"
             << ", sample-ok=" << (ok ? "yes" : "no") << "\n";
        cout << "Total sum of v3 (via MPI_Reduce) = " << global_sum << "\n";
    }

    MPI_Finalize();
    return 0;
}