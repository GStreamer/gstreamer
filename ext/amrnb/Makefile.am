plugin_LTLIBRARIES = libgstamrnb.la

libgstamrnb_la_SOURCES = \
	amrnb.c \
	amrnbdec.c \
	amrnbparse.c

libgstamrnb_la_CFLAGS = $(GST_CFLAGS) $(AMRNB_CFLAGS)
libgstamrnb_la_LIBADD = $(AMRNB_LIBS)
libgstamrnb_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = \
	amrnbdec.h \
	amrnbparse.h
