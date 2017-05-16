
plugin_LTLIBRARIES = libgstfestival.la

libgstfestival_la_SOURCES = gstfestival.c 
libgstfestival_la_CFLAGS = $(GST_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS)
# we use net connection now... perhaps use the lib later
# libgstfestival_la_LIBADD = $(FESTIVAL_LIBS)
libgstfestival_la_LIBADD = $(GST_LIBS) $(GST_PLUGINS_BASE_LIBS) \
                           -lgstaudio-1.0 $(WINSOCK2_LIBS)
libgstfestival_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = gstfestival.h
