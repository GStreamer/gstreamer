
.function deinterlace_line_vfir
.dest 1 d1 guint8
.source 1 s1 guint8
.source 1 s2 guint8
.source 1 s3 guint8
.source 1 s4 guint8
.source 1 s5 guint8
.temp 2 t1
.temp 2 t2
.temp 2 t3

convubw t1, s1
convubw t2, s5
addw t1, t1, t2
convubw t2, s2
convubw t3, s4
addw t2, t2, t3
shlw t2, t2, 2
convubw t3, s3
shlw t3, t3, 1
subw t2, t2, t1
addw t2, t2, t3
addw t2, t2, 4
shrsw t2, t2, 3
convsuswb d1, t2


.function deinterlace_line_linear
.dest 1 d1 guint8
.source 1 s1 guint8
.source 1 s2 guint8

avgub d1, s1, s2


.function deinterlace_line_linear_blend
.dest 1 d1 guint8
.source 1 s1 guint8
.source 1 s2 guint8
.source 1 s3 guint8
.temp 2 t1
.temp 2 t2
.temp 2 t3

convubw t1, s1
convubw t2, s2
convubw t3, s3
addw t1, t1, t2
addw t3, t3, t3
addw t1, t1, t3
addw t1, t1, 2
shrsw t1, t1, 2
convsuswb d1, t1

