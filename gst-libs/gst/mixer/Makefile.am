# variables used for enum/marshal generation
glib_enum_headers=$(mixer_headers)
glib_enum_define=GST_MIXER
glib_root=mixer
glib_enum_prefix=gst_mixer


libgstmixerincludedir = \
	$(includedir)/gstreamer-@GST_MAJORMINOR@/gst/mixer

mixer_headers = \
	mixer.h \
	mixertrack.h

built_headers =	\
	$(glib_root)-marshal.h \
	$(glib_root)-enumtypes.h

libgstmixerinclude_HEADERS = $(mixer_headers) $(built_headers)

noinst_LTLIBRARIES = libgstmixer.la

libgstmixer_la_SOURCES = \
	mixer.c \
	mixertrack.c \
	$(glib_root)-marshal.c \
	$(glib_root)-enumtypes.c

libgstmixer_la_CFLAGS = $(GST_CFLAGS)

BUILT_SOURCES = \
	$(glib_root)-marshal.c \
	$(glib_root)-enumtypes.c \
        $(built_headers)

EXTRA_DIST = mixer-marshal.list

CLEANFILES = $(BUILT_SOURCES)

include $(top_srcdir)/common/glib-gen.mak
