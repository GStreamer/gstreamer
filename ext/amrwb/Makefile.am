plugin_LTLIBRARIES = libgstamrwb.la

libgstamrwb_la_SOURCES = 	\
	gstamrwb.c		\
	gstamrwbdec.c		\
	gstamrwbenc.c		\
	gstamrwbparse.c

libgstamrwb_la_CFLAGS = $(GST_CFLAGS) $(AMRWB_CFLAGS)
libgstamrwb_la_LIBADD = $(GST_BASE_LIBS) $(AMRWB_LIBS)
libgstamrwb_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstamrwb_la_LIBTOOLFLAGS = --tag=disable-static

noinst_HEADERS =	\
	gstamrwbdec.h	\
	gstamrwbenc.h	\
	gstamrwbparse.h

