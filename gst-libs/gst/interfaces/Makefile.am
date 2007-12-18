lib_LTLIBRARIES = libgstinterfaces-@GST_MAJORMINOR@.la
libgstinterfacesincludedir = \
        $(includedir)/gstreamer-@GST_MAJORMINOR@/gst/interfaces

headers_interfaces = 		\
	colorbalance.h		\
	colorbalancechannel.h	\
	mixer.h			\
	mixeroptions.h		\
	mixertrack.h		\
	navigation.h		\
	propertyprobe.h		\
	tuner.h			\
	tunernorm.h		\
	tunerchannel.h		\
	videoorientation.h	\
	xoverlay.h

# variables used for enum/marshal generation
glib_enum_headers=$(headers_interfaces)
glib_enum_define=GST_INTERFACES
glib_enum_prefix=gst_interfaces

built_sources = \
	interfaces-marshal.c \
	interfaces-enumtypes.c

built_headers = \
	interfaces-marshal.h \
	interfaces-enumtypes.h

libgstinterfacesinclude_HEADERS = \
	$(headers_interfaces)

nodist_libgstinterfacesinclude_HEADERS = \
	interfaces-enumtypes.h

libgstinterfaces_@GST_MAJORMINOR@_la_SOURCES = \
	colorbalance.c		\
	colorbalancechannel.c	\
	mixer.c			\
	mixeroptions.c		\
	mixertrack.c		\
	navigation.c		\
	propertyprobe.c		\
	tuner.c			\
	tunernorm.c		\
	tunerchannel.c		\
	videoorientation.c	\
	xoverlay.c

nodist_libgstinterfaces_@GST_MAJORMINOR@_la_SOURCES = 	\
	$(built_sources)				\
	interfaces-marshal.h

libgstinterfaces_@GST_MAJORMINOR@_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS)
libgstinterfaces_@GST_MAJORMINOR@_la_LIBADD = $(GST_LIBS) 
libgstinterfaces_@GST_MAJORMINOR@_la_LDFLAGS = $(GST_LIB_LDFLAGS) $(GST_ALL_LDFLAGS) $(GST_LT_LDFLAGS)

BUILT_SOURCES = \
	$(built_sources) \
	$(built_headers)

EXTRA_DIST = interfaces-marshal.list

CLEANFILES = $(BUILT_SOURCES)

include $(top_srcdir)/common/glib-gen.mak
