
plugin_LTLIBRARIES = libgstlibfame.la

libgstlibfame_la_SOURCES = gstlibfame.c
libgstlibfame_la_CFLAGS = $(GST_CFLAGS) $(LIBFAME_CFLAGS)
libgstlibfame_la_LIBADD = $(LIBFAME_LIBS) 
libgstlibfame_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstlibfame_la_LIBTOOLFLAGS = --tag=disable-static

noinst_HEADERS=gstlibfame.h
