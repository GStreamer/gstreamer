
.function cogorc_memcpy_2d
.flags 2d
.dest 1 d1
.source 1 s1

copyb d1, s1


.function cogorc_downsample_horiz_cosite_1tap
.dest 1 d1
.source 2 s1

select0wb d1, s1


.function cogorc_downsample_horiz_cosite_3tap
.dest 1 d1
.source 2 s1
.source 2 s2
.temp 1 t1
.temp 1 t2
.temp 1 t3
.temp 2 t4
.temp 2 t5
.temp 2 t6

copyw t4, s1
select0wb t1, t4
select1wb t2, t4
select0wb t3, s2
convubw t4, t1
convubw t5, t2
convubw t6, t3
mullw t5, t5, 2
addw t4, t4, t6
addw t4, t4, t5
addw t4, t4, 2
shrsw t4, t4, 2
convsuswb d1, t4


.function cogorc_downsample_420_jpeg
.dest 1 d1
.source 2 s1
.source 2 s2
.temp 2 t1
.temp 1 t2
.temp 1 t3
.temp 1 t4
.temp 1 t5

copyw t1, s1
select0wb t2, t1
select1wb t3, t1
avgub t2, t2, t3
copyw t1, s2
select0wb t4, t1
select1wb t5, t1
avgub t4, t4, t5
avgub d1, t2, t4


.function cogorc_downsample_vert_halfsite_2tap
.dest 1 d1
.source 1 s1
.source 1 s2

avgub d1, s1, s2


.function cogorc_downsample_vert_cosite_3tap
.dest 1 d1
.source 1 s1
.source 1 s2
.source 1 s3
.temp 2 t1
.temp 2 t2
.temp 2 t3

convubw t1, s1
convubw t2, s2
convubw t3, s3
mullw t2, t2, 2
addw t1, t1, t3
addw t1, t1, t2
addw t1, t1, 2
shrsw t1, t1, 2
convsuswb d1, t1



.function cogorc_downsample_vert_halfsite_4tap
.dest 1 d1
.source 1 s1
.source 1 s2
.source 1 s3
.source 1 s4
.temp 2 t1
.temp 2 t2
.temp 2 t3
.temp 2 t4

convubw t1, s1
convubw t2, s2
convubw t3, s3
convubw t4, s4
addw t2, t2, t3
mullw t2, t2, 26
addw t1, t1, t4
mullw t1, t1, 6
addw t2, t2, t1
addw t2, t2, 32
shrsw t2, t2, 6
convsuswb d1, t2


.function cogorc_upsample_horiz_cosite_1tap
.dest 2 d1 guint8
.source 1 s1
.temp 1 t1

copyb t1, s1
mergebw d1, t1, t1


.function cogorc_upsample_horiz_cosite
.dest 2 d1 guint8
.source 1 s1
.source 1 s2
.temp 1 t1
.temp 1 t2

copyb t1, s1
avgub t2, t1, s2
mergebw d1, t1, t2


.function cogorc_upsample_vert_avgub
.dest 1 d1
.source 1 s1
.source 1 s2

avgub d1, s1, s2




.function orc_unpack_yuyv_y
.dest 1 d1
.source 2 s1

select0wb d1, s1


.function orc_unpack_yuyv_u
.dest 1 d1
.source 4 s1
.temp 2 t1

select0lw t1, s1
select1wb d1, t1


.function orc_unpack_yuyv_v
.dest 1 d1
.source 4 s1
.temp 2 t1

select1lw t1, s1
select1wb d1, t1


.function orc_pack_yuyv
.dest 4 d1
.source 2 s1 guint8
.source 1 s2
.source 1 s3
.temp 1 t1
.temp 1 t2
.temp 2 t3
.temp 2 t4
.temp 2 t5

copyw t5, s1
select0wb t1, t5
select1wb t2, t5
mergebw t3, t1, s2
mergebw t4, t2, s3
mergewl d1, t3, t4


.function orc_unpack_uyvy_y
.dest 1 d1
.source 2 s1

select1wb d1, s1


.function orc_unpack_uyvy_u
.dest 1 d1
.source 4 s1
.temp 2 t1

select0lw t1, s1
select0wb d1, t1


.function orc_unpack_uyvy_v
.dest 1 d1
.source 4 s1
.temp 2 t1

