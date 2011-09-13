#ifndef AVCODEC_H
#define AVCODEC_H

/**
 * @file avcodec.h
 * external api header.
 */


#ifdef __cplusplus
extern "C" {
#endif

#include "_stdint.h"

#include <sys/types.h> /* size_t */

#define FFMPEG_VERSION_INT     0x000409
#define FFMPEG_VERSION         "0.4.9-pre1"
#define LIBAVCODEC_BUILD       4728

#define LIBAVCODEC_VERSION_INT FFMPEG_VERSION_INT
#define LIBAVCODEC_VERSION     FFMPEG_VERSION

#define AV_STRINGIFY(s) AV_TOSTRING(s)
#define AV_TOSTRING(s) #s
#define LIBAVCODEC_IDENT        "FFmpeg" LIBAVCODEC_VERSION "b" AV_STRINGIFY(LIBAVCODEC_BUILD)

enum CodecType {
    CODEC_TYPE_UNKNOWN = -1,
    CODEC_TYPE_VIDEO,
    CODEC_TYPE_AUDIO,
    CODEC_TYPE_DATA,
};

/*
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
    PIX_FMT_YUV420P,   ///< Planar YUV 4:2:0 (1 Cr & Cb sample per 2x2 Y samples) (I420)
    PIX_FMT_NV12,      ///< Packed YUV 4:2:0 (separate Y plane, interleaved Cb & Cr planes)
    PIX_FMT_NV21,      ///< Packed YUV 4:2:0 (separate Y plane, interleaved Cb & Cr planes)
    PIX_FMT_YVU420P,   ///< Planar YUV 4:2:0 (1 Cb & Cr sample per 2x2 Y samples) (YV12)
    PIX_FMT_YUV422,    ///< Packed pixel, Y0 Cb Y1 Cr 
    PIX_FMT_RGB24,     ///< Packed pixel, 3 bytes per pixel, RGBRGB...
    PIX_FMT_BGR24,     ///< Packed pixel, 3 bytes per pixel, BGRBGR...
    PIX_FMT_YUV422P,   ///< Planar YUV 4:2:2 (1 Cr & Cb sample per 2x1 Y samples)
    PIX_FMT_YUV444P,   ///< Planar YUV 4:4:4 (1 Cr & Cb sample per 1x1 Y samples)
    PIX_FMT_RGBA32,    ///< Packed pixel, 4 bytes per pixel, BGRABGRA..., stored in cpu endianness
    PIX_FMT_BGRA32,    ///< Packed pixel, 4 bytes per pixel, ARGBARGB...
    PIX_FMT_ARGB32,    ///< Packed pixel, 4 bytes per pixel, ABGRABGR..., stored in cpu endianness
    PIX_FMT_ABGR32,    ///< Packed pixel, 4 bytes per pixel, RGBARGBA...
    PIX_FMT_RGB32,     ///< Packed pixel, 4 bytes per pixel, BGRxBGRx..., stored in cpu endianness
    PIX_FMT_xRGB32,    ///< Packed pixel, 4 bytes per pixel, xBGRxBGR..., stored in cpu endianness
    PIX_FMT_BGR32,     ///< Packed pixel, 4 bytes per pixel, xRGBxRGB...
    PIX_FMT_BGRx32,    ///< Packed pixel, 4 bytes per pixel, RGBxRGBx...
    PIX_FMT_YUV410P,   ///< Planar YUV 4:1:0 (1 Cr & Cb sample per 4x4 Y samples)
    PIX_FMT_YVU410P,   ///< Planar YVU 4:1:0 (1 Cr & Cb sample per 4x4 Y samples)
    PIX_FMT_YUV411P,   ///< Planar YUV 4:1:1 (1 Cr & Cb sample per 4x1 Y samples)
    PIX_FMT_Y800,      ///< 8 bit Y plane (range [16-235])
    PIX_FMT_Y16,       ///< 16 bit Y plane (little endian)
    PIX_FMT_RGB565,    ///< always stored in cpu endianness 
    PIX_FMT_RGB555,    ///< always stored in cpu endianness, most significant bit to 1 
    PIX_FMT_GRAY8,
    PIX_FMT_GRAY16_L,
    PIX_FMT_GRAY16_B,
    PIX_FMT_MONOWHITE, ///< 0 is white 
    PIX_FMT_MONOBLACK, ///< 0 is black 
    PIX_FMT_PAL8,      ///< 8 bit with RGBA palette 
    PIX_FMT_YUVJ420P,  ///< Planar YUV 4:2:0 full scale (jpeg)
    PIX_FMT_YUVJ422P,  ///< Planar YUV 4:2:2 full scale (jpeg)
    PIX_FMT_YUVJ444P,  ///< Planar YUV 4:4:4 full scale (jpeg)
    PIX_FMT_XVMC_MPEG2_MC,///< XVideo Motion Acceleration via common packet passing(xvmc_render.h)
    PIX_FMT_XVMC_MPEG2_IDCT,
    PIX_FMT_UYVY422,   ///< Packed pixel, Cb Y0 Cr Y1 
    PIX_FMT_YVYU422,   ///< Packed pixel, Y0 Cr Y1 Cb 
    PIX_FMT_UYVY411,   ///< Packed pixel, Cb Y0 Y1 Cr Y2 Y3
    PIX_FMT_V308,      ///< Packed pixel, Y0 Cb Cr

    PIX_FMT_AYUV4444,  ///< Packed pixel, A0 Y0 Cb Cr
    PIX_FMT_YUVA420P,   ///< Planar YUV 4:4:2:0 (1 Cr & Cb sample per 2x2 Y & A samples) (A420)
    PIX_FMT_NB
};

/* currently unused, may be used if 24/32 bits samples ever supported */
enum SampleFormat {
    SAMPLE_FMT_S16 = 0,         ///< signed 16 bits
};

/* thomas: extracted from imgconvert.c since it's also used in
 * gstffmpegcodecmap.c */

/* start of extract */

#define FF_COLOR_RGB      0     /* RGB color space */
#define FF_COLOR_GRAY     1     /* gray color space */
#define FF_COLOR_YUV      2     /* YUV color space. 16 <= Y <= 235, 16 <= U, V <= 240 */
#define FF_COLOR_YUV_JPEG 3     /* YUV color space. 0 <= Y <= 255, 0 <= U, V <= 255 */

#define FF_PIXEL_PLANAR   0     /* each channel has one component in AVPicture */
#define FF_PIXEL_PACKED   1     /* only one components containing all the channels */
#define FF_PIXEL_PALETTE  2     /* one components containing indexes for a palette */

typedef struct PixFmtInfo
{
  enum PixelFormat format;
  const char *name;
  uint8_t nb_channels;          /* number of channels (including alpha) */
  uint8_t color_type;           /* color type (see FF_COLOR_xxx constants) */
  uint8_t pixel_type;           /* pixel storage type (see FF_PIXEL_xxx constants) */
  uint8_t is_alpha:1;           /* true if alpha can be specified */
  uint8_t x_chroma_shift;       /* X chroma subsampling factor is 2 ^ shift */
  uint8_t y_chroma_shift;       /* Y chroma subsampling factor is 2 ^ shift */
  uint8_t depth;                /* bit depth of the color components */
} PixFmtInfo;

PixFmtInfo * get_pix_fmt_info (enum PixelFormat format);
/* end of extract */

/**
 * main external api structure.
 */
typedef struct AVCodecContext {
    /* video only */
    /**
     * frames per sec multiplied by frame_rate_base.
     * for variable fps this is the precision, so if the timestamps 
     * can be specified in msec precision then this is 1000*frame_rate_base
     * - encoding: MUST be set by user
     * - decoding: set by lavc. 0 or the frame_rate if available
     */
    int frame_rate;
    
    /**
     * frame_rate_base.
     * for variable fps this is 1
     * - encoding: set by user.
     * - decoding: set by lavc.
     */

    int frame_rate_base;
    /**
     * picture width / height.
     * - encoding: MUST be set by user. 
     * - decoding: set by lavc.
     * Note, for compatibility its possible to set this instead of 
     * coded_width/height before decoding
     */
    int width, height;

    /**
     * pixel format, see PIX_FMT_xxx.
     * - encoding: FIXME: used by ffmpeg to decide whether an pix_fmt
     *                    conversion is in order. This only works for
     *                    codecs with one supported pix_fmt, we should
     *                    do something for a generic case as well.
     * - decoding: set by lavc.
     */
    enum PixelFormat pix_fmt;

    /* audio only */
    int sample_rate; ///< samples per sec 
    int channels;
    int sample_fmt;  ///< sample format, currenly unused 

    /**
     * Palette control structure
     * - encoding: ??? (no palette-enabled encoder yet)
     * - decoding: set by user.
     */
    struct AVPaletteControl *palctrl;
} AVCodecContext;

/**
 * four components are given, that's all.
 * the last component is alpha
 */
typedef struct AVPicture {
    uint8_t *data[4];
    int linesize[4];       ///< number of bytes per line
    int interlaced;
} AVPicture;

/**
 * AVPaletteControl
 * This structure defines a method for communicating palette changes
 * between and demuxer and a decoder.
 */
#define AVPALETTE_SIZE 1024
#define AVPALETTE_COUNT 256
typedef struct AVPaletteControl {

    /* demuxer sets this to 1 to indicate the palette has changed;
     * decoder resets to 0 */
    int palette_changed;

    /* 4-byte ARGB palette entries, stored in native byte order; note that
     * the individual palette components should be on a 8-bit scale; if
     * the palette data comes from a IBM VGA native format, the component
     * data is probably 6 bits in size and needs to be scaled */
    unsigned int palette[AVPALETTE_COUNT];

} AVPaletteControl;

int avpicture_get_size(int pix_fmt, int width, int height);

void avcodec_get_chroma_sub_sample(int pix_fmt, int *h_shift, int *v_shift);
const char *avcodec_get_pix_fmt_name(int pix_fmt);
void avcodec_set_dimensions(AVCodecContext *s, int width, int height);
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

void avcodec_init(void);

void avcodec_get_context_defaults(AVCodecContext *s);
AVCodecContext *avcodec_alloc_context(void);

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
void *av_mallocz_static(unsigned int size);

/* endian macros */
#if !defined(BE_16) || !defined(BE_32) || !defined(LE_16) || !defined(LE_32)
#define BE_16(x)  ((((uint8_t*)(x))[0] << 8) | ((uint8_t*)(x))[1])
#define BE_32(x)  ((((uint8_t*)(x))[0] << 24) | \
                   (((uint8_t*)(x))[1] << 16) | \
                   (((uint8_t*)(x))[2] << 8) | \
                    ((uint8_t*)(x))[3])
#define LE_16(x)  ((((uint8_t*)(x))[1] << 8) | ((uint8_t*)(x))[0])
#define LE_32(x)  ((((uint8_t*)(x))[3] << 24) | \
                   (((uint8_t*)(x))[2] << 16) | \
                   (((uint8_t*)(x))[1] << 8) | \
                    ((uint8_t*)(x))[0])
#endif

#ifdef __cplusplus
}
#endif

#endif /* AVCODEC_H */
