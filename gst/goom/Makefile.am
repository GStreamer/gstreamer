plugindir = $(libdir)/gst

plugin_LTLIBRARIES = libgstgoom.la

if HAVE_LIBMMX
GOOM_FILTER_FILES = filters.c filters_mmx.s
GOOM_FILTER_CFLAGS = -DMMX -DUSE_ASM
else
GOOM_FILTER_FILES = filters.c
GOOM_FILTER_CFLAGS = -UMMX -UUSE_ASM
endif

libgstgoom_la_SOURCES = gstgoom.c goom_core.c $(GOOM_FILTER_FILES) graphic.c lines.c

noinst_HEADERS = filters.h goom_core.h goom_tools.h graphic.h lines.h 

#CFLAGS += -Wall -O3 -fomit-frame-pointer -funroll-all-loops -finline-functions -ffast-math -DNDEBUG
libgstgoom_la_CFLAGS = -O2 -ffast-math $(GST_CFLAGS) $(GOOM_FILTER_CFLAGS)
libgstgoom_la_LIBADD =
libgstgoom_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

EXTRA_DIST = filters.c filters_mmx.s

