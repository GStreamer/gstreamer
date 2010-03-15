
#ifndef __COG_FRAME_H__
#define __COG_FRAME_H__

#include <cog/cogutils.h>

COG_BEGIN_DECLS

typedef struct _CogFrame CogFrame;
typedef struct _CogFrameData CogFrameData;
typedef struct _CogUpsampledFrame CogUpsampledFrame;

typedef void (*CogFrameFreeFunc)(CogFrame *frame, void *priv);
typedef void (*CogFrameRenderFunc)(CogFrame *frame, void *dest, int component, int i);

typedef enum _CogColorMatrix {
  COG_COLOR_MATRIX_UNKNOWN = 0,
  COG_COLOR_MATRIX_HDTV,
  COG_COLOR_MATRIX_SDTV
} CogColorMatrix;

typedef enum _CogChromaSite {
  COG_CHROMA_SITE_UNKNOWN = 0,
  COG_CHROMA_SITE_MPEG2 = 1,
  COG_CHROMA_SITE_JPEG
} CogChromaSite;

/* bit pattern:
 *  0x100 - 0: normal, 1: indirect (packed)
 *  0x001 - horizontal chroma subsampling: 0: 1, 1: 2
 *  0x002 - vertical chroma subsampling: 0: 1, 1: 2
 *  0x00c - depth: 0: u8, 1: s16, 2: s32
 *  */
typedef enum _CogFrameFormat {
  COG_FRAME_FORMAT_U8_444 = 0x00,
  COG_FRAME_FORMAT_U8_422 = 0x01,
  COG_FRAME_FORMAT_U8_420 = 0x03,

  COG_FRAME_FORMAT_S16_444 = 0x04,
  COG_FRAME_FORMAT_S16_422 = 0x05,
  COG_FRAME_FORMAT_S16_420 = 0x07,

  COG_FRAME_FORMAT_S32_444 = 0x08,
  COG_FRAME_FORMAT_S32_422 = 0x09,
  COG_FRAME_FORMAT_S32_420 = 0x0b,

  /* indirectly supported */
  COG_FRAME_FORMAT_YUYV = 0x100, /* YUYV order */
  COG_FRAME_FORMAT_UYVY = 0x101, /* UYVY order */
  COG_FRAME_FORMAT_AYUV = 0x102,
  COG_FRAME_FORMAT_RGB = 0x104,
  COG_FRAME_FORMAT_v216 = 0x105,
  COG_FRAME_FORMAT_v210 = 0x106,
  COG_FRAME_FORMAT_RGBx = 0x110,
  COG_FRAME_FORMAT_xRGB = 0x111,
  COG_FRAME_FORMAT_BGRx = 0x112,
  COG_FRAME_FORMAT_xBGR = 0x113,
  COG_FRAME_FORMAT_RGBA = 0x114,
  COG_FRAME_FORMAT_ARGB = 0x115,
  COG_FRAME_FORMAT_BGRA = 0x116,
  COG_FRAME_FORMAT_ABGR = 0x117,
} CogFrameFormat;

#define COG_FRAME_FORMAT_DEPTH(format) ((format) & 0xc)
#define COG_FRAME_FORMAT_DEPTH_U8 0x00
#define COG_FRAME_FORMAT_DEPTH_S16 0x04
#define COG_FRAME_FORMAT_DEPTH_S32 0x08

#define COG_FRAME_FORMAT_H_SHIFT(format) ((format) & 0x1)
#define COG_FRAME_FORMAT_V_SHIFT(format) (((format)>>1) & 0x1)

#define COG_FRAME_IS_PACKED(format) (((format)>>8) & 0x1)

#define COG_FRAME_CACHE_SIZE 8

struct _CogFrameData {
  CogFrameFormat format;
  void *data;
  int stride;
  int width;
  int height;
  int length;
  int h_shift;
  int v_shift;
};

struct _CogFrame {
  int refcount;
  CogFrameFreeFunc free;
  CogMemoryDomain *domain;
  void *regions[3];
  void *priv;

  CogFrameFormat format;
  int width;
  int height;

  CogFrameData components[3];

  int is_virtual;
  int cache_offset[3];
  int cached_lines[3][COG_FRAME_CACHE_SIZE];
  CogFrame *virt_frame1;
  CogFrame *virt_frame2;
  void (*render_line) (CogFrame *frame, void *dest, int component, int i);
  void *virt_priv;
  void *virt_priv2;
  int param1;
  int param2;

  int extension;
};

struct _CogUpsampledFrame {
  CogFrame *frames[4];
  void *components[3];
};

#define COG_FRAME_DATA_GET_LINE(fd,i) (COG_OFFSET((fd)->data,(fd)->stride*(i)))
#define COG_FRAME_DATA_GET_PIXEL_U8(fd,i,j) ((uint8_t *)COG_OFFSET((fd)->data,(fd)->stride*(j)+(i)))
#define COG_FRAME_DATA_GET_PIXEL_S16(fd,i,j) ((int16_t *)COG_OFFSET((fd)->data,(fd)->stride*(j)+(i)*sizeof(int16_t)))

CogFrame * cog_frame_new (void);
CogFrame * cog_frame_new_and_alloc (CogMemoryDomain *domain,
    CogFrameFormat format, int width, int height);
