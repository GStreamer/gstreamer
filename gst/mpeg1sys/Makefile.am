plugindir = $(libdir)/gstreamer-@GST_MAJORMINOR@

plugin_LTLIBRARIES = libgstmpeg1systemencode.la

libgstmpeg1systemencode_la_SOURCES = gstmpeg1systemencode.c \
			 buffer.c \
			 systems.c  
libgstmpeg1systemencode_la_CFLAGS = -O2 $(FOMIT_FRAME_POINTER) $(GST_OPT_CFLAGS) $(GST_FUNROLL_CFLAGS) $(GST_CFLAGS)
libgstmpeg1systemencode_la_LIBADD =
libgstmpeg1systemencode_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = gstmpeg1systemencode.h \
		 main.h \
		 buffer.h
