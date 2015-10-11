plugin_LTLIBRARIES = libgstgoom2k1.la

GOOM_FILTER_FILES = filters.c
GOOM_FILTER_CFLAGS = -UMMX -UUSE_ASM

noinst_HEADERS = gstgoom.h filters.h goom_core.h goom_tools.h graphic.h lines.h

libgstgoom2k1_la_SOURCES = gstgoom.c goom_core.c $(GOOM_FILTER_FILES) graphic.c \
         lines.c

libgstgoom2k1_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS) $(GOOM_FILTER_CFLAGS) \
	-Dgst_goom_get_type=gst_goom2k1_get_type		\
	-Dgoom_init=goom2k1_init				\
	-Dgoom_close=goom2k1_close -Dgoom_update=goom2k1_update	\
	-Dgoom_set_resolution=goom2k1_set_resolution		\
	-Dgoom_lines=goom2k1_lines				\
	-DBLACK=GOOM2K1_BLACK -DWHITE=GOOM2K1_WHITE		\
	-DRED=GOOM2K1_RED -DBLUE=GOOM2K1_BLUE			\
	-DGREEN=GOOM2K1_GREEN -DYELLOW=GOOM2K1_YELLOW		\
	-DORANGE=GOOM2K1_ORANGE -DVIOLET=GOOM2K1_VIOLET		\
	-DzoomFilterFastRGB=zoomFilterFastRGB2k1		\
	-DpointFilter=pointFilter2k1				\
	-DzoomFilterDestroy=zoomFilterDestroy2k1		\
	-DzoomFilterNew=zoomFilterNew2k1

libgstgoom2k1_la_LIBADD = $(GST_PLUGINS_BASE_LIBS) -lgstpbutils-$(GST_API_VERSION) $(GST_BASE_LIBS) $(GST_LIBS) $(LIBM)
libgstgoom2k1_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstgoom2k1_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

EXTRA_DIST = filters.c
