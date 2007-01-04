plugin_LTLIBRARIES = libgstamrwb.la

libgstamrwb_la_SOURCES = 	\
	gstamrwb.c		\
	gstamrwbdec.c		\
	gstamrwbenc.c		\
	gstamrwbparse.c

libgstamrwb_la_CFLAGS = $(GST_CFLAGS) -I$(top_builddir)/gst-libs/ext/amrwb
libgstamrwb_la_LIBADD = $(GST_BASE_LIBS) $(AMRWB_LIBS) $(top_builddir)/gst-libs/ext/amrwb/libamrwb.la
libgstamrwb_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS =	\
	gstamrwbdec.h	\
	gstamrwbenc.h	\
	gstamrwbparse.h

