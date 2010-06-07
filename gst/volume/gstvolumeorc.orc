
#.function orc_scalarmultiply_f32_ns
#.dest 4 d1 float
#.source 4 s1 float
#.param 4 p1

#mulf d1, s1, p1


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


