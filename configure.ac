dnl autoconf configuration file for gst-plugins 
AC_INIT
AC_CANONICAL_TARGET([])

dnl We disable static building for development, for time savings
dnl this goes before AS_LIBTOOL to appease autoconf
dnl *NOTE*: dnl this line before release, so release does static too
AM_DISABLE_STATIC

dnl when going to/from release please set the nano (fourth number) right !
dnl We disable static building for development, for time savings
AM_DISABLE_STATIC
AS_VERSION(gst-plugins, GST_PLUGINS, 0, 3, 4, 1)
AM_INIT_AUTOMAKE($PACKAGE,$VERSION)
AS_LIBTOOL(GST_PLUGINS, 0, 0, 0, yes)

dnl we'll move this to a configure option later on which will be turned on
dnl by autogen
dnl GST_ERROR="-Wall -Werror"
GST_ERROR="-Werror"

dnl FIXME take something else ?
AC_CONFIG_SRCDIR([gst/law/alaw.c])
AM_CONFIG_HEADER(config.h)

##AUTOMAKE="$AUTOMAKE"
dnl Add parameters for aclocal
dnl (This must come after AM_INIT_AUTOMAKE, since it modifies ACLOCAL)
ACLOCAL="$ACLOCAL -I m4 -I common/m4 $ACLOCAL_FLAGS"

AM_MAINTAINER_MODE

AC_PROG_CC
AM_PROG_CC_STDC
AM_PROG_AS
AS="${CC}"
AC_PROG_CXX
AC_PROG_CXXCPP
AC_ISC_POSIX

AC_HEADER_STDC([])

dnl ############################################
dnl # Super Duper options for plug-in building #
dnl ############################################

dnl ext plug-ins; plug-ins that have external dependencies
GST_CHECK_FEATURE(EXTERNAL, [enable building of plug-ins with external deps],,
[HAVE_EXTERNAL=yes],enabled,
[
  AC_MSG_WARN(building experimental plug-ins)
  BUILD_EXTERNAL="yes"
],[
  AC_MSG_WARN(all plug-ins with external dependencies will not be built)
  BUILD_EXTERNAL="no"
])
# make BUILD_EXTERNAL available to Makefile.am
AM_CONDITIONAL(BUILD_EXTERNAL, test "x$BUILD_EXTERNAL" = "xyes")

dnl experimental plug-ins; stuff that hasn't had the dust settle yet
dnl read 'builds, but might not work'UTO
GST_CHECK_FEATURE(EXPERIMENTAL, [enable building of experimental plug-ins],,
[HAVE_EXPERIMENTAL=yes],disabled,
[
  AC_MSG_WARN(building experimental plug-ins)
  USE_TARKIN="yes"
],[
  AC_MSG_NOTICE(not building experimental plug-ins)
  USE_TARKIN="no"
])

dnl broken plug-ins; stuff that doesn't seem to build at the moment
GST_CHECK_FEATURE(BROKEN, [enable building of broken plug-ins],,
HAVE_BROKEN=yes,disabled,
[  
  AC_MSG_WARN([building broken plug-ins -- no bug reports on these, only patches :)])
  USE_SMOOTHWAVE="yes"
  USE_VGA="yes"
  USE_XMMS="yes"
  USE_ARTS="yes"
],[
  USE_SMOOTHWAVE="no"
  USE_VGA="no"
  USE_XMMS="no"
  USE_ARTS="no"
  AC_MSG_NOTICE(not building broken plug-ins)
])

dnl ##############################
dnl # Do automated configuration #
dnl ##############################

dnl Check for tools:
dnl ================

dnl allow for different autotools
AS_AUTOTOOLS_ALTERNATE()

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

