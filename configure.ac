dnl autoconf configuration file for plugins 
AC_INIT
dnl FIXME take something else ?
AC_CONFIG_SRCDIR([COPYING])

AC_CANONICAL_TARGET([])

AM_CONFIG_HEADER(config.h)

GST_VERSION_MAJOR=0
GST_VERSION_MINOR=3
GST_VERSION_MICRO=11

dnl we use a datestamped release tag during CVS development
GST_VERSION_RELEASE=`date +%Y%m%d`
dnl we use a normal release tag increased manually when releasing
dnl GST_VERSION_RELEASE=1

GST_VERSION=$GST_VERSION_MAJOR.$GST_VERSION_MINOR.$GST_VERSION_MICRO

PACKAGE=gst-plugins
VERSION=$GST_VERSION

AC_DEFINE_UNQUOTED(PACKAGE, "$PACKAGE")
AC_DEFINE_UNQUOTED(VERSION, "$VERSION")
AC_SUBST(PACKAGE)
AC_SUBST(VERSION)
AC_DEFINE_UNQUOTED(GST_VERSION_RELEASE, "$GST_VERSION_RELEASE")
AC_SUBST(GST_VERSION_RELEASE)

dnl libtool
GST_CURRENT=2
GST_REVISION=0
GST_AGE=0
GST_LIBVERSION=$GST_CURRENT:$GST_REVISION:$GST_AGE

AM_INIT_AUTOMAKE($PACKAGE, $VERSION)
dnl Add parameters for aclocal
dnl (This must come after AM_INIT_AUTOMAKE, since it modifies ACLOCAL)
ACLOCAL="$ACLOCAL $ACLOCAL_FLAGS"

AC_SUBST(GST_VERSION_MAJOR)
AC_SUBST(GST_VERSION_MINOR)
AC_SUBST(GST_VERSION_MICRO)
AC_SUBST(GST_VERSION)

AC_SUBST(GST_CURRENT)
AC_SUBST(GST_REVISION)
AC_SUBST(GST_AGE)
AC_SUBST(GST_LIBVERSION)

AM_MAINTAINER_MODE

AC_PROG_CC
AM_PROG_CC_STDC
AM_PROG_AS
AS="${CC}"
AC_PROG_CXX
AC_PROG_CXXCPP
AC_ISC_POSIX

dnl We disable static building for development, for time savings
dnl *NOTE*: dnl this line before release, so release does static too
AM_DISABLE_STATIC
AC_LIBTOOL_DLOPEN
AM_PROG_LIBTOOL

AC_HEADER_STDC([])

dnl ###########################################
dnl # Super Duper options for plugin building #
dnl ###########################################

dnl experimental plugins; stuff that hasn't had the dust settle yet
dnl read 'builds, but might not work'
GST_CHECK_FEATURE(EXPERIMENTAL, [enable building of experimental plugins],,
[HAVE_EXPERIMENTAL=yes],disabled,
[
  AC_MSG_WARN(building experimental plugins)
  USE_SMOOTHWAVE="yes"
],[
  AC_MSG_NOTICE(not building experimental plugins)
  USE_SMOOTHWAVE="no"
])

dnl broken plugins; stuff that doesn't seem to build at the moment
GST_CHECK_FEATURE(BROKEN, [enable building of broken plugins],,
HAVE_BROKEN=yes,disabled,
[  
  AC_MSG_WARN(building broken plugins)
  USE_VGA="yes"
  USE_XMMS="yes"
  dnl AC_MSG_NOTICE(actually there are no broken plugins at the moment)
],[
  USE_VGA="no"
  USE_XMMS="no"
  AC_MSG_NOTICE(not building broken plugins)
])

dnl ##############################
dnl # Do automated configuration #
dnl ##############################

dnl Check for tools:
dnl ================

dnl modify pkg-config path
AC_ARG_WITH(pkg-config-path, 
   AC_HELP_STRING([--with-pkg-config-path],[colon-separated list of pkg-config(1) dirs]),
   [export PKG_CONFIG_PATH=${withval}])

