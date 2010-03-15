
#ifndef __COG_VIRT_FRAME_H__
#define __COG_VIRT_FRAME_H__

#include <cog/cogutils.h>
#include <cog/cogframe.h>

COG_BEGIN_DECLS

CogFrame *cog_frame_new_virtual (CogMemoryDomain *domain,
    CogFrameFormat format, int width, int height);

void *cog_virt_frame_get_line (CogFrame *frame, int component, int i);
void cog_virt_frame_render_line (CogFrame *frame, void *dest,
    int component, int i);

void cog_virt_frame_render (CogFrame *frame, CogFrame *dest);

CogFrame *cog_virt_frame_new_horiz_downsample (CogFrame *vf, int n_taps);
CogFrame *cog_virt_frame_new_vert_downsample (CogFrame *vf, int n_taps);
CogFrame *cog_virt_frame_new_vert_resample (CogFrame *vf, int height, int n_taps);
CogFrame *cog_virt_frame_new_horiz_resample (CogFrame *vf, int width, int n_taps);
CogFrame *cog_virt_frame_new_unpack (CogFrame *vf);
CogFrame *cog_virt_frame_new_pack_YUY2 (CogFrame *vf);
CogFrame *cog_virt_frame_new_pack_UYVY (CogFrame *vf);
CogFrame *cog_virt_frame_new_pack_AYUV (CogFrame *vf);
CogFrame *cog_virt_frame_new_pack_v216 (CogFrame *vf);
CogFrame *cog_virt_frame_new_pack_v210 (CogFrame *vf);
CogFrame *cog_virt_frame_new_pack_RGB (CogFrame *vf);
CogFrame *cog_virt_frame_new_color_matrix_YCbCr_to_RGB (CogFrame *vf, CogColorMatrix color_matrix, int coefficient_bits);
CogFrame * cog_virt_frame_new_color_matrix_RGB_to_YCbCr (CogFrame * vf, CogColorMatrix color_matrix, int coefficient_bits);
CogFrame * cog_virt_frame_new_color_matrix_YCbCr_to_YCbCr (CogFrame * vf,
    CogColorMatrix in_color_matrix, CogColorMatrix out_color_matrix,
    int bits);
CogFrame *cog_virt_frame_new_subsample (CogFrame *vf, CogFrameFormat format,
    CogChromaSite site, int n_taps);

CogFrame * cog_virt_frame_new_convert_u8 (CogFrame *vf);
CogFrame * cog_virt_frame_new_convert_s16 (CogFrame *vf);
CogFrame * cog_virt_frame_new_crop (CogFrame *vf, int width, int height);
CogFrame * cog_virt_frame_new_edgeextend (CogFrame *vf, int width, int height);

CogFrame * cog_virt_frame_new_pack_RGBx (CogFrame *vf);
CogFrame * cog_virt_frame_new_pack_xRGB (CogFrame *vf);
CogFrame * cog_virt_frame_new_pack_BGRx (CogFrame *vf);
CogFrame * cog_virt_frame_new_pack_xBGR (CogFrame *vf);
CogFrame * cog_virt_frame_new_pack_RGBA (CogFrame *vf);
CogFrame * cog_virt_frame_new_pack_ARGB (CogFrame *vf);
CogFrame * cog_virt_frame_new_pack_BGRA (CogFrame *vf);
CogFrame * cog_virt_frame_new_pack_ABGR (CogFrame *vf);

COG_END_DECLS

#endif

