CC=mpicc
CFLAGS=-O2 -Wall -Wextra


all: activity1_point_to_point activity1_bcast vec_mpi seq omp


activity1_point_to_point: activity1_point_to_point.c
$(CC) $(CFLAGS) $< -o $@


activity1_bcast: activity1_bcast.c
$(CC) $(CFLAGS) $< -o $@


vec_mpi: activity2_vector_add_mpi.c
$(CC) $(CFLAGS) $< -o $@


seq: seq_vector_add.c
gcc -O2 -Wall -Wextra $< -o $@


omp: omp_vector_add.c
gcc -O2 -Wall -Wextra -fopenmp $< -o $@


clean:
rm -f activity1_point_to_point activity1_bcast vec_mpi seq omp