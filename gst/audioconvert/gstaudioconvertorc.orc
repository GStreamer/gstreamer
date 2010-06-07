
.function orc_audio_convert_unpack_u8
.dest 4 d1 gint32
.source 1 s1 guint8
.param 4 p1
.const 4 c1 0x80000000
.temp 2 t2
.temp 4 t3

convubw t2, s1
convuwl t3, t2
shll t3, t3, p1
xorl d1, t3, c1


.function orc_audio_convert_unpack_s8
.dest 4 d1 gint32
.source 1 s1 guint8
.param 4 p1
.temp 2 t2
.temp 4 t3

convubw t2, s1
convuwl t3, t2
shll d1, t3, p1


.function orc_audio_convert_unpack_u16
.dest 4 d1 gint32
.source 2 s1 guint8
.param 4 p1
.const 4 c1 0x80000000
.temp 4 t2

convuwl t2, s1
shll t2, t2, p1
xorl d1, t2, c1


.function orc_audio_convert_unpack_s16
.dest 4 d1 gint32
.source 2 s1 guint8
.param 4 p1
.temp 4 t2

convuwl t2, s1
shll d1, t2, p1


.function orc_audio_convert_unpack_u16_swap
.dest 4 d1 gint32
.source 2 s1 guint8
.param 4 p1
.const 4 c1 0x80000000
.temp 2 t1
.temp 4 t2

swapw t1, s1
convuwl t2, t1
shll t2, t2, p1
xorl d1, t2, c1


.function orc_audio_convert_unpack_s16_swap
.dest 4 d1 gint32
.source 2 s1 guint8
.param 4 p1
.temp 2 t1
.temp 4 t2

swapw t1, s1
convuwl t2, t1
shll d1, t2, p1


.function orc_audio_convert_unpack_u32
.dest 4 d1 gint32
.source 4 s1 guint8
.param 4 p1
.const 4 c1 0x80000000
.temp 4 t1

shll t1, s1, p1
xorl d1, t1, c1


.function orc_audio_convert_unpack_s32
.dest 4 d1 gint32
.source 4 s1 guint8
.param 4 p1

shll d1, s1, p1


.function orc_audio_convert_unpack_u32_swap
.dest 4 d1 gint32
.source 4 s1 guint8
.param 4 p1
.const 4 c1 0x80000000
.temp 4 t1

swapl t1, s1
shll t1, t1, p1
xorl d1, t1, c1


.function orc_audio_convert_unpack_s32_swap
.dest 4 d1 gint32
.source 4 s1 guint8
.param 4 p1
.temp 4 t1

swapl t1, s1
shll d1, t1, p1



.function orc_audio_convert_pack_u8
.dest 1 d1 guint8
.source 4 s1 gint32
.param 4 p1
.const 4 c1 0x80000000
.temp 4 t1
.temp 2 t2

xorl t1, s1, c1
shrul t1, t1, p1
convlw t2, t1
convwb d1, t2


.function orc_audio_convert_pack_s8
.dest 1 d1 guint8
.source 4 s1 gint32
.param 4 p1
.temp 4 t1
.temp 2 t2

shrsl t1, s1, p1
convlw t2, t1
convwb d1, t2



.function orc_audio_convert_pack_u16
.dest 2 d1 guint8
.source 4 s1 gint32
.param 4 p1
.const 4 c1 0x80000000
.temp 4 t1

xorl t1, s1, c1
shrul t1, t1, p1
convlw d1, t1


.function orc_audio_convert_pack_s16
.dest 2 d1 guint8
.source 4 s1 gint32
.param 4 p1
.temp 4 t1

shrsl t1, s1, p1
convlw d1, t1


.function orc_audio_convert_pack_u16_swap
.dest 2 d1 guint8
.source 4 s1 gint32
.param 4 p1
.const 4 c1 0x80000000
.temp 4 t1
.temp 2 t2

xorl t1, s1, c1
shrul t1, t1, p1
convlw t2, t1
swapw d1, t2


.function orc_audio_convert_pack_s16_swap
.dest 2 d1 guint8
.source 4 s1 gint32
.param 4 p1
.temp 4 t1
.temp 2 t2

shrsl t1, s1, p1
convlw t2, t1
swapw d1, t2



.function orc_audio_convert_pack_u32
.dest 4 d1 guint8
.source 4 s1 gint32
.param 4 p1
.const 4 c1 0x80000000
.temp 4 t1

xorl t1, s1, c1
shrul d1, t1, p1


.function orc_audio_convert_pack_s32
.dest 4 d1 guint8
.source 4 s1 gint32
.param 4 p1

shrsl d1, s1, p1


.function orc_audio_convert_pack_u32_swap
.dest 4 d1 guint8
.source 4 s1 gint32
.param 4 p1
.const 4 c1 0x80000000
.temp 4 t1

xorl t1, s1, c1
shrul t1, t1, p1
swapl d1, t1


.function orc_audio_convert_pack_s32_swap
.dest 4 d1 guint8
.source 4 s1 gint32
.param 4 p1
.temp 4 t1

shrsl t1, s1, p1
swapl d1, t1