dnl Check for nasm
AC_PATH_PROG(NASM_PATH, nasm, no)
AC_SUBST(NASM_PATH)
if test x$NASM_PATH = xno; then
  AC_MSG_WARN(Couldn't find nasm)
  HAVE_NASM="no"
else
  AC_DEFINE(HAVE_NASM, 1, [Define if NASM, the netwide assembler, is available])
  HAVE_NASM="yes"
fi

dnl check for gstreamer; uninstalled is selected preferentially -- see pkg-config(1)
PKG_CHECK_MODULES(GST, gstreamer >= 0.3.1, HAVE_GST="yes", HAVE_GST="no")

if test "x$HAVE_GST" = "xno"; then
  AC_MSG_ERROR(no GStreamer found)
fi

dnl Set up conditionals for (target) architecture:
dnl ==============================================

dnl Determine CPU
case "x${target_cpu}" in
  xi?86 | k?) HAVE_CPU_I386=yes
              AC_DEFINE(HAVE_CPU_I386, 1, [Define if the target CPU is an x86])
              dnl FIXME could use some better detection
              dnl       (ie CPUID)
              case "x${target_cpu}" in
                xi386 | xi486) ;;
                *)             AC_DEFINE(HAVE_RDTSC) ;;
              esac ;;
  xpowerpc)   HAVE_CPU_PPC=yes
              AC_DEFINE(HAVE_CPU_PPC, 1, [Define if the target CPU is a PPC]) ;;
  xalpha)     HAVE_CPU_ALPHA=yes
              AC_DEFINE(HAVE_CPU_ALPHA, 1, [Define if the target CPU is an Alpha]) ;;
  xarm*)      HAVE_CPU_ARM=yes
              AC_DEFINE(HAVE_CPU_ARM, 1, [Define if the target CPU is an ARM]) ;;
  xsparc*)    HAVE_CPU_SPARC=yes
              AC_DEFINE(HAVE_CPU_SPARC, 1, [Define if the target CPU is a PPC]) ;;
  xmips*)     HAVE_CPU_MIPS=yes
              AC_DEFINE(HAVE_CPU_MIPS, 1, [Define if the target CPU is a MIPS]) ;;
  xhppa*)     HAVE_CPU_HPPA=yes
              AC_DEFINE(HAVE_CPU_HPPA, 1, [Define if the target CPU is a HPPA]) ;;
esac

dnl Determine endianness
AC_C_BIGENDIAN

dnl Check for essential libraries first:
dnl ====================================

if test -n "`echo $GST_CFLAGS | grep USE_GLIB2`"; then
  USE_GLIB2=yes
else
  USE_GLIB2=no
fi

dnl needed for some plugins and demos, now's a nice time to check for gtk
if test x$USE_GLIB2 = xno; then
  AM_PATH_GTK(1.2.0,,
              AC_MSG_ERROR(Cannot find gtk: Is gtk-config in path?))
  HAVE_GTK=yes
  # we have to have it
else
  PKG_CHECK_MODULES(GTK2, gtk+-2.0, HAVE_GTK=yes, HAVE_GTK=no)

  GTK_CFLAGS=$GTK2_CFLAGS
  GTK_LIBS=$GTK2_LIBS
fi
AC_SUBST(GTK_LIBS)
AC_SUBST(GTK_CFLAGS)

dnl Check for X11 extensions
AC_PATH_XTRA
if test "-DX_DISPLAY_MISSING" = "$X_CFLAGS"; then
  AC_MSG_NOTICE([cannot find X11, the build system needs fixage])
fi
AC_SUBST(X_CFLAGS)
AC_SUBST(X_PRE_LIBS)
AC_SUBST(X_EXTRA_LIBS)
AC_SUBST(X_LIBS)

dnl ==========================================================================
dnl ============================= gst plugins ================================
dnl ==========================================================================
GST_PLUGIN_LDFLAGS='-module -avoid-version'
AC_SUBST(GST_PLUGIN_LDFLAGS)

GST_PLUGINS_ALL="\
        ac3parse adder audioscale auparse avi chart\
        cutter deinterlace flx intfloat law level\
        median mpeg1enc mpeg1sys mpeg1videoparse mpeg2enc mpeg2sub\
        mpegaudio mpegaudioparse mpegstream mpegtypes\
        passthrough playondemand rtjpeg silence sine\
        smooth spectrum speed stereo stereomono\
        synaesthesia udp videoscale volenv volume vumeter wavparse y4m"

AC_SUBST(GST_PLUGINS_ALL)

GST_PLUGINS_SELECTED=""

AC_ARG_WITH(plugins,
    AC_HELP_STRING([--with-plugins],[comma-separated list of plugins to compile]),
    [for i in `echo $withval | tr , ' '`; do
        if test -n `echo $i | grep $GST_PLUGINS_ALL`; then
            GST_PLUGINS_SELECTED="$GST_PLUGINS_SELECTED $i"
        else
            echo "plugin $i not recognized, ignoring..."
        fi
    done],
    [GST_PLUGINS_SELECTED=$GST_PLUGINS_ALL])

AC_SUBST(GST_PLUGINS_SELECTED)

dnl ==========================================================================
dnl ============================= sys plugins ================================
dnl ==========================================================================


dnl *** OSS audio ***
translit(dnm, m, l) AM_CONDITIONAL(USE_OSS, true)
GST_CHECK_FEATURE(OSS, [OSS audio], osssrc osssink, [
  AC_CHECK_HEADER(sys/soundcard.h, HAVE_OSS="yes", HAVE_OSS="no")
])

dnl *** QuickCam ***
translit(dnm, m, l) AM_CONDITIONAL(USE_QCAM, true)
GST_CHECK_FEATURE(QCAM, [QuickCam], qcamsrc, [
dnl this one is for hadess, no qcam when no nasm found
  if test "x$HAVE_NASM" = "xno";
  then
    HAVE_QCAM="no"
    AC_MSG_WARN([QuickCam needs nasm])
  else
    HAVE_QCAM="yes"
  fi
])

