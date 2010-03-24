lib_LTLIBRARIES = 				\
	libgstvaapi-@GST_MAJORMINOR@.la		\
	libgstvaapi-x11-@GST_MAJORMINOR@.la	\
	$(NULL)

libgstvaapi_includedir =			\
	$(includedir)/gstreamer-@GST_MAJORMINOR@/gst/vaapi

libgstvaapi_source_c =				\
	gstvaapidisplay.c			\
	gstvaapiimage.c				\
	gstvaapiimageformat.c			\
	gstvaapiimagepool.c			\
	gstvaapimarshal.c			\
	gstvaapiobject.c			\
	gstvaapiparamspecs.c			\
	gstvaapisubpicture.c			\
	gstvaapisurface.c			\
	gstvaapisurfacepool.c			\
	gstvaapitypes.c				\
	gstvaapiutils.c				\
	gstvaapivideobuffer.c			\
	gstvaapivideopool.c			\
	gstvaapivideosink.c			\
	gstvaapiwindow.c			\
	$(NULL)

libgstvaapi_source_h =				\
	gstvaapidisplay.h			\
	gstvaapiimage.h				\
	gstvaapiimageformat.h			\
	gstvaapiimagepool.h			\
	gstvaapiobject.h			\
	gstvaapiparamspecs.h			\
	gstvaapisubpicture.h			\
	gstvaapisurface.h			\
	gstvaapisurfacepool.h			\
	gstvaapitypes.h				\
	gstvaapivideobuffer.h			\
	gstvaapivideopool.h			\
	gstvaapivideosink.h			\
	gstvaapiwindow.h			\
	$(NULL)

libgstvaapi_source_priv_h =			\
	gstvaapidebug.h				\
	gstvaapiobject_priv.h			\
	gstvaapiutils.h				\
	$(NULL)

libgstvaapi_x11_source_c =			\
	gstvaapidisplay_x11.c			\
	gstvaapiutils.c				\
	gstvaapiutils_x11.c			\
	gstvaapiwindow_x11.c			\
	$(NULL)

libgstvaapi_x11_source_h =			\
	gstvaapidisplay_x11.h			\
	gstvaapiwindow_x11.h			\
	$(NULL)

libgstvaapi_x11_source_priv_h =			\
	gstvaapiutils.h				\
	gstvaapiutils_x11.h			\
	$(NULL)

libgstvaapi_@GST_MAJORMINOR@_la_SOURCES =	\
	$(libgstvaapi_source_c)			\
	$(libgstvaapi_source_priv_h)		\
	$(NULL)

libgstvaapi_@GST_MAJORMINOR@include_HEADERS =	\
	$(libgstvaapi_source_h)			\
	$(NULL)

libgstvaapi_@GST_MAJORMINOR@includedir =	\
	$(libgstvaapi_includedir)

libgstvaapi_@GST_MAJORMINOR@_la_CFLAGS =	\
	-I$(top_srcdir)/gst-libs		\
	$(GST_BASE_CFLAGS)			\
	$(GST_CFLAGS)				\
	$(LIBVA_CFLAGS)				\
	$(NULL)

libgstvaapi_@GST_MAJORMINOR@_la_LIBADD =	\
	$(GST_BASE_LIBS)			\
	$(GST_LIBS)				\
	$(LIBVA_LIBS)				\
	$(NULL)

libgstvaapi_x11_@GST_MAJORMINOR@_la_SOURCES =	\
	$(libgstvaapi_x11_source_c)		\
	$(libgstvaapi_x11_source_priv_h)	\
	$(NULL)

libgstvaapi_x11_@GST_MAJORMINOR@include_HEADERS = \
	$(libgstvaapi_x11_source_h)		\
	$(NULL)

libgstvaapi_x11_@GST_MAJORMINOR@includedir =	\
	$(libgstvaapi_includedir)

libgstvaapi_x11_@GST_MAJORMINOR@_la_CFLAGS =	\
	-I$(top_srcdir)/gst-libs		\
	$(GLIB_CFLAGS)				\
	$(GST_BASE_CFLAGS)			\
	$(LIBVA_X11_CFLAGS)			\
	$(NULL)

libgstvaapi_x11_@GST_MAJORMINOR@_la_LIBADD =	\
	$(GLIB_LIBS)				\
	$(LIBVA_X11_LIBS)			\
	libgstvaapi-@GST_MAJORMINOR@.la		\
	$(NULL)

# Extra clean files so that maintainer-clean removes *everything*
MAINTAINERCLEANFILES = Makefile.in

# glib-genmarshal rules
glib_marshal_list = gstvaapimarshal.list
glib_marshal_prefix = gst_vaapi_marshal

marshal_h = $(glib_marshal_list:.list=.h)
marshal_c = $(glib_marshal_list:.list=.c)

CLEANFILES	= stamp-marshal
DISTCLEANFILES	= $(marshal_h) $(marshal_c)
BUILT_SOURCES	= $(marshal_h) $(marshal_c)
EXTRA_DIST	= $(srcdir)/$(glib_marshal_list)

stamp-marshal: $(glib_marshal_list)
	$(GLIB_GENMARSHAL) \
		--prefix=$(glib_marshal_prefix) \
		--header \
	$(srcdir)/$(glib_marshal_list) > xgen-mh \
	&& (cmp -s xgen-mh $(marshal_h) || cp -f xgen-mh $(marshal_h)) \
	&& rm -f xgen-mh \
	&& echo timestamp > $(@F)

$(marshal_h): stamp-marshal
	@true

$(marshal_c): $(marshal_h)
	(echo "#include \"$(marshal_h)\"" ; \
	$(GLIB_GENMARSHAL) \
		--prefix=$(glib_marshal_prefix) \
		--body \
	$(srcdir)/$(glib_marshal_list)) > xgen-mc \
	&& cp xgen-mc $(marshal_c) \
	&& rm -f xgen-mc
