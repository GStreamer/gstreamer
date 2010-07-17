
.function orc_merge_linear_u8
.dest 1 d1
.source 1 s1
.source 1 s2
.param 1 p1
.param 1 p2
.temp 2 t1
.temp 2 t2

mulubw t1, s1, p1
mulubw t2, s2, p2
addw t1, t1, t2
addw t1, t1, 128
shruw t1, t1, 8
convwb d1, t1

.function orc_merge_linear_u16
.dest 2 d1
.source 2 s1
.source 2 s2
.param 2 p1
.param 2 p2
.temp 4 t1
.temp 4 t2

muluwl t1, s1, p1
muluwl t2, s2, p2
addl t1, t1, t2
shrul t1, t1, 16
convlw d1, t1

.function orc_splat_u16
.dest 2 d1
.param 2 p1

copyw d1, p1

.function orc_splat_u32
.dest 4 d1
.param 4 p1

copyl d1, p1

