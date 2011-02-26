plugin_LTLIBRARIES = libgstcurl.la

libgstcurl_la_SOURCES = gstcurl.c gstcurlsink.c
libgstcurl_la_CFLAGS = \
	$(GST_BASE_CFLAGS) \
	$(GST_CFLAGS) \
	$(CURL_CFLAGS)
libgstcurl_la_LIBADD = \
	$(GST_BASE_LIBS) \
	$(GST_LIBS) \
	$(CURL_LIBS)
libgstcurl_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstcurl_la_LIBTOOLFLAGS = --tag=disable-static

noinst_HEADERS = gstcurlsink.h
