# variables used for enum/marshal generation
glib_enum_headers=$(tuner_headers)
glib_enum_define=GST_TUNER
glib_root=tuner
glib_enum_prefix=gst_tuner

libgsttunerincludedir = \
	$(includedir)/gstreamer-@GST_MAJORMINOR@/gst/tuner

tuner_headers = \
	tuner.h \
	tunernorm.h \
	tunerchannel.h

built_headers =	\
	$(glib_root)-marshal.h \
	$(glib_root)-enumtypes.h

libgsttunerinclude_HEADERS = $(tuner_headers) $(built_headers)

noinst_LTLIBRARIES = libgsttuner.la

libgsttuner_la_SOURCES = \
	tuner.c \
	tunernorm.c \
	tunerchannel.c \
	$(glib_root)-marshal.c

libgsttuner_la_CFLAGS = $(GST_CFLAGS)

BUILT_SOURCES = \
	$(glib_root)-marshal.c \
	$(glib_root)-enumtypes.c \
	$(built_headers)

EXTRA_DIST = $(glib_root)-marshal.list

CLEANFILES = $(BUILT_SOURCES)

include $(top_srcdir)/common/glib-gen.mak