select1lw t1, s1
select0wb d1, t1


.function orc_pack_uyvy
.dest 4 d1
.source 2 s1 guint8
.source 1 s2
.source 1 s3
.temp 1 t1
.temp 1 t2
.temp 2 t3
.temp 2 t4
.temp 2 t5

copyw t5, s1
select0wb t1, t5
select1wb t2, t5
mergebw t3, s2, t1
mergebw t4, s3, t2
mergewl d1, t3, t4


.function orc_addc_convert_u8_s16
.dest 1 d1
.source 2 s1 gint16
.temp 2 t1

addw t1, s1, 128
convsuswb d1, t1


.function orc_subc_convert_s16_u8
.dest 2 d1 gint16
.source 1 s1
.temp 2 t1

convubw t1, s1
subw d1, t1, 128


.function orc_splat_u8_ns
.dest 1 d1
.param 1 p1

copyb d1, p1


.function orc_splat_s16_ns
.dest 2 d1 gint16
.param 2 p1

copyw d1, p1


.function orc_matrix2_u8
.dest 1 d1 guint8
.source 1 s1 guint8
.source 1 s2 guint8
.param 2 p1
.param 2 p2
.param 2 p3
.temp 2 t1
.temp 2 t2

convubw t1, s1
mullw t1, t1, p1
convubw t2, s2
mullw t2, t2, p2
addw t1, t1, t2
addw t1, t1, p3
shrsw t1, t1, 6
convsuswb d1, t1


.function orc_matrix2_11_u8
.dest 1 d1 guint8
.source 1 s1 guint8
.source 1 s2 guint8
.param 2 p1
.param 2 p2
.temp 2 t1
.temp 2 t2
.temp 2 t3
.temp 2 t4

convubw t1, s1
subw t1, t1, 16
mullw t3, t1, p1
convubw t2, s2
subw t2, t2, 128
mullw t4, t2, p2
addw t3, t3, t4
addw t3, t3, 128
shrsw t3, t3, 8
addw t3, t3, t1
addw t3, t3, t2
convsuswb d1, t3


.function orc_matrix2_12_u8
.dest 1 d1 guint8
.source 1 s1 guint8
.source 1 s2 guint8
.param 2 p1
.param 2 p2
.temp 2 t1
.temp 2 t2
.temp 2 t3
.temp 2 t4

convubw t1, s1
subw t1, t1, 16
mullw t3, t1, p1
convubw t2, s2
subw t2, t2, 128
mullw t4, t2, p2
addw t3, t3, t4
addw t3, t3, 128
shrsw t3, t3, 8
addw t3, t3, t1
addw t3, t3, t2
addw t3, t3, t2
convsuswb d1, t3


.function orc_matrix3_u8
.dest 1 d1 guint8
.source 1 s1 guint8
.source 1 s2 guint8
.source 1 s3 guint8
.param 2 p1
.param 2 p2
.param 2 p3
.param 2 p4
.temp 2 t1
.temp 2 t2

convubw t1, s1
mullw t1, t1, p1
convubw t2, s2
mullw t2, t2, p2
addw t1, t1, t2
convubw t2, s3
mullw t2, t2, p3
addw t1, t1, t2
addw t1, t1, p4
shrsw t1, t1, 6
convsuswb d1, t1


.function orc_matrix3_100_u8
.dest 1 d1 guint8
.source 1 s1 guint8
.source 1 s2 guint8
.source 1 s3 guint8
.param 2 p1
.param 2 p2
.param 2 p3
.temp 2 t1
.temp 2 t2
.temp 2 t3
#.temp 2 t4

convubw t1, s1
subw t1, t1, 16
mullw t3, t1, p1
convubw t2, s2
subw t2, t2, 128
mullw t2, t2, p2
addw t3, t3, t2
convubw t2, s3
subw t2, t2, 128
mullw t2, t2, p3
addw t3, t3, t2
addw t3, t3, 128
shrsw t3, t3, 8
addw t3, t3, t1
convsuswb d1, t3


.function orc_matrix3_100_offset_u8
.dest 1 d1 guint8
.source 1 s1 guint8
.source 1 s2 guint8
.source 1 s3 guint8
.param 2 p1
.param 2 p2
.param 2 p3
.param 2 p4
.param 2 p5
#.param 2 p6
.temp 2 t1
.temp 2 t2
.temp 2 t3
#.temp 2 t3
#.temp 2 t4

