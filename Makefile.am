SUBDIRS = \
	gst sys ext \
	m4 common

EXTRA_DIST = \
	gst-plugins-bad.spec depcomp \
	AUTHORS COPYING NEWS README RELEASE REQUIREMENTS \
	ChangeLog autogen.sh

DISTCLEANFILES = _stdint.h

include $(top_srcdir)/common/release.mak

check-valgrind:
	@true
