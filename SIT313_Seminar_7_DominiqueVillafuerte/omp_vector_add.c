#include <stdio.h>
#include <stdlib.h>
#include <omp.h>


static void init_vectors(double *a, double *b, long long n) {
#pragma omp parallel for schedule(static)
for (long long i = 0; i < n; ++i) { a[i] = (double)i; b[i] = (double)(n - i); }
}


int main(int argc, char **argv) {
if (argc < 3) { fprintf(stderr, "Usage: %s N NUM_THREADS\n", argv[0]); return 1; }
long long N = atoll(argv[1]);
int T = atoi(argv[2]);
omp_set_num_threads(T);


double *A = malloc(N * sizeof(double));
double *B = malloc(N * sizeof(double));
double *C = malloc(N * sizeof(double));


init_vectors(A, B, N);


double t0 = omp_get_wtime();
#pragma omp parallel for schedule(static)
for (long long i = 0; i < N; ++i) C[i] = A[i] + B[i];
double t1 = omp_get_wtime();


double sum = 0.0;
#pragma omp parallel for reduction(+:sum)
for (long long i = 0; i < N; ++i) sum += C[i];


printf("OMP: N=%lld, T=%d, time=%.6f s, sum=%.3f\n", N, T, t1 - t0, sum);


free(A); free(B); free(C);
return 0;
}