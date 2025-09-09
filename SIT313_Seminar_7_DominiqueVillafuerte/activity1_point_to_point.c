#include <mpi.h>
#include <stdio.h>
#include <string.h>


int main(int argc, char **argv) {
MPI_Init(&argc, &argv);


int world_rank, world_size;
MPI_Comm_rank(MPI_COMM_WORLD, &world_rank);
MPI_Comm_size(MPI_COMM_WORLD, &world_size);


const int master = 0;
const int tag = 42;


if (world_rank == master) {
const char msg[] = "Hello World!";
for (int dest = 1; dest < world_size; ++dest) {
MPI_Send(msg, (int)strlen(msg) + 1, MPI_CHAR, dest, tag, MPI_COMM_WORLD);
printf("[master %d] sent message to rank %d\n", master, dest);
}
} else {
char buf[128];
MPI_Status status;
MPI_Recv(buf, 128, MPI_CHAR, master, tag, MPI_COMM_WORLD, &status);
printf("[worker %d] received: %s\n", world_rank, buf);
}


MPI_Finalize();
return 0;
}