.function video_mixer_orc_splat_u32
.dest 4 d1 guint32
.param 4 p1 guint32

copyl d1, p1

.function video_mixer_orc_memcpy_u32
.dest 4 d1 guint32
.source 4 s1 guint32

copyl d1, s1

.function video_mixer_orc_blend_u8
.flags 2d
.dest 1 d1 guint8
.source 1 s1 guint8
.param 2 p1
.temp 2 t1
.temp 2 t2
.const 1 c1 8 

convubw t1, d1
convubw t2, s1
subw t2, t2, t1
mullw t2, t2, p1
shlw t1, t1, c1
addw t2, t1, t2
shruw t2, t2, c1
convsuswb d1, t2


.function video_mixer_orc_blend_argb
.flags 2d
.dest 4 d guint8
.source 4 s guint8
.param 2 alpha
.temp 4 t
.temp 2 tw
.temp 1 tb
.temp 4 a
.temp 8 d_wide
.temp 8 s_wide
.temp 8 a_wide
.const 4 a_alpha 0x000000ff

loadl t, s
convlw tw, t
convwb tb, tw
splatbl a, tb
x4 convubw a_wide, a
x4 mullw a_wide, a_wide, alpha
x4 shruw a_wide, a_wide, 8
x4 convubw s_wide, t
loadl t, d
x4 convubw d_wide, t
x4 subw s_wide, s_wide, d_wide
x4 mullw s_wide, s_wide, a_wide
x4 div255w s_wide, s_wide
x4 addw d_wide, d_wide, s_wide
x4 convwb t, d_wide
orl t, t, a_alpha
storel d, t

.function video_mixer_orc_blend_bgra
.flags 2d
.dest 4 d guint8
.source 4 s guint8
.param 2 alpha
.temp 4 t
.temp 4 t2
.temp 2 tw
.temp 1 tb
.temp 4 a
.temp 8 d_wide
.temp 8 s_wide
.temp 8 a_wide
.const 4 a_alpha 0xff000000

loadl t, s
shrul t2, t, 24
convlw tw, t2
convwb tb, tw
splatbl a, tb
x4 convubw a_wide, a
x4 mullw a_wide, a_wide, alpha
x4 shruw a_wide, a_wide, 8
x4 convubw s_wide, t
loadl t, d
x4 convubw d_wide, t
x4 subw s_wide, s_wide, d_wide
x4 mullw s_wide, s_wide, a_wide
x4 div255w s_wide, s_wide
x4 addw d_wide, d_wide, s_wide
x4 convwb t, d_wide
orl t, t, a_alpha
storel d, t


.function video_mixer_orc_overlay_argb
.flags 2d
.dest 4 d guint8
.source 4 s guint8
.param 2 alpha
.temp 4 t
.temp 2 tw
.temp 1 tb
.temp 8 alpha_s
.temp 8 alpha_s_inv
.temp 8 alpha_d
.temp 4 a
.temp 8 d_wide
.temp 8 s_wide
.const 4 xfs 0xffffffff
.const 4 a_alpha 0x000000ff
.const 4 a_alpha_inv 0xffffff00

# calc source alpha as alpha_s = alpha_s * alpha / 256
loadl t, s
convlw tw, t
convwb tb, tw
splatbl a, tb
x4 convubw alpha_s, a
x4 mullw alpha_s, alpha_s, alpha
x4 shruw alpha_s, alpha_s, 8
x4 convubw s_wide, t
x4 mullw s_wide, s_wide, alpha_s

# calc destination alpha as alpha_d = (255-alpha_s) * alpha_d / 255
loadpl a, xfs
x4 convubw alpha_s_inv, a
x4 subw alpha_s_inv, alpha_s_inv, alpha_s
loadl t, d
convlw tw, t
convwb tb, tw
splatbl a, tb
x4 convubw alpha_d, a
x4 mullw alpha_d, alpha_d, alpha_s_inv
x4 div255w alpha_d, alpha_d
x4 convubw d_wide, t
x4 mullw d_wide, d_wide, alpha_d

# calc final pixel as pix_d = pix_s*alpha_s + pix_d*alpha_d*(255-alpha_s)/255
x4 addw d_wide, d_wide, s_wide

# calc the final destination alpha_d = alpha_s + alpha_d * (255-alpha_s)/255
x4 addw alpha_d, alpha_d, alpha_s

# now normalize the pix_d by the final alpha to make it associative
x4 divluw, d_wide, d_wide, alpha_d

# pack the new alpha into the correct spot
x4 convwb t, d_wide
andl t, t, a_alpha_inv
x4 convwb a, alpha_d
andl a, a, a_alpha
orl  t, t, a
storel d, t

