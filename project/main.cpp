#include <mpi.h>
#include <stdio.h>
#include <unistd.h>
#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/highgui.hpp>
#include <opencv2/videoio.hpp>  // Necesario para VideoCapture
#include <iostream>
#include <string>

using namespace cv;

int main(int argc, char **argv) {
    // 1. Inicializar MPI
    MPI_Init(&argc, &argv);

    int rank, num_procs;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &num_procs);

    // Variables importantes
    int totalFrames = 0;
    std::string input_path = "video_input.mp4"; // Nombre hardcodeado para la prueba, o recibir por argv

    // 2. Solo el Master (Rank 0) lee los metadatos del video
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

    // 3. Broadcast de la información a todos los workers
    // (El Master avisa a todos cuántos frames hay en total)
    MPI_Bcast(&totalFrames, 1, MPI_INT, 0, MPI_COMM_WORLD);


    // ========================================================
    // INICIO ACTIVIDAD SEMANA 10: DOMAIN DECOMPOSITION
    // ========================================================
    
    // A. Calcular cuántos frames le tocan a cada nodo
    int frames_per_rank = totalFrames / num_procs;

    // B. Calcular frame de inicio
    int start = rank * frames_per_rank;

    // C. Calcular frame de fin (Manejo de residuo para el último nodo)
    int end = (rank == num_procs - 1) ? totalFrames : start + frames_per_rank;

    // ========================================================
    // FIN ACTIVIDAD
    // ========================================================


    printf("[MPI Rank %d] Dominio asignado: Frames %d a %d (Total: %d)\n", 
           rank, start, end, (end - start));

    // 4. Simulación de Apertura y Posicionamiento (Validación de lógica)
    // En el proyecto real, aquí empieza el procesamiento.
    VideoCapture capWorker(input_path);
    if(capWorker.isOpened()) {
        // Esta es la línea CLAVE que demuestra que la descomposición funciona:
        // Cada worker salta directamente a su frame de inicio.
        capWorker.set(cv::CAP_PROP_POS_FRAMES, start);
        
        double current_pos = capWorker.get(cv::CAP_PROP_POS_FRAMES);
        printf("[MPI Rank %d] Video posicionado correctamente en frame %.0f\n", rank, current_pos);
        
        // Aquí iría el bucle de procesamiento:
        // for (int i = start; i < end; i++) { ... }
        
        capWorker.release();
    }

    MPI_Finalize();
    return 0;
}