plugin_LTLIBRARIES = libgstrtpmanagerbad.la

libgstrtpmanagerbad_la_SOURCES = \
	gstrtp-utils.c \
	gstrtpsink.c \
	gstrtpsrc.c \
	plugin.c

libgstrtpmanagerbad_la_CFLAGS = $(GST_BASE_CFLAGS) $(GST_CFLAGS)
libgstrtpmanagerbad_la_LIBADD = $(GST_BASE_LIBS) $(GST_LIBS)
libgstrtpmanagerbad_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = \
	gstrtp-utils.h \
	gstrtpsink.h \
	gstrtpsrc.h
