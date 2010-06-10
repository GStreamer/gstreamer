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