.function video_mixer_orc_overlay_bgra
.flags 2d
.dest 4 d guint8
.source 4 s guint8
.param 2 alpha
.temp 4 t
.temp 4 t2
.temp 2 tw
.temp 1 tb
.temp 8 alpha_s
.temp 8 alpha_s_inv
.temp 8 alpha_d
.temp 4 a
.temp 8 d_wide
.temp 8 s_wide
.const 4 xfs 0xffffffff
.const 4 a_alpha 0xff000000
.const 4 a_alpha_inv 0x00ffffff

# calc source alpha as alpha_s = alpha_s * alpha / 256
loadl t, s
shrul t2, t, 24
convlw tw, t2
convwb tb, tw
splatbl a, tb
x4 convubw alpha_s, a
x4 mullw alpha_s, alpha_s, alpha
x4 shruw alpha_s, alpha_s, 8
x4 convubw s_wide, t
x4 mullw s_wide, s_wide, alpha_s

# calc destination alpha as alpha_d = (255-alpha_s) * alpha_d / 255
loadpl a, xfs
x4 convubw alpha_s_inv, a
x4 subw alpha_s_inv, alpha_s_inv, alpha_s
loadl t, d
shrul t2, t, 24
convlw tw, t2
convwb tb, tw
splatbl a, tb
x4 convubw alpha_d, a
x4 mullw alpha_d, alpha_d, alpha_s_inv
x4 div255w alpha_d, alpha_d
x4 convubw d_wide, t
x4 mullw d_wide, d_wide, alpha_d

# calc final pixel as pix_d = pix_s*alpha_s + pix_d*alpha_d*(255-alpha_s)/255
x4 addw d_wide, d_wide, s_wide

# calc the final destination alpha_d = alpha_s + alpha_d * (255-alpha_s)/255
x4 addw alpha_d, alpha_d, alpha_s

# now normalize the pix_d by the final alpha to make it associative
x4 divluw, d_wide, d_wide, alpha_d

# pack the new alpha into the correct spot
x4 convwb t, d_wide
andl t, t, a_alpha_inv
x4 convwb a, alpha_d
andl a, a, a_alpha
orl  t, t, a
storel d, t

# Videoconvert logic, copy from videomixer_videoconvert.
# Remove that when videomixer_videoconvert lands in libgstvideo.

.function videomixer_video_convert_orc_memcpy_2d
.flags 2d
.dest 1 d1 guint8
.source 1 s1 guint8

copyb d1, s1

.function videomixer_video_convert_orc_convert_I420_UYVY
.dest 4 d1 guint8
.dest 4 d2 guint8
.source 2 y1 guint8
.source 2 y2 guint8
.source 1 u guint8
.source 1 v guint8
.temp 2 uv

mergebw uv, u, v
x2 mergebw d1, uv, y1
x2 mergebw d2, uv, y2


.function videomixer_video_convert_orc_convert_I420_YUY2
.dest 4 d1 guint8
.dest 4 d2 guint8
.source 2 y1 guint8
.source 2 y2 guint8
.source 1 u guint8
.source 1 v guint8
.temp 2 uv

mergebw uv, u, v
x2 mergebw d1, y1, uv
x2 mergebw d2, y2, uv



.function videomixer_video_convert_orc_convert_I420_AYUV
.dest 4 d1 guint8
.dest 4 d2 guint8
.source 1 y1 guint8
.source 1 y2 guint8
.source 1 u guint8
.source 1 v guint8
.const 1 c255 255
.temp 2 uv
.temp 2 ay
.temp 1 tu
.temp 1 tv

loadupdb tu, u
loadupdb tv, v
mergebw uv, tu, tv
mergebw ay, c255, y1
mergewl d1, ay, uv
mergebw ay, c255, y2
mergewl d2, ay, uv


.function videomixer_video_convert_orc_convert_YUY2_I420
.dest 2 y1 guint8
.dest 2 y2 guint8
.dest 1 u guint8
.dest 1 v guint8
.source 4 yuv1 guint8
.source 4 yuv2 guint8
.temp 2 t1
.temp 2 t2
.temp 2 ty

x2 splitwb t1, ty, yuv1
storew y1, ty
x2 splitwb t2, ty, yuv2
storew y2, ty
x2 avgub t1, t1, t2
splitwb v, u, t1


.function videomixer_video_convert_orc_convert_UYVY_YUY2
.flags 2d
.dest 4 yuy2 guint8
.source 4 uyvy guint8

x2 swapw yuy2, uyvy


.function videomixer_video_convert_orc_planar_chroma_420_422
.flags 2d
.dest 1 d1 guint8
.dest 1 d2 guint8
.source 1 s guint8

copyb d1, s
copyb d2, s


.function videomixer_video_convert_orc_planar_chroma_420_444
.flags 2d
.dest 2 d1 guint8
.dest 2 d2 guint8
.source 1 s guint8
.temp 2 t

splatbw t, s
storew d1, t
storew d2, t


.function videomixer_video_convert_orc_planar_chroma_422_444
.flags 2d
.dest 2 d1 guint8
.source 1 s guint8
.temp 2 t

splatbw t, s
storew d1, t


