libgstrtspincludedir = $(includedir)/gstreamer-@GST_MAJORMINOR@/gst/rtsp

libgstrtspinclude_HEADERS = gstrtspbase64.h \
			    gstrtspconnection.h \
			    gstrtspdefs.h       \
			    gstrtspextension.h       \
			    gstrtspmessage.h    \
			    gstrtsprange.h      \
			    gstrtsptransport.h  \
			    gstrtspurl.h 

#gstrtspextreal.h    
#gstrtspextwms.h     

lib_LTLIBRARIES = libgstrtsp-@GST_MAJORMINOR@.la

built_sources = rtsp-marshal.c gstrtsp-enumtypes.c
built_headers = rtsp-marshal.h gstrtsp-enumtypes.h

libgstrtsp_@GST_MAJORMINOR@_la_SOURCES = gstrtspbase64.c \
                                         gstrtspconnection.c \
                                         gstrtspdefs.c       \
                                         gstrtspextension.c  \
					 gstrtspmessage.c    \
					 gstrtsprange.c      \
					 gstrtsptransport.c  \
					 gstrtspurl.c        \
					 md5.c

nodist_libgstrtsp_@GST_MAJORMINOR@_la_SOURCES = $(built_sources)
nodist_libgstrtspinclude_HEADERS = gstrtsp-enumtypes.h

noinst_HEADERS = gstrtsp.h md5.h

#gstrtspextwms.c  
#rtspextreal.c    

libgstrtsp_@GST_MAJORMINOR@_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS)
libgstrtsp_@GST_MAJORMINOR@_la_LIBADD = $(GST_LIBS) $(WIN32_LIBS) $(HSTRERROR_LIBS)
libgstrtsp_@GST_MAJORMINOR@_la_LDFLAGS = $(GST_LIB_LDFLAGS) $(GST_ALL_LDFLAGS) $(GST_LT_LDFLAGS)

BUILT_SOURCES = $(built_headers) $(built_sources)
EXTRA_DIST = rtsp-marshal.list
CLEANFILES = $(BUILT_SOURCES)

glib_enum_prefix=gst_rtsp
glib_enum_define=gst_rtsp
glib_enum_headers=gstrtspdefs.h

include $(top_srcdir)/common/glib-gen.mak
