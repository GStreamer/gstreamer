# variables used for enum/marshal generation
glib_enum_headers=$(mixer_headers)
glib_enum_define=GST_MIXER
glib_enum_prefix=gst_mixer


libgstmixerincludedir = \
	$(includedir)/gstreamer-@GST_MAJORMINOR@/gst/mixer

mixer_headers = \
	mixer.h \
	mixeroptions.h \
	mixertrack.h

built_sources = \
	mixer-marshal.c \
	mixer-enumtypes.c

built_headers =	\
	mixer-marshal.h \
	mixer-enumtypes.h

libgstmixerinclude_HEADERS = \
	$(mixer_headers)

nodist_libgstmixerinclude_HEADERS = \
	mixer-enumtypes.h

noinst_LTLIBRARIES = libgstmixer.la

libgstmixer_la_SOURCES = \
	mixer.c \
	mixeroptions.c \
	mixertrack.c

nodist_libgstmixer_la_SOURCES = \
	$(built_sources)

libgstmixer_la_CFLAGS = $(GST_CFLAGS)

BUILT_SOURCES = \
	$(built_sources) \
        $(built_headers)

EXTRA_DIST = mixer-marshal.list

CLEANFILES = $(BUILT_SOURCES)

include $(top_srcdir)/common/glib-gen.mak
