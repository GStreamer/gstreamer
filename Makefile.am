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
	common \
	m4 \
	pkgconfig

DIST_SUBDIRS=ext gst-libs \
	gst sys \
	examples \
	tools \
	gconf \
	testsuite \
	po \
	common \
	m4 \
	pkgconfig

EXTRA_DIST=gst-plugins.spec depcomp \
	   AUTHORS COPYING COPYING.LIB NEWS README RELEASE REQUIREMENTS \
	   ChangeLog autogen.sh

DISTCLEANFILES=_stdint.h

# check that no marshal or enumtypes files are included
# this in turn ensures that distcheck fails for missing .list files which is currently
# shadowed when the corresponding .c and .h files are included.
distcheck-hook:
	test "x" = "x`find $(distdir) -name \*-enumtypes.[ch]`" && \
	test "x" = "x`find $(distdir) -name \*-marshal.[ch]`" || \
	( $(ECHO) "*** Still leftover enumtypes or marshal files in the tarball. Please make sure those are not disted." && false )

include $(top_srcdir)/common/release.mak
include $(top_srcdir)/common/po.mak
