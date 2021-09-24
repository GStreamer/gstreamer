

.function bayer_orc_horiz_upsample_unaligned
.dest 2 d0 guint8
.dest 2 d1 guint8
.source 2 s guint8
.temp 2 t
.temp 1 b
.temp 1 c
.temp 1 d
.temp 1 e

splitwb c, b, s
loadoffw t, s, 1
splitwb e, d, t
avgub e, c, e
mergebw d0, c, e
avgub b, b, d
mergebw d1, b, d


.function bayer_orc_horiz_upsample
.dest 2 d0 guint8
.dest 2 d1 guint8
.source 2 s guint8
.temp 2 t
.temp 1 b
.temp 1 c
.temp 1 d
.temp 1 e

loadoffw t, s, -1
select1wb b, t
splitwb d, c, s
loadoffw t, s, 1
select0wb e, t
avgub e, c, e
mergebw d0, c, e
avgub b, b, d
mergebw d1, b, d


.function bayer_orc_merge_bg_bgra
.dest 8 d guint8
.source 2 g0 guint8
.source 2 r0 guint8
.source 2 b1 guint8
.source 2 g1 guint8
.source 2 g2 guint8
.source 2 r2 guint8
.temp 4 ra
.temp 4 bg
.temp 2 r
.temp 2 g
.temp 2 t

x2 avgub r, r0, r2
x2 avgub g, g0, g2
copyw t, g1
x2 avgub g, g, t
andw g, g, 255
andw t, t, 65280
orw g, t, g
x2 mergebw bg, b1, g
x2 mergebw ra, r, 255
x2 mergewl d, bg, ra


.function bayer_orc_merge_gr_bgra
.dest 8 d guint8
.source 2 b0 guint8
.source 2 g0 guint8
.source 2 g1 guint8
.source 2 r1 guint8
.source 2 b2 guint8
.source 2 g2 guint8
.temp 4 ra
.temp 4 bg
.temp 2 b
.temp 2 g
.temp 2 t

x2 avgub b, b0, b2
x2 avgub g, g0, g2
copyw t, g1
x2 avgub g, g, t
andw g, g, 65280
andw t, t, 255
orw g, t, g
x2 mergebw bg, b, g
x2 mergebw ra, r1, 255
x2 mergewl d, bg, ra


.function bayer_orc_merge_bg_abgr
.dest 8 d guint8
.source 2 g0 guint8
.source 2 r0 guint8
.source 2 b1 guint8
.source 2 g1 guint8
.source 2 g2 guint8
.source 2 r2 guint8
.temp 4 ab
.temp 4 gr
.temp 2 r
.temp 2 g
.temp 2 t

x2 avgub r, r0, r2
x2 avgub g, g0, g2
copyw t, g1
x2 avgub g, g, t
andw g, g, 255
andw t, t, 65280
orw g, t, g
x2 mergebw ab, 255, b1
x2 mergebw gr, g, r
x2 mergewl d, ab, gr


.function bayer_orc_merge_gr_abgr
.dest 8 d guint8
.source 2 b0 guint8
.source 2 g0 guint8
.source 2 g1 guint8
.source 2 r1 guint8
.source 2 b2 guint8
.source 2 g2 guint8
.temp 4 ab
.temp 4 gr
.temp 2 b
.temp 2 g
.temp 2 t

x2 avgub b, b0, b2
x2 avgub g, g0, g2
copyw t, g1
x2 avgub g, g, t
andw g, g, 65280
andw t, t, 255
orw g, t, g
x2 mergebw ab, 255, b
x2 mergebw gr, g, r1
x2 mergewl d, ab, gr


.function bayer_orc_merge_bg_rgba
.dest 8 d guint8
.source 2 g0 guint8
.source 2 r0 guint8
.source 2 b1 guint8
.source 2 g1 guint8
.source 2 g2 guint8
.source 2 r2 guint8
.temp 4 rg
.temp 4 ba
.temp 2 r
.temp 2 g
.temp 2 t

x2 avgub r, r0, r2
x2 avgub g, g0, g2
copyw t, g1
x2 avgub g, g, t
andw g, g, 255
andw t, t, 65280
orw g, t, g
x2 mergebw rg, r, g
x2 mergebw ba, b1, 255
x2 mergewl d, rg, ba


.function bayer_orc_merge_gr_rgba
.dest 8 d guint8
.source 2 b0 guint8
.source 2 g0 guint8
.source 2 g1 guint8
.source 2 r1 guint8
.source 2 b2 guint8
.source 2 g2 guint8
.temp 4 rg
.temp 4 ba
.temp 2 b
.temp 2 g
.temp 2 t

x2 avgub b, b0, b2
x2 avgub g, g0, g2
copyw t, g1
x2 avgub g, g, t
andw g, g, 65280
andw t, t, 255
orw g, t, g
x2 mergebw rg, r1, g
x2 mergebw ba, b, 255
x2 mergewl d, rg, ba


.function bayer_orc_merge_bg_argb
.dest 8 d guint8
.source 2 g0 guint8
.source 2 r0 guint8
.source 2 b1 guint8
.source 2 g1 guint8
.source 2 g2 guint8
.source 2 r2 guint8
.temp 4 ar
.temp 4 gb
.temp 2 r
.temp 2 g
.temp 2 t

x2 avgub r, r0, r2
x2 avgub g, g0, g2
copyw t, g1
x2 avgub g, g, t
andw g, g, 255
andw t, t, 65280
orw g, t, g
x2 mergebw ar, 255, r
x2 mergebw gb, g, b1
x2 mergewl d, ar, gb


.function bayer_orc_merge_gr_argb
.dest 8 d guint8
.source 2 b0 guint8
.source 2 g0 guint8
.source 2 g1 guint8
.source 2 r1 guint8
.source 2 b2 guint8
.source 2 g2 guint8
.temp 4 ar
.temp 4 gb
.temp 2 b
.temp 2 g
.temp 2 t

x2 avgub b, b0, b2
x2 avgub g, g0, g2
copyw t, g1
x2 avgub g, g, t
andw g, g, 65280
andw t, t, 255
orw g, t, g
x2 mergebw ar, 255, r1
x2 mergebw gb, g, b
x2 mergewl d, ar, gb


