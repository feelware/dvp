#include <mpi.h>
#include <stdio.h>
#include <unistd.h>
#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/highgui.hpp>
#include <opencv2/videoio.hpp>
#include <iostream>
#include <string>

using namespace cv;

int main(int argc, char **argv) {
    MPI_Init(&argc, &argv);

    int rank, num_procs;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &num_procs);

    int totalFrames = 0;
    std::string input_path = "video_input.mp4"; 

    if (rank == 0) {
        VideoCapture cap(input_path);
        if (!cap.isOpened()) {
            std::cerr << "Error: No se pudo abrir el video " << input_path << std::endl;
            MPI_Abort(MPI_COMM_WORLD, -1);
        }
        totalFrames = static_cast<int>(cap.get(cv::CAP_PROP_FRAME_COUNT));
        printf("[Master] Video abierto. Total Frames: %d\n", totalFrames);
        cap.release();
    }

    MPI_Bcast(&totalFrames, 1, MPI_INT, 0, MPI_COMM_WORLD);

    int frames_per_rank = totalFrames / num_procs;

    int start = rank * frames_per_rank;

    int end = (rank == num_procs - 1) ? totalFrames : start + frames_per_rank;

    printf("[MPI Rank %d] Dominio asignado: Frames %d a %d (Total: %d)\n", rank, start, end, (end - start));

    VideoCapture capWorker(input_path);
    if(capWorker.isOpened()) {
        capWorker.set(cv::CAP_PROP_POS_FRAMES, start);
        
        double current_pos = capWorker.get(cv::CAP_PROP_POS_FRAMES);
        printf("[MPI Rank %d] Video posicionado correctamente en frame %.0f\n", rank, current_pos);
        
        capWorker.release();
    }

    MPI_Finalize();
    return 0;
}