DISTCHECK_CONFIGURE_FLAGS=--enable-gtk-doc

SUBDIRS = 		\
	src           	\
	m4   		\
	common

DIST_SUBDIRS = $(SUBDIRS)

EXTRA_DIST = \
	ChangeLog autogen.sh depcomp \
	AUTHORS COPYING NEWS README RELEASE REQUIREMENTS \
	gst-rtsp.spec docs/design/gst-rtp-server-design

ACLOCAL_AMFLAGS = -I common/m4 -I m4

DISTCLEANFILES = _stdint.h gst-rtsp.spec

include $(top_srcdir)/common/release.mak
include $(top_srcdir)/common/po.mak

check-valgrind:
	cd tests/check && make check-valgrind

if HAVE_CHECK
check-torture:
	cd tests/check && make torture
else
check-torture:
	true
endif

