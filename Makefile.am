DISTCHECK_CONFIGURE_FLAGS=--enable-gtk-doc

SUBDIRS = ges tests common m4

DIST_SUBDIRS = ges tests common m4

# include before EXTRA_DIST for win32 assignment
#include $(top_srcdir)/common/win32.mak

EXTRA_DIST = \
	gst-editing-services.spec depcomp \
	AUTHORS COPYING NEWS README RELEASE \
	ChangeLog gst-editing-services.doap autogen.sh

DISTCLEANFILES = _stdint.h

ACLOCAL_AMFLAGS = -I m4 -I common/m4

include $(top_srcdir)/common/release.mak
include $(top_srcdir)/common/po.mak

check-valgrind:
	cd tests/check && make check-valgrind

if HAVE_GST_CHECK
check-torture:
	cd tests/check && make torture
else
check-torture:
	true
endif


