pcfiles_in  = gstreamer-vaapi.pc.in
if USE_DRM
pcfiles_in += gstreamer-vaapi-drm.pc.in
endif
if USE_X11
pcfiles_in += gstreamer-vaapi-x11.pc.in
endif
if USE_GLX
pcfiles_in += gstreamer-vaapi-glx.pc.in
endif
if USE_WAYLAND
pcfiles_in += gstreamer-vaapi-wayland.pc.in
endif

pcfiles = $(pcfiles_in:%.pc.in=%-@GST_MAJORMINOR@.pc)

all_pcfiles_in  = gstreamer-vaapi.pc.in
all_pcfiles_in += gstreamer-vaapi-drm.pc.in
all_pcfiles_in += gstreamer-vaapi-x11.pc.in
all_pcfiles_in += gstreamer-vaapi-glx.pc.in
all_pcfiles_in += gstreamer-vaapi-wayland.pc.in

all_pcfiles = $(all_pcfiles_in:%.pc.in=%-@GST_MAJORMINOR@.pc)

pkgconfigdir = @pkgconfigdir@
pkgconfig_DATA = $(pcfiles)

EXTRA_DIST = $(all_pcfiles_in)

DISTCLEANFILES = $(all_pcfiles)

# Extra clean files so that maintainer-clean removes *everything*
MAINTAINERCLEANFILES = Makefile.in
