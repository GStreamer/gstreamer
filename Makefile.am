DISTCHECK_CONFIGURE_FLAGS=--enable-gtk-doc

SUBDIRS = \
	gst-libs gst ext docs m4 tests common po

win32 = $(shell cat $(top_srcdir)/win32/MANIFEST)

debug:
	echo $(win32)	
	
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

if HAVE_CHECK
check-torture:
	cd tests/check && make torture
else
check-torture:
	true
endif
