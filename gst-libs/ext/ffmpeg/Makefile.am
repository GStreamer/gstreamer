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


checkout:
	cvs -d:pserver:anonymous@mplayerhq.hu:/cvsroot/ffmpeg co ffmpeg

# on running make install, make sure make has been run first
install: all
	@true

# we don't install stuff so no need to uninstall
uninstall:
	@true



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
libavformat_la_CFLAGS = $(defs) -Wall -I$(srcdir) -I$(srcdir)/ffmpeg \
	-I$(srcdir)/ffmpeg/libavformat -I$(srcdir)/ffmpeg/libavcodec \
	-I$(top_srcdir)/gst-libs/ext/linux/ -DHAVE_AV_CONFIG_H \
	-D_FILE_OFFSET_BITS=64 -D_LARGEFILE_SOURCE -D_GNU_SOURCE

files_i386 = \
	ffmpeg/libavcodec/i386/cputest.c \
	ffmpeg/libavcodec/i386/dsputil_mmx.c \
	ffmpeg/libavcodec/i386/fdct_mmx.c \
	ffmpeg/libavcodec/i386/fft_sse.c \
	ffmpeg/libavcodec/i386/idct_mmx.c \
	ffmpeg/libavcodec/i386/motion_est_mmx.c \
	ffmpeg/libavcodec/i386/mpegvideo_mmx.c \
	ffmpeg/libavcodec/i386/simple_idct_mmx.c
if HAVE_CPU_I386
sources_i386 = $(files_i386)
else
sources_i386 = 
endif

files_powerpc = \
	ffmpeg/libavcodec/ppc/dsputil_ppc.c \
	ffmpeg/libavcodec/ppc/mpegvideo_ppc.c
# disabled Altivec support for now until someone shows up that make them compile conditionally
#	ffmpeg/libavcodec/ppc/dsputil_altivec.c \
#	ffmpeg/libavcodec/ppc/fft_altivec.c \
#	ffmpeg/libavcodec/ppc/gmc_altivec.c \
#	ffmpeg/libavcodec/ppc/idct_altivec.c \
#	ffmpeg/libavcodec/ppc/mpegvideo_altivec.c
if HAVE_CPU_PPC
sources_powerpc = $(files_powerpc)
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

more_libavcodec_files = \
	ffmpeg/libavcodec/mdec.c \
	ffmpeg/libavcodec/motion_est_template.c \
	ffmpeg/libavcodec/svq3.c \
	ffmpeg/libavcodec/wmv2.c 


all_headers = \
	ffmpeg/berrno.h \
	ffmpeg/cmdutils.h \
	ffmpeg/cygwin_inttypes.h \
	ffmpeg/ffserver.h \
	ffmpeg/xvmc_render.h \
	ffmpeg/libavcodec/ac3.h \
	ffmpeg/libavcodec/ac3tab.h \
	ffmpeg/libavcodec/avcodec.h \
	ffmpeg/libavcodec/bswap.h \
	ffmpeg/libavcodec/cabac.h \
	ffmpeg/libavcodec/common.h \
	ffmpeg/libavcodec/dsputil.h \
	ffmpeg/libavcodec/dvdata.h \
	ffmpeg/libavcodec/faandct.h \
	ffmpeg/libavcodec/fastmemcpy.h \
	ffmpeg/libavcodec/golomb.h \
	ffmpeg/libavcodec/h263data.h \
	ffmpeg/libavcodec/h264data.h \
	ffmpeg/libavcodec/imgconvert_template.h \
	ffmpeg/libavcodec/indeo3data.h \
	ffmpeg/libavcodec/mpeg12data.h \
	ffmpeg/libavcodec/mpeg4data.h \
	ffmpeg/libavcodec/mpegaudio.h \
	ffmpeg/libavcodec/mpegaudiodectab.h \
	ffmpeg/libavcodec/mpegaudiotab.h \
	ffmpeg/libavcodec/mpegvideo.h \
	ffmpeg/libavcodec/msmpeg4data.h \
	ffmpeg/libavcodec/oggvorbis.h \
	ffmpeg/libavcodec/ra144.h \
	ffmpeg/libavcodec/ra288.h \
	ffmpeg/libavcodec/rational.h \
	ffmpeg/libavcodec/simple_idct.h \
	ffmpeg/libavcodec/sp5x.h \
	ffmpeg/libavcodec/svq1_cb.h \
	ffmpeg/libavcodec/svq1_vlc.h \
	ffmpeg/libavcodec/vp3data.h \
	ffmpeg/libavcodec/wmadata.h \
	ffmpeg/libavcodec/i386/dsputil_mmx_avg.h \
	ffmpeg/libavcodec/i386/dsputil_mmx_rnd.h \
	ffmpeg/libavcodec/i386/mmx.h \
	ffmpeg/libavcodec/ppc/dsputil_altivec.h \
	ffmpeg/libavcodec/ppc/dsputil_ppc.h \
	ffmpeg/libavcodec/ppc/gcc_fixes.h \
	ffmpeg/libavformat/avformat.h \
	ffmpeg/libavformat/avi.h \
	ffmpeg/libavformat/avio.h \
	ffmpeg/libavformat/barpainet.h \
	ffmpeg/libavformat/dv.h \
	ffmpeg/libavformat/dv1394.h \
	ffmpeg/libavformat/framehook.h \
	ffmpeg/libavformat/mpegts.h \
	ffmpeg/libavformat/os_support.h \
	ffmpeg/libavformat/rtp.h \
	ffmpeg/libavformat/rtsp.h \
	ffmpeg/libavformat/rtspcodes.h 

DISTCLEANFILES = \
	ffmpeg/config.h \
	ffmpeg/config.mak 

EXTRA_DIST = \
	$(PATCHES) \
	Tag \
	ffmpeg/CREDITS \
	ffmpeg/INSTALL \
	ffmpeg/README \
	ffmpeg/configure \
	ffmpeg/ffmpeg.c \
	ffmpeg/ffserver.c \
	$(libavcodec_la_SOURCES) \
	$(more_libavcodec_files) \
	$(libavformat_la_SOURCES) \
	$(files_i386) \
	$(files_powerpc) \
	$(all_headers) 
