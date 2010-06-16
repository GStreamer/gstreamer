# we have to put in a little hack here for integration to work well
# ffmpeg can't be in SUBDIRS, because then automake will try to descend in it
# and find conditionals it doesn't know.

# so,
# - don't put it in SUBDIRS so automake doesn't descend
# - put it in DIST_SUBDIRS so make dist picks up the subtree
# - add an all-local hook so it does get built
# this also satisfies make distcheck

SUBDIRS = 
DIST_SUBDIRS = ffmpeg
TMP_DIST_DIR=ffmpeg-dist
DIST_DIR=$(TMP_DIST_DIR)/.ffmpeg
EXTRA_DIST=Makefile.am
MAINTAINERCLEANDIRS = $(DIST_SUBDIRS)

all-local:
	cd ffmpeg && $(MAKE)

clean-local:
	cd ffmpeg && $(MAKE) clean

dist-clean:
	cd ffmpeg && $(MAKE) distclean
	rm -rf $(TMP_DIST_DIR)
	rm -f Makefile
	rm -f ffmpeg/.version

distclean: dist-clean

maintainer-clean: distclean
	rm -rf $(MAINTAINERCLEANDIRS)

maintainerclean: maintainer-clean

dist-local:
	svn -r $(FFMPEG_REVISION) co $(FFMPEG_SVN) $(TMP_DIST_DIR)
	@if [ "x$(FFMPEG_EXTERNALS_REVISION)" != x ]; then \
		svn update -r $(FFMPEG_EXTERNALS_REVISION) $(TMP_DIST_DIR)/libswscale; \
	fi
	mkdir $(DIST_DIR)
	pwd
	cp $(TMP_DIST_DIR)/*.c $(TMP_DIST_DIR)/*.h $(TMP_DIST_DIR)/Makefile $(TMP_DIST_DIR)/configure $(TMP_DIST_DIR)/version.sh $(DIST_DIR) 
	cp $(TMP_DIST_DIR)/*.mak $(TMP_DIST_DIR)/Changelog $(TMP_DIST_DIR)/COPYING.* $(TMP_DIST_DIR)/INSTALL $(DIST_DIR)
	cp $(TMP_DIST_DIR)/Doxyfile $(TMP_DIST_DIR)/CREDITS $(TMP_DIST_DIR)/MAINTAINERS $(DIST_DIR)
	@for d in `cd $(TMP_DIST_DIR) && ls -d */`; \
		do mkdir $(DIST_DIR)/$$d; \
		cp $(TMP_DIST_DIR)/$$d* $(DIST_DIR)/$$d; \
		if [ `ls -d $(TMP_DIST_DIR)/$$d*/ | wc -w` != "" ]; \
		then for id in `cd $(TMP_DIST_DIR)/$$d && ls -d */`; \
			do mkdir $(DIST_DIR)/$$d$$id; \
			cp $(TMP_DIST_DIR)/$$d$$id/*.c $(TMP_DIST_DIR)/$$d$$id/*.h $(TMP_DIST_DIR)/$$d$$id/*.asm $(TMP_DIST_DIR)/$$d$$id/*.S $(DIST_DIR)/$$d$$id; \
			done \
		fi \
	done
	mv $(DIST_DIR) $(TMP_DIST_DIR)/ffmpeg
	touch $(TMP_DIST_DIR)/ffmpeg/config.mak
	echo "Patching ffmpeg ./configure"
	sed -e '/Unknown option/ {N;N;s/exit 1//; }' $(TMP_DIST_DIR)/ffmpeg/configure > $(TMP_DIST_DIR)/ffmpeg/configure.tmp
	mv $(TMP_DIST_DIR)/ffmpeg/configure.tmp $(TMP_DIST_DIR)/ffmpeg/configure
	chmod +x $(TMP_DIST_DIR)/ffmpeg/configure

distdir: dist-local
	cp -r $(TMP_DIST_DIR)/ffmpeg ${distdir}
	cp -f $(top_srcdir)/gst-libs/ext/Makefile.am $(top_srcdir)/gst-libs/ext/Makefile.in ${distdir}
	rm -rf $(TMP_DIST_DIR)

dist: dist-local
	cd $(TMP_DIST_DIR) && tar -czf ffmpeg.tar.gz ffmpeg
	mv $(TMP_DIST_DIR)/ffmpeg.tar.gz ./
	rm -rf $(TMP_DIST_DIR)
