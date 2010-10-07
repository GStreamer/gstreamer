
.init gst_volume_orc_init

.function orc_scalarmultiply_f64_ns
.dest 8 d1 double
.doubleparam 8 p1

muld d1, d1, p1

.function orc_scalarmultiply_f32_ns
.dest 4 d1 float
.floatparam 4 p1

mulf d1, d1, p1

.function orc_process_int32
.dest 4 d1 gint32
.param 4 p1
.temp 8 t1

mulslq t1, d1, p1
shrsq t1, t1, 27
convql d1, t1

.function orc_process_int32_clamp
.dest 4 d1 gint32
.param 4 p1
.temp 8 t1

mulslq t1, d1, p1
shrsq t1, t1, 27
convsssql d1, t1

.function orc_process_int16
.dest 2 d1 gint16
.param 2 p1
.temp 4 t1

mulswl t1, d1, p1
shrsl t1, t1, 13
convlw d1, t1


.function orc_process_int16_clamp
.dest 2 d1 gint16
.param 2 p1
.temp 4 t1

mulswl t1, d1, p1
shrsl t1, t1, 13
convssslw d1, t1

.function orc_process_int8
.dest 1 d1 gint8
.param 1 p1
.temp 2 t1

mulsbw t1, d1, p1
shrsw t1, t1, 5
convwb d1, t1


.function orc_process_int8_clamp
.dest 1 d1 gint8
.param 1 p1
.temp 2 t1

mulsbw t1, d1, p1
shrsw t1, t1, 5
convssswb d1, t1

.function orc_memset_f64
.dest 8 d1 gdouble
.doubleparam 8 p1

copyq d1, p1

.function orc_prepare_volumes
.dest 8 d1 gdouble
.source 4 s1 gboolean
.temp 8 t1

convld t1, s1
subd t1, 0x3FF0000000000000L, t1
muld d1, d1, t1

.function orc_process_controlled_f64_1ch
.dest 8 d1 gdouble
.source 8 s1 gdouble

muld d1, d1, s1

.function orc_process_controlled_f32_1ch
.dest 4 d1 gfloat
.source 8 s1 gdouble
.temp 4 t1

convdf t1, s1
mulf d1, d1, t1

.function orc_process_controlled_f32_2ch
.dest 8 d1 gfloat
.source 8 s1 gdouble
.temp 4 t1
.temp 8 t2

convdf t1, s1
mergelq t2, t1, t1
x2 mulf d1, d1, t2

.function orc_process_controlled_int32_1ch
.dest 4 d1 gint32
.source 8 s1 gdouble
.temp 8 t1
.temp 4 t2

muld t1, s1, 0x41E0000000000000L
convdl t2, t1
mulslq t1, d1, t2
addq t1, t1, 0x0FFFFFFFL
shrsq t1, t1, 31
convql d1, t1

.function orc_process_controlled_int16_1ch
.dest 2 d1 gint16
.source 8 s1 gdouble
.temp 8 t1
.temp 4 t2
.temp 2 t3

muld t1, s1, 0x40E0000000000000L
convdl t2, t1
convssslw t3, t2
mulswl t2, t3, d1
addl t2, t2, 0x0FFF
shrsl t2, t2, 15
convlw d1, t2

.function orc_process_controlled_int16_2ch
.dest 4 d1 gint16
.source 8 s1 gdouble
.temp 8 t1
.temp 4 t2
.temp 2 t3

muld t1, s1, 0x40E0000000000000L
convdl t2, t1
convssslw t3, t2
mergewl t2, t3, t3
x2 mulswl t1, t2, d1
x2 addl t1, t1, 0x0FFF
x2 shrsl t1, t1, 15
x2 convlw d1, t1

.function orc_process_controlled_int8_1ch
.dest 1 d1 gint8
.source 8 s1 gdouble
.temp 8 t1
.temp 4 t2
.temp 2 t3
.temp 1 t4

muld t1, s1, 0x4060000000000000L
convdl t2, t1
convlw t3, t2
convssswb t4, t3
mulsbw t3, t4, d1
addw t3, t3, 0x0F
shrsw t3, t3, 7
convwb d1, t3

.function orc_process_controlled_int8_2ch
.dest 2 d1 gint8
.source 8 s1 gdouble
.temp 8 t1
.temp 4 t2
.temp 2 t3
.temp 1 t4

muld t1, s1, 0x4060000000000000L
convdl t2, t1
convlw t3, t2
convssswb t4, t3
mergebw t3, t4, t4
x2 mulsbw t2, t3, d1
x2 addw t2, t2, 0x0F
x2 shrsw t2, t2, 7
x2 convwb d1, t2

.function orc_process_controlled_int8_4ch
.dest 4 d1 gint8
.source 8 s1 gdouble
.temp 8 t1
.temp 4 t2
.temp 2 t3
.temp 1 t4

muld t1, s1, 0x4060000000000000L
convdl t2, t1
convlw t3, t2
convssswb t4, t3
mergebw t3, t4, t4
mergewl t2, t3, t3
x4 mulsbw t1, t2, d1
x4 addw t1, t1, 0x0F
x4 shrsw t1, t1, 7
x4 convwb d1, t1

