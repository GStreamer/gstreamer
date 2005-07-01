plugin_LTLIBRARIES = libgstmad.la 

libgstmad_la_SOURCES = gstmad.c gstid3tag.c

libgstmad_la_CFLAGS = $(GST_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS) $(MAD_CFLAGS) $(ID3_CFLAGS)
libgstmad_la_LIBADD = $(MAD_LIBS) $(ID3_LIBS) $(GST_INTERFACES_LIBS)
libgstmad_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS) $(GST_PLUGINS_BASE_LIBS) -lgsttagedit-0.9

noinst_HEADERS = gstmad.h
