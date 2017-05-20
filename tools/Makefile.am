
bin_PROGRAMS = \
	gst-inspect-@GST_API_VERSION@ \
	gst-stats-@GST_API_VERSION@ \
	gst-typefind-@GST_API_VERSION@

gst_inspect_@GST_API_VERSION@_SOURCES = gst-inspect.c tools.h
gst_inspect_@GST_API_VERSION@_CFLAGS = $(GST_OBJ_CFLAGS)
gst_inspect_@GST_API_VERSION@_LDADD = $(GST_OBJ_LIBS)

gst_stats_@GST_API_VERSION@_SOURCES = gst-stats.c tools.h
gst_stats_@GST_API_VERSION@_CFLAGS = $(GST_OBJ_CFLAGS)
gst_stats_@GST_API_VERSION@_LDADD = $(GST_OBJ_LIBS)

gst_typefind_@GST_API_VERSION@_SOURCES = gst-typefind.c tools.h
gst_typefind_@GST_API_VERSION@_CFLAGS = $(GST_OBJ_CFLAGS)
gst_typefind_@GST_API_VERSION@_LDADD = $(GST_OBJ_LIBS)

if !GST_DISABLE_PARSE
bin_PROGRAMS += gst-launch-@GST_API_VERSION@

gst_launch_@GST_API_VERSION@_SOURCES = gst-launch.c tools.h
gst_launch_@GST_API_VERSION@_CFLAGS = $(GST_OBJ_CFLAGS)
gst_launch_@GST_API_VERSION@_LDADD = $(GST_OBJ_LIBS)
endif

manpages = \
	gst-inspect-@GST_API_VERSION@.1 \
	gst-stats-@GST_API_VERSION@.1 \
	gst-typefind-@GST_API_VERSION@.1

if !GST_DISABLE_PARSE
manpages += gst-launch-@GST_API_VERSION@.1
endif

man_MANS = $(manpages)

# developer helper tools, not meant for installation
noinst_SCRIPTS = gst-indent

noinst_HEADERS = tools.h

EXTRA_DIST = \
	gst-inspect-@GST_API_VERSION@.1 \
	gst-typefind-@GST_API_VERSION@.1 \
	gst-launch-@GST_API_VERSION@.1 \
	gst-stats-@GST_API_VERSION@.1 \
	$(noinst_SCRIPTS)
