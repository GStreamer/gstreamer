# this doesn't trigger the distdir target in the ffmpeg checkout
DIST_SUBDIRS =

if USE_FFMPEG
FFMPEG_DIR=ffmpeg
else
FFMPEG_DIR=
endif

MAKEFLAGS=-j1

# patch it up before descending into dirs
all-recursive: patches

# ignore failures on patching
# ignore failures on rm
PATCHES = patch/function.patch

patches:
	@echo -n Patching ffmpeg if necessary ...
	@patch -p0 -N -r rejects < patch/function.patch > /dev/null || true
	@rm -f rejects || true
	@echo done.


SUBDIRS = $(FFMPEG_DIR)

DISTFILES = \
	$(PATCHES) \
	Tag \
	ffmpeg/Changelog \
	ffmpeg/COPYING \
	ffmpeg/CREDITS \
	ffmpeg/INSTALL \
	ffmpeg/README \
	ffmpeg/VERSION \
	ffmpeg/config.mak \
	ffmpeg/configure \
	ffmpeg/Makefile \
	ffmpeg/cygwin_inttypes.h \
	ffmpeg/ffserver.h \
	ffmpeg/ffmpeg.c \
	ffmpeg/ffserver.c \
	ffmpeg/berrno.h \
	ffmpeg/config.h \
	ffmpeg/libavcodec/alpha/*.c \
	ffmpeg/libavcodec/alpha/*.h \
	ffmpeg/libavcodec/alpha/*.S \
	ffmpeg/libavcodec/armv4l/*.c \
	ffmpeg/libavcodec/armv4l/*.S \
	ffmpeg/libavcodec/i386/*.c \
	ffmpeg/libavcodec/i386/*.h \
	ffmpeg/libavcodec/liba52/*.c \
	ffmpeg/libavcodec/liba52/*.h \
	ffmpeg/libavcodec/ppc/*.c \
	ffmpeg/libavcodec/ppc/*.h \
	ffmpeg/libavcodec/ps2/*.c \
	ffmpeg/libavcodec/ps2/*.h \
	ffmpeg/libavcodec/libpostproc/*.h \
	ffmpeg/libavcodec/libpostproc/*.c \
	ffmpeg/libavcodec/*.c \
	ffmpeg/libavcodec/*.h \
	ffmpeg/libavcodec/Makefile \
	ffmpeg/libavformat/*.c \
	ffmpeg/libavformat/*.h \
	ffmpeg/libavformat/Makefile \
	ffmpeg/tests/Makefile \
	ffmpeg/tests/*.c \
	ffmpeg/tests/*.ref \
	ffmpeg/tests/*.sh \
	ffmpeg/tests/test.conf \
	ffmpeg/vhook/*.c \
	ffmpeg/vhook/Makefile \
	Makefile.in


checkout:
	cvs -d:pserver:anonymous@cvs.ffmpeg.sourceforge.net:/cvsroot/ffmpeg co ffmpeg

# on running make install, make sure make has been run first
install: all
	@true

# we don't install stuff so no need to uninstall
uninstall:
	@true

# override recursive targets
# FIXME: maybe this can be prettier, so it doesn't generate warnings
dvi-recursive:
	true
check-recursive:
	true
installcheck-recursive:
	true
