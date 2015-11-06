.function audio_convert_orc_s32_to_double
.dest 8 d1 gdouble
.source 4 s1 gint32
.temp 8 t1

convld t1, s1
divd d1, t1, 2147483648.0L

.function audio_convert_orc_double_to_s32
.dest 4 d1 gint32
.source 8 s1 gdouble
.temp 8 t1

muld t1, s1, 2147483648.0L
convdl d1, t1
