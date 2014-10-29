.function video_orc_blend_little
.flags 1d
.dest 4 d guint8
.source 4 s guint8
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

.function video_orc_blend_big
.flags 1d
.dest 4 d guint8
.source 4 s guint8
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

.function video_orc_unpack_I420
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


.function video_orc_pack_I420
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
select0wb u, uu
select0wb v, vv

.function video_orc_unpack_YUY2
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


.function video_orc_pack_YUY2
.dest 4 yuy2 guint8
.source 8 ayuv guint8
.temp 2 yy
.temp 2 uv
.temp 4 ayay
.temp 4 uvuv

x2 splitlw uvuv, ayay, ayuv
select0lw uv, uvuv
x2 select1wb yy, ayay
x2 mergebw yuy2, yy, uv


.function video_orc_pack_UYVY
.dest 4 yuy2 guint8
.source 8 ayuv guint8
.temp 2 yy
.temp 2 uv
.temp 4 ayay
.temp 4 uvuv

x2 splitlw uvuv, ayay, ayuv
select0lw uv, uvuv
x2 select1wb yy, ayay
x2 mergebw yuy2, uv, yy


.function video_orc_unpack_UYVY
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


.function video_orc_unpack_YVYU
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


.function video_orc_pack_YVYU
.dest 4 yuy2 guint8
.source 8 ayuv guint8
.temp 2 yy
.temp 2 uv
.temp 4 ayay
.temp 4 uvuv

x2 splitlw uvuv, ayay, ayuv
select0lw uv, uvuv
x2 select1wb yy, ayay
swapw uv, uv
x2 mergebw yuy2, yy, uv


.function video_orc_unpack_YUV9
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


.function video_orc_unpack_Y42B
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

.function video_orc_pack_Y42B
.dest 2 y guint8
.dest 1 u guint8
.dest 1 v guint8
.source 8 ayuv guint8
.temp 4 ayay
.temp 4 uvuv
.temp 2 uv

x2 splitlw uvuv, ayay, ayuv
select0lw uv, uvuv
splitwb v, u, uv
x2 select1wb y, ayay


.function video_orc_unpack_Y444
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


.function video_orc_pack_Y444
.dest 1 y guint8
.dest 1 u guint8
.dest 1 v guint8
.source 4 ayuv guint8
.temp 2 ay
.temp 2 uv

splitlw uv, ay, ayuv
splitwb v, u, uv
select1wb y, ay

.function video_orc_unpack_GRAY8
.dest 4 ayuv guint8
.source 1 y guint8
.const 1 c255 255
.const 2 c0x8080 0x8080
.temp 2 ay

mergebw ay, c255, y
mergewl ayuv, ay, c0x8080


.function video_orc_pack_GRAY8
.dest 1 y guint8
.source 4 ayuv guint8
.temp 2 ay

select0lw ay, ayuv
select1wb y, ay


.function video_orc_unpack_BGRA
.dest 4 argb guint8
.source 4 bgra guint8

swapl argb, bgra

.function video_orc_pack_BGRA
.dest 4 bgra guint8
.source 4 argb guint8

swapl bgra, argb

.function video_orc_pack_RGBA
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

.function video_orc_unpack_RGBA
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


.function video_orc_unpack_ABGR
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


.function video_orc_pack_ABGR
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

.function video_orc_unpack_NV12
.dest 8 d guint8
.source 2 y guint8
.source 2 uv guint8
.const 1 c255 255
.temp 4 ay
.temp 4 uvuv

mergewl uvuv, uv, uv
x2 mergebw ay, c255, y
x2 mergewl d, ay, uvuv

.function video_orc_pack_NV12
.dest 2 y guint8
.dest 2 uv guint8
.source 8 ayuv guint8
.temp 4 ay
.temp 4 uvuv

x2 splitlw uvuv, ay, ayuv
x2 select1wb y, ay
select0lw uv, uvuv

.function video_orc_unpack_NV21
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


.function video_orc_pack_NV21
.dest 2 y guint8
.dest 2 vu guint8
.source 8 ayuv guint8
.temp 4 ay
.temp 4 uvuv
.temp 2 uv

x2 splitlw uvuv, ay, ayuv
x2 select1wb y, ay
select0lw uv, uvuv
swapw vu, uv

.function video_orc_unpack_NV24
.dest 4 d guint8
.source 1 y guint8
.source 2 uv guint8
.const 1 c255 255
.temp 2 ay

mergebw ay, c255, y
mergewl d, ay, uv

.function video_orc_pack_NV24
.dest 1 y guint8
.dest 2 uv guint8
.source 4 ayuv guint8
.temp 2 ay

splitlw uv, ay, ayuv
select1wb y, ay

.function video_orc_unpack_A420
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

