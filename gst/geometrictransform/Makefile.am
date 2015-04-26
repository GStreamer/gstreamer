plugin_LTLIBRARIES = libgstgeometrictransform.la 

libgstgeometrictransform_la_SOURCES = plugin.c \
                                      gstgeometrictransform.c \
                                      gstcirclegeometrictransform.c \
                                      geometricmath.c \
                                      gstcircle.c \
                                      gstdiffuse.c \
                                      gstkaleidoscope.c \
                                      gstmarble.c \
                                      gstpinch.c \
                                      gstrotate.c \
                                      gstsphere.c \
                                      gsttwirl.c \
                                      gstwaterripple.c \
                                      gststretch.c \
                                      gstbulge.c \
                                      gsttunnel.c \
                                      gstsquare.c \
                                      gstmirror.c \
                                      gstfisheye.c \
                                      gstperspective.c

libgstgeometrictransform_la_CFLAGS = $(GST_CFLAGS) $(GST_BASE_CFLAGS) \
			    $(GST_PLUGINS_BASE_CFLAGS)
libgstgeometrictransform_la_LIBADD = $(GST_PLUGINS_BASE_LIBS) \
                            -lgstvideo-@GST_API_VERSION@ \
                            $(GST_BASE_LIBS) \
                            $(GST_LIBS) $(LIBM)
libgstgeometrictransform_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstgeometrictransform_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

noinst_HEADERS = gstgeometrictransform.h \
                 gstcirclegeometrictransform.h \
                 geometricmath.h \
                 gstcircle.h \
                 gstdiffuse.h \
                 gstkaleidoscope.h \
                 gstmarble.h \
                 gstpinch.h \
                 gstrotate.h \
                 gstsphere.h \
                 gsttwirl.h \
                 gstwaterripple.h \
                 gststretch.h \
                 gstbulge.h \
                 gsttunnel.h \
                 gstsquare.h \
                 gstmirror.h \
                 gstfisheye.h \
                 gstperspective.h
