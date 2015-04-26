plugin_LTLIBRARIES = libgsttta.la

libgsttta_la_SOURCES = gsttta.c \
	 gstttaparse.c \
	 gstttadec.c

noinst_HEADERS = gstttaparse.h gstttadec.h \
	 ttadec.h crc32.h filters.h

libgsttta_la_CFLAGS = $(GST_CFLAGS)
libgsttta_la_LIBADD = $(GST_LIBS) $(LIBM)
libgsttta_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgsttta_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)
