plugin_LTLIBRARIES = libgstmusepack.la

libgstmusepack_la_SOURCES = \
	gstmusepackdec.cpp \
	gstmusepackreader.cpp
libgstmusepack_la_CXXFLAGS = $(MUSEPACK_CFLAGS) $(GST_CFLAGS)
libgstmusepack_la_LIBADD = $(MUSEPACK_LIBS) 
libgstmusepack_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = \
	gstmusepackdec.h \
	gstmusepackreader.h
