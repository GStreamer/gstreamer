
plugin_LTLIBRARIES = libgstfestival.la

if HAVE_WINSOCK2_H
WINSOCK2_LIBS = -lws2_32
else
WINSOCK2_LIBS =
endif

libgstfestival_la_SOURCES = gstfestival.c 
libgstfestival_la_CFLAGS = $(GST_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS)
# we use net connection now... perhaps use the lib later
# libgstfestival_la_LIBADD = $(FESTIVAL_LIBS)
libgstfestival_la_LIBADD = $(GST_LIBS) $(GST_PLUGINS_BASE_LIBS) \
                           -lgstaudio-0.10 $(WINSOCK2_LIBS)
libgstfestival_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstfestival_la_LIBTOOLFLAGS = --tag=disable-static

noinst_HEADERS = gstfestival.h 
