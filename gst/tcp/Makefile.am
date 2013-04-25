plugin_LTLIBRARIES = libgsttcp.la

if HAVE_SYS_SOCKET_H
multifdsink_SOURCES = \
	gstmultifdsink.c
else
multifdsink_SOURCES =
endif

libgsttcp_la_SOURCES = \
	gsttcpplugin.c \
	gsttcpclientsrc.c gsttcpclientsink.c \
	$(multifdsink_SOURCES) \
	gstmultihandlesink.c  \
	gstmultisocketsink.c  \
	gsttcpserversrc.c gsttcpserversink.c

libgsttcp_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS) $(GIO_CFLAGS)
libgsttcp_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgsttcp_la_LIBADD = $(GST_BASE_LIBS) $(GST_LIBS) $(GIO_LIBS)
libgsttcp_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

noinst_HEADERS = \
  gsttcp.h \
  gsttcpclientsrc.h gsttcpclientsink.h \
  gstmultifdsink.h  \
  gstmultisocketsink.h  \
  gsttcpserversrc.h gsttcpserversink.h gstmultihandlesink.h

CLEANFILES = $(BUILT_SOURCES)

Android.mk: Makefile.am $(BUILT_SOURCES)
	androgenizer \
	-:PROJECT libgsttcp -:SHARED libgsttcp \
	 -:TAGS eng debug \
         -:REL_TOP $(top_srcdir) -:ABS_TOP $(abs_top_srcdir) \
	 -:SOURCES $(libgsttcp_la_SOURCES) \
	 	   $(nodist_libgsttcp_la_SOURCES) \
	 -:CFLAGS $(DEFS) $(DEFAULT_INCLUDES) $(libgsttcp_la_CFLAGS) \
	 -:LDFLAGS $(libgsttcp_la_LDFLAGS) \
	           $(libgsttcp_la_LIBADD) \
	           -ldl \
	 -:PASSTHROUGH LOCAL_ARM_MODE:=arm \
		       LOCAL_MODULE_PATH:='$$(TARGET_OUT)/lib/gstreamer-0.10' \
	> $@
