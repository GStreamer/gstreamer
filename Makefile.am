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

include $(top_srcdir)/common/release.mak
include $(top_srcdir)/common/po.mak
