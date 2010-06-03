plugin_LTLIBRARIES = libgstgeometrictransform.la 

libgstgeometrictransform_la_SOURCES = plugin.c \
                                      gstgeometrictransform.c \
                                      gstcirclegeometrictransform.c \
                                      geometricmath.c \
                                      gstcircle.c \
                                      gstkaleidoscope.c \
                                      gstpinch.c \
                                      gstsphere.c \
                                      gsttwirl.c

libgstgeometrictransform_la_CFLAGS = $(GST_CFLAGS) $(GST_BASE_CFLAGS) \
			    $(GST_PLUGINS_BASE_CFLAGS)
libgstgeometrictransform_la_LIBADD = $(GST_PLUGINS_BASE_LIBS) -lgstvideo-@GST_MAJORMINOR@ $(GST_BASE_LIBS) $(GST_LIBS)
libgstgeometrictransform_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstgeometrictransform_la_LIBTOOLFLAGS = --tag=disable-static

noinst_HEADERS = gstgeometrictransform.h \
                 gstcirclegeometrictransform.h \
                 geometricmath.h \
                 gstcircle.h \
                 gstkaleidoscope.h \
                 gstpinch.h \
                 gstsphere.h \
                 gsttwirl.h
