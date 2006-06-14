plugin_LTLIBRARIES = libgstpitch.la

libgstpitch_la_SOURCES = gstpitch.cc

libgstpitch_la_CXXFLAGS = $(GST_CFLAGS) $(GST_BASE_CFLAGS) $(SOUNDTOUCH_CFLAGS) $(GST_CONTROLLER_CFLAGS)
libgstpitch_la_LIBADD = $(GST_LIBS) $(GST_BASE_LIBS) $(SOUNDTOUCH_LIBS) $(GST_CONTROLLER_LIBS)
libgstpitch_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = gstpitch.hh
