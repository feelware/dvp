#include <mpi.h>
#include <stdio.h>
#include <opencv2/opencv.hpp>
#include "video_decompose.h"

using namespace cv;

extern "C" int decompose_video(const char *video_file, int rank, int num_procs) {
    int total_frames = 0;
    
    if (rank == 0) {
        VideoCapture cap(video_file);
        if (!cap.isOpened()) {
            fprintf(stderr, "Error: No se pudo abrir el video %s\n", video_file);
            return 0;
        }
        total_frames = (int)cap.get(CAP_PROP_FRAME_COUNT);
        printf("[Master] Video abierto. Total Frames: %d\n", total_frames);
    }
    
    MPI_Bcast(&total_frames, 1, MPI_INT, 0, MPI_COMM_WORLD);
    
    if (total_frames <= 0) {
        fprintf(stderr, "[Rank %d] Error: No se pudo obtener el total de frames\n", rank);
        return 0;
    }
    
    int frames_per_rank = total_frames / num_procs;
    int start = rank * frames_per_rank;
    int end = (rank == num_procs - 1) ? total_frames : start + frames_per_rank;
    
    printf("[MPI Rank %d] Dominio asignado: Frames %d a %d (Total: %d)\n", 
           rank, start, end, (end - start));
    
    VideoCapture cap_worker(video_file);
    if (cap_worker.isOpened()) {
        cap_worker.set(CAP_PROP_POS_FRAMES, start);
        double current_pos = cap_worker.get(CAP_PROP_POS_FRAMES);
        printf("[MPI Rank %d] Video posicionado correctamente en frame %.0f\n", rank, current_pos);
    } else {
        fprintf(stderr, "[Rank %d] Error: No se pudo abrir el video para worker\n", rank);
        return 0;
    }
    
    return 1;
}