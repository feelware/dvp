#ifndef VIDEO_DECOMPOSE_H
#define VIDEO_DECOMPOSE_H

#ifdef __cplusplus
extern "C" {
#endif

int decompose_video(const char *video_file, int rank, int num_procs);

#ifdef __cplusplus
}
#endif

#endif