plugindir = $(libdir)/gst

plugin_LTLIBRARIES = libgstsnapshot.la

libgstsnapshot_la_SOURCES = gstsnapshot.c ../hermes/yuv2rgb.c  ../hermes/yuv2yuv.c
libgstsnapshot_la_CFLAGS = $(GST_CFLAGS) 
libgstsnapshot_la_LIBADD = $(GST_LIBS) $(LIBPNG_LIBS)
libgstsnapshot_la_LDFLAGS = @GST_PLUGIN_LDFLAGS@

noinst_HEADERS = gstsnapshot.h