convubw t3, s1
mullw t1, t3, p1
convubw t2, s2
mullw t2, t2, p2
addw t1, t1, t2
convubw t2, s3
mullw t2, t2, p3
addw t1, t1, t2
addw t1, t1, p4
shrsw t1, t1, p5
#addw t1, t1, p6
addw t1, t1, t3
convsuswb d1, t1



.function orc_matrix3_000_u8
.dest 1 d1 guint8
.source 1 s1 guint8
.source 1 s2 guint8
.source 1 s3 guint8
.param 2 p1
.param 2 p2
.param 2 p3
.param 2 p4
.param 2 p5
#.param 2 p6
.temp 2 t1
.temp 2 t2
#.temp 2 t3
#.temp 2 t4

convubw t1, s1
mullw t1, t1, p1
convubw t2, s2
mullw t2, t2, p2
addw t1, t1, t2
convubw t2, s3
mullw t2, t2, p3
addw t1, t1, t2
addw t1, t1, p4
shrsw t1, t1, p5
#addw t1, t1, p6
convwb d1, t1



.function orc_pack_123x
.dest 4 d1 guint32
.source 1 s1
.source 1 s2
.source 1 s3
.param 1 p1
.temp 2 t1
.temp 2 t2

mergebw t1, s1, s2
mergebw t2, s3, p1
mergewl d1, t1, t2


.function orc_pack_x123
.dest 4 d1 guint32
.source 1 s1
.source 1 s2
.source 1 s3
.param 1 p1
.temp 2 t1
.temp 2 t2

mergebw t1, p1, s1
mergebw t2, s2, s3
mergewl d1, t1, t2


.function cogorc_combine2_u8
.dest 1 d1
.source 1 s1
.source 1 s2
.param 2 p1
.param 2 p2
.temp 2 t1
.temp 2 t2

convubw t1, s1
mullw t1, t1, p1
convubw t2, s2
mullw t2, t2, p2
addw t1, t1, t2
shruw t1, t1, 8
convsuswb d1, t1


.function cogorc_combine4_u8
.dest 1 d1
.source 1 s1
.source 1 s2
.source 1 s3
.source 1 s4
.param 2 p1
.param 2 p2
.param 2 p3
.param 2 p4
.temp 2 t1
.temp 2 t2

convubw t1, s1
mullw t1, t1, p1
convubw t2, s2
mullw t2, t2, p2
addw t1, t1, t2
convubw t2, s3
mullw t2, t2, p3
addw t1, t1, t2
convubw t2, s4
mullw t2, t2, p4
addw t1, t1, t2
addw t1, t1, 32
shrsw t1, t1, 6
convsuswb d1, t1


.function cogorc_unpack_axyz_0
.dest 1 d1
.source 4 s1
.temp 2 t1

select0lw t1, s1
select0wb d1, t1


.function cogorc_unpack_axyz_1
.dest 1 d1
.source 4 s1
.temp 2 t1

select0lw t1, s1
select1wb d1, t1


.function cogorc_unpack_axyz_2
.dest 1 d1
.source 4 s1
.temp 2 t1

select1lw t1, s1
select0wb d1, t1


.function cogorc_unpack_axyz_3
.dest 1 d1
.source 4 s1
.temp 2 t1

select1lw t1, s1
select1wb d1, t1


.function cogorc_resample_horiz_1tap
.dest 1 d1
.source 1 s1
.param 4 p1
.param 4 p2

ldresnearb d1, s1, p1, p2


.function cogorc_resample_horiz_2tap
.dest 1 d1
.source 1 s1
.param 4 p1
.param 4 p2

ldreslinb d1, s1, p1, p2


.function cogorc_convert_I420_UYVY
.dest 4 d1
.dest 4 d2
.source 2 y1
.source 2 y2
.source 1 u
.source 1 v
.temp 2 uv

mergebw uv, u, v
x2 mergebw d1, uv, y1
x2 mergebw d2, uv, y2


.function cogorc_convert_I420_YUY2
.dest 4 d1
.dest 4 d2
.source 2 y1
.source 2 y2
.source 1 u
.source 1 v
.temp 2 uv

