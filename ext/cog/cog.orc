
.function cogorc_downsample_horiz_cosite_1tap
.dest 1 d1
.source 2 s1

select0wb d1, s1


.function cogorc_downsample_horiz_cosite_3tap
.dest 1 d1
.source 2 s1
.source 2 s2
.temp 1 t1
.temp 1 t2
.temp 1 t3
.temp 2 t4
.temp 2 t5
.temp 2 t6

copyw t4, s1
select0wb t1, t4
select1wb t2, t4
select0wb t3, s2
convubw t4, t1
convubw t5, t2
convubw t6, t3
mullw t5, t5, 2
addw t4, t4, t6
addw t4, t4, t5
addw t4, t4, 2
shrsw t4, t4, 2
convsuswb d1, t4


.function cogorc_downsample_vert_halfsite_2tap
.dest 1 d1
.source 1 s1
.source 1 s2

avgub d1, s1, s2


.function cogorc_downsample_vert_cosite_3tap
.dest 1 d1
.source 1 s1
.source 1 s2
.source 1 s3
.temp 2 t1
.temp 2 t2
.temp 2 t3

convubw t1, s1
convubw t2, s2
convubw t3, s3
mullw t2, t2, 2
addw t1, t1, t3
addw t1, t1, t2
addw t1, t1, 2
shrsw t1, t1, 2
convsuswb d1, t1



.function cogorc_downsample_vert_halfsite_4tap
.dest 1 d1
.source 1 s1
.source 1 s2
.source 1 s3
.source 1 s4
.temp 2 t1
.temp 2 t2
.temp 2 t3
.temp 2 t4

convubw t1, s1
convubw t2, s2
convubw t3, s3
convubw t4, s4
addw t2, t2, t3
mullw t2, t2, 26
addw t1, t1, t4
mullw t1, t1, 6
addw t2, t2, t1
addw t2, t2, 32
shrsw t2, t2, 6
convsuswb d1, t2


.function cogorc_upsample_horiz_cosite_1tap
.dest 2 d1 uint8_t
.source 1 s1
.temp 1 t1

copyb t1, s1
mergebw d1, t1, t1


.function cogorc_upsample_horiz_cosite
.dest 2 d1 uint8_t
.source 1 s1
.source 1 s2
.temp 1 t1
.temp 1 t2

copyb t1, s1
avgub t2, t1, s2
mergebw d1, t1, t2


.function cogorc_upsample_vert_avgub
.dest 1 d1
.source 1 s1
.source 1 s2

avgub d1, s1, s2




.function orc_unpack_yuyv_y
.dest 1 d1
.source 2 s1

select0wb d1, s1


.function orc_unpack_yuyv_u
.dest 1 d1
.source 4 s1
.temp 2 t1

select0lw t1, s1
select1wb d1, t1


.function orc_unpack_yuyv_v
.dest 1 d1
.source 4 s1
.temp 2 t1

select1lw t1, s1
select1wb d1, t1


.function orc_pack_yuyv
.dest 4 d1
.source 2 s1 uint8_t
.source 1 s2
.source 1 s3
.temp 1 t1
.temp 1 t2
.temp 2 t3
.temp 2 t4
.temp 2 t5

copyw t5, s1
select0wb t1, t5
select1wb t2, t5
mergebw t3, t1, s2
mergebw t4, t2, s3
mergewl d1, t3, t4


.function orc_unpack_uyvy_y
.dest 1 d1
.source 2 s1

select1wb d1, s1


.function orc_unpack_uyvy_u
.dest 1 d1
.source 4 s1
.temp 2 t1

select0lw t1, s1
select0wb d1, t1


.function orc_unpack_uyvy_v
.dest 1 d1
.source 4 s1
.temp 2 t1

select1lw t1, s1
select0wb d1, t1


.function orc_pack_uyvy
.dest 4 d1
.source 2 s1 uint8_t
.source 1 s2
.source 1 s3
.temp 1 t1
.temp 1 t2
.temp 2 t3
.temp 2 t4
.temp 2 t5

copyw t5, s1
select0wb t1, t5
select1wb t2, t5
mergebw t3, s2, t1
mergebw t4, s3, t2
mergewl d1, t3, t4


.function orc_memcpy
.dest 1 d1 void
.source 1 s1 void

copyb d1, s1


.function orc_addc_convert_u8_s16
.dest 1 d1
.source 2 s1 int16_t
.temp 2 t1

addw t1, s1, 128
convsuswb d1, t1


.function orc_subc_convert_s16_u8
.dest 2 d1 int16_t
.source 1 s1
.temp 2 t1

convubw t1, s1
subw d1, t1, 128


