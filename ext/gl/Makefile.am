plugin_LTLIBRARIES = libgstopengl.la

# These have to stay in -bad until we can move GstVideoAggregator to -base
#	gstglbasemixer.c
#	gstglbasemixer.h
#	gstglmixer.c
#	gstglmixer.h
#	gstglstereomix.c
#	gstglstereomix.h
#	gstglvideomixer.c
#	gstglvideomixer.h

libgstopengl_la_SOURCES = \
	gstopengl.c \
	gstgluploadelement.c \
	gstgldownloadelement.c \
	gstglcolorconvertelement.c \
	gstglfilterbin.c \
	gstglmixerbin.c \
	gstglsinkbin.c \
	gstglsrcbin.c \
	gstglimagesink.c \
	gstglfiltercube.c \
	gstgleffects.c \
	effects/gstgleffectscurves.c \
	effects/gstgleffectssources.c \
	effects/gstgleffectidentity.c \
	effects/gstgleffectmirror.c \
	effects/gstgleffectsqueeze.c \
	effects/gstgleffectstretch.c \
	effects/gstgleffectfisheye.c \
	effects/gstgleffecttwirl.c \
	effects/gstgleffectbulge.c \
	effects/gstgleffecttunnel.c \
	effects/gstgleffectsquare.c \
	effects/gstgleffectlumatocurve.c \
	effects/gstgleffectrgbtocurve.c \
	effects/gstgleffectsin.c \
	effects/gstgleffectxray.c \
	effects/gstgleffectglow.c \
	effects/gstgleffectblur.c \
	effects/gstgleffectsobel.c \
	effects/gstgleffectlaplacian.c \
	gstglcolorscale.c \
	gstglcolorbalance.c \
	gstglfiltershader.c \
	gstglfilterapp.c \
	gstglviewconvert.c \
	gstglstereosplit.c \
	gstgldeinterlace.c \
	gltestsrc.c \
	gstgltestsrc.c \
	gstglutils.c

noinst_HEADERS = \
	gstgluploadelement.h \
	gstgldownloadelement.h \
	gstglcolorconvertelement.h \
	gstglfilterbin.h \
	gstglmixerbin.h \
	gstglsinkbin.h \
	gstglsrcbin.h \
	gstglimagesink.h \
	gstglfiltercube.h \
	gstgleffects.h \
	effects/gstgleffectssources.h \
	gstglcolorscale.h \
	gstglcolorbalance.h \
	gstglfiltershader.h \
	gstglfilterapp.h \
	gstglstereosplit.h \
	gstgldeinterlace.h \
	gstglviewconvert.h \
	gltestsrc.h \
	gstgltestsrc.h \
	gstglutils.h

# full opengl required
if USE_OPENGL
#	gstglmosaic.c
libgstopengl_la_SOURCES += \
	gstglfilterglass.c

#	gstglmosaic.h
noinst_HEADERS += \
	gstglfilterglass.h \
	effects/gstgleffectscurves.h \
	effects/gstgleffectlumatocurve.h

if HAVE_PNG
libgstopengl_la_SOURCES += \
	gstgldifferencematte.c

noinst_HEADERS += \
	gstgldifferencematte.h
endif
endif

if HAVE_GRAPHENE
libgstopengl_la_SOURCES += \
	gstgltransformation.c \
	gstglvideoflip.c

noinst_HEADERS += \
	gstgltransformation.h \
	gstglvideoflip.h
endif

if HAVE_JPEG
if HAVE_PNG
libgstopengl_la_SOURCES += \
	gstgloverlay.c

noinst_HEADERS += \
	gstgloverlay.h
endif
endif

if HAVE_WINDOW_COCOA
libgstopengl_la_SOURCES += \
	caopengllayersink.m

noinst_HEADERS += \
	caopengllayersink.h
endif

libgstopengl_la_OBJCFLAGS = \
	$(GST_PLUGINS_BASE_CFLAGS) \
	-fobjc-arc \
	$(GST_OBJCFLAGS) \
	$(GST_BASE_CFLAGS) \
	$(GST_CONTROLLER_CFLAGS) \
	$(GL_OBJCFLAGS)

# check order of CFLAGS and LIBS, shouldn't the order be the other way around
# (like in AM_CFLAGS)?
libgstopengl_la_CFLAGS = \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_BASE_CFLAGS) \
	$(GST_CONTROLLER_CFLAGS) \
	$(GST_CFLAGS) \
	$(GL_CFLAGS) \
	$(LIBPNG_CFLAGS) \
	$(GRAPHENE_CFLAGS)

libgstopengl_la_LIBADD = \
	$(top_builddir)/gst-libs/gst/video/libgstvideo-@GST_API_VERSION@.la \
	$(top_builddir)/gst-libs/gst/pbutils/libgstpbutils-@GST_API_VERSION@.la \
	$(top_builddir)/gst-libs/gst/gl/libgstgl-@GST_API_VERSION@.la \
	$(top_builddir)/gst-libs/gst/allocators/libgstallocators-@GST_API_VERSION@.la \
	$(GST_BASE_LIBS) \
	$(GST_CONTROLLER_LIBS) \
	$(GST_LIBS) \
	$(GL_LIBS) \
	$(LIBPNG_LIBS) \
	$(JPEG_LIBS) \
	$(LIBM) \
	$(GRAPHENE_LIBS)

libgstopengl_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstopengl_la_LIBTOOLFLAGS = --tag=CC


