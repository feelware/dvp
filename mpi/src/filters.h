#ifndef FILTERS_H
#define FILTERS_H

#include <vector>

// Filter types
enum FilterType {
  FILTER_NONE = 0,
  FILTER_GRAYSCALE = 1,
  FILTER_BLUR = 2,
  FILTER_INVERT = 3,
  FILTER_EDGE = 4
};

// Interface for CPU/OpenMP filters
void apply_filter_cpu(int choice, unsigned char *frame, int width, int height);

// Interface for GPU/CUDA filters
// Returns float representing milliseconds taken, or -1.0 if failed/not
// available
float apply_filter_gpu(int choice, unsigned char *frame, int width, int height);

// Check if GPU is available
bool is_gpu_available();

#endif // FILTERS_H