dnl check for gconf
translit(dnm, m, l) AM_CONDITIONAL(USE_GCONF, true)
GST_CHECK_FEATURE(GCONF, [GConf stuff], , [
  AC_PATH_PROG(GCONFTOOL, gconftool-2, no)
  if test x$GCONFTOOL = xno; then
    AC_MSG_WARN(Not installing GConf schemas)
    HAVE_GCONF="no"
  else
    AM_GCONF_SOURCE_2
    HAVE_GCONF="yes"
  fi
  AC_SUBST(HAVE_GCONF)
])

dnl check for gstreamer; uninstalled is selected preferentially -- see pkg-config(1)
PKG_CHECK_MODULES(GST, gstreamer >= $GST_PLUGINS_MAJOR.$GST_PLUGINS_MINOR.$GST_PLUGINS_MICRO,
  HAVE_GST="yes", HAVE_GST="no")

if test "x$HAVE_GST" = "xno"; then
  AC_MSG_ERROR(no GStreamer found)
fi

GST_TOOLS_DIR=`pkg-config --variable=toolsdir gstreamer`
if test -z $GST_TOOLS_DIR; then
  AC_MSG_ERROR([no tools dir defined in GStreamer pkg-config file; core upgrade needed.])
fi
AC_SUBST(GST_TOOLS_DIR)

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
                *)             AC_DEFINE(HAVE_RDTSC, 1, [Define if RDTSC is available]) ;;
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

dnl Check for fast float to int casting as defined in C99
AC_C99_FUNC_LRINT()
AC_C99_FUNC_LRINTF()

dnl Check for essential libraries first:
dnl ====================================

GST_GLIB2_CHECK()
PKG_CHECK_MODULES(GTK2, gtk+-2.0, HAVE_GTK=yes, HAVE_GTK=no)
GTK_CFLAGS=$GTK2_CFLAGS
GTK_LIBS=$GTK2_LIBS
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

dnl ===========================================================================
dnl ============================= gst plug-ins ================================
dnl ===========================================================================
GST_PLUGIN_LDFLAGS='-module -avoid-version'
AC_SUBST(GST_PLUGIN_LDFLAGS)

dnl these are all the gst plug-ins, compilable without additional libs
GST_PLUGINS_ALL="\
        ac3parse adder audioscale auparse avi cdxaparse chart\
        cutter deinterlace effectv festival filter flx goom\
        intfloat law level\
        median mpeg1enc mpeg1sys mpeg1videoparse mpeg2enc mpeg2sub\
        mpegaudio mpegaudioparse mpegstream mpegtypes modplug\
        monoscope passthrough playondemand qtdemux rtjpeg silence sine\
        smooth spectrum speed stereo stereomono\
        synaesthesia udp videoscale volenv volume vumeter wavparse y4m"

AC_SUBST(GST_PLUGINS_ALL)

GST_PLUGINS_SELECTED=""

AC_ARG_WITH(plugins,
    AC_HELP_STRING([--with-plugins],[comma-separated list of plug-ins to compile]),
    [for i in `echo $withval | tr , ' '`; do
        if test -n `echo $i | grep $GST_PLUGINS_ALL`; then
            GST_PLUGINS_SELECTED="$GST_PLUGINS_SELECTED $i"
        else
            echo "plug-in $i not recognized, ignoring..."
        fi
    done],
    [GST_PLUGINS_SELECTED=$GST_PLUGINS_ALL])

AC_SUBST(GST_PLUGINS_SELECTED)

dnl ==========================================================================
dnl ============================= sys plug-ins ================================
dnl ==========================================================================

