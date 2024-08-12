#pragma once

#include <glib.h>

G_BEGIN_DECLS

typedef struct CUstream_st* cudaStream_t;
typedef unsigned long long cudaTextureObject_t;
typedef unsigned long long cudaSurfaceObject_t;

typedef struct _dim3
{
  unsigned int x;
  unsigned int y;
  unsigned int z;
} dim3;

G_END_DECLS
