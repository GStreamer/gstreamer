# variables used for enum generation
glib_enum_headers=$(play_headers)
glib_enum_define=GST_PLAY
glib_enum_prefix=gst_play

librarydir = $(libdir)

play_headers = \
	play.h

built_headers = \
	play-enumtypes.h

library_LTLIBRARIES = libgstplay-@GST_MAJORMINOR@.la

libgstplay_@GST_MAJORMINOR@_la_SOURCES = play.c 
nodist_libgstplay_@GST_MAJORMINOR@_la_SOURCES = play-enumtypes.c

libgstplay_@GST_MAJORMINOR@includedir = $(includedir)/gstreamer-@GST_MAJORMINOR@/gst/play
libgstplay_@GST_MAJORMINOR@include_HEADERS = $(play_headers) $(built_headers)

libgstplay_@GST_MAJORMINOR@_la_CFLAGS = $(GST_CFLAGS) $(GST_PLUGINS_CFLAGS)
libgstplay_@GST_MAJORMINOR@_la_LIBADD =	\
	$(GST_LIBS) $(GST_PLUGINS_LIBS) \
	$(GST_CONTROL_LIBS) \
	 $(top_builddir)/gst-libs/gst/libgstinterfaces-$(GST_MAJORMINOR).la
libgstplay_@GST_MAJORMINOR@_la_LDFLAGS = \
		-version-info @GST_PLUGINS_LIBVERSION@

BUILT_SOURCES = \
	play-enumtypes.c \
	$(built_headers)

CLEANFILES = $(BUILT_SOURCES)

include $(top_srcdir)/common/glib-gen.mak
