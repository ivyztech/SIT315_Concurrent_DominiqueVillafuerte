#include <mpi.h>
#include <stdio.h>
#include <string.h>


int main(int argc, char **argv) {
MPI_Init(&argc, &argv);


int world_rank, world_size;
MPI_Comm_rank(MPI_COMM_WORLD, &world_rank);
MPI_Comm_size(MPI_COMM_WORLD, &world_size);


char message[128];
if (world_rank == 0) {
strcpy(message, "Hello World!");
}


// Broadcast from root (0) to all ranks
MPI_Bcast(message, 128, MPI_CHAR, 0, MPI_COMM_WORLD);


printf("[rank %d/%d] message: %s\n", world_rank, world_size, message);


MPI_Finalize();
return 0;
}