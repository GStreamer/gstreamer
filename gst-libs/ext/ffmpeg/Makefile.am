# this doesn't trigger the distdir target in the ffmpeg checkout
DIST_SUBDIR=

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
	@patch -p0 -N -r rejects <$(srcdir)/patch/function.patch > /dev/null || true
	@rm -f rejects || true
	@echo done.


SUBDIRS =

EXTRA_DIST = \
	$(PATCHES) \
	Tag \
	ffmpeg/CREDITS \
	ffmpeg/INSTALL \
	ffmpeg/README \
	ffmpeg/VERSION \
	ffmpeg/config.mak \
	ffmpeg/configure \
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
	ffmpeg/libavcodec/*.c \
	ffmpeg/libavcodec/*.h \
	ffmpeg/libavformat/*.c \
	ffmpeg/libavformat/*.h \
	ffmpeg/tests/*.c \
	ffmpeg/tests/*.ref \
	ffmpeg/tests/*.sh \
	ffmpeg/tests/test.conf \
	ffmpeg/vhook/*.c


checkout:
	cvs -d:pserver:anonymous@mplayerhq.hu:/cvsroot/ffmpeg co ffmpeg

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



noinst_LTLIBRARIES = libavformat.la libavcodec.la

if HAVE_CPU_I386
defs_i386 = -DARCH_X86=1 -DHAVE_MMX=1
else
defs_i386 =
endif

if HAVE_CPU_PPC
defs_powerpc = -DARCH_PPC=1
else
defs_powerpc = 
endif

defs = \
 $(defs_i386) \
 $(defs_powerpc) \
 -DTUNECPU=generic \
 -DHAVE_STRPTIME=1 \
 -DHAVE_LRINTF=1 \
 -DCONFIG_ENCODERS=1 \
 -DCONFIG_DECODERS=1 \
 -DCONFIG_PP=1 \
 -DCONFIG_MPEGAUDIO_HP=1 \
 -DCONFIG_HAVE_DLOPEN=1 \
 -DCONFIG_HAVE_DLFCN=1 \
 -DHAVE_MALLOC_H=1 \
 -DHAVE_MEMALIGN=1 \
 -DSIMPLE_IDCT=1 \
 -DCONFIG_RISKY=1

libavformat_la_SOURCES = \
	ffmpeg/libavformat/4xm.c \
	ffmpeg/libavformat/allformats.c \
	ffmpeg/libavformat/amr.c \
	ffmpeg/libavformat/asf.c \
	ffmpeg/libavformat/au.c \
	ffmpeg/libavformat/audio.c \
	ffmpeg/libavformat/avidec.c \
	ffmpeg/libavformat/avienc.c \
	ffmpeg/libavformat/aviobuf.c \
	ffmpeg/libavformat/avio.c \
	ffmpeg/libavformat/crc.c \
	ffmpeg/libavformat/cutils.c \
	ffmpeg/libavformat/dv1394.c \
	ffmpeg/libavformat/dv.c \
	ffmpeg/libavformat/ffm.c \
	ffmpeg/libavformat/file.c \
	ffmpeg/libavformat/flvdec.c \
	ffmpeg/libavformat/flvenc.c \
	ffmpeg/libavformat/framehook.c \
	ffmpeg/libavformat/gif.c \
	ffmpeg/libavformat/gifdec.c \
	ffmpeg/libavformat/grab.c \
	ffmpeg/libavformat/idcin.c \
	ffmpeg/libavformat/idroq.c \
	ffmpeg/libavformat/img.c \
	ffmpeg/libavformat/ipmovie.c \
	ffmpeg/libavformat/jpeg.c \
	ffmpeg/libavformat/mov.c \
	ffmpeg/libavformat/movenc.c \
	ffmpeg/libavformat/mp3.c \
	ffmpeg/libavformat/mpeg.c \
	ffmpeg/libavformat/mpegts.c \
	ffmpeg/libavformat/mpegtsenc.c \
	ffmpeg/libavformat/mpjpeg.c \
	ffmpeg/libavformat/nut.c \
	ffmpeg/libavformat/os_support.c \
	ffmpeg/libavformat/png.c \
	ffmpeg/libavformat/pnm.c \
	ffmpeg/libavformat/psxstr.c \
	ffmpeg/libavformat/raw.c \
	ffmpeg/libavformat/rm.c \
	ffmpeg/libavformat/segafilm.c \
	ffmpeg/libavformat/swf.c \
	ffmpeg/libavformat/utils.c \
	ffmpeg/libavformat/wav.c \
	ffmpeg/libavformat/wc3movie.c \
	ffmpeg/libavformat/westwood.c \
	ffmpeg/libavformat/yuv4mpeg.c \
	ffmpeg/libavformat/yuv.c

libavformat_la_LDFLAGS = -lm -lz
libavformat_la_CFLAGS = $(defs) -Wall -I$(srcdir)/ffmpeg -I$(srcdir)/ffmpeg/libavformat \
	-I$(srcdir)/ffmpeg/libavcodec -DHAVE_AV_CONFIG_H \
	-D_FILE_OFFSET_BITS=64 -D_LARGEFILE_SOURCE -D_GNU_SOURCE


if HAVE_CPU_I386
sources_i386 = \
	ffmpeg/libavcodec/i386/cputest.c \
	ffmpeg/libavcodec/i386/dsputil_mmx.c \
	ffmpeg/libavcodec/i386/fdct_mmx.c \
	ffmpeg/libavcodec/i386/fft_sse.c \
	ffmpeg/libavcodec/i386/idct_mmx.c \
	ffmpeg/libavcodec/i386/motion_est_mmx.c \
	ffmpeg/libavcodec/i386/mpegvideo_mmx.c \
	ffmpeg/libavcodec/i386/simple_idct_mmx.c
else
sources_i386 = 
endif

if HAVE_CPU_PPC
sources_powerpc = \
	ffmpeg/libavcodec/ppc/dsputil_ppc.c \
	ffmpeg/libavcodec/ppc/mpegvideo_ppc.c
# disabled Altivec support for now until someone shows up that make them compile conditionally
#	ffmpeg/libavcodec/ppc/dsputil_altivec.c \
#	ffmpeg/libavcodec/ppc/fft_altivec.c \
#	ffmpeg/libavcodec/ppc/gmc_altivec.c \
#	ffmpeg/libavcodec/ppc/idct_altivec.c \
#	ffmpeg/libavcodec/ppc/mpegvideo_altivec.c \
else
sources_powerpc =
endif

libavcodec_la_DEFS = $(defs)
libavcodec_la_CFLAGS = $(defs) -Wall -I$(srcdir)/ffmpeg -I$(srcdir)/ffmpeg/libavformat \
	-I$(srcdir)/ffmpeg/libavcodec -DHAVE_AV_CONFIG_H \
	-I$(srcdir)/ffmpeg/libavcodec/liba52 \
	-D_FILE_OFFSET_BITS=64 -D_LARGEFILE_SOURCE -D_GNU_SOURCE
libavcodec_la_LDFLAGS = -lm -lz
libavcodec_la_SOURCES = \
	ffmpeg/libavcodec/4xm.c \
	ffmpeg/libavcodec/ac3enc.c \
	ffmpeg/libavcodec/adpcm.c \
	ffmpeg/libavcodec/adx.c \
	ffmpeg/libavcodec/allcodecs.c \
	ffmpeg/libavcodec/apiexample.c \
	ffmpeg/libavcodec/asv1.c \
	ffmpeg/libavcodec/avcodec.c \
	ffmpeg/libavcodec/cabac.c \
	ffmpeg/libavcodec/cinepak.c \
	ffmpeg/libavcodec/cljr.c \
	ffmpeg/libavcodec/common.c \
	ffmpeg/libavcodec/cyuv.c \
	ffmpeg/libavcodec/dpcm.c \
	ffmpeg/libavcodec/dsputil.c \
	ffmpeg/libavcodec/dv.c \
	ffmpeg/libavcodec/error_resilience.c \
	ffmpeg/libavcodec/eval.c \
	ffmpeg/libavcodec/faandct.c \
	ffmpeg/libavcodec/fdctref.c \
	ffmpeg/libavcodec/fft.c \
	ffmpeg/libavcodec/ffv1.c \
	ffmpeg/libavcodec/golomb.c \
	ffmpeg/libavcodec/h263.c \
	ffmpeg/libavcodec/h263dec.c \
	ffmpeg/libavcodec/h264.c \
	ffmpeg/libavcodec/huffyuv.c \
	ffmpeg/libavcodec/idcinvideo.c \
	ffmpeg/libavcodec/imgconvert.c \
	ffmpeg/libavcodec/imgresample.c \
	ffmpeg/libavcodec/indeo3.c \
	ffmpeg/libavcodec/interplayvideo.c \
	ffmpeg/libavcodec/jfdctfst.c \
	ffmpeg/libavcodec/jfdctint.c \
	ffmpeg/libavcodec/jrevdct.c \
	ffmpeg/libavcodec/mace.c \
	ffmpeg/libavcodec/mdct.c \
	ffmpeg/libavcodec/mem.c \
	ffmpeg/libavcodec/mjpeg.c \
	ffmpeg/libavcodec/motion_est.c \
	ffmpeg/libavcodec/mpeg12.c \
	ffmpeg/libavcodec/mpegaudio.c \
	ffmpeg/libavcodec/mpegaudiodec.c \
	ffmpeg/libavcodec/mpegvideo.c \
	ffmpeg/libavcodec/msmpeg4.c \
	ffmpeg/libavcodec/msrle.c \
	ffmpeg/libavcodec/msvideo1.c \
	ffmpeg/libavcodec/opts.c \
	ffmpeg/libavcodec/pcm.c \
	ffmpeg/libavcodec/ra144.c \
	ffmpeg/libavcodec/ra288.c \
	ffmpeg/libavcodec/ratecontrol.c \
	ffmpeg/libavcodec/rational.c \
	ffmpeg/libavcodec/raw.c \
	ffmpeg/libavcodec/resample.c \
	ffmpeg/libavcodec/roqvideo.c \
	ffmpeg/libavcodec/rpza.c \
	ffmpeg/libavcodec/rv10.c \
	ffmpeg/libavcodec/simple_idct.c \
	ffmpeg/libavcodec/svq1.c \
	ffmpeg/libavcodec/utils.c \
	ffmpeg/libavcodec/vcr1.c \
	ffmpeg/libavcodec/vp3.c \
	ffmpeg/libavcodec/vqavideo.c \
	ffmpeg/libavcodec/wmadec.c \
	ffmpeg/libavcodec/xan.c \
	ffmpeg/libavcodec/xvmcvideo.c \
	$(sources_i386) \
	$(sources_powerpc)

