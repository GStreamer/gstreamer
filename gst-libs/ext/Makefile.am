# we have to put in a little hack here for integration to work well
# libav can't be in SUBDIRS, because then automake will try to descend in it
# and find conditionals it doesn't know.

# so,
# - don't put it in SUBDIRS so automake doesn't descend
# - put it in DIST_SUBDIRS so make dist picks up the subtree
# - add an all-local hook so it does get built
# this also satisfies make distcheck

SUBDIRS = 
DIST_SUBDIRS = libav
TMP_DIST_DIR=libav-dist
DIST_DIR=$(TMP_DIST_DIR)/.libav
EXTRA_DIST=Makefile.am
MAINTAINERCLEANDIRS = $(DIST_SUBDIRS)

all-local:
	cd libav && $(MAKE)

clean-local:
	cd libav && $(MAKE) clean

dist-clean:
	cd libav && $(MAKE) distclean
	rm -rf libav/$(TMP_DIST_DIR)
	rm -f Makefile
	rm -f libav/.version
	rm -f libav/.config

distclean: dist-clean

maintainer-clean: distclean
	rm -rf $(MAINTAINERCLEANDIRS)

maintainerclean: maintainer-clean

dist-local:
	GIT_DIR=$(srcdir)/libav/.git git checkout-index --prefix=$(abs_builddir)/libav/$(TMP_DIST_DIR)/libav/ -a
	touch libav/$(TMP_DIST_DIR)/libav/config.mak
	echo "Patching libav ./configure"
	sed -e '/Unknown option/ {N;N;s/exit 1//; }' libav/$(TMP_DIST_DIR)/libav/configure > libav/$(TMP_DIST_DIR)/libav/configure.tmp
	mv libav/$(TMP_DIST_DIR)/libav/configure.tmp libav/$(TMP_DIST_DIR)/libav/configure
	chmod +x libav/$(TMP_DIST_DIR)/libav/configure

distdir: dist-local
	cp -r libav/$(TMP_DIST_DIR)/libav ${distdir}
	cp -f $(top_srcdir)/gst-libs/ext/Makefile.am $(top_srcdir)/gst-libs/ext/Makefile.in ${distdir}
	rm -rf libav/$(TMP_DIST_DIR)

dist: dist-local
	cd libav/$(TMP_DIST_DIR) && tar -czf libav.tar.gz libav
	mv libav/$(TMP_DIST_DIR)/libav.tar.gz ./
	rm -rf libav/$(TMP_DIST_DIR)
