plugindir = $(libdir)/gst

plugin_LTLIBRARIES = libgstgoom.la

libgstgoom_la_SOURCES = gstgoom.c goom_core.c filters.c filters_mmx.s graphic.c lines.c

noinst_HEADERS = filters.h goom_core.h goom_tools.h graphic.h lines.h 

#CFLAGS += -Wall -O3 -fomit-frame-pointer -funroll-all-loops -finline-functions -ffast-math -DNDEBUG
libgstgoom_la_CFLAGS = -O2 -ffast-math $(GST_CFLAGS) -DMMX
libgstgoom_la_LIBADD = $(GST_LIBS)
libgstgoom_la_LDFLAGS = @GST_PLUGIN_LDFLAGS@

