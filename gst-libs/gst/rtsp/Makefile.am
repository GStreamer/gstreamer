libgstrtspincludedir = $(includedir)/gstreamer-@GST_MAJORMINOR@/gst/rtsp

libgstrtspinclude_HEADERS = gstrtspbase64.h \
			    gstrtspconnection.h \
			    gstrtspdefs.h       \
			    gstrtspmessage.h    \
			    gstrtsprange.h      \
			    gstrtsptransport.h  \
			    gstrtspurl.h 

#gstrtspextreal.h    
#gstrtspextwms.h     

lib_LTLIBRARIES = libgstrtsp-@GST_MAJORMINOR@.la

libgstrtsp_@GST_MAJORMINOR@_la_SOURCES = gstrtspbase64.c \
                                         gstrtspconnection.c \
                                         gstrtspdefs.c       \
					 gstrtspmessage.c    \
					 gstrtsprange.c      \
					 gstrtsptransport.c  \
					 gstrtspurl.c 

#gstrtspextwms.c  
#rtspextreal.c    

libgstrtsp_@GST_MAJORMINOR@_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS)
libgstrtsp_@GST_MAJORMINOR@_la_LIBADD = $(GST_LIBS)
libgstrtsp_@GST_MAJORMINOR@_la_LDFLAGS = $(GST_LIB_LDFLAGS) $(GST_ALL_LDFLAGS) $(GST_LT_LDFLAGS)
