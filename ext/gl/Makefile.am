
plugin_LTLIBRARIES = libgstopengl.la

AM_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS)
AM_LIBS = $(GST_BASE_LIBS) $(GST_PLUGINS_BASE_LIBS)

# full opengl required
if USE_OPENGL
OPENGL_SOURCES =  \
	gstglfilterblur.c \
	gstglfilterblur.h \
	gstglfiltersobel.c \
	gstglfiltersobel.h \
	gstglfilterlaplacian.c \
	gstglfilterlaplacian.h \
	gstglfilterglass.c \
	gstglfilterglass.h \
	gstglfilterapp.c \
	gstglfilterapp.h \
	gstglfilterreflectedscreen.c \
	gstglfilterreflectedscreen.h \
	gstgldeinterlace.c \
	gstgldeinterlace.h \
	gltestsrc.c \
	gltestsrc.h \
	gstgltestsrc.c \
	gstgltestsrc.h \
	gstglmosaic.c \
	gstglmosaic.h \
	effects/gstgleffectscurves.h \
	effects/gstgleffectstretch.c \
	effects/gstgleffecttunnel.c \
	effects/gstgleffectfisheye.c \
	effects/gstgleffecttwirl.c \
	effects/gstgleffectbulge.c \
	effects/gstgleffectsquare.c \
	effects/gstgleffectlumatocurve.c \
	effects/gstgleffectlumatocurve.h \
	effects/gstgleffectrgbtocurve.c \
	effects/gstgleffectsin.c \
	effects/gstgleffectglow.c \
	effects/gstgleffectxray.c

if HAVE_PNG
OPENGL_SOURCES += \
	gstglbumper.c \
	gstglbumper.h \
	gstgldifferencematte.c \
	gstgldifferencematte.h
if HAVE_JPEG
OPENGL_SOURCES += \
	gstgloverlay.c \
	gstgloverlay.h
endif
endif
endif

libgstopengl_la_SOURCES = \
	gstopengl.c \
	gstglimagesink.c \
	gstglimagesink.h \
	gstglfiltercube.c \
	gstglfiltercube.h \
	gstgleffects.c \
	gstgleffects.h \
	effects/gstgleffectssources.c \
	effects/gstgleffectssources.h \
	effects/gstgleffectidentity.c \
	effects/gstgleffectmirror.c \
	effects/gstgleffectsqueeze.c \
	gstglcolorscale.c \
	gstglcolorscale.h \
	gstglmixer.c \
	gstglmixer.h \
	gstglmixerpad.h \
	gstglvideomixer.c \
	gstglvideomixer.h \
	gstglfiltershader.c \
	gstglfiltershader.h \
	$(OPENGL_SOURCES)

if HAVE_GRAPHENE
libgstopengl_la_SOURCES += \
	gstgltransformation.c \
	gstgltransformation.h
endif

# check order of CFLAGS and LIBS, shouldn't the order be the other way around
# (like in AM_CFLAGS)?
libgstopengl_la_CFLAGS = \
	-I$(top_srcdir)/gst-libs \
	-I$(top_builddir)/gst-libs \
	$(GST_CFLAGS) \
	$(GST_BASE_CFLAGS) \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GL_CFLAGS) \
	$(LIBPNG_CFLAGS) \
	$(GRAPHENE_CFLAGS)

libgstopengl_la_LIBADD = \
	$(top_builddir)/gst-libs/gst/gl/libgstgl-$(GST_API_VERSION).la \
	$(top_builddir)/gst-libs/gst/base/libgstbadbase-$(GST_API_VERSION).la \
	$(top_builddir)/gst-libs/gst/video/libgstbadvideo-$(GST_API_VERSION).la \
	$(GST_BASE_LIBS) \
	$(GST_PLUGINS_BASE_LIBS) -lgstvideo-$(GST_API_VERSION) \
	-lgstpbutils-$(GST_API_VERSION) \
	$(GL_LIBS) \
	$(LIBPNG_LIBS) \
	$(JPEG_LIBS) \
	$(LIBM) \
	$(GRAPHENE_LIBS)

libgstopengl_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstopengl_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

