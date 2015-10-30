.function audio_convert_orc_s32_to_double
.dest 8 d1 gdouble
.source 4 s1 gint32
.temp 8 t1

convld t1, s1
divd d1, t1, 0x41DFFFFFFFC00000L

.function audio_convert_orc_double_to_s32
.dest 4 d1 gint32
.source 8 s1 gdouble
.temp 8 t1

muld t1, s1, 0x41DFFFFFFFC00000L
convdl d1, t1

.function audio_convert_orc_int_bias
.dest 4 d1 gint32
.source 4 s1 gint32
.param 4 bias gint32
.param 4 mask gint32
.temp 4 t1

addssl t1, s1, bias
andl d1, t1, mask

.function audio_convert_orc_int_dither
.dest 4 d1 gint32
.source 4 s1 gint32
.source 4 dither gint32
.param 4 mask gint32
.temp 4 t1

addssl t1, s1, dither
andl d1, t1, mask

