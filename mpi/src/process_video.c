#include "video_decompose.h"
#include <curl/curl.h>
#include <mpi.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define MINIO_ENDPOINT "http://minio:9000"
#define MINIO_BUCKET "uploads"
#define NUM_DOWNLOAD_THREADS 4
#define CHUNK_SIZE (1024 * 1024) // 1 MB chunks

typedef struct {
  char *data;
  size_t size;
  size_t capacity;
} MemoryBuffer;

typedef struct {
  char *url;
  long start_byte;
  long end_byte;
  char *output_buffer;
  size_t bytes_downloaded;
  int thread_id;
  int success;
} DownloadChunk;

typedef struct {
  DownloadChunk *chunks;
  int num_chunks;
  pthread_mutex_t progress_mutex;
  size_t total_downloaded;
  size_t total_size;
} DownloadContext;

static size_t write_memory_callback(void *contents, size_t size, size_t nmemb,
                                    void *userp) {
  size_t realsize = size * nmemb;
  MemoryBuffer *mem = (MemoryBuffer *)userp;

  if (mem->size + realsize > mem->capacity) {
    size_t new_capacity = mem->capacity * 2;
    if (new_capacity < mem->size + realsize) {
      new_capacity = mem->size + realsize;
    }
    char *new_data = (char *)malloc(new_capacity);
    if (new_data == NULL) {
      fprintf(stderr, "Error: No se pudo realocar memoria\n");
      return 0;
    }
    mem->data = new_data;
    mem->capacity = new_capacity;
  }

  memcpy(&(mem->data[mem->size]), contents, realsize);
  mem->size += realsize;
  return realsize;
}

long get_file_size(const char *url) {
  CURL *curl;
  CURLcode res;
  long file_size = -1;

  curl = curl_easy_init();
  if (curl) {
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, NULL);

    res = curl_easy_perform(curl);

    if (res == CURLE_OK) {
      double content_length;
      res = curl_easy_getinfo(curl, CURLINFO_CONTENT_LENGTH_DOWNLOAD,
                              &content_length);
      if (res == CURLE_OK && content_length > 0) {
        file_size = (long)content_length;
      }
    }

    curl_easy_cleanup(curl);
  }

  return file_size;
}

void *download_chunk_thread(void *arg) {
  DownloadChunk *chunk = (DownloadChunk *)arg;
  CURL *curl;
  CURLcode res;
  MemoryBuffer mem = {0};

  mem.capacity = chunk->end_byte - chunk->start_byte + 1;
  mem.data = (char *)malloc(mem.capacity);
  if (mem.data == NULL) {
    fprintf(stderr, "Thread %d: Error al asignar memoria\n", chunk->thread_id);
    chunk->success = 0;
    return NULL;
  }

  curl = curl_easy_init();
  if (!curl) {
    fprintf(stderr, "Thread %d: Error al inicializar curl\n", chunk->thread_id);
    free(mem.data);
    chunk->success = 0;
    return NULL;
  }

  char range[128];
  snprintf(range, sizeof(range), "%ld-%ld", chunk->start_byte, chunk->end_byte);

  curl_easy_setopt(curl, CURLOPT_URL, chunk->url);
  curl_easy_setopt(curl, CURLOPT_RANGE, range);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_memory_callback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&mem);
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, 300L);

  printf("Thread %d: Descargando bytes %ld-%ld (%ld bytes)\n", chunk->thread_id,
         chunk->start_byte, chunk->end_byte,
         chunk->end_byte - chunk->start_byte + 1);
  fflush(stdout);

  res = curl_easy_perform(curl);

  if (res != CURLE_OK) {
    fprintf(stderr, "Thread %d: Error en descarga: %s\n", chunk->thread_id,
            curl_easy_strerror(res));
    chunk->success = 0;
  } else {
    chunk->output_buffer = mem.data;
    chunk->bytes_downloaded = mem.size;
    chunk->success = 1;
    printf("Thread %d: Descarga completada (%zu bytes)\n", chunk->thread_id,
           mem.size);
    fflush(stdout);
  }

  curl_easy_cleanup(curl);
  return NULL;
}

char *generate_presigned_url(const char *bucket, const char *object_key) {
  char *presigned_url = (char *)malloc(1024);
  if (!presigned_url) {
    fprintf(stderr, "Error al asignar memoria para URL\n");
    return NULL;
  }

  snprintf(presigned_url, 1024, "%s/%s/%s", MINIO_ENDPOINT, bucket, object_key);

  printf("URL publica generada: %s\n", presigned_url);
  fflush(stdout);

  return presigned_url;
}

