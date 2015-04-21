plugin_LTLIBRARIES = libgstvideoframe_audiolevel.la

libgstvideoframe_audiolevel_la_SOURCES = gstvideoframe-audiolevel.c
libgstvideoframe_audiolevel_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS)
libgstvideoframe_audiolevel_la_LIBADD = $(GST_PLUGINS_BASE_LIBS) -lgstaudio-$(GST_API_VERSION) $(GST_BASE_LIBS) $(LIBM)
libgstvideoframe_audiolevel_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstvideoframe_audiolevel_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

noinst_HEADERS = gstvideoframe-audiolevel.h
