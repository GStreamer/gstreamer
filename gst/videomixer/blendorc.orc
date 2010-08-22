.function orc_splat_u32
.dest 4 d1 guint32
.param 4 p1 guint32

copyl d1, p1

.function orc_memcpy_u32
.dest 4 d1 guint32
.source 4 s1 guint32

copyl d1, s1

.function orc_blend_u8
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


.function gst_videomixer_orc_blend_ayuv
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
.const 4 c_alpha 0xffffff00


loadl t, s
convlw tw, t
convwb tb, tw
splatbl a, tb
x4 convubw a_wide, a
x4 mullw a_wide, a_wide, alpha
x4 shruw a_wide, a_wide, 8
andl t, t, c_alpha
x4 convubw s_wide, t
andl t, d, c_alpha
x4 convubw d_wide, t
x4 subw s_wide, s_wide, d_wide
x4 mullw s_wide, s_wide, a_wide
x4 div255w s_wide, s_wide
x4 addw d_wide, d_wide, s_wide
x4 convwb d, d_wide



