plugin_LTLIBRARIES = libgstogg.la

libgstogg_la_SOURCES = \
	gstogg.c \
	gstoggdemux.c \
	gstoggmux.c \
	gstogmparse.c \
	gstoggaviparse.c \
	gstoggparse.c \
	gstoggstream.c \
	dirac_parse.c \
	vorbis_parse.c

noinst_HEADERS = \
	gstogg.h \
	gstoggdemux.h \
	gstoggmux.h \
	gstoggstream.h \
	dirac_parse.h \
	vorbis_parse.h

libgstogg_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS) $(OGG_CFLAGS) \
	-Dschro_video_format_set_std_video_format=ogg_schro_video_format_set_std_video_format \
	-Dschro_video_format_set_std_frame_rate=ogg_schro_video_format_set_std_frame_rate \
	-Dschro_video_format_set_std_aspect_ratio=ogg_schro_video_format_set_std_aspect_ratio \
	-Dschro_video_format_set_std_signal_range=ogg_schro_video_format_set_std_signal_range \
	-Dschro_video_format_set_std_colour_spec=ogg_schro_video_format_set_std_colour_spec

libgstogg_la_LIBADD = \
	$(top_builddir)/gst-libs/gst/riff/libgstriff-$(GST_API_VERSION).la \
	$(top_builddir)/gst-libs/gst/tag/libgsttag-$(GST_API_VERSION).la \
	$(top_builddir)/gst-libs/gst/pbutils/libgstpbutils-$(GST_API_VERSION).la \
	$(top_builddir)/gst-libs/gst/audio/libgstaudio-$(GST_API_VERSION).la \
	$(GST_BASE_LIBS) \
	$(GST_LIBS) \
	$(OGG_LIBS)
libgstogg_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstogg_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)
