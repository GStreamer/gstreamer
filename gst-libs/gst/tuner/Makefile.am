# variables used for enum/marshal generation
glib_enum_headers=$(tuner_headers)
glib_enum_define=GST_TUNER
glib_enum_prefix=gst_tuner

libgsttunerincludedir = \
	$(includedir)/gstreamer-@GST_MAJORMINOR@/gst/tuner

tuner_headers = \
	tuner.h \
	tunernorm.h \
	tunerchannel.h

built_sources =	\
	tuner-marshal.c \
	tuner-enumtypes.c

built_headers =	\
	tuner-marshal.h \
	tuner-enumtypes.h

libgsttunerinclude_HEADERS = $(tuner_headers) $(built_headers)

noinst_LTLIBRARIES = libgsttuner.la

libgsttuner_la_SOURCES = \
	tuner.c \
	tunernorm.c \
	tunerchannel.c \
	$(built_sources)

libgsttuner_la_CFLAGS = $(GST_CFLAGS)

BUILT_SOURCES = \
	$(built_sources) \
	$(built_headers)

EXTRA_DIST = tuner-marshal.list

CLEANFILES = $(BUILT_SOURCES)

include $(top_srcdir)/common/glib-gen.mak
