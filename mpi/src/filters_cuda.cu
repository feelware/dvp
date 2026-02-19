#include "filters.h"
#include <cmath>
#include <cuda_runtime.h>
#include <iostream>

#define BLOCK_SIZE 16

// Error checking macro
#define CUDA_CHECK(call)                                                       \
  do {                                                                         \
    cudaError_t err = call;                                                    \
    if (err != cudaSuccess) {                                                  \
      std::cerr << "CUDA Error: " << cudaGetErrorString(err) << " at "         \
                << __FILE__ << ":" << __LINE__ << std::endl;                   \
      return -1.0f;                                                            \
    }                                                                          \
  } while (0)

__global__ void kernel_invert(unsigned char *frame, int width, int height) {
  int x = blockIdx.x * blockDim.x + threadIdx.x;
  int y = blockIdx.y * blockDim.y + threadIdx.y;
  int idx = (y * width + x) * 3;

  if (x < width && y < height) {
    frame[idx + 0] = 255 - frame[idx + 0];
    frame[idx + 1] = 255 - frame[idx + 1];
    frame[idx + 2] = 255 - frame[idx + 2];
  }
}

__global__ void kernel_grayscale(unsigned char *frame, int width, int height) {
  int x = blockIdx.x * blockDim.x + threadIdx.x;
  int y = blockIdx.y * blockDim.y + threadIdx.y;
  int idx = (y * width + x) * 3;

  if (x < width && y < height) {
    unsigned char b = frame[idx + 0];
    unsigned char g = frame[idx + 1];
    unsigned char r = frame[idx + 2];

    unsigned char gray = (unsigned char)(0.299f * r + 0.587f * g + 0.114f * b);

    frame[idx + 0] = gray;
    frame[idx + 1] = gray;
    frame[idx + 2] = gray;
  }
}

__global__ void kernel_blur(unsigned char *in, unsigned char *out, int width,
                            int height, int radius) {
  int x = blockIdx.x * blockDim.x + threadIdx.x;
  int y = blockIdx.y * blockDim.y + threadIdx.y;
  int channels = 3;

  if (x >= width || y >= height)
    return;

  int sumB = 0;
  int sumG = 0;
  int sumR = 0;
  int count = 0;

  for (int dy = -radius; dy <= radius; ++dy) {
    for (int dx = -radius; dx <= radius; ++dx) {
      int sx = x + dx;
      int sy = y + dy;

      // Clamp to edge
      if (sx < 0)
        sx = 0;
      if (sx >= width)
        sx = width - 1;
      if (sy < 0)
        sy = 0;
      if (sy >= height)
        sy = height - 1;

      int src_idx = (sy * width + sx) * channels;
      sumB += in[src_idx + 0];
      sumG += in[src_idx + 1];
      sumR += in[src_idx + 2];
      count++;
    }
  }

  int dst_idx = (y * width + x) * channels;
  // Using 9 as divisor to match reference/common box blur logic or count for
  // correctness Reference used count in one place and 9 in another. We use
  // count for safety.
  out[dst_idx + 0] = (unsigned char)(sumB / count);
  out[dst_idx + 1] = (unsigned char)(sumG / count);
  out[dst_idx + 2] = (unsigned char)(sumR / count);
}

