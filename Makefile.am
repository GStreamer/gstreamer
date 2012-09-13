DISTCHECK_CONFIGURE_FLAGS=--enable-gtk-doc

SUBDIRS = ges tests tools common m4 pkgconfig docs bindings

DIST_SUBDIRS = $(SUBDIRS)

# include before EXTRA_DIST for win32 assignment
#include $(top_srcdir)/common/win32.mak

EXTRA_DIST = \
	gst-editing-services.spec depcomp \
	AUTHORS COPYING NEWS README RELEASE \
	ChangeLog autogen.sh

DISTCLEANFILES = _stdint.h

ACLOCAL_AMFLAGS = -I m4 -I common/m4

include $(top_srcdir)/common/release.mak
include $(top_srcdir)/common/po.mak

include $(top_srcdir)/common/coverage/lcov.mak

check-valgrind:
	cd tests/check && make check-valgrind

if HAVE_GST_CHECK
check-torture:
	cd tests/check && make torture
else
check-torture:
	true
endif

# cruft: plugins that have been merged or moved or renamed
CRUFT_FILES = \
 $(top_builddir)/common/shave \
 $(top_builddir)/common/shave-libtool

include $(top_srcdir)/common/cruft.mak

all-local: check-cruft
