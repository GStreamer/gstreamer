if USE_GCONFTOOL
GCONF_DIR=gconf
else
GCONF_DIR=
endif

if BUILD_EXTERNAL
EXT_DIR=ext
else
EXT_DIR=
endif

if BUILD_EXAMPLES
EXAMPLES_DIR=examples
else
EXAMPLES_DIR=
endif

all-local: gstreamer-libs-@GST_MAJORMINOR@.pc gstreamer-play-@GST_MAJORMINOR@.pc
gstreamer-libs-@GST_MAJORMINOR@.pc: gstreamer-libs.pc gstreamer-libs.pc.in
	cp gstreamer-libs.pc gstreamer-libs-@GST_MAJORMINOR@.pc
gstreamer-play-@GST_MAJORMINOR@.pc: gstreamer-play.pc gstreamer-play.pc.in
	cp gstreamer-play.pc gstreamer-play-@GST_MAJORMINOR@.pc
gstreamer-libs-@GST_MAJORMINOR@-uninstalled.pc: gstreamer-libs-uninstalled.pc gstreamer-libs-uninstalled.pc.in
	cp gstreamer-libs-uninstalled.pc gstreamer-libs-@GST_MAJORMINOR@-uninstalled.pc
gstreamer-play-@GST_MAJORMINOR@-uninstalled.pc: gstreamer-play-uninstalled.pc gstreamer-play-uninstalled.pc.in
	cp gstreamer-play-uninstalled.pc gstreamer-play-@GST_MAJORMINOR@-uninstalled.pc
gst-element-check-@GST_MAJORMINOR@.m4: gst-element-check.m4
	cp gst-element-check.m4 gst-element-check-@GST_MAJORMINOR@.m4

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = \
	gstreamer-libs-@GST_MAJORMINOR@.pc \
	gstreamer-play-@GST_MAJORMINOR@.pc

aclocaldir = $(datadir)/aclocal
aclocal_DATA = gst-element-check-@GST_MAJORMINOR@.m4

SUBDIRS=gst-libs gst sys $(EXT_DIR) $(EXAMPLES_DIR) tools $(GCONF_DIR) testsuite

DIST_SUBDIRS=gst-libs gst sys ext examples tools gconf testsuite

EXTRA_DIST=gst-plugins.spec depcomp gst-element-check.m4 \
           gstreamer-libs.pc.in gstreamer-libs-uninstalled.pc.in \
           gstreamer-play.pc.in gstreamer-play-uninstalled.pc.in \
	   AUTHORS COPYING README RELEASE REQUIREMENTS \
	   ChangeLog autogen.sh common m4

dist-hook:
	rm -rf `find $(distdir)/common -name CVS`
	rm -rf `find $(distdir)/m4 -name CVS`