dnl *** Video 4 Linux ***
translit(dnm, m, l) AM_CONDITIONAL(USE_V4L, true)
GST_CHECK_FEATURE(V4L, [Video 4 Linux], v4lsrc, [
  AC_CHECK_DECL(VID_TYPE_MPEG_ENCODER, HAVE_V4L="yes", HAVE_V4L="no", [#include <linux/videodev.h>])
])

dnl *** Video CD ***
translit(dnm, m, l) AM_CONDITIONAL(USE_VCD, true)
GST_CHECK_FEATURE(VCD, [Video CD], vcdsrc, [
  AC_CHECK_HEADER(linux/cdrom.h, HAVE_VCD="yes", HAVE_VCD="no")
])

dnl *** VGA ***
translit(dnm, m, l) AM_CONDITIONAL(USE_VGA, true)
GST_CHECK_FEATURE(VGA, [VGA], vgavideosink, [
  AC_CHECK_HEADER(asm/vga.h, HAVE_VGA="yes", HAVE_VGA="no")
])

dnl *** XVideo ***
dnl Look for the PIC library first, Debian requires it.
dnl Check debian-devel archives for gory details.
dnl 20020110:
dnl At the moment XFree86 doesn't distribute shared libXv due
dnl to unstable API.  On many platforms you CAN NOT link a shared
dnl lib to a static non-PIC lib.  This is what the xvideo GStreamer
dnl plugin wants to do.  So Debian distributes a PIC compiled
dnl version of the static lib for plugins to link to when it is
dnl inappropriate to link the main application to libXv directly.
dnl FIXME: add check if this platform can support linking to a
dnl        non-PIC libXv, if not then don not use Xv.
dnl FIXME: perhaps warn user if they have a shared libXv since
dnl        this is an error until XFree86 starts shipping one
translit(dnm, m, l) AM_CONDITIONAL(USE_XVIDEO, true)
GST_CHECK_FEATURE(XVIDEO, [X11 XVideo extensions], xvideosink, [
  dnl check for PIC static lib
  GST_CHECK_LIBHEADER(XVIDEO, Xv_pic, XvQueryExtension, $X_LIBS $X_PRE_LIBS -lXext -lX11 $X_EXTRA_LIBS, X11/extensions/Xvlib.h, XVIDEO_LIBS="-lXv_pic -lXext")
  if test x$HAVE_XVIDEO = xno; then
    dnl PIC lib not found, check for regular lib
    GST_CHECK_LIBHEADER(XVIDEO, Xv, XvQueryExtension, $X_LIBS $X_PRE_LIBS -lXext -lX11 $X_EXTRA_LIBS, X11/extensions/Xvlib.h, XVIDEO_LIBS="-lXv -lXext")
  fi
  AC_SUBST(XVIDEO_LIBS)
])

dnl Next, check for the optional libraries:
dnl These are all libraries used in building plugins
dnl ================================================
dnl let's try and sort them alphabetically, shall we ?

echo
echo
AC_MSG_NOTICE(Checking for plugin libraries)

dnl *** a52dec ***
translit(dnm, m, l) AM_CONDITIONAL(USE_A52DEC, true)
GST_CHECK_FEATURE(A52DEC, [a52dec], a52dec, [
  GST_CHECK_LIBHEADER(A52DEC, a52, a52_init, -lm, a52dec/a52.h, A52DEC_LIBS="-la52")
])

dnl *** aalib ***
translit(dnm, m, l) AM_CONDITIONAL(USE_AALIB, true)
GST_CHECK_FEATURE(AALIB, [aasink plugin], aasink, [
  AM_PATH_AALIB(, HAVE_AALIB=yes, HAVE_AALIB=no)
])

dnl *** alsa ***
translit(dnm, m, l) AM_CONDITIONAL(USE_ALSA, true)
GST_CHECK_FEATURE(ALSA, [alsa plugins], gstalsa, [
  AM_PATH_ALSA(0.9.0, HAVE_ALSA=yes, HAVE_ALSA=no)
])

dnl *** arts ***

dnl if mcopidl can't be found there's no use in compiling it
AC_CHECK_PROG(MCOPIDL, mcopidl, yes, no)
if test "xHAVE_MCOPIDL" = "xno";
then
  USE_ARTS=no
fi

translit(dnm, m, l) AM_CONDITIONAL(USE_ARTS, true)
GST_CHECK_FEATURE(ARTS, [arts plugins], arts, [
  AM_PATH_ARTS(, HAVE_ARTS=yes, HAVE_ARTS=no)
])
dnl if mcopidl can't be found there's no use in compiling it
AC_PATH_PROG(MCOPIDL, mcopidl, yes, no)
if test "xHAVE_MCOPIDL" = "xno";
then
  USE_ARTS=NO
fi

dnl *** artsc ***
translit(dnm, m, l) AM_CONDITIONAL(USE_ARTSC, true)
GST_CHECK_FEATURE(ARTSC, [artsd plugins], artsdsink, [
  GST_CHECK_ARTSC()
])

dnl *** audiofile ***
dnl this check uses the GST_CHECK_CONFIGPROG macro
translit(dnm, m, l) AM_CONDITIONAL(USE_AUDIOFILE, true)
GST_CHECK_FEATURE(AUDIOFILE, [audiofile], afsink afsrc, [
  translit(dnm, m, l) AC_SUBST(AUDIOFILE_LIBS)
  translit(dnm, m, l) AC_SUBST(AUDIOFILE_CFLAGS)
  GST_CHECK_CONFIGPROG(AUDIOFILE, audiofile-config)
])

dnl *** avifile ***
dnl this check uses the GST_CHECK_CONFIGPROG macro
dnl this check needs more fixing
dnl translit(dnm, m, l) AC_SUBST(AVIFILE_LIBS)
translit(dnm, m, l) AM_CONDITIONAL(USE_AVIFILE, true)
GST_CHECK_FEATURE(AVIFILE, [avifile], windec winenc, [
  translit(dnm, m, l) AC_SUBST(AVIFILE_CFLAGS)
  GST_CHECK_CONFIGPROG(AVIFILE, avifile-config)
  AVIFILE_LIBS="$AVIFILE_LIBS -lstdc++"
  AC_SUBST(AVIFILE_LIBS)
])

dnl *** CDParanoia ***
translit(dnm, m, l) AM_CONDITIONAL(USE_CDPARANOIA, true)
GST_CHECK_FEATURE(CDPARANOIA, [CDParanoia], cdparanoia, [
  GST_CHECK_LIBHEADER(CDPARANOIA, cdda_interface, cdda_open, -lm, cdda_interface.h, CDPARANOIA_LIBS="-lcdda_interface -lcdda_paranoia")
  AC_SUBST(CDPARANOIA_LIBS)
])
dnl FIXME : add second check somehow if that is necessary
dnl AC_CHECK_LIB(cdda_paranoia, paranoia_init, : , HAVE_CDPARANOIA=no, -lcdda_interface )
dnl AC_CHECK_HEADER(cdda_paranoia.h, :, HAVE_CDPARANOIA=no)

dnl *** dv ***
translit(dnm, m, l) AM_CONDITIONAL(USE_DV, true)
GST_CHECK_FEATURE(DV, [dv library], dv, [
  GST_CHECK_LIBHEADER(DV, dv, dv_init, -lm $GST_CFLAGS $GST_LIBS, libdv/dv.h, DV_LIBS="-ldv")
  AC_SUBST(DV_LIBS)
])

dnl *** dvdread ***
translit(dnm, m, l) AM_CONDITIONAL(USE_DVDREAD, true)
GST_CHECK_FEATURE(DVDREAD, [dvdread library], dvdsrc, [
  GST_CHECK_LIBHEADER(DVDREAD, dvdread, DVDOpen, , dvdread/dvd_reader.h, DVDREAD_LIBS="-ldvdread")
  AC_SUBST(DVDREAD_LIBS)
])

dnl **** ESound ****
translit(dnm, m, l) AM_CONDITIONAL(USE_ESD, true)
GST_CHECK_FEATURE(ESD, [esound plugins], esdsrc esdsink, [
  AM_PATH_ESD(0.2.12, HAVE_ESD=yes, HAVE_ESD=no)
])

dnl **** festival ****
translit(dnm, m, l) AM_CONDITIONAL(USE_FESTIVAL, true)
GST_CHECK_FEATURE(FESTIVAL, [festival plugins], festivalsrc, [
  dnl NOTE: just using local net connection now, add this lib check
  dnl       in the future if needed
  dnl AC_LANG_PUSH(C++)
  dnl dnl FIXME: took out func to check for
  dnl dnl This check puts festival_tidy_up in extern "C".
  dnl dnl But, at least on Debian as of 20020110, it is compiled with name
  dnl dnl mangling C++ nonsense and symbols can't resolve
  dnl dnl GST_CHECK_LIBHEADER(FESTIVAL, Festival, festival_tidy_up, , festival/festival.h, FESTIVAL_LIBS="-lFestival")
  dnl GST_CHECK_LIBHEADER(FESTIVAL, Festival, , , festival/festival.h, FESTIVAL_LIBS="-lFestival")
  dnl AC_LANG_POP(C++)
  dnl AC_SUBST(FESTIVAL_LIBS)
  HAVE_FESTIVAL=yes
])

dnl *** FLAC ***
translit(dnm, m, l) AM_CONDITIONAL(USE_FLAC, true)
GST_CHECK_FEATURE(FLAC, [FLAC lossless audio], flacenc flacdec, [
  GST_CHECK_LIBHEADER(FLAC, FLAC, FLAC__stream_decoder_new, -lm, FLAC/all.h, FLAC_LIBS="-lFLAC")
  AC_SUBST(FLAC_LIBS)
])

dnl *** Gnome VFS ***
dnl this check uses the GST_CHECK_CONFIGPROG macro with gnome-config vfs
translit(dnm, m, l) AM_CONDITIONAL(USE_GNOME_VFS, true)
GST_CHECK_FEATURE(GNOME_VFS, [Gnome VFS], gnomevfssrc, [
translit(dnm, m, l) AC_SUBST(GNOME_VFS_LIBS)
translit(dnm, m, l) AC_SUBST(GNOME_VFS_CFLAGS)
GST_CHECK_CONFIGPROG(GNOME_VFS, gnome-config vfs)
])

dnl *** gsm ***
translit(dnm, m, l) AM_CONDITIONAL(USE_GSM, true)
GST_CHECK_FEATURE(GSM, [GSM library], gsmenc gsmdec, [
  GST_CHECK_LIBHEADER(GSM, gsm, gsm_create, , gsm.h, GSM_LIBS="-lgsm")
  if test $HAVE_GSM != "yes"; then
    GST_CHECK_LIBHEADER(GSM, gsm, gsm_create, , gsm/gsm.h, GSM_LIBS="-lgsm")
    if test $HAVE_GSM = "yes"; then 
      AC_DEFINE(GSM_HEADER_IN_SUBDIR)
    fi
  fi
  AC_SUBST(GSM_LIBS)
])

dnl *** Hermes ***
translit(dnm, m, l) AM_CONDITIONAL(USE_HERMES, true)
GST_CHECK_FEATURE(HERMES, [Hermes library], colorspace, [
  GST_CHECK_LIBHEADER(HERMES, Hermes, Hermes_ConverterInstance, , Hermes/Hermes.h, HERMES_LIBS="-lHermes")
], AC_SUBST(HERMES_LIBS))

dnl *** http ***
translit(dnm, m, l) AM_CONDITIONAL(USE_HTTP, true)
GST_CHECK_FEATURE(HTTP, [http plugins], gsthttpsrc, [
  dnl FIXME: need to check for header
  GHTTP_LIBS=
  GST_HTTPSRC_GET_TYPE=
  if test x$USE_GLIB2 = xyes; then
    AC_MSG_WARN(ghttp disabled for glib2.0)
  else
    AC_CHECK_LIB(ghttp, ghttp_request_new,
      [HTTP_LIBS="-lghttp"
       GST_HTTPSRC_GET_TYPE="gst_httpsrc_get_type"
       HAVE_HTTP=yes
      ], :, $LIBS)
  fi
  AC_SUBST(HTTP_LIBS)
  AC_SUBST(GST_HTTPSRC_GET_TYPE)
])

dnl *** lame ***
translit(dnm, m, l) AM_CONDITIONAL(USE_LAME, true)
GST_CHECK_FEATURE(LAME, [lame mp3 encoder library], lame, [
GST_CHECK_LIBHEADER(LAME, mp3lame, lame_init, -lm, lame/lame.h, LAME_LIBS="-lmp3lame")
])
AC_SUBST(LAME_LIBS)


dnl FIXME : check if these CPP flags can be dealt with otherwise
dnl libdvcheck_save_CPPFLAGS="$CPPFLAGS"
dnl CPPFLAGS="$CPPFLAGS $GST_CFLAGS"
dnl AC_CHECK_HEADER(libdv/dv.h, :, HAVE_LIBDV=no)
dnl CPPFLAGS="$libdvcheck_save_CPPFLAGS"

dnl *** jpeg ***
dnl FIXME: we could use header checks here as well IMO
translit(dnm, m, l) AM_CONDITIONAL(USE_JPEG, true)
GST_CHECK_FEATURE(JPEG, [jpeg], jpegenc jpegdec, [
  AC_CHECK_LIB(jpeg, jpeg_set_defaults, HAVE_JPEG="yes", HAVE_JPEG="no")
  JPEG_LIBS="-ljpeg"
  AC_SUBST(JPEG_LIBS)
])

dnl *** mad ***
dnl FIXME: we could use header checks here as well IMO
translit(dnm, m, l) AM_CONDITIONAL(USE_MAD, true)
GST_CHECK_FEATURE(MAD, [mad mp3 decoder], mad, [
  AC_CHECK_LIB(mad, mad_decoder_finish, HAVE_MAD="yes" MAD_LIBS="-lmad")
])
AC_SUBST(MAD_LIBS)

dnl *** mikmod ***
translit(dnm, m, l) AM_CONDITIONAL(USE_MIKMOD, true)
GST_CHECK_FEATURE(MIKMOD, [mikmod plugin], mikmod, [
  AM_PATH_LIBMIKMOD(, HAVE_MIKMOD=yes, HAVE_MIKMOD=no)
  AC_SUBST(MIKMOD_LIBS, "$LIBMIKMOD_LIBS")
  AC_SUBST(MIKMOD_CFLAGS, "$LIBMIKMODCFLAGS")
])

dnl *** mjpegtools ***
translit(dnm, m, l) AM_CONDITIONAL(USE_MJPEGTOOLS, true)
GST_CHECK_FEATURE(MJPEGTOOLS, [mjpegtools], jpegmmxenc jpegmmxdec, [
  PKG_CHECK_MODULES(MJPEGTOOLS, mjpegtools, HAVE_MJPEGTOOLS="yes", HAVE_MJPEGTOOLS="no")
  AC_SUBST(MJPEGTOOLS_LIBS)
  AC_SUBST(MJPEGTOOLS_CFLAGS)
])

dnl *** mpeg2dec ***
translit(dnm, m, l) AM_CONDITIONAL(USE_MPEG2DEC, true)
GST_CHECK_FEATURE(MPEG2DEC, [mpeg2dec], mpeg2dec, [ GST_CHECK_MPEG2DEC() ])

dnl *** openquicktime ***
translit(dnm, m, l) AM_CONDITIONAL(USE_OPENQUICKTIME, true)
GST_CHECK_FEATURE(OPENQUICKTIME, [Open Quicktime], quicktime_parser quicktime_decoder quicktime_demux,[
  GST_CHECK_LIBHEADER(OPENQUICKTIME, openquicktime, quicktime_init,, openquicktime/openquicktime.h, OPENQUICKTIME_LIBS="-lopenquicktime")
  AC_SUBST(OPENQUICKTIME_LIBS)
])

dnl *** raw1394 ***
translit(dnm, m, l) AM_CONDITIONAL(USE_RAW1394, true)
GST_CHECK_FEATURE(RAW1394, [raw1394 library], dv1394src, [
  GST_CHECK_LIBHEADER(RAW1394, raw1394, raw1394_get_handle,, libraw1394/raw1394.h, RAW1394_LIBS="-raw1394")
  AC_SUBST(RAW1394_LIBS)
])

dnl *** rtp ***
dnl FIXME : adapt and make it work
translit(dnm, m, l) AM_CONDITIONAL(USE_RTP, true)
GST_CHECK_FEATURE(RTP, [RTP library], rtpenc rtpdec,[
  AC_CHECK_LIB(rtp, rtp_packet_new_take_data, HAVE_RTP=yes, HAVE_RTP=no, $GST_CFLAGS $GST_LIBS)
  RTP_LIBS="-lrtp"
  AC_SUBST(RTP_LIBS)
])


dnl FIXME header check needs to use GLIB_CFLAGS in order to succeed for rtp
dnl can use GST_* which should have GLIB_* info
dnl AC_CHECK_HEADERS(rtp/rtp.h, HAVE_LIBRTP=yes, HAVE_LIBRTP=no)
dnl AC_CHECK_HEADERS(rtp/rtp-packet.h, :, HAVE_LIBRTP=no)
dnl AC_CHECK_HEADERS(rtp/rtcp-packet.h, :, HAVE_LIBRTP=no)
dnl AC_CHECK_HEADERS(rtp/rtp-audio.h, :, HAVE_LIBRTP=no)

dnl *** SDL ***
translit(dnm, m, l) AM_CONDITIONAL(USE_SDL, true)
GST_CHECK_FEATURE(SDL, [SDL plugin], sdlvideosink, [
 AM_PATH_SDL(, HAVE_SDL=yes, HAVE_SDL=no)
])

dnl *** shout ***
translit(dnm, m, l) AM_CONDITIONAL(USE_SHOUT, true)
GST_CHECK_FEATURE(SHOUT, [shout plugin], icecastsend, [
  GST_CHECK_LIBHEADER(SHOUT, shout, shout_init_connection,, shout/shout.h, SHOUT_LIBS="-lshout")
  AC_SUBST(SHOUT_LIBS)
])

dnl *** sidplay ***
translit(dnm, m, l) AM_CONDITIONAL(USE_SIDPLAY, true)
GST_CHECK_FEATURE(SIDPLAY, [sidplay plugin], sidplay, [
  GST_PATH_SIDPLAY()
])

dnl *** smoothwave ***
translit(dnm, m, l) AM_CONDITIONAL(USE_SMOOTHWAVE, true)
GST_CHECK_FEATURE(SMOOTHWAVE, [smoothwave plugin], smoothwave, [
  if test $HAVE_GTK = "yes"; then HAVE_SMOOTHWAVE=yes; fi;
])

dnl *** vorbis ***
dnl AM_PATH_VORBIS only takes two options
translit(dnm, m, l) AM_CONDITIONAL(USE_VORBIS, true)
GST_CHECK_FEATURE(VORBIS, [vorbis plugin], vorbisenc vorbisdec, [
  AM_PATH_VORBIS(HAVE_VORBIS=yes, HAVE_VORBIS=no)
])

dnl *** XMMS ***
translit(dnm, m, l) AM_CONDITIONAL(USE_XMMS, true)
GST_CHECK_FEATURE(XMMS, [xmms plugin], xmms, [
  AM_PATH_XMMS(0.1.0, HAVE_XMMS=yes, HAVE_XMMS=no)
])

dnl Check for atomic.h
dnl Note: use AC_CHECK_HEADER not AC_CHECK_HEADERS, because the latter
dnl defines the wrong default symbol as well (HAVE_ASM_ATOMIC_H)
AC_CHECK_HEADER(asm/atomic.h, HAVE_ATOMIC_H=yes, HAVE_ATOMIC_H=no)
dnl Do a compile to check that it has atomic_set (eg, linux 2.0 didn't)
if test x$HAVE_ATOMIC_H = xyes; then
  AC_TRY_RUN([
#include "asm/atomic.h"
main() { atomic_t t; atomic_set(&t,0); atomic_inc(&t); atomic_add(1,&t);return 0;}
  ],, [
    # Not successful
    if test x$HAVE_ATOMIC_H = xyes; then
      AC_MSG_WARN(Atomic reference counting is out of date: doing without.)
    fi
    HAVE_ATOMIC_H=no
  ], [
    # Cross compiling
    AC_MSG_RESULT(yes)
    AC_MSG_WARN(Can't check properly for atomic reference counting.  Assuming OK.)
  ])
fi

dnl ######################################################################
dnl # Check command line parameters, and set shell variables accordingly #
dnl ######################################################################

AC_ARG_ENABLE(libmmx,
[  --enable-libmmx              use libmmx, if available],
[case "${enableval}" in
  yes) USE_LIBMMX=$HAVE_LIBMMX ;;
  no)  USE_LIBMMX=no ;;
  *) AC_MSG_ERROR(bad value ${enableval} for --enable-libmmx) ;;
esac], 
[USE_LIBMMX=$HAVE_LIBMMX]) dnl Default value

AC_ARG_ENABLE(atomic,
[  --enable-atomic              use atomic reference counting header],
[case "${enableval}" in
  yes) USE_ATOMIC_H=$HAVE_ATOMIC_H;;
  noset) USE_ATOMIC_H=$HAVE_ATOMIC_H;;
  no)  USE_ATOMIC_H=no;;
  *) AC_MSG_ERROR(bad value ${enableval} for --enable-atomic) ;;
