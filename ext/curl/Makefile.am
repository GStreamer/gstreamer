plugin_LTLIBRARIES = libgstcurl.la

if USE_SSH2
gstcurlsshsink_SOURCES = gstcurlsshsink.c gstcurlsftpsink.c
else
gstcurlsshsink_SOURCES =
endif

libgstcurl_la_SOURCES = gstcurl.c \
			gstcurlbasesink.c \
			gstcurltlssink.c \
			gstcurlhttpsink.c \
			gstcurlfilesink.c \
			gstcurlftpsink.c \
			$(gstcurlsshsink_SOURCES) \
			gstcurlsmtpsink.c \
			gstcurlhttpsrc.c \
			gstcurlqueue.c

libgstcurl_la_CFLAGS = \
	$(GST_PLUGINS_BAD_CFLAGS) \
	$(GST_BASE_CFLAGS) \
	$(GST_CFLAGS) \
	$(SSH2_CFLAGS) \
	$(CURL_CFLAGS)
libgstcurl_la_LIBADD = \
	$(GST_PLUGINS_BASE_LIBS) \
	$(GST_BASE_LIBS) \
	$(GST_LIBS) \
	$(WINSOCK2_LIBS) \
	$(SSH2_LIBS) \
	$(CURL_LIBS)
libgstcurl_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = gstcurlbasesink.h \
		 gstcurltlssink.h \
		 gstcurlhttpsink.h \
		 gstcurlfilesink.h \
		 gstcurlftpsink.h \
		 gstcurlsmtpsink.h \
		 gstcurlsshsink.h \
		 gstcurlsftpsink.h \
		 gstcurlhttpsrc.h \
		 curltask.h \
		 gstcurldefaults.h \
		 gstcurlqueue.h