dnl *** DXR3 card ***
translit(dnm, m, l) AM_CONDITIONAL(USE_DXR3, true)
GST_CHECK_FEATURE(DXR3, [DXR3 hardware mpeg video decoder], dxr3videosink, [
  AC_CHECK_HEADER(linux/em8300.h, HAVE_DXR3="yes", HAVE_DXR3="no")
])

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
GST_CHECK_FEATURE(V4L, [Video 4 Linux], v4lsrc v4lmjpegsrc v4lmjpegsink, [
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
dnl plug-in wants to do.  So Debian distributes a PIC compiled
dnl version of the static lib for plug-ins to link to when it is
dnl inappropriate to link the main application to libXv directly.
dnl FIXME: add check if this platform can support linking to a
dnl        non-PIC libXv, if not then don not use Xv.
dnl FIXME: perhaps warn user if they have a shared libXv since
dnl        this is an error until XFree86 starts shipping one
translit(dnm, m, l) AM_CONDITIONAL(USE_XVIDEO, true)
GST_CHECK_FEATURE(XVIDEO, [X11 XVideo extensions], xvideosink videosink, [
  dnl check for PIC static lib
  GST_CHECK_LIBHEADER(XVIDEO, Xv_pic, XvQueryExtension, $X_LIBS $X_PRE_LIBS -lXext -lX11 $X_EXTRA_LIBS, X11/extensions/Xvlib.h, XVIDEO_LIBS="-lXv_pic -lXext")
  if test x$HAVE_XVIDEO = xno; then
    dnl PIC lib not found, check for regular lib
    GST_CHECK_LIBHEADER(XVIDEO, Xv, XvQueryExtension, $X_LIBS $X_PRE_LIBS -lXext -lX11 $X_EXTRA_LIBS, X11/extensions/Xvlib.h, XVIDEO_LIBS="-lXv -lXext")
  fi
  AC_SUBST(XVIDEO_LIBS)
])

dnl Next, check for the optional libraries:
dnl These are all libraries used in building plug-ins
dnl ================================================
dnl let's try and sort them alphabetically, shall we ?

if test "x$BUILD_EXTERNAL" = "xyes"; then

AC_MSG_NOTICE(Checking for plug-in dependency libraries)

dnl *** a52dec ***
translit(dnm, m, l) AM_CONDITIONAL(USE_A52DEC, true)
GST_CHECK_FEATURE(A52DEC, [a52dec], a52dec, [
  AC_CHECK_A52DEC(HAVE_A52DEC=yes, HAVE_A52DEC=no)
])

dnl *** aalib ***
translit(dnm, m, l) AM_CONDITIONAL(USE_AALIB, true)
GST_CHECK_FEATURE(AALIB, [aasink plug-in], aasink, [
  AM_PATH_AALIB(, HAVE_AALIB=yes, HAVE_AALIB=no)
])

dnl *** alsa ***
translit(dnm, m, l) AM_CONDITIONAL(USE_ALSA, true)
GST_CHECK_FEATURE(ALSA, [alsa plug-ins], gstalsa, [
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
GST_CHECK_FEATURE(ARTS, [arts plug-ins], arts, [
  AM_PATH_ARTS(, HAVE_ARTS=yes, HAVE_ARTS=no)
])

dnl *** artsc ***
translit(dnm, m, l) AM_CONDITIONAL(USE_ARTSC, true)
GST_CHECK_FEATURE(ARTSC, [artsd plug-ins], artsdsink, [
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
  AM_PATH_LIBDV(HAVE_DV=yes, HAVE_DV=no)
  AC_SUBST(DV_LIBS)
])

dnl *** dvdread ***
translit(dnm, m, l) AM_CONDITIONAL(USE_DVDREAD, true)
GST_CHECK_FEATURE(DVDREAD, [dvdread library], dvdreadsrc, [
  GST_CHECK_LIBHEADER(DVDREAD, dvdread, DVDOpen, , dvdread/dvd_reader.h, DVDREAD_LIBS="-ldvdread")
  AC_SUBST(DVDREAD_LIBS)
])

dnl *** dvdnav ***
translit(dnm, m, l) AM_CONDITIONAL(USE_DVDNAV, true)
GST_CHECK_FEATURE(DVDNAV, [dvdnav library], dvdnavsrc, [
  translit(dnm, m, l) AC_SUBST(DVDNAV_LIBS)
  translit(dnm, m, l) AC_SUBST(DVDNAV_CFLAGS)
  GST_CHECK_CONFIGPROG(DVDNAV, dvdnav-config)
])

dnl **** ESound ****
translit(dnm, m, l) AM_CONDITIONAL(USE_ESD, true)
GST_CHECK_FEATURE(ESD, [esound plug-ins], esdsink esdmon, [
  AM_PATH_ESD(0.2.12, HAVE_ESD=yes, HAVE_ESD=no)
])

dnl **** festival ****
dnl translit(dnm, m, l) AM_CONDITIONAL(USE_FESTIVAL, true)
dnl GST_CHECK_FEATURE(FESTIVAL, [festival plug-ins], festivalsrc, [
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
dnl  HAVE_FESTIVAL=yes
dnl])

dnl *** FLAC ***
translit(dnm, m, l) AM_CONDITIONAL(USE_FLAC, true)
GST_CHECK_FEATURE(FLAC, [FLAC lossless audio], flacenc flacdec, [
  GST_CHECK_LIBHEADER(FLAC, FLAC, FLAC__stream_decoder_new, -lm, FLAC/all.h, FLAC_LIBS="-lFLAC")
  AC_SUBST(FLAC_LIBS)
])

dnl *** Gnome VFS ***
translit(dnm, m, l) AM_CONDITIONAL(USE_GNOME_VFS, true)
GST_CHECK_FEATURE(GNOME_VFS, [Gnome VFS], gnomevfssrc, [
  PKG_CHECK_MODULES(GNOME_VFS, gnome-vfs-2.0, HAVE_GNOME_VFS="yes", HAVE_GNOME_VFS="no")
  AC_SUBST(GNOME_VFS_CFLAGS)
  AC_SUBST(GNOME_VFS_LIBS)
])

dnl *** gsm ***
translit(dnm, m, l) AM_CONDITIONAL(USE_GSM, true)
GST_CHECK_FEATURE(GSM, [GSM library], gsmenc gsmdec, [
  GST_CHECK_LIBHEADER(GSM, gsm, gsm_create, , gsm.h, GSM_LIBS="-lgsm")
  if test $HAVE_GSM != "yes"; then
    GST_CHECK_LIBHEADER(GSM, gsm, gsm_create, , gsm/gsm.h, GSM_LIBS="-lgsm")
    if test $HAVE_GSM = "yes"; then 
      AC_DEFINE(GSM_HEADER_IN_SUBDIR, 1, [Define if GSM header in gsm/ subdir])
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
GST_CHECK_FEATURE(HTTP, [http plug-ins], gsthttpsrc, [
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

dnl *** Jack ***
translit(dnm, m, l) AM_CONDITIONAL(USE_JACK, true)
GST_CHECK_FEATURE(JACK, Jack, jack, [
  PKG_CHECK_MODULES(JACK, jack >= 0.29.0, HAVE_JACK="yes", HAVE_JACK="no")
  AC_SUBST(JACK_CFLAGS)
  AC_SUBST(JACK_LIBS)
])

dnl *** jpeg ***
dnl FIXME: we could use header checks here as well IMO
translit(dnm, m, l) AM_CONDITIONAL(USE_JPEG, true)
GST_CHECK_FEATURE(JPEG, [jpeg], jpegenc jpegdec, [
  AC_CHECK_LIB(jpeg, jpeg_set_defaults, HAVE_JPEG="yes", HAVE_JPEG="no")
  JPEG_LIBS="-ljpeg"
  AC_SUBST(JPEG_LIBS)
])

dnl *** ladspa ***
translit(dnm, m, l) AM_CONDITIONAL(USE_LADSPA, true)
GST_CHECK_FEATURE(LADSPA, [ladspa], ladspa, [
  AC_CHECK_HEADER(ladspa.h, HAVE_LADSPA="yes", HAVE_LADSPA="no")
])

dnl *** lame ***
translit(dnm, m, l) AM_CONDITIONAL(USE_LAME, true)
GST_CHECK_FEATURE(LAME, [lame mp3 encoder library], lame, [
  GST_CHECK_LIBHEADER(LAME, mp3lame, lame_init, -lm, lame/lame.h, HAVE_LAME="yes" LAME_LIBS="-lmp3lame")
])
AC_SUBST(LAME_LIBS)

dnl *** libcolorspace ***
translit(dnm, m, l) AM_CONDITIONAL(USE_LCS, true)
GST_CHECK_FEATURE(LCS, Lcs, lcs, [
  PKG_CHECK_MODULES(LCS, lcs, HAVE_LCS="yes", HAVE_LCS="no")
  AC_SUBST(LCS_CFLAGS)
  AC_SUBST(LCS_LIBS)
])

dnl *** libdv ***
translit(dnm, m, l) AM_CONDITIONAL(USE_LIBDV, true)
GST_CHECK_FEATURE(LIBDV, [libdv DV/video decoder], dvdec, [
  PKG_CHECK_MODULES(LIBDV, libdv, HAVE_LIBDV="yes", HAVE_LIBDV="no")
  AC_SUBST(LIBDV_CFLAGS)
  AC_SUBST(LIBDV_LIBS)
])

dnl *** libfame ***
translit(dnm, m, l) AM_CONDITIONAL(USE_LIBFAME, true)
GST_CHECK_FEATURE(LIBFAME, [libfame MPEG1/4 encoder], libfame, [
  AM_PATH_LIBFAME(0.9.0, HAVE_LIBFAME="yes", HAVE_LIBFAME="no")
  AC_SUBST(LIBFAME_CFLAGS)
  AC_SUBST(LIBFAME_LIBS)
])

dnl *** mad ***
dnl FIXME: we could use header checks here as well IMO
translit(dnm, m, l) AM_CONDITIONAL(USE_MAD, true)
GST_CHECK_FEATURE(MAD, [mad mp3 decoder], mad, [
  AC_CHECK_LIB(mad, mad_decoder_finish, HAVE_MAD="yes" MAD_LIBS="-lmad")
  if test "x$HAVE_MAD" = "xyes"; then
    # installed with mad >= 0.14
    HAVE_MAD="no"
    save_libs=$LIBS
    LIBS="-lz"
    AC_CHECK_LIB(id3tag, id3_tag_query, HAVE_MAD="yes" MAD_LIBS="$MAD_LIBS -lid3tag")
    LIBS=$save_LIBS
  fi    
])
AC_SUBST(MAD_LIBS)

dnl *** mikmod ***
translit(dnm, m, l) AM_CONDITIONAL(USE_MIKMOD, true)
GST_CHECK_FEATURE(MIKMOD, [mikmod plug-in], mikmod, [
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
GST_CHECK_FEATURE(MPEG2DEC, [mpeg2dec], mpeg2dec, [
  AC_CHECK_MPEG2DEC(HAVE_MPEG2DEC=yes, HAVE_MPEG2DEC=no)
])

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
GST_CHECK_FEATURE(SDL, [SDL plug-in], sdlvideosink, [
 AM_PATH_SDL(, HAVE_SDL=yes, HAVE_SDL=no)
])

dnl *** shout ***
translit(dnm, m, l) AM_CONDITIONAL(USE_SHOUT, true)
GST_CHECK_FEATURE(SHOUT, [shout plug-in], icecastsend, [
  GST_CHECK_LIBHEADER(SHOUT, shout, shout_init_connection,, shout/shout.h, SHOUT_LIBS="-lshout")
  AC_SUBST(SHOUT_LIBS)
])

dnl *** sidplay ***
translit(dnm, m, l) AM_CONDITIONAL(USE_SIDPLAY, true)
GST_CHECK_FEATURE(SIDPLAY, [sidplay plug-in], sidplay, [
  GST_PATH_SIDPLAY()
])

dnl *** smoothwave ***
translit(dnm, m, l) AM_CONDITIONAL(USE_SMOOTHWAVE, true)
GST_CHECK_FEATURE(SMOOTHWAVE, [smoothwave plug-in], smoothwave, [
  if test $HAVE_GTK = "yes"; then HAVE_SMOOTHWAVE=yes; fi;
])


dnl *** snapshot ***
translit(dnm, m, l) AM_CONDITIONAL(USE_LIBPNG, true)
GST_CHECK_FEATURE(LIBPNG, [snapshot plug-in], snapshot, [
  GST_CHECK_LIBHEADER(LIBPNG, png, png_read_info, -lz, png.h, LIBPNG_LIBS="-lpng -lz")
  AC_SUBST(LIBPNG_LIBS)
])

dnl *** tarkin ***
dnl for now the sources are included in the plug-in
dnl and should be moved to ext-libs/ perhaps
translit(dnm, m, l) AM_CONDITIONAL(USE_TARKIN, true)
GST_CHECK_FEATURE(TARKIN, [tarkinenc tarkindec], tarkin, [
  HAVE_TARKIN="yes"
])

dnl *** vorbis ***
dnl AM_PATH_VORBIS only takes two options
translit(dnm, m, l) AM_CONDITIONAL(USE_VORBIS, true)
GST_CHECK_FEATURE(VORBIS, [vorbis plug-in], vorbisenc vorbisdec, [
  AM_PATH_VORBIS(HAVE_VORBIS=yes, HAVE_VORBIS=no)
])

dnl *** XMMS ***
translit(dnm, m, l) AM_CONDITIONAL(USE_XMMS, true)
GST_CHECK_FEATURE(XMMS, [xmms plug-in], xmms, [
  AM_PATH_XMMS(0.1.0, HAVE_XMMS=yes, HAVE_XMMS=no)
])

fi dnl of EXT plugins

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

GST_DEBUGINFO

AC_ARG_ENABLE(libmmx,
  AC_HELP_STRING([--enable-libmmx],[use libmmx, if available]),
[case "${enableval}" in
  yes) USE_LIBMMX=$HAVE_LIBMMX ;;
  no)  USE_LIBMMX=no ;;
  *) AC_MSG_ERROR(bad value ${enableval} for --enable-libmmx) ;;
esac], 
[USE_LIBMMX=$HAVE_LIBMMX]) dnl Default value

AC_ARG_ENABLE(atomic,
  AC_HELP_STRING([--enable-atomic],[use atomic reference counting header]),
[case "${enableval}" in
  yes) USE_ATOMIC_H=$HAVE_ATOMIC_H;;
  noset) USE_ATOMIC_H=$HAVE_ATOMIC_H;;
  no)  USE_ATOMIC_H=no;;
  *) AC_MSG_ERROR(bad value ${enableval} for --enable-atomic) ;;
esac], 
[USE_ATOMIC_H=$HAVE_ATOMIC_H]) dnl Default value

AC_ARG_ENABLE(profiling,
  AC_HELP_STRING([--enable-profiling],
                 [-pg to compiler commandline, for profiling]),
[case "${enableval}" in
  yes) USE_PROFILING=yes ;;
  no)  UES_PROFILING=no ;;
  *) AC_MSG_ERROR(bad value ${enableval} for --enable-profiling) ;;
esac], 
[USE_PROFILING=no]) dnl Default value

