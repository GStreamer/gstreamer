DISTCHECK_CONFIGURE_FLAGS=--enable-gtk-doc

SUBDIRS = \
	gst ext docs m4 common

EXTRA_DIST = \
	gst-plugins-ugly.spec depcomp \
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
