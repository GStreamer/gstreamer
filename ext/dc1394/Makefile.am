
plugin_LTLIBRARIES =	libgstdc1394.la

libgstdc1394_la_SOURCES =	gstdc1394.c 


libgstdc1394_la_CFLAGS = 	$(GST_CFLAGS) \
				$(LIBDC1394_CFLAGS)

libgstdc1394_la_LDFLAGS = 	$(GST_PLUGIN_LDFLAGS)
libgstdc1394_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

libgstdc1394_la_LIBADD = 	$(GST_BASE_LIBS) \
				$(LIBDC1394_LIBS) 

noinst_HEADERS = gstdc1394.h