.function video_orc_pack_A420
.dest 2 y guint8
.dest 1 u guint8
.dest 1 v guint8
.dest 2 a guint8
.source 8 ayuv guint8
.temp 4 ay
.temp 4 uv
.temp 2 uu
.temp 2 vv

x2 splitlw uv, ay, ayuv
x2 select1wb y, ay
x2 select0wb a, ay
x2 splitwb vv, uu, uv
select0wb u, uu
select0wb v, vv

.function video_orc_resample_bilinear_u32
.dest 4 d1 guint8
.source 4 s1 guint8
.param 4 p1
.param 4 p2

ldreslinl d1, s1, p1, p2

.function video_orc_merge_linear_u8
.dest 1 d1
.source 1 s1
.source 1 s2
.param 1 p1
.temp 2 t1
.temp 2 t2
.temp 1 a
.temp 1 t

loadb a, s1
convubw t1, s1
convubw t2, s2
subw t2, t2, t1
mullw t2, t2, p1
addw t2, t2, 128
convhwb t, t2
addb d1, t, a


.function video_orc_memcpy_2d
.flags 2d
.dest 1 d1 guint8
.source 1 s1 guint8

copyb d1, s1

.function video_orc_convert_I420_UYVY
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


.function video_orc_convert_I420_YUY2
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



.function video_orc_convert_I420_AYUV
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


.function video_orc_convert_YUY2_I420
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


.function video_orc_convert_UYVY_YUY2
.flags 2d
.dest 4 yuy2 guint8
.source 4 uyvy guint8

x2 swapw yuy2, uyvy


.function video_orc_planar_chroma_420_422
.flags 2d
.dest 1 d1 guint8
.dest 1 d2 guint8
.source 1 s guint8

copyb d1, s
copyb d2, s


.function video_orc_planar_chroma_420_444
.flags 2d
.dest 2 d1 guint8
.dest 2 d2 guint8
.source 1 s guint8
.temp 2 t

splatbw t, s
storew d1, t
storew d2, t


.function video_orc_planar_chroma_422_444
.flags 2d
.dest 2 d1 guint8
.source 1 s guint8
.temp 2 t

splatbw t, s
storew d1, t


.function video_orc_planar_chroma_444_422
.flags 2d
.dest 1 d guint8
.source 2 s guint8
.temp 1 t1
.temp 1 t2

splitwb t1, t2, s
avgub d, t1, t2


.function video_orc_planar_chroma_444_420
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


.function video_orc_planar_chroma_422_420
.flags 2d
.dest 1 d guint8
.source 1 s1 guint8
.source 1 s2 guint8

avgub d, s1, s2


.function video_orc_convert_YUY2_AYUV
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


.function video_orc_convert_UYVY_AYUV
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


.function video_orc_convert_YUY2_Y42B
.flags 2d
.dest 2 y guint8
.dest 1 u guint8
.dest 1 v guint8
.source 4 yuy2 guint8
.temp 2 uv

x2 splitwb uv, y, yuy2
splitwb v, u, uv


.function video_orc_convert_UYVY_Y42B
.flags 2d
.dest 2 y guint8
.dest 1 u guint8
.dest 1 v guint8
.source 4 uyvy guint8
.temp 2 uv

x2 splitwb y, uv, uyvy
splitwb v, u, uv


.function video_orc_convert_YUY2_Y444
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


.function video_orc_convert_UYVY_Y444
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


.function video_orc_convert_UYVY_I420
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



.function video_orc_convert_AYUV_I420
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



.function video_orc_convert_AYUV_YUY2
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


.function video_orc_convert_AYUV_UYVY
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



.function video_orc_convert_AYUV_Y42B
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


.function video_orc_convert_AYUV_Y444
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


.function video_orc_convert_Y42B_YUY2
.flags 2d
.dest 4 yuy2 guint8
.source 2 y guint8
.source 1 u guint8
.source 1 v guint8
.temp 2 uv

mergebw uv, u, v
x2 mergebw yuy2, y, uv


.function video_orc_convert_Y42B_UYVY
.flags 2d
.dest 4 uyvy guint8
.source 2 y guint8
.source 1 u guint8
.source 1 v guint8
.temp 2 uv

mergebw uv, u, v
x2 mergebw uyvy, uv, y


.function video_orc_convert_Y42B_AYUV
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


.function video_orc_convert_Y444_YUY2
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


.function video_orc_convert_Y444_UYVY
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


.function video_orc_convert_Y444_AYUV
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



.function video_orc_convert_AYUV_ARGB
.flags 2d
.dest 4 argb guint8
.source 4 ayuv guint8
.param 2 p1
.param 2 p2
.param 2 p3
.param 2 p4
.param 2 p5
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
.const 1 c128 128

x4 subb x, ayuv, c128 
splitlw wv, wy, x
splitwb y, a, wy
splitwb v, u, wv

