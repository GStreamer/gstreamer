
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