.function videomixer_video_convert_orc_planar_chroma_444_422
.flags 2d
.dest 1 d guint8
.source 2 s guint8
.temp 1 t1
.temp 1 t2

splitwb t1, t2, s
avgub d, t1, t2


.function videomixer_video_convert_orc_planar_chroma_444_420
.flags 2d
.dest 1 d guint8
.source 2 s1 guint8
.source 2 s2 guint8
.temp 2 t
.temp 1 t1
.temp 1 t2

x2 avgub t, s1, s2
splitwb t1, t2, t
avgub d, t1, t2


.function videomixer_video_convert_orc_planar_chroma_422_420
.flags 2d
.dest 1 d guint8
.source 1 s1 guint8
.source 1 s2 guint8

avgub d, s1, s2


.function videomixer_video_convert_orc_convert_YUY2_AYUV
.flags 2d
.dest 8 ayuv guint8
.source 4 yuy2 guint8
.const 2 c255 0xff
.temp 2 yy
.temp 2 uv
.temp 4 ayay
.temp 4 uvuv

x2 splitwb uv, yy, yuy2
x2 mergebw ayay, c255, yy
mergewl uvuv, uv, uv
x2 mergewl ayuv, ayay, uvuv


.function videomixer_video_convert_orc_convert_UYVY_AYUV
.flags 2d
.dest 8 ayuv guint8
.source 4 uyvy guint8
.const 2 c255 0xff
.temp 2 yy
.temp 2 uv
.temp 4 ayay
.temp 4 uvuv

x2 splitwb yy, uv, uyvy
x2 mergebw ayay, c255, yy
mergewl uvuv, uv, uv
x2 mergewl ayuv, ayay, uvuv


.function videomixer_video_convert_orc_convert_YUY2_Y42B
.flags 2d
.dest 2 y guint8
.dest 1 u guint8
.dest 1 v guint8
.source 4 yuy2 guint8
.temp 2 uv

x2 splitwb uv, y, yuy2
splitwb v, u, uv


.function videomixer_video_convert_orc_convert_UYVY_Y42B
.flags 2d
.dest 2 y guint8
.dest 1 u guint8
.dest 1 v guint8
.source 4 uyvy guint8
.temp 2 uv

x2 splitwb y, uv, uyvy
splitwb v, u, uv


.function videomixer_video_convert_orc_convert_YUY2_Y444
.flags 2d
.dest 2 y guint8
.dest 2 uu guint8
.dest 2 vv guint8
.source 4 yuy2 guint8
.temp 2 uv
.temp 1 u
.temp 1 v

x2 splitwb uv, y, yuy2
splitwb v, u, uv
splatbw uu, u
splatbw vv, v


.function videomixer_video_convert_orc_convert_UYVY_Y444
.flags 2d
.dest 2 y guint8
.dest 2 uu guint8
.dest 2 vv guint8
.source 4 uyvy guint8
.temp 2 uv
.temp 1 u
.temp 1 v

x2 splitwb y, uv, uyvy
splitwb v, u, uv
splatbw uu, u
splatbw vv, v


.function videomixer_video_convert_orc_convert_UYVY_I420
.dest 2 y1 guint8
.dest 2 y2 guint8
.dest 1 u guint8
.dest 1 v guint8
.source 4 yuv1 guint8
.source 4 yuv2 guint8
.temp 2 t1
.temp 2 t2
.temp 2 ty

x2 splitwb ty, t1, yuv1
storew y1, ty
x2 splitwb ty, t2, yuv2
storew y2, ty
x2 avgub t1, t1, t2
splitwb v, u, t1



.function videomixer_video_convert_orc_convert_AYUV_I420
.flags 2d
.dest 2 y1 guint8
.dest 2 y2 guint8
.dest 1 u guint8
.dest 1 v guint8
.source 8 ayuv1 guint8
.source 8 ayuv2 guint8
.temp 4 ay
.temp 4 uv1
.temp 4 uv2
.temp 4 uv
.temp 2 uu
.temp 2 vv
.temp 1 t1
.temp 1 t2

x2 splitlw uv1, ay, ayuv1
x2 select1wb y1, ay
x2 splitlw uv2, ay, ayuv2
x2 select1wb y2, ay
x4 avgub uv, uv1, uv2
x2 splitwb vv, uu, uv
splitwb t1, t2, uu
avgub u, t1, t2
splitwb t1, t2, vv
avgub v, t1, t2



.function videomixer_video_convert_orc_convert_AYUV_YUY2
.flags 2d
.dest 4 yuy2 guint8
.source 8 ayuv guint8
.temp 2 yy
.temp 2 uv1
.temp 2 uv2
.temp 4 ayay
.temp 4 uvuv

x2 splitlw uvuv, ayay, ayuv
splitlw uv1, uv2, uvuv
x2 avgub uv1, uv1, uv2
x2 select1wb yy, ayay
x2 mergebw yuy2, yy, uv1


