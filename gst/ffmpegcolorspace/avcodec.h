#ifndef AVCODEC_H
#define AVCODEC_H

/**
 * @file avcodec.h
 * external api header.
 */


#ifdef __cplusplus
extern "C" {
#endif

#include "common.h"
#include <sys/types.h> /* size_t */

#define FFMPEG_VERSION_INT     0x000408
#define FFMPEG_VERSION         "0.4.8"
#define LIBAVCODEC_BUILD       4707

#define LIBAVCODEC_VERSION_INT FFMPEG_VERSION_INT
#define LIBAVCODEC_VERSION     FFMPEG_VERSION

#define AV_STRINGIFY(s)	AV_TOSTRING(s)
#define AV_TOSTRING(s) #s
#define LIBAVCODEC_IDENT	"FFmpeg" LIBAVCODEC_VERSION "b" AV_STRINGIFY(LIBAVCODEC_BUILD)

#define AV_NOPTS_VALUE int64_t_C(0x8000000000000000)
#define AV_TIME_BASE 1000000

enum CodecType {
    CODEC_TYPE_UNKNOWN = -1,
    CODEC_TYPE_VIDEO,
    CODEC_TYPE_AUDIO,
    CODEC_TYPE_DATA,
};

/**
 * Pixel format. Notes: 
 *
 * PIX_FMT_RGBA32 is handled in an endian-specific manner. A RGBA
 * color is put together as:
 *  (A << 24) | (R << 16) | (G << 8) | B
 * This is stored as BGRA on little endian CPU architectures and ARGB on
 * big endian CPUs.
 *
 * When the pixel format is palettized RGB (PIX_FMT_PAL8), the palettized
 * image data is stored in AVFrame.data[0]. The palette is transported in
 * AVFrame.data[1] and, is 1024 bytes long (256 4-byte entries) and is
 * formatted the same as in PIX_FMT_RGBA32 described above (i.e., it is
 * also endian-specific). Note also that the individual RGB palette
 * components stored in AVFrame.data[1] should be in the range 0..255.
 * This is important as many custom PAL8 video codecs that were designed
 * to run on the IBM VGA graphics adapter use 6-bit palette components.
 */
enum PixelFormat {
    PIX_FMT_YUV420P,   ///< Planar YUV 4:2:0 (1 Cr & Cb sample per 2x2 Y samples)
    PIX_FMT_YUV422,    
    PIX_FMT_RGB24,     ///< Packed pixel, 3 bytes per pixel, RGBRGB...
    PIX_FMT_BGR24,     ///< Packed pixel, 3 bytes per pixel, BGRBGR...
    PIX_FMT_YUV422P,   ///< Planar YUV 4:2:2 (1 Cr & Cb sample per 2x1 Y samples)
    PIX_FMT_YUV444P,   ///< Planar YUV 4:4:4 (1 Cr & Cb sample per 1x1 Y samples)
    PIX_FMT_RGBA32,    ///< Packed pixel, 4 bytes per pixel, BGRABGRA..., stored in cpu endianness
    PIX_FMT_YUV410P,   ///< Planar YUV 4:1:0 (1 Cr & Cb sample per 4x4 Y samples)
    PIX_FMT_YUV411P,   ///< Planar YUV 4:1:1 (1 Cr & Cb sample per 4x1 Y samples)
    PIX_FMT_RGB565,    ///< always stored in cpu endianness 
    PIX_FMT_RGB555,    ///< always stored in cpu endianness, most significant bit to 1 
    PIX_FMT_GRAY8,
    PIX_FMT_MONOWHITE, ///< 0 is white 
    PIX_FMT_MONOBLACK, ///< 0 is black 
    PIX_FMT_PAL8,      ///< 8 bit with RGBA palette 
    PIX_FMT_YUVJ420P,  ///< Planar YUV 4:2:0 full scale (jpeg)
    PIX_FMT_YUVJ422P,  ///< Planar YUV 4:2:2 full scale (jpeg)
    PIX_FMT_YUVJ444P,  ///< Planar YUV 4:4:4 full scale (jpeg)
    PIX_FMT_XVMC_MPEG2_MC,///< XVideo Motion Acceleration via common packet passing(xvmc_render.h)
    PIX_FMT_XVMC_MPEG2_IDCT,
    PIX_FMT_NB,
};

/**
 * four components are given, that's all.
 * the last component is alpha
 */
typedef struct AVPicture {
    uint8_t *data[4];
    int linesize[4];       ///< number of bytes per line
} AVPicture;

/**
 * Allocate memory for a picture.  Call avpicture_free to free it.
 *
 * @param picture the picture to be filled in.
 * @param pix_fmt the format of the picture.
 * @param width the width of the picture.
 * @param height the height of the picture.
 * @return 0 if successful, -1 if not.
 */
int avpicture_alloc(AVPicture *picture, int pix_fmt, int width, int height);

/* Free a picture previously allocated by avpicture_alloc. */
void avpicture_free(AVPicture *picture);

int avpicture_fill(AVPicture *picture, uint8_t *ptr,
                   int pix_fmt, int width, int height);
int avpicture_layout(const AVPicture* src, int pix_fmt, int width, int height,
                     unsigned char *dest, int dest_size);
int avpicture_get_size(int pix_fmt, int width, int height);
void avcodec_get_chroma_sub_sample(int pix_fmt, int *h_shift, int *v_shift);
const char *avcodec_get_pix_fmt_name(int pix_fmt);
enum PixelFormat avcodec_get_pix_fmt(const char* name);

#define FF_LOSS_RESOLUTION  0x0001 /* loss due to resolution change */
#define FF_LOSS_DEPTH       0x0002 /* loss due to color depth change */
#define FF_LOSS_COLORSPACE  0x0004 /* loss due to color space conversion */
#define FF_LOSS_ALPHA       0x0008 /* loss of alpha bits */
#define FF_LOSS_COLORQUANT  0x0010 /* loss due to color quantization */
#define FF_LOSS_CHROMA      0x0020 /* loss of chroma (e.g. rgb to gray conversion) */

int avcodec_get_pix_fmt_loss(int dst_pix_fmt, int src_pix_fmt,
                             int has_alpha);
int avcodec_find_best_pix_fmt(int pix_fmt_mask, int src_pix_fmt,
                              int has_alpha, int *loss_ptr);

#define FF_ALPHA_TRANSP       0x0001 /* image has some totally transparent pixels */
#define FF_ALPHA_SEMI_TRANSP  0x0002 /* image has some transparent pixels */
int img_get_alpha_info(const AVPicture *src,
		       int pix_fmt, int width, int height);

/* convert among pixel formats */
int img_convert(AVPicture *dst, int dst_pix_fmt,
                const AVPicture *src, int pix_fmt, 
                int width, int height);

/* deinterlace a picture */
int avpicture_deinterlace(AVPicture *dst, const AVPicture *src,
                          int pix_fmt, int width, int height);

void avcodec_init(void);

/* memory */
void *av_malloc(unsigned int size);
void *av_mallocz(unsigned int size);
void *av_realloc(void *ptr, unsigned int size);
void av_free(void *ptr);
char *av_strdup(const char *s);
void av_freep(void *ptr);
void *av_fast_realloc(void *ptr, unsigned int *size, unsigned int min_size);
/* for static data only */
/* call av_free_static to release all staticaly allocated tables */
void av_free_static(void);
void *__av_mallocz_static(void** location, unsigned int size);
#define av_mallocz_static(p, s) __av_mallocz_static((void **)(p), s)

/* add by bero : in adx.c */
int is_adx(const unsigned char *buf,size_t bufsize);

void img_copy(AVPicture *dst, const AVPicture *src,
              int pix_fmt, int width, int height);

#ifdef __cplusplus
}
#endif

#endif /* AVCODEC_H */
