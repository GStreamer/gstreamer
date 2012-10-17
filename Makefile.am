ACLOCAL_AMFLAGS = -I m4 ${ACLOCAL_FLAGS}

AUTOMAKE_OPTIONS = foreign

SUBDIRS = debian.upstream gst-libs gst pkgconfig tests docs ext

# Extra clean files so that maintainer-clean removes *everything*
MAINTAINERCLEANFILES = \
	aclocal.m4 compile config.guess config.sub \
	configure depcomp install-sh ltmain.sh     \
	Makefile.in missing config.h.in gtk-doc.make

DEB_BUILDDIR = debian.build

deb:
	dpkg-buildpackage -rfakeroot -uc -us

deb.upstream: dist-bzip2
	-mkdir -p $(DEB_BUILDDIR)
	cd $(DEB_BUILDDIR)				&& \
	rm -rf $(PACKAGE)-$(VERSION)			&& \
	tar jxvf ../$(PACKAGE)-$(VERSION).tar.bz2	&& \
	cd $(PACKAGE)-$(VERSION)			&& \
	$(LN_S) debian.upstream debian			&& \
	$(MAKE) deb -f Makefile.am
