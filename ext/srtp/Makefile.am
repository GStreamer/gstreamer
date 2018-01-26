plugin_LTLIBRARIES = libgstsrtp.la

libgstsrtp_la_SOURCES = \
	gstsrtp.c	\
	gstsrtp.h	\
	gstsrtpdec.c	\
	gstsrtpdec.h	\
	gstsrtpenc.c	\
	gstsrtpenc.h	\
	gstsrtpenums.h	\
	$(NULL)

libgstsrtp_la_CFLAGS = $(GST_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS) $(SRTP_CFLAGS)
libgstsrtp_la_LIBADD = $(GST_PLUGINS_BASE_LIBS) \
	-lgstrtp-@GST_API_VERSION@ 		\
	$(GST_LIBS)				\
	$(SRTP_LIBS) 
libgstsrtp_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

glib_enum_headers=gstsrtpenums.h
glib_enum_define=GST_SRTP
glib_gen_prefix=gst_srtp
glib_gen_basename=gstsrtp

BUILT_SOURCES = gstsrtp-enumtypes.c gstsrtp-enumtypes.h
nodist_libgstsrtp_la_SOURCES = $(BUILT_SOURCES)

CLEANFILES = $(BUILT_SOURCES)

include $(top_srcdir)/common/gst-glib-gen.mak
