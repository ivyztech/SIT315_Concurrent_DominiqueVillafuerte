#include <stdio.h>
#include <stdlib.h>
#include <time.h>


static void init_vectors(double *a, double *b, long long n) {
for (long long i = 0; i < n; ++i) { a[i] = (double)i; b[i] = (double)(n - i); }
}


static double now() {
struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
return ts.tv_sec + ts.tv_nsec * 1e-9;
}


int main(int argc, char **argv) {
if (argc < 2) { fprintf(stderr, "Usage: %s N\n", argv[0]); return 1; }
long long N = atoll(argv[1]);


double *A = malloc(N * sizeof(double));
double *B = malloc(N * sizeof(double));
double *C = malloc(N * sizeof(double));
init_vectors(A, B, N);


double t0 = now();
for (long long i = 0; i < N; ++i) C[i] = A[i] + B[i];
double t1 = now();


double sum = 0.0; for (long long i = 0; i < N; ++i) sum += C[i];
printf("SEQ: N=%lld, time=%.6f s, sum=%.3f\n", N, t1 - t0, sum);


free(A); free(B); free(C);
return 0;
}