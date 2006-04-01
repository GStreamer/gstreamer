DISTCHECK_CONFIGURE_FLAGS=--enable-gtk-doc

if BUILD_EXAMPLES
SUBDIRS_EXAMPLES = examples
else
SUBDIRS_EXAMPLES =
endif

SUBDIRS = \
	gst gst-libs sys ext \
	m4 common docs tests po \
	$(SUBDIRS_EXAMPLES)

DIST_SUBDIRS = \
	gst gst-libs sys ext \
	m4 common docs tests po \
	examples

EXTRA_DIST = \
	gst-plugins-bad.spec depcomp \
	AUTHORS COPYING NEWS README RELEASE REQUIREMENTS \
	ChangeLog autogen.sh

DISTCLEANFILES = _stdint.h

ACLOCAL_AMFLAGS = -I m4 -I common/m4

include $(top_srcdir)/common/release.mak
include $(top_srcdir)/common/po.mak

check-valgrind:
	@true

check-torture:
	@true