.function videomixer_video_convert_orc_convert_AYUV_UYVY
.flags 2d
.dest 4 yuy2 guint8
.source 8 ayuv guint8
.temp 2 yy
.temp 2 uv1
.temp 2 uv2
.temp 4 ayay
.temp 4 uvuv

x2 splitlw uvuv, ayay, ayuv
splitlw uv1, uv2, uvuv
x2 avgub uv1, uv1, uv2
x2 select1wb yy, ayay
x2 mergebw yuy2, uv1, yy



.function videomixer_video_convert_orc_convert_AYUV_Y42B
.flags 2d
.dest 2 y guint8
.dest 1 u guint8
.dest 1 v guint8
.source 8 ayuv guint8
.temp 4 ayay
.temp 4 uvuv
.temp 2 uv1
.temp 2 uv2

x2 splitlw uvuv, ayay, ayuv
splitlw uv1, uv2, uvuv
x2 avgub uv1, uv1, uv2
splitwb v, u, uv1
x2 select1wb y, ayay


.function videomixer_video_convert_orc_convert_AYUV_Y444
.flags 2d
.dest 1 y guint8
.dest 1 u guint8
.dest 1 v guint8
.source 4 ayuv guint8
.temp 2 ay
.temp 2 uv

splitlw uv, ay, ayuv
splitwb v, u, uv
select1wb y, ay


.function videomixer_video_convert_orc_convert_Y42B_YUY2
.flags 2d
.dest 4 yuy2 guint8
.source 2 y guint8
.source 1 u guint8
.source 1 v guint8
.temp 2 uv

mergebw uv, u, v
x2 mergebw yuy2, y, uv


.function videomixer_video_convert_orc_convert_Y42B_UYVY
.flags 2d
.dest 4 uyvy guint8
.source 2 y guint8
.source 1 u guint8
.source 1 v guint8
.temp 2 uv

mergebw uv, u, v
x2 mergebw uyvy, uv, y


.function videomixer_video_convert_orc_convert_Y42B_AYUV
.flags 2d
.dest 8 ayuv guint8
.source 2 yy guint8
.source 1 u guint8
.source 1 v guint8
.const 1 c255 255
.temp 2 uv
.temp 2 ay
.temp 4 uvuv
.temp 4 ayay

mergebw uv, u, v
x2 mergebw ayay, c255, yy
mergewl uvuv, uv, uv
x2 mergewl ayuv, ayay, uvuv


.function videomixer_video_convert_orc_convert_Y444_YUY2
.flags 2d
.dest 4 yuy2 guint8
.source 2 y guint8
.source 2 u guint8
.source 2 v guint8
.temp 2 uv
.temp 4 uvuv
.temp 2 uv1
.temp 2 uv2

x2 mergebw uvuv, u, v
splitlw uv1, uv2, uvuv
x2 avgub uv, uv1, uv2
x2 mergebw yuy2, y, uv


.function videomixer_video_convert_orc_convert_Y444_UYVY
.flags 2d
.dest 4 uyvy guint8
.source 2 y guint8
.source 2 u guint8
.source 2 v guint8
.temp 2 uv
.temp 4 uvuv
.temp 2 uv1
.temp 2 uv2

x2 mergebw uvuv, u, v
splitlw uv1, uv2, uvuv
x2 avgub uv, uv1, uv2
x2 mergebw uyvy, uv, y


.function videomixer_video_convert_orc_convert_Y444_AYUV
.flags 2d
.dest 4 ayuv guint8
.source 1 yy guint8
.source 1 u guint8
.source 1 v guint8
.const 1 c255 255
.temp 2 uv
.temp 2 ay

mergebw uv, u, v
mergebw ay, c255, yy
mergewl ayuv, ay, uv



.function videomixer_video_convert_orc_convert_AYUV_ARGB
.flags 2d
.dest 4 argb guint8
.source 4 ayuv guint8
.temp 2 t1
.temp 2 t2
.temp 1 a
.temp 1 y
.temp 1 u
.temp 1 v
.temp 2 wy
.temp 2 wu
.temp 2 wv
.temp 2 wr
.temp 2 wg
.temp 2 wb
.temp 1 r
.temp 1 g
.temp 1 b
.temp 4 x
.const 1 c8 8

x4 subb x, ayuv, 128
splitlw t1, t2, x
splitwb y, a, t2
splitwb v, u, t1
convsbw wy, y
convsbw wu, u
convsbw wv, v

mullw t1, wy, 42
shrsw t1, t1, c8
addssw wy, wy, t1

addssw wr, wy, wv
mullw t1, wv, 103
shrsw t1, t1, c8
subssw wr, wr, t1
addssw wr, wr, wv

addssw wb, wy, wu
addssw wb, wb, wu
mullw t1, wu, 4
shrsw t1, t1, c8
addssw wb, wb, t1

