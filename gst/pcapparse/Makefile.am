plugin_LTLIBRARIES = libgstpcapparse.la

# when compiling for windows we need to link with the winsock library
if HAVE_WINSOCK2_H
  WINSOCK2_LIBS = -lws2_32
else
  WINSOCK2_LIBS = 
endif

libgstpcapparse_la_SOURCES = \
	gstpcapparse.c

noinst_HEADERS = \
	gstpcapparse.h

libgstpcapparse_la_CFLAGS = $(GST_CFLAGS) $(GST_BASE_CFLAGS)
libgstpcapparse_la_LIBADD = $(GST_LIBS) $(GST_BASE_LIBS) $(WINSOCK2_LIBS)
libgstpcapparse_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstpcapparse_la_LIBTOOLFLAGS = --tag=disable-static

