#include <mpi.h>
#include <stdio.h>

int main(int argc, char **argv) {
  MPI_Init(&argc, &argv);

  int rank, num_procs;
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);
  MPI_Comm_size(MPI_COMM_WORLD, &num_procs);

  if (argc < 5) {
    if (rank == 0) {
      fprintf(stderr, "Usage: %s <job_id> <video_path> <task> <params>\n", argv[0]);
    }
    MPI_Finalize();
    return 1;
  }
  const char *job_id = argv[1];
  const char *video_path = argv[2];
  const char *task = argv[3];
  const char *params = argv[4];

  printf("Rank %d/%d - Job ID: %s, Video Path: %s, Task: %s, Params: %s\n",
         rank, num_procs, job_id, video_path, task, params);

  MPI_Finalize();
  return 0;
}