DISTCHECK_CONFIGURE_FLAGS=--enable-gtk-doc

SUBDIRS = \
	gst-libs gst ext docs m4 tests common po

# include before EXTRA_DIST for win32 assignment
include $(top_srcdir)/common/win32.mak
	
EXTRA_DIST = \
	gst-plugins-ugly.spec depcomp \
	AUTHORS COPYING NEWS README RELEASE REQUIREMENTS \
	ChangeLog gst-plugins-ugly.doap autogen.sh \
	$(win32)
	

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

win32-update:
	cp $(top_builddir)/win32/common/config.h-new \
	    $(top_builddir)/win32/common/config.h

include $(top_srcdir)/common/coverage/lcov.mak

