plugin_LTLIBRARIES = libgstgoom.la

GOOM_FILTER_FILES = filters.c
GOOM_FILTER_CFLAGS = -UMMX -UUSE_ASM

libgstgoom_la_SOURCES = gstgoom.c goom_core.c $(GOOM_FILTER_FILES) graphic.c lines.c

noinst_HEADERS = gstgoom.h filters.h goom_core.h goom_tools.h graphic.h lines.h 

libgstgoom_la_CFLAGS = $(GST_CFLAGS) $(GOOM_FILTER_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS)
libgstgoom_la_LIBADD = $(GST_BASE_LIBS)
libgstgoom_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

EXTRA_DIST = filters.c