mergebw uv, u, v
x2 mergebw d1, y1, uv
x2 mergebw d2, y2, uv



.function cogorc_convert_I420_AYUV
.dest 4 d1
.dest 4 d2
.source 1 y1
.source 1 y2
.source 1 u
.source 1 v
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


.function cogorc_convert_YUY2_I420
.dest 2 y1
.dest 2 y2
.dest 1 u
.dest 1 v
.source 4 yuv1
.source 4 yuv2
.temp 2 t1
.temp 2 t2
.temp 2 ty

x2 splitwb t1, ty, yuv1
storew y1, ty
x2 splitwb t2, ty, yuv2
storew y2, ty
x2 avgub t1, t1, t2
splitwb v, u, t1


.function cogorc_convert_UYVY_YUY2
.flags 2d
.dest 4 yuy2
.source 4 uyvy

x2 swapw yuy2, uyvy


.function cogorc_planar_chroma_420_422
.flags 2d
.dest 1 d1
.dest 1 d2
.source 1 s

copyb d1, s
copyb d2, s


.function cogorc_planar_chroma_420_444
.flags 2d
.dest 2 d1
.dest 2 d2
.source 1 s
.temp 2 t

splatbw t, s
storew d1, t
storew d2, t


.function cogorc_planar_chroma_422_444
.flags 2d
.dest 2 d1
.source 1 s
.temp 2 t

splatbw t, s
storew d1, t


.function cogorc_planar_chroma_444_422
.flags 2d
.dest 1 d
.source 2 s
.temp 1 t1
.temp 1 t2

splitwb t1, t2, s
avgub d, t1, t2


.function cogorc_planar_chroma_444_420
.flags 2d
.dest 1 d
.source 2 s1
.source 2 s2
.temp 2 t
.temp 1 t1
.temp 1 t2

x2 avgub t, s1, s2
splitwb t1, t2, t
avgub d, t1, t2


.function cogorc_planar_chroma_422_420
.flags 2d
.dest 1 d
.source 1 s1
.source 1 s2

avgub d, s1, s2


.function cogorc_convert_YUY2_AYUV
.flags 2d
.dest 8 ayuv
.source 4 yuy2
.const 2 c255 0xff
.temp 2 yy
.temp 2 uv
.temp 4 ayay
.temp 4 uvuv

x2 splitwb uv, yy, yuy2
x2 mergebw ayay, c255, yy
mergewl uvuv, uv, uv
x2 mergewl ayuv, ayay, uvuv


.function cogorc_convert_UYVY_AYUV
.flags 2d
.dest 8 ayuv
.source 4 uyvy
.const 2 c255 0xff
.temp 2 yy
.temp 2 uv
.temp 4 ayay
.temp 4 uvuv

x2 splitwb yy, uv, uyvy
x2 mergebw ayay, c255, yy
mergewl uvuv, uv, uv
x2 mergewl ayuv, ayay, uvuv


.function cogorc_convert_YUY2_Y42B
.flags 2d
.dest 2 y
.dest 1 u
.dest 1 v
.source 4 yuy2
.temp 2 uv

x2 splitwb uv, y, yuy2
splitwb v, u, uv


.function cogorc_convert_UYVY_Y42B
.flags 2d
.dest 2 y
.dest 1 u
.dest 1 v
.source 4 uyvy
.temp 2 uv

x2 splitwb y, uv, uyvy
splitwb v, u, uv


.function cogorc_convert_YUY2_Y444
.flags 2d
.dest 2 y
.dest 2 uu
.dest 2 vv
.source 4 yuy2
.temp 2 uv
.temp 1 u
.temp 1 v

x2 splitwb uv, y, yuy2
splitwb v, u, uv
splatbw uu, u
splatbw vv, v


.function cogorc_convert_UYVY_Y444
.flags 2d
.dest 2 y
.dest 2 uu
.dest 2 vv
.source 4 uyvy
.temp 2 uv
.temp 1 u
.temp 1 v

x2 splitwb y, uv, uyvy
splitwb v, u, uv
splatbw uu, u
splatbw vv, v


.function cogorc_convert_UYVY_I420
.dest 2 y1
.dest 2 y2
.dest 1 u
.dest 1 v
.source 4 yuv1
.source 4 yuv2
.temp 2 t1
.temp 2 t2
.temp 2 ty