esac], 
[USE_ATOMIC_H=$HAVE_ATOMIC_H]) dnl Default value

AC_ARG_ENABLE(profiling,
[  --enable-profiling           adds -pg to compiler commandline, for profiling],
[case "${enableval}" in
  yes) USE_PROFILING=yes ;;
  no)  UES_PROFILING=no ;;
  *) AC_MSG_ERROR(bad value ${enableval} for --enable-profiling) ;;
esac], 
[USE_PROFILING=no]) dnl Default value

AC_ARG_ENABLE(tests,
[  --disable-tests              disable building test apps],
[case "${enableval}" in
  yes) BUILD_TESTS=yes ;;
  no)  BUILD_TESTS=no ;;
  *) AC_MSG_ERROR(bad value ${enableval} for --disable-tests) ;;
esac], 
[BUILD_TESTS=yes]) dnl Default value

AC_ARG_ENABLE(examples,
[  --disable-examples           disable building examples],
[case "${enableval}" in
  yes) BUILD_EXAMPLES=yes ;;
  no)  BUILD_EXAMPLES=no ;;
  *) AC_MSG_ERROR(bad value ${enableval} for --disable-examples) ;;
esac], 
[BUILD_EXAMPLES=yes]) dnl Default value

dnl ################################################
dnl # Set defines according to variables set above #
dnl ################################################


