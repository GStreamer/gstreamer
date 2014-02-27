plugin_LTLIBRARIES = libgstrtpmanager.la

libgstrtpmanager_la_SOURCES = gstrtpmanager.c \
			      gstrtpbin.c \
			      gstrtpdtmfmux.c \
			      gstrtpjitterbuffer.c \
			      gstrtpmux.c \
			      gstrtpptdemux.c \
			      gstrtprtxqueue.c \
			      gstrtprtxreceive.c \
			      gstrtprtxsend.c \
			      gstrtpssrcdemux.c \
			      rtpjitterbuffer.c      \
			      rtpsession.c      \
			      rtpsource.c      \
			      rtpstats.c      \
			      gstrtpsession.c

noinst_HEADERS = gstrtpbin.h \
		 gstrtpdtmfmux.h \
		 gstrtpjitterbuffer.h \
		 gstrtpmux.h \
                 gstrtpptdemux.h \
                 gstrtpssrcdemux.h \
                 gstrtprtxqueue.h \
                 gstrtprtxreceive.h \
                 gstrtprtxsend.h \
                 rtpjitterbuffer.h \
		 rtpsession.h  \
		 rtpsource.h  \
		 rtpstats.h  \
		 gstrtpsession.h

libgstrtpmanager_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS) \
	$(GST_NET_CFLAGS) $(WARNING_CFLAGS) $(ERROR_CFLAGS)
libgstrtpmanager_la_LIBADD = $(GST_PLUGINS_BASE_LIBS) \
	$(GST_NET_LIBS) -lgstrtp-@GST_API_VERSION@ \
	$(GST_BASE_LIBS) $(GST_LIBS_LIBS)
libgstrtpmanager_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstrtpmanager_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

Android.mk: Makefile.am
	androgenizer \
	-:PROJECT libgstrtpmanager -:SHARED libgstrtpmanager \
	 -:TAGS eng debug \
         -:REL_TOP $(top_srcdir) -:ABS_TOP $(abs_top_srcdir) \
	 -:SOURCES $(libgstrtpmanager_la_SOURCES) \
	 	   $(nodist_libgstrtpmanager_la_SOURCES) \
	 -:CFLAGS $(DEFS) $(DEFAULT_INCLUDES) $(libgstrtpmanager_la_CFLAGS) \
	 -:LDFLAGS $(libgstrtpmanager_la_LDFLAGS) \
	           $(libgstrtpmanager_la_LIBADD) \
	           -ldl \
	 -:PASSTHROUGH LOCAL_ARM_MODE:=arm \
		       LOCAL_MODULE_PATH:='$$(TARGET_OUT)/lib/gstreamer-0.10' \
	> $@
