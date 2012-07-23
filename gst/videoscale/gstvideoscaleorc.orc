
.function video_scale_orc_merge_linear_u8
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



.function video_scale_orc_merge_linear_u16
.dest 2 d1
.source 2 s1
.source 2 s2
.param 2 p1
.param 2 p2
.temp 4 t1
.temp 4 t2

# This is slightly different thatn the u8 case, since muluwl
# tends to be much faster than mulll
muluwl t1, s1, p1
muluwl t2, s2, p2
addl t1, t1, t2
shrul t1, t1, 16
convlw d1, t1


.function video_scale_orc_splat_u16
.dest 2 d1
.param 2 p1

copyw d1, p1


.function video_scale_orc_splat_u32
.dest 4 d1
.param 4 p1

copyl d1, p1


.function video_scale_orc_splat_u64
.dest 8 d1
.longparam 8 p1

copyq d1, p1


.function video_scale_orc_downsample_u8
.dest 1 d1 guint8
.source 2 s1 guint8
.temp 1 t1
.temp 1 t2

splitwb t1, t2, s1
avgub d1, t1, t2


.function video_scale_orc_downsample_u16
.dest 2 d1 guint16
.source 4 s1 guint16
.temp 2 t1
.temp 2 t2

splitlw t1, t2, s1
avguw d1, t1, t2


.function video_scale_orc_downsample_u32
.dest 4 d1 guint8
.source 8 s1 guint8
.temp 4 t1
.temp 4 t2

splitql t1, t2, s1
x4 avgub d1, t1, t2


.function video_scale_orc_downsample_yuyv
.dest 4 d1 guint8
.source 8 s1 guint8
.temp 4 yyyy
.temp 4 uvuv
.temp 2 t1
.temp 2 t2
.temp 2 yy
.temp 2 uv

x4 splitwb yyyy, uvuv, s1
x2 splitwb t1, t2, yyyy
x2 avgub yy, t1, t2
splitlw t1, t2, uvuv
x2 avgub uv, t1, t2
x2 mergebw d1, yy, uv



.function video_scale_orc_resample_nearest_u8
.dest 1 d1 guint8
.source 1 s1 guint8
.param 4 p1
.param 4 p2

ldresnearb d1, s1, p1, p2


.function video_scale_orc_resample_bilinear_u8
.dest 1 d1 guint8
.source 1 s1 guint8
.param 4 p1
.param 4 p2

ldreslinb d1, s1, p1, p2


.function video_scale_orc_resample_nearest_u32
.dest 4 d1 guint8
.source 4 s1 guint8
.param 4 p1
.param 4 p2

ldresnearl d1, s1, p1, p2


.function video_scale_orc_resample_bilinear_u32
.dest 4 d1 guint8
.source 4 s1 guint8
.param 4 p1
.param 4 p2

ldreslinl d1, s1, p1, p2


.function video_scale_orc_resample_merge_bilinear_u32
.dest 4 d1 guint8
.dest 4 d2 guint8
.source 4 s1 guint8
.source 4 s2 guint8
.temp 4 a
.temp 4 b
.temp 4 t
.temp 8 t1
.temp 8 t2
.param 4 p1
.param 4 p2
.param 4 p3

ldreslinl b, s2, p2, p3
storel d2, b
loadl a, s1
x4 convubw t1, a
x4 convubw t2, b
x4 subw t2, t2, t1
x4 mullw t2, t2, p1
x4 convhwb t, t2
x4 addb d1, t, a



.function video_scale_orc_merge_bicubic_u8
.dest 1 d1 guint8
.source 1 s1 guint8
.source 1 s2 guint8
.source 1 s3 guint8
.source 1 s4 guint8
.param 4 p1
.param 4 p2
.param 4 p3
.param 4 p4
.temp 2 t1
.temp 2 t2

mulubw t1, s2, p2
mulubw t2, s3, p3
addw t1, t1, t2
mulubw t2, s1, p1
subw t1, t1, t2
mulubw t2, s4, p4
subw t1, t1, t2
addw t1, t1, 32
shrsw t1, t1, 6
convsuswb d1, t1


