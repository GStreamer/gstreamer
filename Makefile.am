if USE_GCONF
GCONF_DIR=gconf
else
GCONF_DIR=
endif

if BUILD_EXTERNAL
EXT_DIR=ext
else
EXT_DIR=
endif

SUBDIRS=gst-libs gst sys $(EXT_DIR) examples tools $(GCONF_DIR) ## testsuite

DIST_SUBDIRS=gst-libs gst sys ext examples tools gconf ## testsuite

EXTRA_DIST=gst-plugins.spec depcomp AUTHORS COPYING README RELEASE ChangeLog autogen.sh