mullw t1, wu, 100
shrsw t1, t1, c8
subssw wg, wy, t1
mullw t1, wv, 104
shrsw t1, t1, c8
subssw wg, wg, t1
subssw wg, wg, t1

convssswb r, wr
convssswb g, wg
convssswb b, wb

mergebw t1, a, r
mergebw t2, g, b
mergewl x, t1, t2
x4 addb argb, x, 128



.function videomixer_video_convert_orc_convert_AYUV_BGRA
.flags 2d
.dest 4 argb guint8
.source 4 ayuv guint8
.temp 2 t1
.temp 2 t2
.temp 1 a
.temp 1 y
.temp 1 u
.temp 1 v
.temp 2 wy
.temp 2 wu
.temp 2 wv
.temp 2 wr
.temp 2 wg
.temp 2 wb
.temp 1 r
.temp 1 g
.temp 1 b
.temp 4 x
.const 1 c8 8

x4 subb x, ayuv, 128
splitlw t1, t2, x
splitwb y, a, t2
splitwb v, u, t1
convsbw wy, y
convsbw wu, u
convsbw wv, v

mullw t1, wy, 42
shrsw t1, t1, c8
addssw wy, wy, t1

addssw wr, wy, wv
mullw t1, wv, 103
shrsw t1, t1, c8
subssw wr, wr, t1
addssw wr, wr, wv

addssw wb, wy, wu
addssw wb, wb, wu
mullw t1, wu, 4
shrsw t1, t1, c8
addssw wb, wb, t1

mullw t1, wu, 100
shrsw t1, t1, c8
subssw wg, wy, t1
mullw t1, wv, 104
shrsw t1, t1, c8
subssw wg, wg, t1
subssw wg, wg, t1

convssswb r, wr
convssswb g, wg
convssswb b, wb

mergebw t1, b, g
mergebw t2, r, a
mergewl x, t1, t2
x4 addb argb, x, 128




.function videomixer_video_convert_orc_convert_AYUV_ABGR
.flags 2d
.dest 4 argb guint8
.source 4 ayuv guint8
.temp 2 t1
.temp 2 t2
.temp 1 a
.temp 1 y
.temp 1 u
.temp 1 v
.temp 2 wy
.temp 2 wu
.temp 2 wv
.temp 2 wr
.temp 2 wg
.temp 2 wb
.temp 1 r
.temp 1 g
.temp 1 b
.temp 4 x
.const 1 c8 8

x4 subb x, ayuv, 128
splitlw t1, t2, x
splitwb y, a, t2
splitwb v, u, t1
convsbw wy, y
convsbw wu, u
convsbw wv, v

mullw t1, wy, 42
shrsw t1, t1, c8
addssw wy, wy, t1

addssw wr, wy, wv
mullw t1, wv, 103
shrsw t1, t1, c8
subssw wr, wr, t1
addssw wr, wr, wv

addssw wb, wy, wu
addssw wb, wb, wu
mullw t1, wu, 4
shrsw t1, t1, c8
addssw wb, wb, t1

mullw t1, wu, 100
shrsw t1, t1, c8
subssw wg, wy, t1
mullw t1, wv, 104
shrsw t1, t1, c8
subssw wg, wg, t1
subssw wg, wg, t1

convssswb r, wr
convssswb g, wg
convssswb b, wb

mergebw t1, a, b
mergebw t2, g, r
mergewl x, t1, t2
x4 addb argb, x, 128



.function videomixer_video_convert_orc_convert_AYUV_RGBA
.flags 2d
.dest 4 argb guint8
.source 4 ayuv guint8
.temp 2 t1
.temp 2 t2
.temp 1 a
.temp 1 y
.temp 1 u
.temp 1 v
.temp 2 wy
.temp 2 wu
.temp 2 wv
.temp 2 wr
.temp 2 wg
.temp 2 wb
.temp 1 r
.temp 1 g
.temp 1 b
.temp 4 x
.const 1 c8 8

x4 subb x, ayuv, 128
splitlw t1, t2, x
splitwb y, a, t2
splitwb v, u, t1
convsbw wy, y
convsbw wu, u
convsbw wv, v

mullw t1, wy, 42
shrsw t1, t1, c8
addssw wy, wy, t1

addssw wr, wy, wv
mullw t1, wv, 103
shrsw t1, t1, c8
subssw wr, wr, t1
addssw wr, wr, wv

addssw wb, wy, wu
addssw wb, wb, wu
mullw t1, wu, 4
shrsw t1, t1, c8
addssw wb, wb, t1

mullw t1, wu, 100
shrsw t1, t1, c8
subssw wg, wy, t1
mullw t1, wv, 104
shrsw t1, t1, c8
subssw wg, wg, t1
subssw wg, wg, t1

convssswb r, wr
convssswb g, wg
convssswb b, wb

mergebw t1, r, g
mergebw t2, b, a
mergewl x, t1, t2
x4 addb argb, x, 128



