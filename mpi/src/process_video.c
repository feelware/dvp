#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <curl/curl.h>
#include <unistd.h>
#include <sys/stat.h>
#include "apply_lut.h"
#include "download_video.h"

int main(int argc, char **argv) {
    MPI_Init(&argc, &argv);

    int rank, num_procs;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &num_procs);

    if (argc < 4) {
        if (rank == 0) {
            fprintf(stderr, "Usage: %s <job_id> <video_path> <task> [params]\n", argv[0]);
        }
        MPI_Finalize();
        return 1;
    }

    const char *job_id = argv[1];
    const char *video_path = argv[2];
    const char *task = argv[3];
    const char *params = (argc > 4) ? argv[4] : "{}";
    char output_file[512];

    if (rank == 0) {
        printf("========================================\n");
        printf("PROCESS_VIDEO - Iniciando procesamiento\n");
        printf("========================================\n");
        printf("Job ID: %s\n", job_id);
        printf("Video Path: %s\n", video_path);
        printf("Task: %s\n", task);
        printf("Params: %s\n", params);
        printf("MPI Processes: %d\n", num_procs);
        printf("========================================\n\n");
        fflush(stdout);

        curl_global_init(CURL_GLOBAL_DEFAULT);

        snprintf(output_file, sizeof(output_file), "/tmp/video_%s.mp4", job_id);

        printf("Descargando video desde MinIO...\n");
        fflush(stdout);

        if (!download_video_parallel(video_path, output_file, rank)) {
            fprintf(stderr, "Error: Fallo la descarga del video\n");
            curl_global_cleanup();
            MPI_Finalize();
            return 1;
        }

        printf("\n========================================\n");
        printf("Descarga completada - Video disponible en: %s\n", output_file);
        printf("========================================\n\n");
        fflush(stdout);

        printf("Iniciando descomposición del video...\n");
        fflush(stdout);

        curl_global_cleanup();
    }

    MPI_Barrier(MPI_COMM_WORLD);

    MPI_Bcast(output_file, 512, MPI_CHAR, 0, MPI_COMM_WORLD);

    if (!apply_lut(output_file, rank, num_procs)) {
        fprintf(stderr, "[Rank %d] Error en la descomposición del video\n", rank);
        MPI_Finalize();
        return 1;
    }

    MPI_Barrier(MPI_COMM_WORLD);

    if (rank == 0) {
        printf("\n========================================\n");
        printf("Descomposición completada exitosamente\n");
        printf("Task: %s\n", task);
        printf("Archivo local: %s\n", output_file);
        printf("========================================\n\n");
        fflush(stdout);
    }

    MPI_Finalize();
    return 0;
}
