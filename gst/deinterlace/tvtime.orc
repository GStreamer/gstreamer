
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


.function deinterlace_line_greedy
.dest 1 d1
.source 1 m0
.source 1 t1
.source 1 b1
.source 1 m2
.param 1 max_comb
.temp 1 avg
.temp 1 l2_diff
.temp 1 lp2_diff
.temp 1 t2
.temp 1 t3
.temp 1 best
.temp 1 min
.temp 1 max


avgub avg, t1, b1
#absdiffb l2_diff, m0, avg
maxub t2, m0, avg
minub t3, m0, avg
subb l2_diff, t2, t3

#absdiffb lp2_diff, m2, avg
maxub t2, m2, avg
minub t3, m2, avg
subb lp2_diff, t2, t3

#cmpgtub t1, l2_diff, lp2_diff
xorb l2_diff, l2_diff, 0x80
xorb lp2_diff, lp2_diff, 0x80
cmpgtsb t3, l2_diff, lp2_diff

#selectb best, m0, m2, t3
andb t2, m0, t3
andnb t3, m2, t3
orb best, t2, t3

maxub max, t1, b1
minub min, t1, b1
addusb max, max, max_comb
subusb min, min, max_comb
minub best, best, max
maxub d1, best, min



