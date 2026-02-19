#include <mpi.h>
#include <omp.h>
#include <opencv2/opencv.hpp>
#include <opencv2/videoio.hpp>
#include <stdio.h>
#include <stdlib.h>

using namespace cv;

extern "C" int apply_lut(const char *input_video_path, int rank, int num_procs) {
    int total_frames = 0;

    if (rank == 0) {
        VideoCapture cap(input_video_path);
        if (!cap.isOpened()) {
            fprintf(stderr, "Error: No se pudo abrir el video %s\n", input_video_path);
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

    VideoCapture cap_worker(input_video_path);
    if (!cap_worker.isOpened()) {
        fprintf(stderr, "[Rank %d] Error: No se pudo abrir el video para worker\n", rank);
        return 0;
    }
    cap_worker.set(CAP_PROP_POS_FRAMES, start);

    Mat lut_image = imread("/home/mpiuser/luts/bw.png", IMREAD_COLOR);
    if (lut_image.empty()) {
        fprintf(stderr, "[Rank %d] Error: No se pudo cargar el archivo LUT /tmp/lut.png\n", rank);
        return 0;
    }

    if (lut_image.rows != 512 || lut_image.cols != 512) {
        fprintf(stderr, "[Rank %d] Error: El archivo LUT debe ser de tamaño 512x512\n", rank);
        return 0;
    }

    int sizes[3] = { 64, 64, 64 };
    Mat lut(3, sizes, CV_8UC3);
    for (int r = 0; r < 64; r++) {
        for (int g = 0; g < 64; g++) {
            for (int b = 0; b < 64; b++) {
                int lut_x = r * 8 + g / 8;
                int lut_y = b * 8 + g % 8;
                Vec3b pixel = lut_image.at<Vec3b>(lut_y, lut_x);
                lut.at<Vec3b>(r, g, b) = pixel;
            }
        }
    }

    const char *video_name = strrchr(input_video_path, '/');
    if (video_name) {
        video_name++;
    } else {
        video_name = input_video_path;
    }

    char output_video_path[256];
    snprintf(output_video_path, sizeof(output_video_path), "/tmp/%s_rank_%d.avi", video_name, rank);
    int fourcc = VideoWriter::fourcc('M', 'J', 'P', 'G'); // MJPG codec
    double fps = cap_worker.get(CAP_PROP_FPS);
    Size frame_size(
        (int)cap_worker.get(CAP_PROP_FRAME_WIDTH),
        (int)cap_worker.get(CAP_PROP_FRAME_HEIGHT)
    );
    VideoWriter video_writer(output_video_path, fourcc, fps, frame_size);

    if (!video_writer.isOpened()) {
        fprintf(stderr, "[Rank %d] Error: No se pudo abrir el archivo de salida %s\n", rank, output_video_path);
        return 0;
    }

    Mat frame, output_frame;
    for (int i = start; i < end; i++) {
        cap_worker.set(CAP_PROP_POS_FRAMES, i);
        cap_worker >> frame;

        if (frame.empty()) {
            fprintf(stderr, "[Rank %d] Error: No se pudo leer el frame %d\n", rank, i);
            continue;
        }

        output_frame = Mat(frame.size(), frame.type());
        #pragma omp parallel for collapse(2)
        for (int y = 0; y < frame.rows; y++) {
            for (int x = 0; x < frame.cols; x++) {
                Vec3b pixel = frame.at<Vec3b>(y, x);
                int r = pixel[2]; // Red channel
                int g = pixel[1]; // Green channel
                int b = pixel[0]; // Blue channel

                Vec3b lut_pixel = lut.at<Vec3b>(r / 4, g / 4, b / 4);
                output_frame.at<Vec3b>(y, x) = lut_pixel;
            }
        }

        video_writer.write(output_frame);
    }

    cap_worker.release();
    video_writer.release();
    return 1;
}