splatbw wy, y
splatbw wu, u
splatbw wv, v

mulhsw wy, wy, p1

mulhsw wr, wv, p2
addw wr, wy, wr
convssswb r, wr
mergebw wr, a, r

mulhsw wb, wu, p3
addw wb, wy, wb
convssswb b, wb

mulhsw wg, wu, p4
addw wg, wy, wg
mulhsw wy, wv, p5
addw wg, wg, wy

convssswb g, wg

mergebw wb, g, b
mergewl x, wr, wb
x4 addb argb, x, c128

.function video_orc_convert_AYUV_BGRA
.flags 2d
.dest 4 bgra guint8
.source 4 ayuv guint8
.param 2 p1
.param 2 p2
.param 2 p3
.param 2 p4
.param 2 p5
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
.const 1 c128 128

x4 subb x, ayuv, c128 
splitlw wv, wy, x
splitwb y, a, wy
splitwb v, u, wv

splatbw wy, y
splatbw wu, u
splatbw wv, v

mulhsw wy, wy, p1

mulhsw wr, wv, p2
addw wr, wy, wr
convssswb r, wr
mergebw wr, r, a

mulhsw wb, wu, p3
addw wb, wy, wb
convssswb b, wb

mulhsw wg, wu, p4
addw wg, wy, wg
mulhsw wy, wv, p5
addw wg, wg, wy

convssswb g, wg

mergebw wb, b, g
mergewl x, wb, wr
x4 addb bgra, x, c128


.function video_orc_convert_AYUV_ABGR
.flags 2d
.dest 4 argb guint8
.source 4 ayuv guint8
.param 2 p1
.param 2 p2
.param 2 p3
.param 2 p4
.param 2 p5
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
.const 1 c128 128

x4 subb x, ayuv, c128 
splitlw wv, wy, x
splitwb y, a, wy
splitwb v, u, wv

splatbw wy, y
splatbw wu, u
splatbw wv, v

mulhsw wy, wy, p1

mulhsw wr, wv, p2
addw wr, wy, wr
convssswb r, wr

mulhsw wb, wu, p3
addw wb, wy, wb
convssswb b, wb
mergebw wb, a, b

mulhsw wg, wu, p4
addw wg, wy, wg
mulhsw wy, wv, p5
addw wg, wg, wy

convssswb g, wg

mergebw wr, g, r
mergewl x, wb, wr
x4 addb argb, x, c128

.function video_orc_convert_AYUV_RGBA
.flags 2d
.dest 4 argb guint8
.source 4 ayuv guint8
.param 2 p1
.param 2 p2
.param 2 p3
.param 2 p4
.param 2 p5
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
.const 1 c128 128

x4 subb x, ayuv, c128 
splitlw wv, wy, x
splitwb y, a, wy
splitwb v, u, wv

splatbw wy, y
splatbw wu, u
splatbw wv, v

mulhsw wy, wy, p1

mulhsw wr, wv, p2
addw wr, wy, wr
convssswb r, wr

mulhsw wb, wu, p3
addw wb, wy, wb
convssswb b, wb
mergebw wb, b, a

mulhsw wg, wu, p4
addw wg, wy, wg
mulhsw wy, wv, p5
addw wg, wg, wy

convssswb g, wg

mergebw wr, r, g
mergewl x, wr, wb
x4 addb argb, x, c128

.function video_orc_convert_I420_BGRA
.dest 4 argb guint8
.source 1 y guint8
.source 1 u guint8
.source 1 v guint8
.param 2 p1
.param 2 p2
.param 2 p3
.param 2 p4
.param 2 p5
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
.const 1 c128 128

subb r, y, c128
splatbw wy, r
loadupdb r, u
subb r, r, c128
splatbw wu, r
loadupdb r, v
subb r, r, c128
splatbw wv, r

mulhsw wy, wy, p1

mulhsw wr, wv, p2
addw wr, wy, wr
convssswb r, wr
mergebw wr, r, 127

mulhsw wb, wu, p3
addw wb, wy, wb
convssswb b, wb

mulhsw wg, wu, p4
addw wg, wy, wg
mulhsw wy, wv, p5
addw wg, wg, wy

convssswb g, wg

mergebw wb, b, g
mergewl x, wb, wr
x4 addb argb, x, c128

.function video_orc_matrix8
.source 4 argb guint8
.dest 4 ayuv guint8
.longparam 8 p1
.longparam 8 p2
.longparam 8 p3
.const 1 c128 128
.temp 2 w1
.temp 2 w2
.temp 1 b1
.temp 1 b2
.temp 4 l1
.temp 4 ayuv2
.temp 8 aq
.temp 8 q1
.temp 8 pr1
.temp 8 pr2
.temp 8 pr3

loadpq pr1, p1
loadpq pr2, p2
loadpq pr3, p3