if test "x$USE_LIBMMX" = xyes; then
  AC_DEFINE(HAVE_LIBMMX, 1, [Define if libmmx is available])
fi

if test "x$USE_ATOMIC_H" = xyes; then
  AC_DEFINE(HAVE_ATOMIC_H, 1, [Define if atomic.h header file is available])
fi

dnl if test "x$USE_DEBUG" = xyes; then
dnl   CFLAGS="$CFLAGS -g"
dnl fi

if test "x$USE_PROFILING" = xyes; then
dnl  CFLAGS="$CFLAGS -pg -fprofile-arcs"
  FOMIT_FRAME_POINTER=""
else
  FOMIT_FRAME_POINTER="-fomit-frame-pointer"
fi

dnl
dnl AC_SUBST(FOMIT_FRAME_POINTER)
dnl

dnl #############################
dnl # Set automake conditionals #
dnl #############################

dnl These should be "USE_*" instead of "HAVE_*", but some packages expect
dnl HAVE_ and it is likely to be easier to stick with the old name
AM_CONDITIONAL(HAVE_CPU_I386,       test "x$HAVE_CPU_I386" = "xyes")
AM_CONDITIONAL(HAVE_CPU_PPC,        test "x$HAVE_CPU_PPC" = "xyes")
AM_CONDITIONAL(HAVE_CPU_ALPHA,      test "x$HAVE_CPU_ALPHA" = "xyes")
AM_CONDITIONAL(HAVE_CPU_ARM,        test "x$HAVE_CPU_ARM" = "xyes")
AM_CONDITIONAL(HAVE_CPU_SPARC,      test "x$HAVE_CPU_SPARC" = "xyes")
AM_CONDITIONAL(HAVE_LIBMMX,         test "x$USE_LIBMMX" = "xyes")