.function orc_splat_u8_ns
.dest 1 d1
.param 1 p1

copyb d1, p1


.function orc_splat_s16_ns
.dest 2 d1 int16_t
.param 2 p1

copyw d1, p1


.function orc_matrix2_u8
.dest 1 d1 uint8_t
.source 1 s1 uint8_t
.source 1 s2 uint8_t
.param 2 p1
.param 2 p2
.param 2 p3
.temp 2 t1
.temp 2 t2

convubw t1, s1
mullw t1, t1, p1
convubw t2, s2
mullw t2, t2, p2
addw t1, t1, t2
addw t1, t1, p3
shrsw t1, t1, 6
convsuswb d1, t1


.function orc_matrix2_2_u8
.dest 1 d1 uint8_t
.source 1 s1 uint8_t
.source 1 s2 uint8_t
.param 2 p1
.param 2 p2
.param 2 p3
.param 2 p4
.temp 2 t1
.temp 2 t2

convubw t1, s1
subw t1, t1, 16
mullw t1, t1, p1
convubw t2, s2
subw t2, t2, 128
mullw t2, t2, p2
addw t1, t1, t2
addw t1, t1, p3
shrsw t1, t1, p4
convsuswb d1, t1


.function orc_matrix3_u8
.dest 1 d1 uint8_t
.source 1 s1 uint8_t
.source 1 s2 uint8_t
.source 1 s3 uint8_t
.param 2 p1
.param 2 p2
.param 2 p3
.param 2 p4
.temp 2 t1
.temp 2 t2

convubw t1, s1
mullw t1, t1, p1
convubw t2, s2
mullw t2, t2, p2
addw t1, t1, t2
convubw t2, s3
mullw t2, t2, p3
addw t1, t1, t2
addw t1, t1, p4
shrsw t1, t1, 6
convsuswb d1, t1


.function orc_matrix3_2_u8
.dest 1 d1 uint8_t
.source 1 s1 uint8_t
.source 1 s2 uint8_t
.source 1 s3 uint8_t
.param 2 p1
.param 2 p2
.param 2 p3
.param 2 p4
.param 2 p5
.temp 2 t1
.temp 2 t2

convubw t1, s1
subw t1, t1, 16
mullw t1, t1, p1
convubw t2, s2
subw t2, t2, 128
mullw t2, t2, p2
addw t1, t1, t2
convubw t2, s3
subw t2, t2, 128
mullw t2, t2, p3
addw t1, t1, t2
addw t1, t1, p4
shrsw t1, t1, p5
convsuswb d1, t1



.function orc_pack_123x
.dest 4 d1 uint32_t
.source 1 s1
.source 1 s2
.source 1 s3
.param 1 p1
.temp 2 t1
.temp 2 t2

mergebw t1, s1, s2
mergebw t2, s3, p1
mergewl d1, t1, t2


.function orc_pack_x123
.dest 4 d1 uint32_t
.source 1 s1
.source 1 s2
.source 1 s3
.param 1 p1
.temp 2 t1
.temp 2 t2

mergebw t1, p1, s1
mergebw t2, s2, s3
mergewl d1, t1, t2


.function cogorc_combine2_u8
.dest 1 d1
.source 1 s1
.source 1 s2
.param 2 p1
.param 2 p2
.temp 2 t1
.temp 2 t2

convubw t1, s1
mullw t1, t1, p1
convubw t2, s2
mullw t2, t2, p2
addw t1, t1, t2
shruw t1, t1, 8
convuuswb d1, t1


.function cogorc_combine4_u8
.dest 1 d1
.source 1 s1
.source 1 s2
.source 1 s3
.source 1 s4
.param 2 p1
.param 2 p2
.param 2 p3
.param 2 p4
.temp 2 t1
.temp 2 t2

convubw t1, s1
mullw t1, t1, p1
convubw t2, s2
mullw t2, t2, p2
addw t1, t1, t2
convubw t2, s3
mullw t2, t2, p3
addw t1, t1, t2
convubw t2, s4
mullw t2, t2, p4
addw t1, t1, t2
addw t1, t1, 32
shrsw t1, t1, 6
convsuswb d1, t1


.function cogorc_unpack_ayuv_y
.dest 1 d1
.source 4 s1
.temp 2 t1

select0lw t1, d1
select1wb d1, s1


.function cogorc_unpack_ayuv_u
.dest 1 d1
.source 4 s1
.temp 2 t1

select1lw t1, d1
select0wb d1, s1


.function cogorc_unpack_ayuv_v
.dest 1 d1
.source 4 s1
.temp 2 t1

select1lw t1, d1
select1wb d1, s1