x4 subb l1, argb, c128

select0lw w1, l1
select1lw w2, l1
select0wb b1, w1
select1wb b2, w1

splatbl l1, b1
mergelq aq, l1, l1
andq aq, aq, 0xff

splatbl l1, b2
mergelq q1, l1, l1
x4 mulhsw q1, q1, pr1
x4 addssw aq, aq, q1

select0wb b1, w2
splatbl l1,b1
mergelq q1, l1, l1
x4 mulhsw q1, q1, pr2
x4 addssw aq, aq, q1

select1wb b2, w2
splatbl l1, b2
mergelq q1, l1, l1
x4 mulhsw q1, q1, pr3
x4 addssw aq, aq, q1

x4 convssswb ayuv2, aq
x4 addb ayuv, ayuv2, c128

#.function video_orc_resample_h_near_8888
#.source 4 src guint32
#.source 4 idx
#.dest 4 dest guint32
#.temp 4 t
#
#loadidxl t, src, idx
#storel dest, t

#.function video_orc_resample_h_2tap_8888_16
#.source 4 src1 guint32
#.source 4 src2 guint32
#.source 8 coef1 guint64
#.source 8 coef2 guint64
#.source 4 idx
#.dest 4 dest guint32
#.temp 4 t1
#.temp 4 t2
#.temp 8 q1
#.temp 8 q2
#
#loadidxl t1, src1, idx
#x4 convubw q1, t1
#x4 mulhuw q1, q1, coef1
#
#loadidxl t2, src2, idx
#x4 convubw q2, t2
#x4 mulhuw q2, q2, coef2
#
#x4 addw q2, q2, q1
#x4 convuuswb dest, q2
#
#.function video_orc_resample_h_2tap_8888_lq
#.source 4 src1 guint32
#.source 4 src2 guint32
#.source 8 coef1 guint64
#.source 4 idx
#.dest 4 dest guint32
#.temp 4 t1
#.temp 4 t2
#.temp 8 q1
#.temp 8 q2
#
#loadidxl t1, src1, idx
#x4 convubw q1, t1
#loadidxl t2, src2, idx
#x4 convubw q2, t2
#x4 subw q2, q2, q1
#
#x4 mullw q2, q2, coef1
#x4 addw q2, q2, 128
#x4 convhwb t2, q2
#x4 addb dest, t2, t1

.function video_orc_resample_v_2tap_8_lq
.source 1 src1 guint32
.source 1 src2 guint32
.dest 1 dest guint32
.param 2 p1
.temp 1 t
.temp 2 w1
.temp 2 w2

convubw w1, src1
convubw w2, src2
subw w2, w2, w1
mullw w2, w2, p1
addw w2, w2, 128
convhwb t, w2
addb dest, t, src1

.function video_orc_resample_v_2tap_8
.source 1 s1 guint32
.source 1 s2 guint32
.dest 1 d1 guint32
.param 2 p1
.temp 1 t
.temp 2 w1
.temp 2 w2
.temp 4 t1
.temp 4 t2

convubw w1, s1
convubw w2, s2
subw w2, w2, w1
mulswl t2, w2, p1
addl t2, t2, 4095
shrsl t2, t2, 12
convlw w2, t2
addw w2, w2, w1
convsuswb d1, w2

.function video_orc_resample_v_4tap_8_lq
.source 1 s1 guint32
.source 1 s2 guint32
.source 1 s3 guint32
.source 1 s4 guint32
.dest 1 d1 guint32
.param 2 p1
.param 2 p2
.param 2 p3
.param 2 p4
.temp 2 w1
.temp 2 w2

convubw w1, s1
mullw w1, w1, p1
convubw w2, s2
mullw w2, w2, p2
addw w1, w1, w2
convubw w2, s3
mullw w2, w2, p3
addw w1, w1, w2
convubw w2, s4
mullw w2, w2, p4
addw w1, w1, w2
addw w1, w1, 32
shrsw w1, w1, 6
convsuswb d1, w1

.function video_orc_resample_v_4tap_8
.source 1 s1 guint32
.source 1 s2 guint32
.source 1 s3 guint32
.source 1 s4 guint32
.dest 1 d1 guint32
.param 2 p1
.param 2 p2
.param 2 p3
.param 2 p4
.temp 2 w1
.temp 2 w2
.temp 4 t1
.temp 4 t2

convubw w1, s1
mulswl t1, w1, p1
convubw w2, s2
mulswl t2, w2, p2
addl t1, t1, t2
convubw w2, s3
mulswl t2, w2, p3
addl t1, t1, t2
convubw w2, s4
mulswl t2, w2, p4
addl t1, t1, t2
addl t1, t1, 4095
shrsl t1, t1, 12
convlw w1, t1
convsuswb d1, w1


