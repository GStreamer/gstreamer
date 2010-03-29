pcfiles_in  = gstreamer-vaapi.pc.in
pcfiles_in += gstreamer-vaapi-x11.pc.in
if USE_GLX
pcfiles_in += gstreamer-vaapi-glx.pc.in
endif

pcfiles = $(pcfiles_in:%.pc.in=%-@GST_MAJORMINOR@.pc)

all_pcfiles_in  = gstreamer-vaapi.pc.in
all_pcfiles_in += gstreamer-vaapi-x11.pc.in
all_pcfiles_in += gstreamer-vaapi-glx.pc.in

all_pcfiles = $(all_pcfiles_in:%.pc.in=%-@GST_MAJORMINOR@.pc)

pkgconfigdir = @pkgconfigdir@
pkgconfig_DATA = $(pcfiles)

EXTRA_DIST = 				\
	gstreamer-vaapi.pc.in		\
	gstreamer-vaapi-glx.pc.in	\
	gstreamer-vaapi-x11.pc.in	\
	$(NULL)

DISTCLEANFILES = $(all_pcfiles)

# Extra clean files so that maintainer-clean removes *everything*
MAINTAINERCLEANFILES = Makefile.in
