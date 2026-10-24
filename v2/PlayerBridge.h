#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
void* ds_create(const char* const* paths,const double* matched,const double* original,int count);
void ds_destroy(void* handle);
void ds_render(void* handle,float* left,float* right,int frames);
void ds_select(void* handle,int index);
void ds_play(void* handle,int playing);
void ds_seek(void* handle,double seconds);
void ds_mode(void* handle,int originalLevel);
void ds_mono(void* handle,int mono);
double ds_position(void* handle);
double ds_duration(void* handle,int index);
int64_t ds_underruns(void* handle);
const char* ds_error(void);
#ifdef __cplusplus
}
#endif
