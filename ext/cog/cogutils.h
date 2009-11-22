
#ifndef __COG_UTILS_H__
#define __COG_UTILS_H__

#if defined(_MSC_VER)
#ifndef COG_NO_STDINT_TYPEDEFS
typedef __int8 int8_t;
typedef __int16 int16_t;
typedef __int32 int32_t;
typedef unsigned __int8 uint8_t;
typedef unsigned __int16 uint16_t;
typedef unsigned __int32 uint32_t;
#endif
#else
#include "_stdint.h"
#endif

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif

typedef void CogMemoryDomain;
typedef unsigned int cog_bool;

#ifdef COG_ENABLE_UNSTABLE_API

#define COG_PICTURE_NUMBER_INVALID (-1)

#define ARRAY_SIZE(x) (sizeof(x)/sizeof(x[0]))
#define DIVIDE_ROUND_UP(a,b) (((a) + (b) - 1)/(b))
#ifndef MIN
#define MIN(a,b) ((a)<(b) ? (a) : (b))
#endif
#ifndef MAX
#define MAX(a,b) ((a)>(b) ? (a) : (b))
#endif
#ifndef CLAMP
#define CLAMP(x,a,b) ((x)<(a) ? (a) : ((x)>(b) ? (b) : (x)))
#endif
#define NEED_CLAMP(x,y,a,b) ((x) < (a) || (y) > (b))
#define ROUND_UP_SHIFT(x,y) (((x) + (1<<(y)) - 1)>>(y))
#define ROUND_UP_POW2(x,y) (((x) + (1<<(y)) - 1)&((~0)<<(y)))
#define ROUND_UP_2(x) ROUND_UP_POW2(x,1)
#define ROUND_UP_4(x) ROUND_UP_POW2(x,2)
#define ROUND_UP_8(x) ROUND_UP_POW2(x,3)
#define ROUND_UP_64(x) ROUND_UP_POW2(x,6)
#define OFFSET(ptr,offset) ((void *)(((uint8_t *)(ptr)) + (offset)))
#define ROUND_SHIFT(x,y) (((x) + (1<<((y)-1)))>>(y))

#define cog_divide(a,b) (((a)<0)?(((a) - (b) + 1)/(b)):((a)/(b)))

#endif

#define COG_OFFSET(ptr,offset) ((void *)(((uint8_t *)(ptr)) + (offset)))
#define COG_GET(ptr, offset, type) (*(type *)((uint8_t *)(ptr) + (offset)) )

#if defined(__GNUC__) && defined(__GNUC_MINOR__)
#define COG_GNUC_PREREQ(maj, min) \
  ((__GNUC__ << 16) + __GNUC_MINOR__ >= ((maj) << 16) + (min))
#else
#define COG_GNUC_PREREQ(maj, min) 0
#endif

#if COG_GNUC_PREREQ(3,3) && defined(__ELF__)
#define COG_INTERNAL __attribute__ ((visibility ("internal")))
#else
#define COG_INTERNAL
#endif

#ifdef __cplusplus
#define COG_BEGIN_DECLS extern "C" {
#define COG_END_DECLS }
#else
#define COG_BEGIN_DECLS
#define COG_END_DECLS
#endif


COG_BEGIN_DECLS

void * cog_malloc (int size);
void * cog_malloc0 (int size);
void * cog_realloc (void *ptr, int size);
void cog_free (void *ptr);

int cog_utils_multiplier_to_quant_index (double x);
int cog_dequantise (int q, int quant_factor, int quant_offset);
int cog_quantise (int value, int quant_factor, int quant_offset);
void cog_quantise_s16 (int16_t *dest, int16_t *src, int quant_factor,
    int quant_offset, int n);
void cog_quantise_s16_table (int16_t *dest, int16_t *src, int quant_index,
    cog_bool is_intra, int n);
void cog_dequantise_s16 (int16_t *dest, int16_t *src, int quant_factor,
    int quant_offset, int n);
void cog_dequantise_s16_table (int16_t *dest, int16_t *src, int quant_index,
    cog_bool is_intra, int n);
double cog_utils_probability_to_entropy (double x);
double cog_utils_entropy (double a, double total);
void cog_utils_reduce_fraction (int *n, int *d);
double cog_utils_get_time (void);

COG_END_DECLS

#endif