int download_video_parallel(const char *video_path, const char *output_file,
                            int rank) {
  // Only one process per node should download
  MPI_Comm node_comm;
  MPI_Comm_split_type(MPI_COMM_WORLD, MPI_COMM_TYPE_SHARED, rank, MPI_INFO_NULL,
                      &node_comm);

  int node_rank;
  MPI_Comm_rank(node_comm, &node_rank);

  int success = 1;

  // Node leader downloads
  if (node_rank == 0) {
    // Only print logs from global rank 0 (Master) or maybe leader on each node
    // for debug Let's print only from global rank 0 to keep logs clean, or use
    // [Node Leader rank] prefix For debugging now, let's print if process fails
    // mainly.

    char bucket[256];
    char object_key[512];

    if (sscanf(video_path, "%255[^/]/%511s", bucket, object_key) != 2) {
      fprintf(stderr, "[Rank %d] Error: video_path format check failed: %s\n",
              rank, video_path);
      success = 0;
    } else {
      char *url = generate_presigned_url(bucket, object_key);
      if (!url) {
        fprintf(stderr, "[Rank %d] Error: Failed to generate presigned URL\n",
                rank);
        success = 0;
      } else {
        long file_size = get_file_size(url);
        if (file_size <= 0) {
          fprintf(stderr, "[Rank %d] Error: Failed to get file size\n", rank);
          success = 0;
        } else {
          int num_threads = NUM_DOWNLOAD_THREADS;
          long chunk_size = file_size / num_threads;
          DownloadChunk *chunks =
              (DownloadChunk *)malloc(num_threads * sizeof(DownloadChunk));
          pthread_t *threads =
              (pthread_t *)malloc(num_threads * sizeof(pthread_t));

          if (chunks && threads) {
            for (int i = 0; i < num_threads; i++) {
              chunks[i].url = url;
              chunks[i].thread_id = i;
              chunks[i].start_byte = i * chunk_size;
              chunks[i].end_byte = (i == num_threads - 1)
                                       ? file_size - 1
                                       : (i + 1) * chunk_size - 1;
              chunks[i].output_buffer = NULL;
              chunks[i].bytes_downloaded = 0;
              chunks[i].success = 0;
              pthread_create(&threads[i], NULL, download_chunk_thread,
                             &chunks[i]);
            }

            for (int i = 0; i < num_threads; i++)
              pthread_join(threads[i], NULL);

            // Check thread success
            int thread_success = 1;
            for (int i = 0; i < num_threads; i++) {
              if (!chunks[i].success)
                thread_success = 0;
            }

            if (thread_success) {
              FILE *fp = fopen(output_file, "wb");
              if (fp) {
                for (int i = 0; i < num_threads; i++) {
                  if (chunks[i].success)
                    fwrite(chunks[i].output_buffer, 1,
                           chunks[i].bytes_downloaded, fp);
                  if (chunks[i].output_buffer)
                    free(chunks[i].output_buffer);
                }
                fclose(fp);
                if (rank == 0)
                  printf("Descarga completada exitosamente: %s\n", output_file);
              } else {
                fprintf(stderr,
                        "[Rank %d] Error: Failed to open output file %s\n",
                        rank, output_file);
                success = 0;
              }
            } else {
              success = 0;
            }

            free(chunks);
            free(threads);
          }
          free(url);
        }
      }
    }
  }

  // Broadcast success status to other ranks on the node
  MPI_Bcast(&success, 1, MPI_INT, 0, node_comm);

  // Ensure file is written before others proceed
  MPI_Barrier(node_comm);
  MPI_Comm_free(&node_comm);

  return success;
}

int main(int argc, char **argv) {
  MPI_Init(&argc, &argv);

  int rank, num_procs;
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);
  MPI_Comm_size(MPI_COMM_WORLD, &num_procs);

  if (argc < 4) {
    if (rank == 0) {
      fprintf(stderr, "Usage: %s <job_id> <video_path> <task> [params]\n",
              argv[0]);
    }
    MPI_Finalize();
    return 1;
  }

  const char *job_id = argv[1];
  const char *video_path = argv[2];
  const char *task = argv[3];
  const char *params = (argc > 4) ? argv[4] : "{}";
  char output_file[512];

  // Initialize curl globally for all processes (low overhead, safe)
  curl_global_init(CURL_GLOBAL_DEFAULT);
  snprintf(output_file, sizeof(output_file), "/tmp/video_%s.mp4", job_id);

  if (rank == 0) {
    printf("========================================\n");
    printf("PROCESS_VIDEO - Iniciando procesamiento\n");
    printf("Job ID: %s\n", job_id);
    printf("MPI Processes: %d\n", num_procs);
    printf("========================================\n");
    fflush(stdout);
  }

  // ALL ranks enter download logic, but only one per node actually downloads
  if (!download_video_parallel(video_path, output_file, rank)) {
    if (rank == 0)
      fprintf(stderr,
              "Error: Fallo la descarga del video en alguno de los nodos\n");
    curl_global_cleanup();
    MPI_Finalize();
    return 1;
  }

  MPI_Barrier(MPI_COMM_WORLD);

  if (rank == 0) {
    printf("Iniciando descomposición del video...\n");
    fflush(stdout);
  }

  if (!decompose_video(output_file, rank, num_procs, task, params)) {
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
  }

  curl_global_cleanup();
  MPI_Finalize();
  return 0;
}