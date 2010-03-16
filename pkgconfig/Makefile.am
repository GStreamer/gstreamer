pcfiles  = gstreamer-vaapi-@GST_MAJORMINOR@.pc
pcfiles += gstreamer-vaapi-x11-@GST_MAJORMINOR@.pc

pkgconfigdir = @pkgconfigdir@
pkgconfig_DATA = $(pcfiles)

EXTRA_DIST = 				\
	gstreamer-vaapi.pc.in		\
	gstreamer-vaapi-x11.pc.in	\
	$(NULL)

CLEANFILES = $(pcfiles)

%-@GST_MAJORMINOR@.pc: %.pc
	cp $< $@

# Extra clean files so that maintainer-clean removes *everything*
MAINTAINERCLEANFILES = Makefile.in
