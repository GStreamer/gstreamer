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

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = gstreamer-libs.pc gstreamer-play.pc

aclocaldir = $(datadir)/aclocal
aclocal_DATA = gst-element-check.m4

SUBDIRS=gst-libs gst sys $(EXT_DIR) $(EXAMPLES_DIR) tools $(GCONF_DIR) testsuite

DIST_SUBDIRS=gst-libs gst sys ext examples tools gconf testsuite

EXTRA_DIST=gst-plugins.spec depcomp gst-element-check.m4 \
           gstreamer-libs.pc.in gstreamer-libs-uninstalled.pc.in \
           gstreamer-play.pc.in gstreamer-play-uninstalled.pc.in \
	   AUTHORS COPYING README RELEASE \
	   ChangeLog autogen.sh common m4

dist-hook:
	rm -rf `find $(distdir)/common -name CVS`
	rm -rf `find $(distdir)/m4 -name CVS`
