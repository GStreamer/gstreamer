AUTOMAKE_OPTIONS = dist-bzip2

SUBDIRS = source parser generator elementgen gstreamer-sharp doc tests samples

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = gstreamer-sharp-0.10.pc

EXTRA_DIST = \
	RELEASE \
	gstreamer-sharp-0.10.pc.in \
	gstreamer-sharp.snk

DISTCLEANFILES = gstreamer-sharp-0.10.pc 

MAINTAINERCLEANFILES = \
	compile \
	INSTALL \
	config.h.in \
	aclocal.m4 \
	ltmain.sh \
	Makefile.in \
	depcomp \
	missing \
	install-sh \
	configure \
	config.sub \
	config.guess \
	intltool-extract.in \
	intltool-merge.in   \
	intltool-update.in  \
	mkinstalldirs

check: all
	$(MAKE) -C tests run-test