.function videomixer_video_convert_orc_convert_I420_BGRA
.dest 4 argb guint8
.source 1 y guint8
.source 1 u guint8
.source 1 v guint8
.temp 2 t1
.temp 2 t2
.temp 1 t3
.temp 2 wy
.temp 2 wu
.temp 2 wv
.temp 2 wr
.temp 2 wg
.temp 2 wb
.temp 1 r
.temp 1 g
.temp 1 b
.temp 4 x
.const 1 c8 8
.const 1 c128 128

subb t3, y, c128
convsbw wy, t3
loadupib t3, u
subb t3, t3, c128
convsbw wu, t3
loadupib t3, v
subb t3, t3, c128
convsbw wv, t3

mullw t1, wy, 42
shrsw t1, t1, c8
addssw wy, wy, t1

addssw wr, wy, wv
mullw t1, wv, 103
shrsw t1, t1, c8
subssw wr, wr, t1
addssw wr, wr, wv

addssw wb, wy, wu
addssw wb, wb, wu
mullw t1, wu, 4
shrsw t1, t1, c8
addssw wb, wb, t1

mullw t1, wu, 100
shrsw t1, t1, c8
subssw wg, wy, t1
mullw t1, wv, 104
shrsw t1, t1, c8
subssw wg, wg, t1
subssw wg, wg, t1

convssswb r, wr
convssswb g, wg
convssswb b, wb

mergebw t1, b, g
mergebw t2, r, 255
mergewl x, t1, t2
x4 addb argb, x, c128



.function videomixer_video_convert_orc_convert_I420_BGRA_avg
.dest 4 argb guint8
.source 1 y guint8
.source 1 u1 guint8
.source 1 u2 guint8
.source 1 v1 guint8
.source 1 v2 guint8
.temp 2 t1
.temp 2 t2
.temp 1 t3
.temp 1 t4
.temp 2 wy
.temp 2 wu
.temp 2 wv
.temp 2 wr
.temp 2 wg
.temp 2 wb
.temp 1 r
.temp 1 g
.temp 1 b
.temp 4 x
.const 1 c8 8
.const 1 c128 128

subb t3, y, c128
convsbw wy, t3
loadupib t3, u1
loadupib t4, u2
avgub t3, t3, t4
subb t3, t3, c128
convsbw wu, t3
loadupib t3, v1
loadupib t4, v2
avgub t3, t3, t4
subb t3, t3, c128
convsbw wv, t3

mullw t1, wy, 42
shrsw t1, t1, c8
addssw wy, wy, t1

addssw wr, wy, wv
mullw t1, wv, 103
shrsw t1, t1, c8
subssw wr, wr, t1
addssw wr, wr, wv

addssw wb, wy, wu
addssw wb, wb, wu
mullw t1, wu, 4
shrsw t1, t1, c8
addssw wb, wb, t1

mullw t1, wu, 100
shrsw t1, t1, c8
subssw wg, wy, t1
mullw t1, wv, 104
shrsw t1, t1, c8
subssw wg, wg, t1
subssw wg, wg, t1

convssswb r, wr
convssswb g, wg
convssswb b, wb

mergebw t1, b, g
mergebw t2, r, 255
mergewl x, t1, t2
x4 addb argb, x, c128



.function videomixer_video_convert_orc_getline_I420
.dest 4 d guint8
.source 1 y guint8
.source 1 u guint8
.source 1 v guint8
.const 1 c255 255
.temp 2 uv
.temp 2 ay
.temp 1 tu
.temp 1 tv

loadupdb tu, u
loadupdb tv, v
mergebw uv, tu, tv
mergebw ay, c255, y
mergewl d, ay, uv

.function videomixer_video_convert_orc_getline_YUV9
.dest 8 d guint8
.source 2 y guint8
.source 1 u guint8
.source 1 v guint8
.const 1 c255 255
.temp 2 tuv
.temp 4 ay
.temp 4 uv
.temp 1 tu
.temp 1 tv

loadupdb tu, u
loadupdb tv, v
mergebw tuv, tu, tv
mergewl uv, tuv, tuv
x2 mergebw ay, c255, y
x2 mergewl d, ay, uv

.function videomixer_video_convert_orc_getline_YUY2
.dest 8 ayuv guint8
.source 4 yuy2 guint8
.const 2 c255 0xff
.temp 2 yy
.temp 2 uv
.temp 4 ayay
.temp 4 uvuv

x2 splitwb uv, yy, yuy2
x2 mergebw ayay, c255, yy
mergewl uvuv, uv, uv
x2 mergewl ayuv, ayay, uvuv


.function videomixer_video_convert_orc_getline_UYVY
.dest 8 ayuv guint8
.source 4 uyvy guint8
.const 2 c255 0xff
.temp 2 yy
.temp 2 uv
.temp 4 ayay
.temp 4 uvuv

x2 splitwb yy, uv, uyvy
x2 mergebw ayay, c255, yy
mergewl uvuv, uv, uv
x2 mergewl ayuv, ayay, uvuv