AM_CONDITIONAL(HAVE_ATOMIC_H,       test "x$USE_ATOMIC_H" = "xyes")

AM_CONDITIONAL(EXPERIMENTAL,        test "$EXPERIMENTAL" = "$xyes")
AM_CONDITIONAL(BROKEN,              test "$BROKEN" = "$xyes")

AM_CONDITIONAL(HAVE_LIBMIKMOD,      test "x$HAVE_LIBMIKMOD" = "xyes")
AM_CONDITIONAL(HAVE_LIBSDL,         test "x$HAVE_LIBSDL" = "xyes")
AM_CONDITIONAL(HAVE_NASM,           test "x$HAVE_NASM" = "xyes")
AM_CONDITIONAL(HAVE_LIBGLADE_GNOME, test "x$HAVE_LIBGLADE_GNOME" = "xyes")
AM_CONDITIONAL(HAVE_GNOME,          test "x$HAVE_GNOME" = "xyes")
AM_CONDITIONAL(HAVE_GTK,            test "x$HAVE_GTK" = "xyes")
AM_CONDITIONAL(HAVE_GTK_DOC,        $HAVE_GTK_DOC)
AM_CONDITIONAL(BUILD_DOCS,          test "x$BUILD_DOCS" = "xyes")
AM_CONDITIONAL(BUILD_TESTS,         test "x$BUILD_TESTS" = "xyes")
AM_CONDITIONAL(BUILD_EXAMPLES,      test "x$BUILD_EXAMPLES" = "xyes")
AM_CONDITIONAL(BUILD_PLUGIN_DOCS,   test "x$BUILD_PLUGIN_DOCS" = "xyes")
AM_CONDITIONAL(HAVE_PDFXMLTEX,      $HAVE_PDFXMLTEX)
AM_CONDITIONAL(HAVE_PDFTOPS,        $HAVE_PDFTOPS)
AM_CONDITIONAL(HAVE_XSLTPROC,       $HAVE_XSLTPROC)
AM_CONDITIONAL(HAVE_FIG2DEV_PNG,    $HAVE_FIG2DEV_PNG)
AM_CONDITIONAL(HAVE_FIG2DEV_PDF,    $HAVE_FIG2DEV_PDF)
AM_CONDITIONAL(HAVE_LIBRTP,         test "x$HAVE_LIBRTP" = "xyes")
AM_CONDITIONAL(HAVE_ARTS,           test "x$HAVE_ARTS" = "xyes")
AM_CONDITIONAL(HAVE_RAW1394,        test "x$HAVE_RAW1394" = "xyes")
AM_CONDITIONAL(HAVE_LIBDV,          test "x$HAVE_LIBDV" = "xyes")
AM_CONDITIONAL(HAVE_GNOME_VFS,      test "x$HAVE_GNOME_VFS" = "xyes")
AM_CONDITIONAL(HAVE_LIBGSM,         test "x$HAVE_LIBGSM" = "xyes")
AM_CONDITIONAL(PLUGINS_USE_BUILDDIR,  test "x$PLUGINS_USE_BUILDDIR" = "xyes")