CogFrame * cog_frame_new_from_data_I420 (void *data, int width, int height);
CogFrame * cog_frame_new_from_data_YV12 (void *data, int width, int height);
CogFrame * cog_frame_new_from_data_YUY2 (void *data, int width, int height);
CogFrame * cog_frame_new_from_data_UYVY (void *data, int width, int height);
CogFrame * cog_frame_new_from_data_UYVY_full (void *data, int width, int height, int stride);
CogFrame * cog_frame_new_from_data_AYUV (void *data, int width, int height);
CogFrame * cog_frame_new_from_data_v216 (void *data, int width, int height);
CogFrame * cog_frame_new_from_data_v210 (void *data, int width, int height);
CogFrame * cog_frame_new_from_data_Y42B (void *data, int width, int height);
CogFrame * cog_frame_new_from_data_Y444 (void *data, int width, int height);
CogFrame * cog_frame_new_from_data_RGB (void *data, int width, int height);
CogFrame * cog_frame_new_from_data_RGBx (void *data, int width, int height);
CogFrame * cog_frame_new_from_data_xRGB (void *data, int width, int height);
CogFrame * cog_frame_new_from_data_BGRx (void *data, int width, int height);
CogFrame * cog_frame_new_from_data_xBGR (void *data, int width, int height);
CogFrame * cog_frame_new_from_data_RGBA (void *data, int width, int height);
CogFrame * cog_frame_new_from_data_ARGB (void *data, int width, int height);
CogFrame * cog_frame_new_from_data_BGRA (void *data, int width, int height);
CogFrame * cog_frame_new_from_data_ABGR (void *data, int width, int height);
void cog_frame_set_free_callback (CogFrame *frame,
    CogFrameFreeFunc free_func, void *priv);
void cog_frame_unref (CogFrame *frame);
CogFrame *cog_frame_ref (CogFrame *frame);
CogFrame *cog_frame_dup (CogFrame *frame);
CogFrame *cog_frame_clone (CogMemoryDomain *domain, CogFrame *frame);

void cog_frame_convert (CogFrame *dest, CogFrame *src);
void cog_frame_add (CogFrame *dest, CogFrame *src);
void cog_frame_subtract (CogFrame *dest, CogFrame *src);
void cog_frame_shift_left (CogFrame *frame, int shift);
void cog_frame_shift_right (CogFrame *frame, int shift);

//void cog_frame_downsample (CogFrame *dest, CogFrame *src);
void cog_frame_upsample_horiz (CogFrame *dest, CogFrame *src);
void cog_frame_upsample_vert (CogFrame *dest, CogFrame *src);
double cog_frame_calculate_average_luma (CogFrame *frame);

CogFrame * cog_frame_convert_to_444 (CogFrame *frame);
void cog_frame_md5 (CogFrame *frame, uint32_t *state);

CogFrame * cog_frame_new_and_alloc_extended (CogMemoryDomain *domain,
    CogFrameFormat format, int width, int height, int extension);
CogFrame *cog_frame_dup_extended (CogFrame *frame, int extension);
void cog_frame_edge_extend (CogFrame *frame, int width, int height);
void cog_frame_zero_extend (CogFrame *frame, int width, int height);
void cog_frame_mark (CogFrame *frame, int value);
void cog_frame_mc_edgeextend (CogFrame *frame);

void cog_frame_data_get_codeblock (CogFrameData *dest, CogFrameData *src,
        int x, int y, int horiz_codeblocks, int vert_codeblocks);

CogUpsampledFrame * cog_upsampled_frame_new (CogFrame *frame);
void cog_upsampled_frame_free (CogUpsampledFrame *df);
void cog_upsampled_frame_upsample (CogUpsampledFrame *df);
#ifdef ENABLE_MOTION_REF
int cog_upsampled_frame_get_pixel_prec0 (CogUpsampledFrame *upframe, int k,
    int x, int y);
int cog_upsampled_frame_get_pixel_prec1 (CogUpsampledFrame *upframe, int k,
    int x, int y);
int cog_upsampled_frame_get_pixel_prec3 (CogUpsampledFrame *upframe, int k,
    int x, int y);
int cog_upsampled_frame_get_pixel_precN (CogUpsampledFrame *upframe, int k,
    int x, int y, int mv_precision);
#endif
void cog_upsampled_frame_get_block_precN (CogUpsampledFrame *upframe, int k,
    int x, int y, int prec, CogFrameData *dest);
void cog_upsampled_frame_get_block_fast_precN (CogUpsampledFrame *upframe, int k,
    int x, int y, int prec, CogFrameData *dest, CogFrameData *fd);
void cog_upsampled_frame_get_subdata_prec0 (CogUpsampledFrame *upframe,
    int k, int x, int y, CogFrameData *fd);
void cog_upsampled_frame_get_subdata_prec1 (CogUpsampledFrame *upframe,
    int k, int x, int y, CogFrameData *fd);

void cog_frame_get_subdata (CogFrame *frame, CogFrameData *fd,
        int comp, int x, int y);

void cog_frame_split_fields (CogFrame *dest1, CogFrame *dest2, CogFrame *src);


COG_END_DECLS

#endif