.function videomixer_video_convert_orc_getline_YVYU
.dest 8 ayuv guint8
.source 4 uyvy guint8
.const 2 c255 0xff
.temp 2 yy
.temp 2 uv
.temp 4 ayay
.temp 4 uvuv

x2 splitwb uv, yy, uyvy
swapw uv, uv
x2 mergebw ayay, c255, yy
mergewl uvuv, uv, uv
x2 mergewl ayuv, ayay, uvuv


.function videomixer_video_convert_orc_getline_Y42B
.dest 8 ayuv guint8
.source 2 yy guint8
.source 1 u guint8
.source 1 v guint8
.const 1 c255 255
.temp 2 uv
.temp 2 ay
.temp 4 uvuv
.temp 4 ayay

mergebw uv, u, v
x2 mergebw ayay, c255, yy
mergewl uvuv, uv, uv
x2 mergewl ayuv, ayay, uvuv


.function videomixer_video_convert_orc_getline_Y444
.dest 4 ayuv guint8
.source 1 y guint8
.source 1 u guint8
.source 1 v guint8
.const 1 c255 255
.temp 2 uv
.temp 2 ay

mergebw uv, u, v
mergebw ay, c255, y
mergewl ayuv, ay, uv


.function videomixer_video_convert_orc_getline_Y800
.dest 4 ayuv guint8
.source 1 y guint8
.const 1 c255 255
.const 2 c0x8080 0x8080
.temp 2 ay

mergebw ay, c255, y
mergewl ayuv, ay, c0x8080

.function videomixer_video_convert_orc_getline_Y16
.dest 4 ayuv guint8
.source 2 y guint8
.const 1 c255 255
.const 2 c0x8080 0x8080
.temp 2 ay
.temp 1 yb

convhwb yb, y
mergebw ay, c255, yb
mergewl ayuv, ay, c0x8080

.function videomixer_video_convert_orc_getline_BGRA
.dest 4 argb guint8
.source 4 bgra guint8

swapl argb, bgra


.function videomixer_video_convert_orc_getline_ABGR
.dest 4 argb guint8
.source 4 abgr guint8
.temp 1 a
.temp 1 r
.temp 1 g
.temp 1 b
.temp 2 gr
.temp 2 ab
.temp 2 ar
.temp 2 gb

splitlw gr, ab, abgr
splitwb r, g, gr
splitwb b, a, ab
mergebw ar, a, r
mergebw gb, g, b
mergewl argb, ar, gb


.function videomixer_video_convert_orc_getline_RGBA
.dest 4 argb guint8
.source 4 rgba guint8
.temp 1 a
.temp 1 r
.temp 1 g
.temp 1 b
.temp 2 rg
.temp 2 ba
.temp 2 ar
.temp 2 gb

splitlw ba, rg, rgba
splitwb g, r, rg
splitwb a, b, ba
mergebw ar, a, r
mergebw gb, g, b
mergewl argb, ar, gb


.function videomixer_video_convert_orc_getline_NV12
.dest 8 d guint8
.source 2 y guint8
.source 2 uv guint8
.const 1 c255 255
.temp 4 ay
.temp 4 uvuv

mergewl uvuv, uv, uv
x2 mergebw ay, c255, y
x2 mergewl d, ay, uvuv


.function videomixer_video_convert_orc_getline_NV21
.dest 8 d guint8
.source 2 y guint8
.source 2 vu guint8
.const 1 c255 255
.temp 2 uv
.temp 4 ay
.temp 4 uvuv

swapw uv, vu
mergewl uvuv, uv, uv
x2 mergebw ay, c255, y
x2 mergewl d, ay, uvuv

.function videomixer_video_convert_orc_getline_A420
.dest 4 d guint8
.source 1 y guint8
.source 1 u guint8
.source 1 v guint8
.source 1 a guint8
.temp 2 uv
.temp 2 ay
.temp 1 tu
.temp 1 tv

loadupdb tu, u
loadupdb tv, v
mergebw uv, tu, tv
mergebw ay, a, y
mergewl d, ay, uv

.function videomixer_video_convert_orc_putline_I420
.dest 2 y guint8
.dest 1 u guint8
.dest 1 v guint8
.source 8 ayuv guint8
.temp 4 ay
.temp 4 uv
.temp 2 uu
.temp 2 vv
.temp 1 t1
.temp 1 t2

x2 splitlw uv, ay, ayuv
x2 select1wb y, ay
x2 splitwb vv, uu, uv
splitwb t1, t2, uu
avgub u, t1, t2
splitwb t1, t2, vv
avgub v, t1, t2



.function videomixer_video_convert_orc_putline_YUY2
.dest 4 yuy2 guint8
.source 8 ayuv guint8
.temp 2 yy
.temp 2 uv1
.temp 2 uv2
.temp 4 ayay
.temp 4 uvuv

