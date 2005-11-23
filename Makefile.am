DISTCHECK_CONFIGURE_FLAGS=--enable-gtk-doc

if BUILD_EXAMPLES
SUBDIRS_EXAMPLES = examples
else
SUBDIRS_EXAMPLES =
endif

SUBDIRS = \
	gst sys ext \
	m4 common docs \
	$(SUBDIRS_EXAMPLES)

DIST_SUBDIRS = \
	gst sys ext m4 common docs examples

EXTRA_DIST = \
	gst-plugins-bad.spec depcomp \
	AUTHORS COPYING NEWS README RELEASE REQUIREMENTS \
	ChangeLog autogen.sh

DISTCLEANFILES = _stdint.h

include $(top_srcdir)/common/release.mak

check-valgrind:
	@true

check-torture:
	@true
