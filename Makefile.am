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

ACLOCAL_AMFLAGS = -I common/m4

SUBDIRS=gst-libs \
	gst sys $(EXT_DIR) \
	$(EXAMPLES_DIR) \
	tools \
	$(GCONF_DIR) \
	testsuite \
	pkgconfig

DIST_SUBDIRS=gst-libs \
	gst sys ext \
	examples \
	tools \
	gconf \
	testsuite \
	pkgconfig

EXTRA_DIST=gst-plugins.spec depcomp \
	   AUTHORS COPYING COPYING.LIB README RELEASE REQUIREMENTS \
	   ChangeLog autogen.sh common m4

dist-hook:
	rm -rf `find $(distdir)/common -name CVS`
	rm -rf `find $(distdir)/m4 -name CVS`
