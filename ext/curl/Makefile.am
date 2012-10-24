plugin_LTLIBRARIES = libgstcurl.la

libgstcurl_la_SOURCES = gstcurl.c \
			gstcurlbasesink.c \
			gstcurltlssink.c \
			gstcurlhttpsink.c \
			gstcurlfilesink.c \
			gstcurlftpsink.c \
			gstcurlsmtpsink.c
libgstcurl_la_CFLAGS = \
	$(GST_PLUGINS_BAD_CFLAGS) \
	$(GST_BASE_CFLAGS) \
	$(GST_CFLAGS) \
	$(CURL_CFLAGS)
libgstcurl_la_LIBADD = \
	$(GST_PLUGINS_BASE_LIBS) \
	$(GST_BASE_LIBS) \
	$(GST_LIBS) \
	$(WINSOCK2_LIBS) \
	$(CURL_LIBS)
libgstcurl_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstcurl_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

noinst_HEADERS = gstcurlbasesink.h \
		 gstcurltlssink.h \
		 gstcurlhttpsink.h \
		 gstcurlfilesink.h \
		 gstcurlftpsink.h \
		 gstcurlsmtpsink.h