x2 splitlw uvuv, ayay, ayuv
splitlw uv1, uv2, uvuv
x2 avgub uv1, uv1, uv2
x2 select1wb yy, ayay
x2 mergebw yuy2, yy, uv1


.function videomixer_video_convert_orc_putline_YVYU
.dest 4 yuy2 guint8
.source 8 ayuv guint8
.temp 2 yy
.temp 2 uv1
.temp 2 uv2
.temp 4 ayay
.temp 4 uvuv

x2 splitlw uvuv, ayay, ayuv
splitlw uv1, uv2, uvuv
x2 avgub uv1, uv1, uv2
x2 select1wb yy, ayay
swapw uv1, uv1
x2 mergebw yuy2, yy, uv1


.function videomixer_video_convert_orc_putline_UYVY
.dest 4 yuy2 guint8
.source 8 ayuv guint8
.temp 2 yy
.temp 2 uv1
.temp 2 uv2
.temp 4 ayay
.temp 4 uvuv

x2 splitlw uvuv, ayay, ayuv
splitlw uv1, uv2, uvuv
x2 avgub uv1, uv1, uv2
x2 select1wb yy, ayay
x2 mergebw yuy2, uv1, yy



.function videomixer_video_convert_orc_putline_Y42B
.dest 2 y guint8
.dest 1 u guint8
.dest 1 v guint8
.source 8 ayuv guint8
.temp 4 ayay
.temp 4 uvuv
.temp 2 uv1
.temp 2 uv2

x2 splitlw uvuv, ayay, ayuv
splitlw uv1, uv2, uvuv
x2 avgub uv1, uv1, uv2
splitwb v, u, uv1
x2 select1wb y, ayay


.function videomixer_video_convert_orc_putline_Y444
.dest 1 y guint8
.dest 1 u guint8
.dest 1 v guint8
.source 4 ayuv guint8
.temp 2 ay
.temp 2 uv

splitlw uv, ay, ayuv
splitwb v, u, uv
select1wb y, ay


.function videomixer_video_convert_orc_putline_Y800
.dest 1 y guint8
.source 4 ayuv guint8
.temp 2 ay

select0lw ay, ayuv
select1wb y, ay

.function videomixer_video_convert_orc_putline_Y16
.dest 2 y guint8
.source 4 ayuv guint8
.temp 2 ay
.temp 1 yb

select0lw ay, ayuv
select1wb yb, ay
convubw ay, yb
shlw y, ay, 8

.function videomixer_video_convert_orc_putline_BGRA
.dest 4 bgra guint8
.source 4 argb guint8

swapl bgra, argb


.function videomixer_video_convert_orc_putline_ABGR
.dest 4 abgr guint8
.source 4 argb guint8
.temp 1 a
.temp 1 r
.temp 1 g
.temp 1 b
.temp 2 gr
.temp 2 ab
.temp 2 ar
.temp 2 gb

splitlw gb, ar, argb
splitwb b, g, gb
splitwb r, a, ar
mergebw ab, a, b
mergebw gr, g, r
mergewl abgr, ab, gr


.function videomixer_video_convert_orc_putline_RGBA
.dest 4 rgba guint8
.source 4 argb guint8
.temp 1 a
.temp 1 r
.temp 1 g
.temp 1 b
.temp 2 rg
.temp 2 ba
.temp 2 ar
.temp 2 gb

splitlw gb, ar, argb
splitwb b, g, gb
splitwb r, a, ar
mergebw ba, b, a
mergebw rg, r, g
mergewl rgba, rg, ba


.function videomixer_video_convert_orc_putline_NV12
.dest 2 y guint8
.dest 2 uv guint8
.source 8 ayuv guint8
.temp 4 ay
.temp 4 uvuv
.temp 2 uv1
.temp 2 uv2

x2 splitlw uvuv, ay, ayuv
x2 select1wb y, ay
splitlw uv1, uv2, uvuv
x2 avgub uv, uv1, uv2


.function videomixer_video_convert_orc_putline_NV21
.dest 2 y guint8
.dest 2 vu guint8
.source 8 ayuv guint8
.temp 4 ay
.temp 4 uvuv
.temp 2 uv1
.temp 2 uv2
.temp 2 uv

x2 splitlw uvuv, ay, ayuv
x2 select1wb y, ay
splitlw uv1, uv2, uvuv
x2 avgub uv, uv1, uv2
swapw vu, uv

.function videomixer_video_convert_orc_putline_A420
.dest 2 y guint8
.dest 1 u guint8
.dest 1 v guint8
.dest 2 a guint8
.source 8 ayuv guint8
.temp 4 ay
.temp 4 uv
.temp 2 uu
.temp 2 vv
.temp 1 t1
.temp 1 t2

x2 splitlw uv, ay, ayuv
x2 select1wb y, ay
x2 select0wb a, ay
x2 splitwb vv, uu, uv
splitwb t1, t2, uu
avgub u, t1, t2
splitwb t1, t2, vv
avgub v, t1, t2