GST_CFLAGS="$GST_CFLAGS -I\$(top_srcdir)/gst-libs"
AC_SUBST(GST_LIBS)
AC_SUBST(GST_CFLAGS)

dnl #########################
dnl # Make the output files #
dnl #########################

AC_OUTPUT(
Makefile
gstreamer-libs.pc
gstreamer-libs-uninstalled.pc
gst-plugins.spec
gst/Makefile
gst/ac3parse/Makefile
gst/adder/Makefile
gst/audioscale/Makefile
gst/auparse/Makefile
gst/avi/Makefile
gst/chart/Makefile
gst/cutter/Makefile
gst/deinterlace/Makefile
gst/flx/Makefile
gst/intfloat/Makefile
gst/law/Makefile
gst/level/Makefile
gst/median/Makefile
gst/mpeg1enc/Makefile
gst/mpeg1sys/Makefile
gst/mpeg1videoparse/Makefile
gst/mpeg2enc/Makefile
gst/mpeg2sub/Makefile
gst/mpegaudio/Makefile
gst/mpegaudioparse/Makefile
gst/mpegstream/Makefile
gst/mpegtypes/Makefile
gst/passthrough/Makefile
gst/playondemand/Makefile
gst/rtjpeg/Makefile
gst/silence/Makefile
gst/sine/Makefile
gst/smooth/Makefile
gst/spectrum/Makefile
gst/speed/Makefile
gst/stereo/Makefile
gst/stereomono/Makefile
gst/synaesthesia/Makefile
gst/udp/Makefile
gst/videoscale/Makefile
gst/volenv/Makefile
gst/volume/Makefile
gst/vumeter/Makefile
gst/wavparse/Makefile
gst/y4m/Makefile
sys/Makefile
sys/oss/Makefile
sys/qcam/Makefile
sys/v4l/Makefile
sys/vcd/Makefile
sys/vga/Makefile
sys/xvideo/Makefile
ext/Makefile
ext/a52dec/Makefile
ext/aalib/Makefile
ext/alsa/Makefile
ext/arts/Makefile
ext/artsd/Makefile
ext/audiofile/Makefile
ext/avifile/Makefile
ext/cdparanoia/Makefile
ext/dv/Makefile
ext/dvdread/Makefile
ext/esd/Makefile
ext/festival/Makefile
ext/flac/Makefile
ext/gnomevfs/Makefile
ext/gsm/Makefile
ext/hermes/Makefile
ext/http/Makefile
ext/jpeg/Makefile
ext/ladspa/Makefile
ext/lame/Makefile
ext/mad/Makefile
ext/mikmod/Makefile
ext/mjpegtools/Makefile
ext/mpeg2dec/Makefile
ext/openquicktime/Makefile
ext/raw1394/Makefile
ext/rtp/Makefile
ext/sdl/Makefile
ext/shout/Makefile
ext/sidplay/Makefile
ext/smoothwave/Makefile
ext/vorbis/Makefile
ext/xmms/Makefile
gst-libs/Makefile
gst-libs/gst/Makefile
gst-libs/gst/audio/Makefile
gst-libs/gst/idct/Makefile
gst-libs/gst/resample/Makefile
gst-libs/gst/riff/Makefile
examples/capsfilter/Makefile
examples/Makefile
)

echo -e "configure: *** Plugins that will be built : $GST_PLUGINS_YES"
echo
echo -e "configure: *** Plugins that will not be built : $GST_PLUGINS_NO"
echo
