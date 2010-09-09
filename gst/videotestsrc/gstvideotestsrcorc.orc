
.init gst_videotestsrc_orc_init

.function gst_orc_splat_u8
.dest 1 d1 guint8
.param 1 p1

copyb d1, p1


.function gst_orc_splat_s16
.dest 2 d1 gint8
.param 2 p1

copyw d1, p1


.function gst_orc_splat_u16
.dest 2 d1 guint8
.param 2 p1

copyw d1, p1


.function gst_orc_splat_u32
.dest 4 d1 guint8
.param 4 p1

copyl d1, p1


