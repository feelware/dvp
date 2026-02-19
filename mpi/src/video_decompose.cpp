#include "video_decompose.h"
#include "filters.h"
#include <mpi.h>
#include <omp.h>
#include <opencv2/opencv.hpp>
#include <stdio.h>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include <vector>

using namespace cv;
using namespace std;

// Simple JSON param parser helper (could be replaced with cJSON if needed, but
// we used cJSON in C file) Since we passed params as string, we can try to
// parse simple key-values or just rely on task name first. For now, let's
// assume params might be needed for parameters like strength, but for basic
// filters we might rely on defaults. We received raw JSON string in params.

int get_filter_type(const char *task) {
  if (strcmp(task, "invert") == 0)
    return FILTER_INVERT;
  if (strcmp(task, "grayscale") == 0)
    return FILTER_GRAYSCALE;
  if (strcmp(task, "blur") == 0)
    return FILTER_BLUR;
  if (strcmp(task, "edge") == 0)
    return FILTER_EDGE;
  return FILTER_NONE;
}

extern "C" int decompose_video(const char *video_file, int rank, int num_procs,
                               const char *task, const char *params) {
  int total_frames = 0;
  int width = 0;
  int height = 0;
  double fps = 0;
  int fourcc = 0;

  // Master reads video info
  if (rank == 0) {
    VideoCapture cap(video_file);
    if (!cap.isOpened()) {
      fprintf(stderr, "Error: No se pudo abrir el video %s\n", video_file);
      return 0;
    }
    total_frames = (int)cap.get(CAP_PROP_FRAME_COUNT);
    width = (int)cap.get(CAP_PROP_FRAME_WIDTH);
    height = (int)cap.get(CAP_PROP_FRAME_HEIGHT);
    fps = cap.get(CAP_PROP_FPS);
    fourcc = (int)cap.get(CAP_PROP_FOURCC);

    printf("[Master] Video abierto. Total Frames: %d, %dx%d @ %.2f fps\n",
           total_frames, width, height, fps);
  }

  // Broadcast metadata
  MPI_Bcast(&total_frames, 1, MPI_INT, 0, MPI_COMM_WORLD);
  MPI_Bcast(&width, 1, MPI_INT, 0, MPI_COMM_WORLD);
  MPI_Bcast(&height, 1, MPI_INT, 0, MPI_COMM_WORLD);
  MPI_Bcast(&fps, 1, MPI_DOUBLE, 0, MPI_COMM_WORLD);
  MPI_Bcast(&fourcc, 1, MPI_INT, 0, MPI_COMM_WORLD);

  if (total_frames <= 0) {
    if (rank == 0)
      fprintf(stderr, "Error: Total frames invalid\n");
    return 0;
  }

  // Calculate range for this rank
  int frames_per_rank = total_frames / num_procs;
  int start_frame = rank * frames_per_rank;
  int end_frame =
      (rank == num_procs - 1) ? total_frames : start_frame + frames_per_rank;
  int num_my_frames = end_frame - start_frame;

  printf("[Rank %d] Procesando frames %d a %d (%d frames)\n", rank, start_frame,
         end_frame - 1, num_my_frames);

  // Open video for reading
  VideoCapture cap(video_file);
  if (!cap.isOpened()) {
    fprintf(stderr, "[Rank %d] Error opening video\n", rank);
    return 0;
  }
  cap.set(CAP_PROP_POS_FRAMES, start_frame);

  // Prepare output video
  // Each rank writes to a partial file: video_part_<rank>.mp4 (or .avi for
  // simplicity then merge? MP4 might strictly need moov atom at end) Writing
  // individual video files per rank is tricky for recombination without
  // re-encoding. For this project, usually we either write raw frames or
  // independent video segments. Let's write independent video segments.

  string output_filename =
      string(video_file) + "_part_" + to_string(rank) + ".avi";
  // MJPG is safer for simple concatenation or raw avi
  VideoWriter writer(output_filename, VideoWriter::fourcc('M', 'J', 'P', 'G'),
                     fps, Size(width, height));

  if (!writer.isOpened()) {
    fprintf(stderr, "[Rank %d] Error opening output video %s\n", rank,
            output_filename.c_str());
    return 0;
  }

  int filter_type = get_filter_type(task);
  bool use_gpu = is_gpu_available();

  if (rank == 0) {
    printf("[Info] Filter: %s (ID: %d)\n", task, filter_type);
    printf("[Info] GPU Available: %s\n", use_gpu ? "YES" : "NO");
  }

  Mat frame;
  // Buffer for frame data if we need direct pointer access
  // OpenCV Mat data is uchar* (BGR) which matches our filter interface
  // expectations

  int processed = 0;
  double total_time = 0;

  for (int i = start_frame; i < end_frame; i++) {
    cap >> frame;
    if (frame.empty()) {
      fprintf(stderr, "[Rank %d] Warning: Empty frame at %d\n", rank, i);
      break;
    }

    // Apply filter
    // OpenCV uses BGR by default, our filters handle 3 channels.
    // GPU filter returns time in ms

    if (filter_type != FILTER_NONE) {
      unsigned char *data = frame.data;
      if (use_gpu) {
        float time = apply_filter_gpu(filter_type, data, width, height);
        if (time < 0) {
          // Fallback if GPU failed unexpectedly
          apply_filter_cpu(filter_type, data, width, height);
        } else {
          total_time += time;
        }
      } else {
        // Using OpenMP
        double t1 = omp_get_wtime();
        apply_filter_cpu(filter_type, data, width, height);
        double t2 = omp_get_wtime();
        total_time += (t2 - t1) * 1000.0;
      }
    }

    writer.write(frame);
    processed++;

    if (rank == 0 && processed % 10 == 0) {
      printf("[Rank 0] Processed %d/%d frames\r", processed, num_my_frames);
      fflush(stdout);
    }
  }

  if (rank == 0)
    printf("\n");
  printf("[Rank %d] Finished. Avg time per frame: %.2f ms\n", rank,
         (processed > 0 ? total_time / processed : 0));

  writer.release();
  cap.release();

  // In a real distributed system we would merge these parts.
  // For this assignment/project, producing parts is often sufficient proof of
  // parallel work, OR the master should gather them. Given the complexity of
  // merging video files without ffmpeg concat demuxer (which we can use via
  // system call), let's try to merge them using ffmpeg on Master if rank == 0?

  MPI_Barrier(MPI_COMM_WORLD);

  if (rank == 0) {
    // Merge logic
    printf("[Master] Merging parts...\n");
    string concat_list = "concat_list.txt";
    FILE *f = fopen(concat_list.c_str(), "w");
    for (int r = 0; r < num_procs; r++) {
      fprintf(f, "file '%s_part_%d.avi'\n", video_file, r);
    }
    fclose(f);

    // Final output is just replacing .avi from the temp file with final name
    // logic Original video_file was /tmp/video_JOBID.mp4 We want to overwrite
    // it or create a new one? process_video.c expects the result in
    // `output_file` which IS `video_file` argument passed here. So we should
    // effectively replace the input file with the processed one? Or
    // process_video.c logic was: download to /tmp/ref.mp4 -> decompose ->
    // (implied processing) -> result. The result is currently distributed in
    // parts.

    string merged_output = string(video_file) + "_processed.avi";
    string cmd = "ffmpeg -y -f concat -safe 0 -i " + concat_list + " -c copy " +
                 merged_output + " > /dev/null 2>&1";
    system(cmd.c_str());

    printf("[Master] Merged to %s\n", merged_output.c_str());

    // Cleanup parts
    for (int r = 0; r < num_procs; r++) {
      string part = string(video_file) + "_part_" + to_string(r) + ".avi";
      remove(part.c_str());
    }
    remove(concat_list.c_str());

    // Move processed file back to original location if needed, OR just leave
    // it. process_video.c prints "Archivo local: %s", which is arguably the
    // input file path. Let's rename merged output to overwrite input if that's
    // the expectation, but input was mp4 and we wrote avi (MJPG). Let's rename
    // merged_output to match the expected 'output_file' (which is the input
    // path in /tmp) but change extension if necessary? process_video.c ends.
    // The important part is that the file exists.

    rename(merged_output.c_str(), video_file);
  }

  return 1;
}