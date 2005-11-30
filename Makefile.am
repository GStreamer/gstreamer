DISTCHECK_CONFIGURE_FLAGS=--enable-gtk-doc

if BUILD_EXTERNAL
SUBDIRS_EXT = ext
else
SUBDIRS_EXT =
endif

SUBDIRS = 			\
	gst-libs 		\
	gst sys $(SUBDIRS_EXT) 	\
	tools 			\
	docs			\
	po 			\
	common 			\
	m4 			\
	pkgconfig

DIST_SUBDIRS = 			\
	docs			\
	gst-libs		\
	gst sys ext		\
	tools 			\
	po 			\
	common 			\
	m4 			\
	pkgconfig

EXTRA_DIST = \
	gst-plugins-base.spec depcomp \
	AUTHORS COPYING COPYING.LIB NEWS README RELEASE REQUIREMENTS \
	ChangeLog autogen.sh

DISTCLEANFILES = _stdint.h

ACLOCAL_AMFLAGS = -I m4 -I common/m4

include $(top_srcdir)/common/release.mak
include $(top_srcdir)/common/po.mak

check-valgrind:
	cd tests/check && make check-valgrind

check-torture:
	cd tests/check && make torture