x2 splitwb ty, t1, yuv1
storew y1, ty
x2 splitwb ty, t2, yuv2
storew y2, ty
x2 avgub t1, t1, t2
splitwb v, u, t1



.function cogorc_convert_AYUV_I420
.flags 2d
.dest 2 y1
.dest 2 y2
.dest 1 u
.dest 1 v
.source 8 ayuv1
.source 8 ayuv2
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



.function cogorc_convert_AYUV_YUY2
.flags 2d
.dest 4 yuy2
.source 8 ayuv
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


.function cogorc_convert_AYUV_UYVY
.flags 2d
.dest 4 yuy2
.source 8 ayuv
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



.function cogorc_convert_AYUV_Y42B
.flags 2d
.dest 2 y
.dest 1 u
.dest 1 v
.source 8 ayuv
.temp 4 ayay
.temp 4 uvuv
.temp 2 uv1
.temp 2 uv2

x2 splitlw uvuv, ayay, ayuv
splitlw uv1, uv2, uvuv
x2 avgub uv1, uv1, uv2
splitwb v, u, uv1
x2 select1wb y, ayay


.function cogorc_convert_AYUV_Y444
.flags 2d
.dest 1 y
.dest 1 u
.dest 1 v
.source 4 ayuv
.temp 2 ay
.temp 2 uv

splitlw uv, ay, ayuv
splitwb v, u, uv
select1wb y, ay


.function cogorc_convert_Y42B_YUY2
.flags 2d
.dest 4 yuy2
.source 2 y
.source 1 u
.source 1 v
.temp 2 uv

mergebw uv, u, v
x2 mergebw yuy2, y, uv


.function cogorc_convert_Y42B_UYVY
.flags 2d
.dest 4 uyvy
.source 2 y
.source 1 u
.source 1 v
.temp 2 uv

mergebw uv, u, v
x2 mergebw uyvy, uv, y


.function cogorc_convert_Y42B_AYUV
.flags 2d
.dest 8 ayuv
.source 2 yy
.source 1 u
.source 1 v
.const 1 c255 255
.temp 2 uv
.temp 2 ay
.temp 4 uvuv
.temp 4 ayay

mergebw uv, u, v
x2 mergebw ayay, c255, yy
mergewl uvuv, uv, uv
x2 mergewl ayuv, ayay, uvuv


.function cogorc_convert_Y444_YUY2
.flags 2d
.dest 4 yuy2
.source 2 y
.source 2 u
.source 2 v
.temp 2 uv
.temp 4 uvuv
.temp 2 uv1
.temp 2 uv2

x2 mergebw uvuv, u, v
splitlw uv1, uv2, uvuv
x2 avgub uv, uv1, uv2
x2 mergebw yuy2, y, uv


.function cogorc_convert_Y444_UYVY
.flags 2d
.dest 4 uyvy
.source 2 y
.source 2 u
.source 2 v
.temp 2 uv
.temp 4 uvuv
.temp 2 uv1
.temp 2 uv2

x2 mergebw uvuv, u, v
splitlw uv1, uv2, uvuv
x2 avgub uv, uv1, uv2
x2 mergebw uyvy, uv, y


.function cogorc_convert_Y444_AYUV
.flags 2d
.dest 4 ayuv
.source 1 yy
.source 1 u
.source 1 v
.const 1 c255 255
.temp 2 uv
.temp 2 ay

mergebw uv, u, v
mergebw ay, c255, yy
mergewl ayuv, ay, uv



.function cogorc_convert_AYUV_ARGB
.flags 2d
.dest 4 argb
.source 4 ayuv
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



.function cogorc_convert_AYUV_BGRA
.flags 2d
.dest 4 argb
.source 4 ayuv
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




.function cogorc_convert_AYUV_ABGR
.flags 2d
.dest 4 argb
.source 4 ayuv
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



.function cogorc_convert_AYUV_RGBA
.flags 2d
.dest 4 argb
.source 4 ayuv
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



.function cogorc_convert_I420_BGRA
.dest 4 argb
.source 1 y
.source 1 u
.source 1 v
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



.function cogorc_convert_I420_BGRA_avg
.dest 4 argb
.source 1 y
.source 1 u1
.source 1 u2
.source 1 v1
.source 1 v2
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



