plugin_LTLIBRARIES = libgstvideoparsersbad.la

libgstvideoparsersbad_la_SOURCES = plugin.c \
	h263parse.c gsth263parse.c \
	gstdiracparse.c dirac_parse.c \
	gsth264parse.c gstmpegvideoparse.c \
	gstmpeg4videoparse.c \
	gstjpeg2000parse.c \
	gstpngparse.c \
	gstvc1parse.c \
	gsth265parse.c

libgstvideoparsersbad_la_CFLAGS = \
	$(GST_PLUGINS_BAD_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS) \
	-DGST_USE_UNSTABLE_API \
	$(GST_BASE_CFLAGS) $(GST_CFLAGS)\
	-Dschro_video_format_set_std_video_format=gst_videoparsers_schro_video_format_set_std_video_format\
	-Dschro_video_format_set_std_frame_rate=gst_videoparsers_schro_video_format_set_std_frame_rate\
	-Dschro_video_format_set_std_aspect_ratio=gst_videoparsers_schro_video_format_set_std_aspect_ratio\
	-Dschro_video_format_set_std_signal_range=gst_videoparsers_schro_video_format_set_std_signal_range\
	-Dschro_video_format_set_std_colour_spec=gst_videoparsers_schro_video_format_set_std_colour_spec
libgstvideoparsersbad_la_LIBADD = \
	$(top_builddir)/gst-libs/gst/codecparsers/libgstcodecparsers-$(GST_API_VERSION).la \
	$(GST_PLUGINS_BASE_LIBS) -lgstpbutils-$(GST_API_VERSION) -lgstvideo-$(GST_API_VERSION) \
	$(GST_BASE_LIBS) $(GST_LIBS)
libgstvideoparsersbad_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstvideoparsersbad_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

noinst_HEADERS = gsth263parse.h h263parse.h \
	gstdiracparse.h dirac_parse.h \
	gsth264parse.h gstmpegvideoparse.h \
	gstmpeg4videoparse.h \
	gstjpeg2000parse.h \
	gstpngparse.h \
	gstvc1parse.h \
	gsth265parse.h
