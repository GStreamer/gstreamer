plugin_LTLIBRARIES = libgstamrwbdec.la

libgstamrwbdec_la_SOURCES = \
	amrwb.c \
	amrwbdec.c

libgstamrwbdec_la_CFLAGS = $(GST_BASE_CFLAGS) $(GST_CFLAGS) $(AMRWB_CFLAGS)
libgstamrwbdec_la_LIBADD = $(GST_BASE_LIBS) $(GST_LIBS) $(AMRWB_LIBS)
libgstamrwbdec_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstamrwbdec_la_LIBTOOLFLAGS = --tag=disable-static

noinst_HEADERS = \
	amrwbdec.h
