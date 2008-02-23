plugin_LTLIBRARIES = libgstgoom.la

GOOM_FILTER_FILES = filters.c
GOOM_FILTER_CFLAGS = -UMMX -UUSE_ASM

noinst_HEADERS = gstgoom.h filters.h goom_core.h goom_tools.h graphic.h lines.h 

libgstgoom_la_SOURCES = gstgoom.c goom_core.c $(GOOM_FILTER_FILES) graphic.c lines.c

libgstgoom_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS) $(GOOM_FILTER_CFLAGS)
libgstgoom_la_LIBADD = $(GST_BASE_LIBS) $(GST_LIBS) $(LIBM)
libgstgoom_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

EXTRA_DIST = filters.c
