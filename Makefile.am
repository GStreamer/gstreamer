ACLOCAL_AMFLAGS = -I m4 ${ACLOCAL_FLAGS}

AUTOMAKE_OPTIONS = foreign

SUBDIRS = debian.upstream gst-libs gst tests docs

# Extra clean files so that maintainer-clean removes *everything*
MAINTAINERCLEANFILES = \
	$(GITIGNORE_MAINTAINERCLEANFILES_TOPLEVEL)	\
	$(GITIGNORE_MAINTAINERCLEANFILES_MAKEFILE_IN)	\
	$(GITIGNORE_MAINTAINERCLEANFILES_M4_LIBTOOL)	\
	$(srcdir)/gtk-doc.make $(srcdir)/m4/gtk-doc.m4	\
	$(NULL)

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

-include $(top_srcdir)/git.mk