__global__ void kernel_edge(unsigned char *in, unsigned char *out, int width,
                            int height) {
  int x = blockIdx.x * blockDim.x + threadIdx.x;
  int y = blockIdx.y * blockDim.y + threadIdx.y;
  int channels = 3;

  if (x >= 1 && x < width - 1 && y >= 1 && y < height - 1) {
    float gx[3][3] = {{-1, 0, 1}, {-2, 0, 2}, {-1, 0, 1}};
    float gy[3][3] = {{-1, -2, -1}, {0, 0, 0}, {1, 2, 1}};

    float sumRx = 0, sumRy = 0;

    // Using luminance for edge detection like in filter_cuda.cu reference (L
    // formula)
    for (int dy = -1; dy <= 1; dy++) {
      for (int dx = -1; dx <= 1; dx++) {
        int nx = x + dx;
        int ny = y + dy;
        int idx = (ny * width + nx) * channels;

        unsigned char b = in[idx + 0];
        unsigned char g = in[idx + 1];
        unsigned char r = in[idx + 2];

        float val = 0.299f * r + 0.587f * g + 0.114f * b;

        sumRx += val * gx[dx + 1][dy + 1];
        sumRy += val * gy[dx + 1][dy + 1];
      }
    }

    int mag = (int)sqrtf(sumRx * sumRx + sumRy * sumRy);
    if (mag > 255)
      mag = 255;

    int out_idx = (y * width + x) * channels;
    out[out_idx + 0] = (unsigned char)mag;
    out[out_idx + 1] = (unsigned char)mag;
    out[out_idx + 2] = (unsigned char)mag;
  } else if (x < width && y < height) {
    // Zero out borders
    int idx = (y * width + x) * channels;
    out[idx + 0] = 0;
    out[idx + 1] = 0;
    out[idx + 2] = 0;
  }
}

// Host wrapper
float apply_filter_gpu(int choice, unsigned char *frame, int width,
                       int height) {
  unsigned char *d_in = NULL;
  unsigned char *d_out = NULL;
  size_t size = width * height * 3 * sizeof(unsigned char);

  cudaEvent_t start, stop;
  cudaEventCreate(&start);
  cudaEventCreate(&stop);

  // Allocate memory on GPU
  CUDA_CHECK(cudaMalloc((void **)&d_in, size));

  // Copy data to GPU
  CUDA_CHECK(cudaMemcpy(d_in, frame, size, cudaMemcpyHostToDevice));

  // Setup grid and blocks
  dim3 block(BLOCK_SIZE, BLOCK_SIZE);
  dim3 grid((width + block.x - 1) / block.x, (height + block.y - 1) / block.y);

  cudaEventRecord(start);

  if (choice == FILTER_INVERT) {
    kernel_invert<<<grid, block>>>(d_in, width, height);
    // Copy back directly from d_in as it's in-place
    CUDA_CHECK(cudaMemcpy(frame, d_in, size, cudaMemcpyDeviceToHost));
  } else if (choice == FILTER_GRAYSCALE) {
    kernel_grayscale<<<grid, block>>>(d_in, width, height);
    CUDA_CHECK(cudaMemcpy(frame, d_in, size, cudaMemcpyDeviceToHost));
  } else if (choice == FILTER_BLUR) {
    // Blur needs separate output buffer
    CUDA_CHECK(cudaMalloc((void **)&d_out, size));
    kernel_blur<<<grid, block>>>(d_in, d_out, width, height, 1); // Radius 1
    CUDA_CHECK(cudaMemcpy(frame, d_out, size, cudaMemcpyDeviceToHost));
    cudaFree(d_out);
  } else if (choice == FILTER_EDGE) {
    // Edge needs separate output buffer
    CUDA_CHECK(cudaMalloc((void **)&d_out, size));
    kernel_edge<<<grid, block>>>(d_in, d_out, width, height);
    CUDA_CHECK(cudaMemcpy(frame, d_out, size, cudaMemcpyDeviceToHost));
    cudaFree(d_out);
  }

  cudaEventRecord(stop);
  cudaEventSynchronize(stop);

  float milliseconds = 0;
  cudaEventElapsedTime(&milliseconds, start, stop);

  cudaFree(d_in);
  cudaEventDestroy(start);
  cudaEventDestroy(stop);

  return milliseconds;
}

bool is_gpu_available() {
  int deviceCount = 0;
  cudaError_t err = cudaGetDeviceCount(&deviceCount);
  return (err == cudaSuccess && deviceCount > 0);
}