AC_ARG_ENABLE(tests,
  AC_HELP_STRING([--disable-tests],[disable building test apps]),
[case "${enableval}" in
  yes) BUILD_TESTS=yes ;;
  no)  BUILD_TESTS=no ;;
  *) AC_MSG_ERROR(bad value ${enableval} for --disable-tests) ;;
esac], 
[BUILD_TESTS=yes]) dnl Default value

AC_ARG_ENABLE(examples,
  AC_HELP_STRING([--disable-examples],[disable building examples]),
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

AM_CONDITIONAL(HAVE_NASM,           test "x$HAVE_NASM" = "xyes")
AM_CONDITIONAL(HAVE_LIBGLADE_GNOME, test "x$HAVE_LIBGLADE_GNOME" = "xyes")
AM_CONDITIONAL(HAVE_GNOME,          test "x$HAVE_GNOME" = "xyes")
AM_CONDITIONAL(HAVE_GTK,            test "x$HAVE_GTK" = "xyes")
AM_CONDITIONAL(HAVE_GTK_DOC,        $HAVE_GTK_DOC)
AM_CONDITIONAL(BUILD_DOCS,          test "x$BUILD_DOCS" = "xyes")
AM_CONDITIONAL(BUILD_TESTS,         test "x$BUILD_TESTS" = "xyes")
AM_CONDITIONAL(BUILD_EXAMPLES,      test "x$BUILD_EXAMPLES" = "xyes")
AM_CONDITIONAL(BUILD_PLUGIN_DOCS,   test "x$BUILD_PLUGIN_DOCS" = "xyes")
AM_CONDITIONAL(HAVE_FIG2DEV_PNG,    $HAVE_FIG2DEV_PNG)
AM_CONDITIONAL(HAVE_FIG2DEV_PDF,    $HAVE_FIG2DEV_PDF)
AM_CONDITIONAL(HAVE_RAW1394,        test "x$HAVE_RAW1394" = "xyes")

GST_CFLAGS="$GST_CFLAGS -I\$(top_srcdir)/gst-libs -Wall -Werror"
AC_SUBST(GST_LIBS)
AC_SUBST(GST_CFLAGS)

dnl #########################
dnl # Make the output files #
dnl #########################

dnl testsuite/autoplug/Makefile
dnl testsuite/Makefile
AC_CONFIG_FILES(
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
gst/cdxaparse/Makefile
gst/chart/Makefile
gst/cutter/Makefile
gst/deinterlace/Makefile
gst/effectv/Makefile
gst/festival/Makefile
gst/filter/Makefile
gst/flx/Makefile
gst/goom/Makefile
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
gst/modplug/Makefile
gst/modplug/libmodplug/Makefile
gst/monoscope/Makefile
gst/passthrough/Makefile
gst/playondemand/Makefile
gst/qtdemux/Makefile
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
sys/dxr3/Makefile
sys/oss/Makefile
sys/qcam/Makefile
sys/v4l/Makefile
sys/vcd/Makefile
sys/vga/Makefile
sys/xvideo/Makefile
sys/videosink/Makefile
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
ext/dvdnav/Makefile
ext/esd/Makefile
ext/ffmpeg/Makefile
ext/flac/Makefile
ext/gnomevfs/Makefile
ext/gsm/Makefile
ext/hermes/Makefile
ext/http/Makefile
ext/jack/Makefile
ext/jpeg/Makefile
ext/ladspa/Makefile
ext/lame/Makefile
ext/lcs/Makefile
ext/libfame/Makefile
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
ext/tarkin/Makefile
ext/xmms/Makefile
gst-libs/Makefile
gst-libs/gst/Makefile
gst-libs/gst/audio/Makefile
gst-libs/gst/idct/Makefile
gst-libs/gst/resample/Makefile
gst-libs/gst/riff/Makefile
gst-libs/gst/floatcast/Makefile
examples/dynparams/Makefile
examples/capsfilter/Makefile
examples/seeking/Makefile
examples/Makefile
tools/Makefile
gconf/Makefile
)

AC_OUTPUT

echo -e "configure: *** Plug-ins that will be built : $GST_PLUGINS_YES"
echo
echo -e "configure: *** Plug-ins that will not be built : $GST_PLUGINS_NO"
echo
if test "x$BUILD_EXTERNAL" = "xno"; then
  echo "configure: *** No external plug-ins will be built"
fi
