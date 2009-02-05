
plugin_LTLIBRARIES = libgstopengl.la

AM_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS)
AM_LIBS = $(GST_BASE_LIBS)

libgstopengl_la_SOURCES = \
	gstopengl.c \
	gstglimagesink.c \
	gstglimagesink.h \
	gstglupload.c \
	gstglupload.h \
	gstglbumper.c \
	gstglbumber.h \
	gstgldownload.c \
	gstgldownload.h \
	gstglfilterblur.c \
	gstglfiltersobel.c \
	gstglfiltercube.c \
	gstglfiltercube.h \
	gstglfilterlaplacian.c \
	gstglfilterlaplacian.h \
	gstglfilterglass.c \
	gstglfilterglass.h \
	gstglfilterapp.c \
	gstglfilterapp.h \
	gstglcolorscale.c \
	gstglcolorscale.h \
	gstgldifferencematte.c \
	gstgloverlay.c \
	gltestsrc.c \
	gltestsrc.h \
	gstgltestsrc.c \
	gstgltestsrc.h \
	gstgleffects.c \
	gstgleffects.h \
	effects/gstgleffectssources.c \
	effects/gstgleffectssources.h \
	effects/gstgleffectscurves.h \
	effects/gstgleffectidentity.c \
	effects/gstgleffectmirror.c \
	effects/gstgleffectsqueeze.c \
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

# check order of CFLAGS and LIBS, shouldn't the order be the other way around
# (like in AM_CFLAGS)?
libgstopengl_la_CFLAGS = $(GST_CFLAGS) $(X_CFLAGS) $(GST_BASE_CFLAGS) \
	$(GST_PLUGINS_BASE_CFLAGS) $(GL_CFLAGS) $(LIBPNG_CFLAGS) \
	-I$(top_srcdir)/gst-libs \
	-I$(top_srcdir)/gst-libs/gst/gl \
	-I$(top_srcdir)/gst/gl/effects

libgstopengl_la_LIBADD = \
	$(top_builddir)/gst-libs/gst/gl/libgstgl-$(GST_MAJORMINOR).la \
	$(GST_BASE_LIBS) \
	$(GST_PLUGINS_BASE_LIBS) -lgstinterfaces-$(GST_MAJORMINOR) \
	$(GL_LIBS) \
	$(LIBPNG_LIBS)

libgstopengl_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

