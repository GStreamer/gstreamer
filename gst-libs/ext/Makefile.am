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

all-local:
	cd ffmpeg && $(MAKE)

clean-local:
	cd ffmpeg && $(MAKE) distclean

dist-clean:
	rm -rf $(TMP_DIST_DIR) 

dist-local: dist-clean
	svn -r $(FFMPEG_REVISION) co $(FFMPEG_SVN) $(TMP_DIST_DIR)
	mkdir $(DIST_DIR)
	pwd
	cp $(TMP_DIST_DIR)/*.c $(TMP_DIST_DIR)/*.h $(TMP_DIST_DIR)/Makefile $(TMP_DIST_DIR)/configure $(TMP_DIST_DIR)/version.sh $(DIST_DIR) 
	cp $(TMP_DIST_DIR)/common.mak $(TMP_DIST_DIR)/Changelog $(TMP_DIST_DIR)/COPYING.* $(TMP_DIST_DIR)/INSTALL $(DIST_DIR)
	cp $(TMP_DIST_DIR)/Doxyfile $(TMP_DIST_DIR)/ffinstall.nsi $(TMP_DIST_DIR)/CREDITS $(TMP_DIST_DIR)/MAINTAINERS $(DIST_DIR)
	@for d in `cd $(TMP_DIST_DIR) && ls -d */`; \
		do mkdir $(DIST_DIR)/$$d; \
		cp $(TMP_DIST_DIR)/$$d* $(DIST_DIR)/$$d; \
		if [ `ls -d $(TMP_DIST_DIR)/$$d*/ | wc -w` != "" ]; \
		then for id in `cd $(TMP_DIST_DIR)/$$d && ls -d */`; \
			do mkdir $(DIST_DIR)/$$d$$id; \
			cp $(TMP_DIST_DIR)/$$d$$id/*.c $(TMP_DIST_DIR)/$$d$$id/*.h $(DIST_DIR)/$$d$$id; \
			done \
		fi \
	done 
	rm -rf ffmpeg
	mv $(DIST_DIR) ffmpeg
	echo "Patching ffmpeg ./configure"
	sed -e '/Unknown option/ {N;N;s/exit 1//; }' ffmpeg/configure > ffmpeg/configure.tmp
	mv ffmpeg/configure.tmp ffmpeg/configure
	chmod +x ffmpeg/configure
	rm -rf $(TMP_DIST_DIR)

distdir: dist-local
	cp -r ffmpeg Makefile* ${distdir}
	
dist: dist-local
	tar -czf ffmpeg.tar.gz ffmpeg	

