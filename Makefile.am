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

SUBDIRS=gst-libs \
	gst sys $(EXT_DIR) \
	$(EXAMPLES_DIR) \
	tools \
	$(GCONF_DIR) \
	testsuite \
	po \
	pkgconfig

DIST_SUBDIRS=ext gst-libs \
	gst sys \
	examples \
	tools \
	gconf \
	testsuite \
	po \
	pkgconfig

EXTRA_DIST=gst-plugins.spec depcomp \
	   AUTHORS COPYING COPYING.LIB README RELEASE REQUIREMENTS \
	   ChangeLog autogen.sh common m4

DISTCLEANFILES=_stdint.h

# to do a release, run "make release"
# this will generate all archives and md5sums
                                                                                
# make bz2 as well
AUTOMAKE_OPTIONS = dist-bzip2
                                                                                
release: dist
	make @PACKAGE@-@VERSION@.tar.gz.md5
	make @PACKAGE@-@VERSION@.tar.bz2.md5
                                                                                
# generate md5 sum files
%.md5: %
	md5sum $< > $@
                                                                                

dist-hook:
	rm -rf `find $(distdir)/common -name CVS`
	rm -rf `find $(distdir)/m4 -name CVS`
