dnl Note: this file is now the main autoconf file
AC_INIT
AC_CONFIG_SRCDIR([gst/gstobject.h])

AC_CANONICAL_TARGET([])

AM_CONFIG_HEADER(config.h)

GST_VERSION_MAJOR=0
GST_VERSION_MINOR=3
GST_VERSION_MICRO=01

dnl we use the release tag during CVS development
GST_VERSION_RELEASE=`date +%Y%m%d`

GST_VERSION=$GST_VERSION_MAJOR.$GST_VERSION_MINOR.$GST_VERSION_MICRO

PACKAGE=gstreamer
VERSION=$GST_VERSION

AC_DEFINE_UNQUOTED(PACKAGE, "$PACKAGE")
AC_DEFINE_UNQUOTED(VERSION, "$VERSION")
AC_SUBST(PACKAGE)
AC_SUBST(VERSION)
AC_DEFINE_UNQUOTED(GST_VERSION_RELEASE, "$GST_VERSION_RELEASE")
AC_SUBST(GST_VERSION_RELEASE)

dnl libtool
GST_CURRENT=1
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

dnl This is used for the -config script...
builddir=`pwd`
AC_SUBST(builddir)


dnl ###########################################
dnl # Super Duper options for plugin building #
dnl ###########################################

dnl experimental plugins; stuff that hasn't had the dust settle yet
AC_ARG_ENABLE(experimental,
[  --enable-experimental          enable building of experimental plugins],
[case "${enableval}" in
  yes) EXPERIMENTAL="yes" ;;
  no) EXPERIMENTAL="no" ;;
esac], 
[EXPERIMENTAL=no]) dnl Default value

if test "x$EXPERIMENTAL" = "$xyes"; then
  AC_MSG_WARN(building experimental plugins)
  USE_FLACLIB="yes"
  USE_LIBMIKMOD="yes"
else
  AC_MSG_NOTICE(not building experimental plugins)
  USE_FLACLIB="no"
  USE_LIBMIKMOD="no"
fi

dnl broken plugins; stuff that doesn't seem to build at the moment
AC_ARG_ENABLE(broken,
[  --enable-broken          enable building of broken plugins],
[case "${enableval}" in
  yes) BROKEN="yes" ;;
  no) BROKEN="no" ;;
esac], 
[BROKEN=no]) dnl Default value

if test "x$broken-plugins" = "$xyes"; then
  AC_MSG_WARN(building broken plugins)
  USE_ARTS="yes"
  USE_ARTSC="yes"
else
  AC_MSG_NOTICE(not building broken plugins)
  USE_ARTS="no"
  USE_ARTSC="no"
fi



dnl ##############################
dnl # Do automated configuration #
dnl ##############################

dnl Check for tools:
dnl ================

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

dnl fix pkg-config's broken default search path
if test -z $PKG_CONFIG_PATH; then
  PKG_CONFIG_PATH=/usr/local/lib/pkgconfig:/usr/lib/pkgconfig
fi
export PKG_CONFIG_PATH

dnl Check for pkgconfig
AC_CHECK_PROG(HAVE_PKGCONFIG, pkg-config, true, false)

dnl check for gtk-doc
AC_ARG_WITH(html-dir, [  --with-html-dir=PATH path to installed docs ])

if test "x$with_html_dir" = "x" ; then
  HTML_DIR='${datadir}/gst/html'
else
  HTML_DIR=$with_html_dir
fi

AC_SUBST(HTML_DIR)

AC_CHECK_PROG(HAVE_GTK_DOC, gtkdoc-mkdb, true, false)
gtk_doc_min_version=0.6
if $HAVE_GTK_DOC ; then 
    gtk_doc_version=`gtkdoc-mkdb --version`
    AC_MSG_CHECKING([gtk-doc version ($gtk_doc_version) >= $gtk_doc_min_version])
    if perl <<EOF ; then
      exit (("$gtk_doc_version" =~ /^[[0-9]]+\.[[0-9]]+$/) &&
            ("$gtk_doc_version" >= "$gtk_doc_min_version") ? 0 : 1);
EOF
      AC_MSG_RESULT(yes)
   else
      AC_MSG_RESULT(no)
      HAVE_GTK_DOC=false
   fi
fi
GTK_DOC_SCANOBJ=gtkdoc-scanobj

AC_SUBST(HAVE_GTK_DOC)
AC_SUBST(GTK_DOC_SCANOBJ)

dnl check for docbook tools
AC_CHECK_PROG(HAVE_XSLTPROC, xsltproc, true, false)
AC_CHECK_PROG(HAVE_PDFTOPS, pdftops, true, false)
dnl this does not yet work properly, at least on debain -- wingo
HAVE_PDFXMLTEX=false

dnl check for image conversion tool
AC_CHECK_PROG(HAVE_FIG2DEV, fig2dev, true, false)

dnl The following is a hack: if fig2dev doesn't display an error message
dnl for the desired type, we assume it supports it.
HAVE_FIG2DEV_PNG=false
if test "x$HAVE_FIG2DEV" = "xtrue" ; then
  fig2dev_quiet=`fig2dev -L png </dev/null 2>&1 >/dev/null`
  if test "x$fig2dev_quiet" = "x" ; then
    HAVE_FIG2DEV_PNG=true
  fi
fi
HAVE_FIG2DEV_PDF=false
if test "x$HAVE_FIG2DEV" = "xtrue" ; then
  fig2dev_quiet=`fig2dev -L pdf </dev/null 2>&1 >/dev/null`
  if test "x$fig2dev_quiet" = "x" ; then
    HAVE_FIG2DEV_PDF=true
  fi
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

dnl Check for MMX-capable compiler
AC_MSG_CHECKING(for MMX-capable compiler)
AC_TRY_RUN([
#include "include/mmx.h"

main()
{ movq_r2r(mm0, mm1); return 0; }
],
[
HAVE_LIBMMX="yes"
AC_MSG_RESULT(yes)
],
HAVE_LIBMMX="no"
AC_MSG_RESULT(no)
,
HAVE_LIBMMX="no"
AC_MSG_RESULT(no)
)

dnl
dnl We should really use AC_SYS_LARGEFILE, but the problem is
dnl many of the plugins don't include "config.h".  To assure
dnl binary compatibility, it is necessary that all gstreamer
dnl code be compiled with the same sizeof(off_t), so we use
dnl the following crude hack.
dnl

AC_MSG_CHECKING(for large file support)
AC_TRY_RUN([
#define _LARGEFILE_SOURCE
#define _FILE_OFFSET_BITS 64
#include <sys/types.h>
int main () { return !(sizeof(off_t) == 8); }
],
[
AC_MSG_RESULT(yes)
GST_CFLAGS="$GST_CFLAGS -D_LARGEFILE_SOURCE -D_FILE_OFFSET_BITS=64"
],
[
AC_MSG_RESULT(no)
],
[
AC_MSG_RESULT(no)
])

dnl Check for essential libraries first:
dnl ====================================

dnl Check for glib2
translit(dnm, m, l) AM_CONDITIONAL(USE_GLIB2, true)
GST_CHECK_FEATURE(GLIB2, [use of glib-2.0 and GObject], , [
  PKG_CHECK_MODULES(GLIB2, glib-2.0 gobject-2.0 gthread-2.0 gmodule-2.0,
    HAVE_GLIB2=yes,HAVE_GLIB2=no)
  GLIB_LIBS=$GLIB2_LIBS
  GLIB_CFLAGS=$GLIB2_CFLAGS
  AC_SUBST(GLIB_LIBS)
  AC_SUBST(GLIB_CFLAGS)
], disabled, [
  AC_DEFINE(USE_GLIB2)
  GST_CFLAGS="$GST_CFLAGS -DUSE_GLIB2"
])
AC_SUBST(USE_GLIB2)

if test x$USE_GLIB2 = xno; then
  GST_PKG_DEPS="glib >= 1.2.0, gtk+ >= 1.2.0"
  
  dnl Check for glib and gtk
  AM_PATH_GLIB(1.2.0,,
              AC_MSG_ERROR(Cannot find glib: Is glib-config in path?),
              glib gmodule gthread)
  AM_PATH_GTK(1.2.0,,
              AC_MSG_ERROR(Cannot find gtk: Is gtk-config in path?))
  HAVE_GTK=yes

  GLIB_LIBS="$GLIB_LIBS $GTK_LIBS"
  GLIB_CFLAGS="$GLIB_CFLAGS $GTK_CFLAGS"
else
  GST_PKG_DEPS="glib-2.0 gobject-2.0 gmodule-2.0 gthread-2.0"
  
  PKG_CHECK_MODULES(GTK2, gtk+-2.0, HAVE_GTK=yes, HAVE_GTK=no)

  GTK_FLAGS=$GTK2_CFLAGS
  GTK_LIBS=$GTK2_LIBS
  
  if $HAVE_GTK_DOC; then  
    AC_CHECK_PROG(HAVE_GTK_DOC, gtkdoc-scangobj, true, false)
    if $HAVE_GTK_DOC; then
      GTK_DOC_SCANOBJ=gtkdoc-scangobj
    fi;
  fi

  dnl FIXME: check for  gnome2 - this conditional is currently
  dnl always false.
  AM_CONDITIONAL(USE_GNOME2, test "x$USE_GNOME2" = "xyes")
  AC_SUBST(USE_GNOME2)
fi

AC_SUBST(GST_PKG_DEPS)
AC_SUBST(GTK_LIBS)
AC_SUBST(GTK_CFLAGS)

if test x$USE_GLIB2 = xno; then
  LIBXML_PKG='libxml >= 1.8.1'
else
  LIBXML_PKG='libxml-2.0'
  AC_DEFINE(HAVE_LIBXML2)
fi
AC_SUBST(LIBXML_PKG)

dnl Check for libxml
AC_PATH_PROG(XML_CONFIG, xml-config, no)
dnl Thomas tries to convert this to pkg-config
dnl PKG_CHECK_MODULES(XML, $LIBXML_PKG, XML_CONFIG=yes, XML_CONFIG=no)
dnl AC_PATH_PROG(XML_CONFIG, xml-config, no)
if test x$XML_CONFIG = xno; then
  AC_MSG_ERROR(Couldn't find $LIBXML_PKG)
fi
XML_LIBS="`xml-config --libs`"
XML_CFLAGS="`xml-config --cflags`"
AC_CHECK_LIB(xml, xmlDocGetRootElement, :,
  [ AC_MSG_ERROR(Need version 1.8.1 or better of libxml) ],
  $XML_LIBS)
AC_SUBST(XML_LIBS)
AC_SUBST(XML_CFLAGS)


dnl ==========================================================================
dnl ========================= Macro definitions ==============================
dnl ==========================================================================

dnl These macros should be moved out to separate files (acinclude.m4?), but
dnl can't currently be because of the SUBSTFOR magic.

dnl Perform a check for existence of ARTS
dnl Richard Boulton <richard-alsa@tartarus.org>
dnl Last modification: 26/06/2001
dnl GST_CHECK_FEATURE(FEATURE-NAME, FEATURE-DESCRIPTION,
dnl                   DEPENDENT-PLUGINS, TEST-FOR-FEATURE)
dnl
dnl This check was written for GStreamer: it should be renamed and checked
dnl for portability if you decide to use it elsewhere.
dnl
AC_DEFUN(GST_CHECK_ARTS,
[
dnl Set the flags
ARTS_LIBS="-L/usr/local/lib -L/usr/lib -lmcop -lartsflow -lartsflow_idl"
ARTS_CFLAGS="-I/usr/local/include/arts -I/usr/include/kde/arts -I/usr/include/arts"
ARTS_MCOPFLAGS=$ARTS_CFLAGS
dnl There's no arts-config script, so we make a guess based on the artsc-config
AC_PATH_PROG(ARTSC_CONFIG, artsc-config, no)
if test x$ARTSC_CONFIG = xyes; then
  ARTS_ARTSC_CFLAGS=`artsc-config --cflags | sed 's/artsc/arts/g'`
  ARTS_CFLAGS="$ARTS_CFLAGS $ARTS_ARTSC_CFLAGS"
  ARTS_MCOPFLAGS="$ARTS_MCOPFLAGS $ARTS_ARTSC_CFLAGS"
fi
AC_SUBST(ARTS_LIBS)
AC_SUBST(ARTS_CFLAGS)
AC_SUBST(ARTS_MCOPFLAGS)

dnl Now check if it exists
AC_LANG_PUSH(C++)
HAVE_ARTS=yes
dnl FIXME: Can't get this test to pass, so commented it out.  Rely on header check.
dnl AC_CHECK_LIB(artsflow, convert_stereo_2float_i16le, :, HAVE_ARTS=no, $LIBS)

dnl AC_CHECK_HEADER uses CPPFLAGS, but not CFLAGS.
dnl FIXME: ensure only suitable flags result from artsc-config --cflags
AC_CHECK_HEADER(artsflow.h, :, HAVE_ARTS=no)

AC_LANG_POP(C++)
dnl Check for the idl generator
AC_CHECK_PROG(HAVE_MCOPIDL, mcopidl, yes, no)
if test x$HAVE_MCOPIDL = xno; then
  HAVE_ARTS=no
fi
])

dnl ==========================================================================
dnl ========================= End macro definitions ==========================
dnl ==========================================================================


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
dnl FIXME: check if the version should be 0.9.0 or 0.5.0
translit(dnm, m, l) AM_CONDITIONAL(USE_ALSA, true)
GST_CHECK_FEATURE(ALSA, [alsa plugins], gstalsa, [
AM_PATH_ALSA(0.5.0, HAVE_ALSA=yes, HAVE_ALSA=no)
])

dnl *** arts ***
dnl translit(dnm, m, l) AM_CONDITIONAL(USE_ARTS, true)
dnl GST_CHECK_FEATURE(ARTS, [arts plugins], arts, [GST_CHECK_ARTS()])

dnl *** artsc ***
dnl translit(dnm, m, l) AM_CONDITIONAL(USE_ARTSC, true)
dnl GST_CHECK_FEATURE(ARTSC, [artsd plugins], artsdsink, [GST_CHECK_ARTSC()])

dnl *** audiofile ***
dnl this check uses the GST_CHECK_CONFIGPROG macro
translit(dnm, m, l) AM_CONDITIONAL(USE_LIBAUDIOFILE, true)
GST_CHECK_FEATURE(LIBAUDIOFILE, [audiofile], afsink afsrc, [
translit(dnm, m, l) AC_SUBST(LIBAUDIOFILE_LIBS)
translit(dnm, m, l) AC_SUBST(LIBAUDIOFILE_CFLAGS)
GST_CHECK_CONFIGPROG(LIBAUDIOFILE, audiofile-config)
])

dnl *** avifile ***
dnl this check uses the GST_CHECK_CONFIGPROG macro
translit(dnm, m, l) AM_CONDITIONAL(USE_AVIFILE, true)
GST_CHECK_FEATURE(AVIFILE, [avifile], windec winenc, [
translit(dnm, m, l) AC_SUBST(AVIFILE_LIBS)
translit(dnm, m, l) AC_SUBST(AVIFILE_CFLAGS)
GST_CHECK_CONFIGPROG(AVIFILE, avifile-config)
])

dnl *** CDParanoia ***
translit(dnm, m, l) AM_CONDITIONAL(USE_CDPARANOIA, true)
GST_CHECK_FEATURE(CDPARANOIA, [CDParanoia], cdparanoia, [
  GST_CHECK_LIBHEADER(CDPARANOIA, cdda_interface, cdda_open, , cdda_interface.h, CDPARANOIA_LIBS="-lcdda_interface -lcdda_paranoia")
])
dnl FIXME : add second check somehow if that is necessary
dnl AC_CHECK_LIB(cdda_paranoia, paranoia_init, : , HAVE_CDPARANOIA=no, -lcdda_interface )
dnl AC_CHECK_HEADER(cdda_paranoia.h, :, HAVE_CDPARANOIA=no)

dnl *** dvdread ***
translit(dnm, m, l) AM_CONDITIONAL(USE_LIBDVDREAD, true)
GST_CHECK_FEATURE(LIBDVDREAD, [dvdread library], libdvdread, [
  GST_CHECK_LIBHEADER(LIBDVDREAD, dvdread, DVDOpen, , dvdread/dvd_reader.h, LIBDVDREAD_LIBS="-ldvdread")
])

dnl **** ESound ****
translit(dnm, m, l) AM_CONDITIONAL(USE_LIBESD, true)
GST_CHECK_FEATURE(LIBESD, [esound plugins], esdsrc esdsink, [
  AM_PATH_ESD(0.2.12, HAVE_LIBESD=yes, HAVE_LIBESD=no)
])

dnl *** FLAC ***
dnl thomas : checking for compile with main instead of actual function,
dnl since that made autoconf break (for version 2.13)
translit(dnm, m, l) AM_CONDITIONAL(USE_LIBFLAC, true)
GST_CHECK_FEATURE(FLACLIB, [FLAC lossless audio], flacenc flacdec, [
  GST_CHECK_LIBHEADER(FLACLIB, FLAC, main, -lm, FLAC/all.h, LIBFLAC_LIBS="-lFLAC")
])

dnl 
dnl *** Gnome VFS ***
dnl this check uses the GST_CHECK_CONFIGPROG macro with gnome-config vfs
translit(dnm, m, l) AM_CONDITIONAL(USE_GNOME_VFS, true)
GST_CHECK_FEATURE(GNOME_VFS, [Gnome VFS], gnomevfssrc, [
translit(dnm, m, l) AC_SUBST(GNOME_VFS_LIBS)
translit(dnm, m, l) AC_SUBST(GNOME_VFS_CFLAGS)
GST_CHECK_CONFIGPROG(GNOME_VFS, gnome-config vfs)
])

dnl *** gsm ***
translit(dnm, m, l) AM_CONDITIONAL(USE_LIBGSM, true)
GST_CHECK_FEATURE(LIBGSM, [GSM library], gsmenc gsmdec, [
GST_CHECK_LIBHEADER(LIBGSM, gsm, gsm_create, , gsm/gsm.h, LIBGSM_LIBS="-lgsm")
])

dnl *** Hermes ***
translit(dnm, m, l) AM_CONDITIONAL(USE_LIBHERMES, true)
GST_CHECK_FEATURE(LIBHERMES, [Hermes library], colorspace, [
GST_CHECK_LIBHEADER(LIBHERMES, Hermes, Hermes_ConverterInstance, , Hermes/Hermes.h, LIBHERMES_LIBS="-lHermes")
], AC_SUBST(LIBHERMES_LIBS))

dnl *** lame ***
translit(dnm, m, l) AM_CONDITIONAL(USE_LIBLAME, true)
GST_CHECK_FEATURE(LIBLAME, [lame mp3 encoder library], lame, [
GST_CHECK_LIBHEADER(LIBLAME, mp3lame, lame_init, -lm, lame/lame.h, LIBLAME_LIBS="-lmp3lame")
])

dnl AC_SUBST(HAVE_LIBLAME)

dnl *** libdv ***
dnl FIXME : check if those GLIB thingies are ok
translit(dnm, m, l) AM_CONDITIONAL(USE_LIBDV, true)
GST_CHECK_FEATURE(LIBDV, [dv library], dv, [
GST_CHECK_LIBHEADER(LIBDV, dv, dv_init, -lm $GLIB_LIBS $GLIB_CFLAGS, libdv/dv.h, LIBDV_LIBS="-ldv")
])
dnl FIXME : check if these CPP flags can be dealt with otherwise
dnl libdvcheck_save_CPPFLAGS="$CPPFLAGS"
dnl CPPFLAGS="$CPPFLAGS $GLIB_CFLAGS"
dnl AC_CHECK_HEADER(libdv/dv.h, :, HAVE_LIBDV=no)
dnl CPPFLAGS="$libdvcheck_save_CPPFLAGS"

dnl *** libjpeg ***
dnl FIXME: we could use header checks here as well IMO
translit(dnm, m, l) AM_CONDITIONAL(USE_LIBJPEG, true)
GST_CHECK_FEATURE(LIBJPEG, [libjpeg], jpegenc jpegdec, [
  AC_CHECK_LIB(jpeg, jpeg_set_defaults, HAVE_LIBJPEG="yes", HAVE_LIBJPEG="no")
])

dnl *** mad ***
dnl FIXME: we could use header checks here as well IMO
translit(dnm, m, l) AM_CONDITIONAL(USE_LIBMAD, true)
GST_CHECK_FEATURE(LIBMAD, [mad], mad, [
AC_CHECK_LIB(mad, mad_decoder_finish, HAVE_LIBMAD="yes")
])

dnl *** mikmod ***
translit(dnm, m, l) AM_CONDITIONAL(USE_LIBMIKMOD, true)
GST_CHECK_FEATURE(LIBMIKMOD, [mikmod plugin], mikmod, [
  AM_PATH_LIBMIKMOD(, HAVE_LIBMIKMOD=yes, HAVE_LIBMIKMOD=no)
])

dnl *** mpeg2dec ***
translit(dnm, m, l) AM_CONDITIONAL(USE_MPEG2DEC, true)
GST_CHECK_FEATURE(MPEG2DEC, [mpeg2dec], mpeg2dec, [
  GST_CHECK_LIBHEADER(MPEG2DEC, mpeg2, mpeg2_init,, mpeg2dec/mpeg2.h, MPEG2DEC_LIBS="-lmpeg2")
])

dnl *** quicktime ***
dnl FIXME: make this be called openquicktime
translit(dnm, m, l) AM_CONDITIONAL(USE_QUICKTIME, true)
GST_CHECK_FEATURE(QUICKTIME, [Open Quicktime], quicktime_parser quicktime_decoder quicktime_demux,[
  GST_CHECK_LIBHEADER(QUICKTIME, openquicktime, quicktime_init,, openquicktime/openquicktime.h, QUICKTIME_LIBS="-lopenquicktime")
])

dnl *** raw1394 ***
translit(dnm, m, l) AM_CONDITIONAL(USE_RAW1394, true)
GST_CHECK_FEATURE(RAW1394, [raw1394 library], dv1394src, [
  GST_CHECK_LIBHEADER(RAW1394, raw1934, raw1934_get_handle,, libraw1394/raw1394.h, RAW1394_LIBS="-raw1394")
])

dnl Check for librtp
dnl FIXME : adapt and make it work
translit(dnm, m, l) AM_CONDITIONAL(USE_LIBRTP, true)
GST_CHECK_FEATURE(LIBRTP, [RTP library], rtpenc rtpdec,[
  AC_CHECK_LIB(rtp, rtp_packet_new_take_data, HAVE_LIBRTP=yes, HAVE_LIBRTP=no, $GLIB_LIBS $GLIB_CFLAGS)
])
dnl FIXME header check needs to use GLIB_CFLAGS in order to succeed for rtp
dnl AC_CHECK_HEADERS(rtp/rtp.h, HAVE_LIBRTP=yes, HAVE_LIBRTP=no)
dnl AC_CHECK_HEADERS(rtp/rtp-packet.h, :, HAVE_LIBRTP=no)
dnl AC_CHECK_HEADERS(rtp/rtcp-packet.h, :, HAVE_LIBRTP=no)
dnl AC_CHECK_HEADERS(rtp/rtp-audio.h, :, HAVE_LIBRTP=no)


dnl *** SDL ***
translit(dnm, m, l) AM_CONDITIONAL(USE_LIBSDL, true)
GST_CHECK_FEATURE(LIBSDL, [SDL plugin], sdlvideosink, [
 AM_PATH_SDL(, HAVE_LIBSDL=yes, HAVE_LIBSDL=no)
])

dnl *** shout ***
translit(dnm, m, l) AM_CONDITIONAL(USE_LIBSHOUT, true)
GST_CHECK_FEATURE(LIBSHOUT, [shout plugin], icecastsend, [
  GST_CHECK_LIBHEADER(LIBSHOUT, shout, shout_init_connection,, shout/shout.h, LIBSHOUT_LIBS="-lshout")
])

dnl for sidplay
dnl FIXME : make this work
dnl AC_PATH_LIBSIDPLAY


dnl *** vorbis ***
dnl AM_PATH_VORBIS only takes two options
translit(dnm, m, l) AM_CONDITIONAL(USE_VORBIS, true)
GST_CHECK_FEATURE(VORBIS, [vorbis plugin], vorbisenc vorbisdec, [
  AM_PATH_VORBIS(HAVE_VORBIS=yes, HAVE_VORBIS=no)
])

dnl *** xmms ***
translit(dnm, m, l) AM_CONDITIONAL(USE_LIBXMMS, true)
GST_CHECK_FEATURE(LIBXMMS, [xmms plugin], xmms, [
  AM_PATH_XMMS(0.1.0, HAVE_LIBXMMS=yes, HAVE_LIBXMMS=no)
])

dnl Check for libgdk-pixbuf
dnl FIXME: not sure that this is still used anywhere.
dnl DELETEME
dnl translit(dnm, m, l) AM_CONDITIONAL(USE_GDK_PIXBUF, true)
dnl GST_CHECK_FEATURE(GDK_PIXBUF, [use gdk pixbuf], , [
dnl translit(dnm, m, l) AC_SUBST(GDK_PIXBUF_LIBS)
dnl translit(dnm, m, l) AC_SUBST(GDK_PIXBUF_CFLAGS)
dnl GST_CHECK_CONFIGPROG(GDK_PIXBUF, gdk-pixbuf-config)
dnl ])
dnl 


if test x$USE_GLIB2 = xyes; then
  dnl we don't support gnome/gtk with glib2.0 yet
  GNOME_LIBS=
  GNOME_CFLAGS=
  HAVE_GNOME=no
  AC_MSG_WARN(gnome disabled for glib2.0)
else
  dnl Check for libgnome
  GST_CHECK_CONFIGPROG(GNOME, gnome-config, gnome gnomeui)
fi
AC_SUBST(GNOME_LIBS)
AC_SUBST(GNOME_CFLAGS)
AC_SUBST(HAVE_GNOME)

dnl Check for libghttp
translit(dnm, m, l) AM_CONDITIONAL(USE_LIBGHTTP, true)
GST_CHECK_FEATURE(LIBGHTTP, [libghttp plugins], gsthttpsrc, [
  dnl FIXME: need to check for header
  GHTTP_LIBS=
  GST_HTTPSRC_GET_TYPE=
  if test x$USE_GLIB2 = xyes; then
    AC_MSG_WARN(ghttp disabled for glib2.0)
  else
    AC_CHECK_LIB(ghttp, ghttp_request_new,
      [GHTTP_LIBS="-lghttp"
       GST_HTTPSRC_GET_TYPE="gst_httpsrc_get_type"
       HAVE_LIBGHTTP=yes
      ], :, $LIBS)
  fi
  AC_SUBST(GHTTP_LIBS)
  AC_SUBST(GST_HTTPSRC_GET_TYPE)
])


dnl thomas : adding an arts check taken from xine with it's own .m4
dnl FIXME: put this higher up
dnl AM_PATH_ARTS(, HAVE_ARTS=yes, HAVE_ARTS=no)

dnl also define ARTS_MCOPFLAGS
dnl ARTS_MCOPFLAGS="$ARTS_CFLAGS"
dnl AC_SUBST(ARTS_MCOPFLAGS)   

dnl Check for libglade with gnome support; no libglade for glib2.0
HAVE_LIBGLADE_GNOME="no"
if test x$USE_GLIB2 = xyes; then
  AC_MSG_WARN(libglade disabled for glib2.0)
  PKG_CHECK_MODULES(LIBGLADE_GNOME, libglade-2.0 libgnomeui-2.0, HAVE_LIBGLADE_GNOME=yes, HAVE_LIBGLADE_GNOME=no)
  if test x$HAVE_LIBGLADE_GNOME = xno; then
    AC_MSG_WARN(Couldn't find libglade-2.0 - Can't build gstplay)
    LIBGLADE_GNOME_LIBS=
    LIBGLADE_GNOME_CFLAGS=
  fi;
else
  AM_PATH_LIBGLADE(HAVE_LIBGLADE_GNOME="yes", HAVE_LIBGLADE_GNOME="no", gnome)
  if test x$HAVE_LIBGLADE_GNOME = xno; then
    AC_MSG_WARN(Couldn't find libglade-config - Can't build gstplay)
  else
    LIBGLADE_GNOME_LIBS=$LIBGLADE_LIBS
    LIBGLADE_GNOME_CFLAGS=$LIBGLADE_CFLAGS
        AC_TRY_LINK([#include <glade/glade.h>],[glade_gnome_init();],
                HAVE_LIBGLADE_GNOME="yes"
                AC_MSG_WARN(
        [Couldn't find gnome libraries for libglade - Can't build gstmediaplay and gsteditor])
               )
  fi
fi
AC_SUBST(HAVE_LIBGLADE_GNOME)
AC_SUBST(LIBGLADE_GNOME_LIBS)
AC_SUBST(LIBGLADE_GNOME_CFLAGS)

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


dnl Check for X11 extensions
AC_PATH_XTRA
if test "-DX_DISPLAY_MISSING" = "$X_CFLAGS"; then
  AC_MSG_ERROR(can not find X11)
fi
AC_SUBST(X_CFLAGS)
AC_SUBST(X_PRE_LIBS)
AC_SUBST(X_EXTRA_LIBS)
AC_SUBST(X_LIBS)


dnl Check for the Xv library
xvsave_LIBS=${LIBS}
AC_CHECK_LIB(Xv, XvQueryExtension,
  HAVE_LIBXV=yes,
  HAVE_LIBXV=no,
  $X_LIBS $X_PRE_LIBS -lXext -lX11 $X_EXTRA_LIBS
)
LIBS=${xvsave_LIBS}
AC_CHECK_HEADER(X11/extensions/Xv.h, :, HAVE_LIBXV=no)
AC_CHECK_HEADER(X11/extensions/Xvlib.h, :, HAVE_LIBXV=no)

dnl Check for OSS audio
AC_CHECK_HEADER(sys/soundcard.h, HAVE_OSS=yes, HAVE_OSS=no)

dnl Check for xaudio
AC_CHECK_HEADER(xaudio/decoder.h,
  AC_DEFINE(HAVE_XAUDIO, 1, [Define if xaudio is available])
  HAVE_XAUDIO="yes",
  AC_MSG_WARN(
***** NOTE: These plugins won't be built: gstxa
)
  HAVE_XAUDIO="no",
)

dnl Check for linux/cdrom.h
AC_CHECK_HEADER(linux/cdrom.h,
  HAVE_LINUX_CDROM=yes, HAVE_LINUX_CDROM=no
)

dnl Check for linux/videodev.h
AC_CHECK_HEADER(linux/videodev.h,
  HAVE_LINUX_VIDEODEV=yes, HAVE_LINUX_VIDEODEV=no
)


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

AC_ARG_ENABLE(plugin-builddir,
[  --enable-plugin-builddir     allow tests/demos to use non-installed plugins ],
[case "${enableval}" in
  yes) PLUGINS_USE_BUILDDIR=yes ;;
  no)  PLUGINS_USE_BUILDDIR=no ;;
  *) AC_MSG_ERROR(bad value ${enableval} for --enable-plugin-builddir) ;;
esac], 
[PLUGINS_USE_BUILDDIR=no]) dnl Default value

AC_ARG_ENABLE(debug,
[  --enable-debug               compile with -g debugging info],
[case "${enableval}" in
  yes) USE_DEBUG=yes ;;
  no)  USE_DEBUG=no ;;
  *) AC_MSG_ERROR(bad value ${enableval} for --enable-debug) ;;
esac], 
[USE_DEBUG=no]) dnl Default value

AC_ARG_ENABLE(DEBUG,
[  --enable-DEBUG               compiles in a large number of debugging messages],
[case "${enableval}" in
  yes) ENABLE_DEBUG=yes ;;
  no)  ENABLE_DEBUG=no ;;
  *) AC_MSG_ERROR(bad value ${enableval} for --enable-DEBUG) ;;
esac], 
[ENABLE_DEBUG=no]) dnl Default value
if test x$ENABLE_DEBUG = xyes; then
  AC_DEFINE(GST_DEBUG_ENABLED, 1, [Define if DEBUG statements should be compiled in])
fi

AC_ARG_ENABLE(INFO,
[  --disable-INFO               disables compilation of informational messages],
[case "${enableval}" in
  yes) ENABLE_INFO=yes ;;
  no)  ENABLE_INFO=no ;;
  *) AC_MSG_ERROR(bad value ${enableval} for --enable-INFO) ;;
esac], 
[ENABLE_INFO=yes]) dnl Default value
if test x$ENABLE_INFO = xyes; then
  AC_DEFINE(GST_INFO_ENABLED, 1, [Define if INFO statements should be compiled in])
fi

AC_ARG_ENABLE(debug-color,
[  --disable-debug-color        disables color output of DEBUG and INFO output],
[case "${enableval}" in
  yes) ENABLE_DEBUG_COLOR=yes ;;
  no)  ENABLE_DEBUG_COLOR=no ;;
  *) AC_MSG_ERROR(bad value ${enableval} for --enable-debug-color) ;;
esac], 
[ENABLE_DEBUG_COLOR=yes]) dnl Default value
if test "x$ENABLE_DEBUG_COLOR" = xyes; then
  AC_DEFINE(GST_DEBUG_COLOR, 1, [Define if debugging messages should be colorized])
fi

AC_ARG_ENABLE(profiling,
[  --enable-profiling           adds -pg to compiler commandline, for profiling],
[case "${enableval}" in
  yes) USE_PROFILING=yes ;;
  no)  UES_PROFILING=no ;;
  *) AC_MSG_ERROR(bad value ${enableval} for --enable-profiling) ;;
esac], 
[USE_PROFILING=no]) dnl Default value

dnl default to building registry in the source tree if we are enabling plugin build dir
if test "x$PLUGINS_USE_BUILDDIR" = xyes; then
  GST_CONFIG_DIR=$builddir
else
  GST_CONFIG_DIR=/etc/gstreamer
fi
AC_ARG_WITH(configdir,
[  --with-configdir             specify path to use for configdir],
[case "${withval}" in
  yes) AC_MSG_ERROR(bad value ${withval} for --with-configdir) ;;
  no) AC_MSG_ERROR(bad value ${withval} for --with-configdir) ;;
  *) GST_CONFIG_DIR="${withval}" ;;
esac], 
[:]) dnl Default value

dnl Default value
GST_WIN32_LIBDIR="/usr/lib/win32"
AC_ARG_WITH(win32_libdir,
[  --with-win32-libdir          specify location for win32 DLLs],
[case "${withval}" in
  yes) AC_MSG_ERROR(bad value ${withval} for --with-win32-libdir) ;;
  no) AC_MSG_ERROR(bad value ${withval} for --with-win32-libdir) ;;
  *) GST_WIN32_LIBDIR="${withval}" ;;
esac],
[:]) dnl Default value

AC_ARG_ENABLE(docs-build,
[  --enable-docs-build          enable building of documentation],
[case "${enableval}" in
  yes) if $HAVE_GTK_DOC; then BUILD_DOCS=yes; else AC_MSG_ERROR([you don't have gtk-doc, so don't use --docs-build]); fi; ;;
  no)  BUILD_DOCS=no ;;
  *) AC_MSG_ERROR(bad value ${enableval} for --enable-docs-build) ;;
esac], 
[BUILD_DOCS=no]) dnl Default value

AC_ARG_ENABLE(plugin-docs,
[  --enable-plugin-docs         enable the building of plugin documentation
                               (this is currently broken, so off by default)],
[case "${enableval}" in
  yes) BUILD_PLUGIN_DOCS=yes ;;
  no)  BUILD_PLUGIN_DOCS=no ;;
  *) AC_MSG_ERROR(bad value ${enableval} for --enable-plugin-docs) ;;
esac], 
[BUILD_PLUGIN_DOCS=no]) dnl Default value

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

dnl Next, check for the optional components:
dnl ========================================


AC_DEFUN(GST_SUBSYSTEM_DISABLE,
[dnl
dnl Add a subsystem --disable flag and all the necessary symbols and substitions
dnl
AC_ARG_ENABLE(translit([$1], A-Z, a-z), 
[  ]builtin(format, --disable-%-17s  disable %s, translit([$1], A-Z, a-z), $2),
[ case "${enableval}" in
    yes) GST_DISABLE_[$1]=no ;;
    no) GST_DISABLE_[$1]=yes ;;
    *) AC_MSG_ERROR(bad value ${enableval} for --enable-translit([$1], A-Z, a-z)) ;;
  esac],
[GST_DISABLE_[$1]=no]) dnl Default value
if test x$GST_DISABLE_[$1] = xyes; then
  AC_DEFINE(GST_DISABLE_[$1], 1, [Disable $2])
  GST_DISABLE_[$1]_DEFINE=-DGST_DISABLE_[$1]
fi
AM_CONDITIONAL(GST_DISABLE_[$1], test x$GST_DISABLE_[$1] = xyes)
AC_SUBST(GST_DISABLE_[$1]_DEFINE)
GST_SUBSYSTEM_DISABLE_DEFINES="$GST_SUBSYTEM_DISABLE_DEFINES $GST_DISABLE_[$1]_DEFINE"
])

translit(dnm, m, l) AM_CONDITIONAL(GST_DISABLE_LOADSAVE, true)
GST_SUBSYSTEM_DISABLE(LOADSAVE,[pipeline XML load/save])
translit(dnm, m, l) AM_CONDITIONAL(GST_DISABLE_TYPEFIND, true)
GST_SUBSYSTEM_DISABLE(TYPEFIND,[typefind plugin],)
translit(dnm, m, l) AM_CONDITIONAL(GST_DISABLE_AUTOPLUG, true)
GST_SUBSYSTEM_DISABLE(AUTOPLUG,[autoplugger subsystem])
translit(dnm, m, l) AM_CONDITIONAL(GST_DISABLE_PARSE, true)
GST_SUBSYSTEM_DISABLE(PARSE,[command-line parser])
translit(dnm, m, l) AM_CONDITIONAL(GST_DISABLE_TRACE, true)
GST_SUBSYSTEM_DISABLE(TRACE,[tracing subsystem])
translit(dnm, m, l) AM_CONDITIONAL(GST_DISABLE_REGISTRY, true)
GST_SUBSYSTEM_DISABLE(REGISTRY,[plugin registry])

GST_CFLAGS="$GST_CFLAGS $GST_SUBSYSTEM_DISABLE_DEFINES"


dnl ################################################
dnl # Set defines according to variables set above #
dnl ################################################


dnl These should be "USE_*" instead of "HAVE_*", but some packages expect
dnl HAVE_ and it is likely to be easier to stick with the old name
if test "x$USE_LIBGHTTP" = xyes; then
  AC_DEFINE(HAVE_LIBGHTTP, 1, [Define if ghttp library is available])
fi

if test "x$USE_LIBMMX" = xyes; then
  AC_DEFINE(HAVE_LIBMMX, 1, [Define if libmmx is available])
fi

if test "x$USE_ATOMIC_H" = xyes; then
  AC_DEFINE(HAVE_ATOMIC_H, 1, [Define if atomic.h header file is available])
fi

if test "x$PLUGINS_USE_BUILDDIR" = xyes; then
  AC_DEFINE(PLUGINS_USE_BUILDDIR, 1, [Define if plugins should be loaded from the build tree - only developers should use this])
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

if test "x$HAVE_LIBXV" = xyes; then
  AC_DEFINE(HAVE_LIBXV)
fi

if test "x$HAVE_OSS" = xyes; then
  AC_DEFINE(HAVE_OSS)
fi

if test "x$HAVE_XAUDIO" = xyes; then
  AC_DEFINE(HAVE_XAUDIO)
fi

if test "x$HAVE_LIBMAD" = xyes; then
  AC_DEFINE(HAVE_LIBMAD)
fi

if test "x$HAVE_LIBMIKMOD" = xyes; then
  AC_DEFINE(HAVE_LIBMIKMOD)
fi

if test "x$HAVE_VORBIS" = xyes; then
  AC_DEFINE(HAVE_VORBIS)
fi

if test "x$HAVE_AALIB" = "xyes"; then
  AC_DEFINE(HAVE_AALIB)
fi

if test "x$HAVE_LIBJPEG" = xyes; then
  AC_DEFINE(HAVE_LIBJPEG)
fi

if test "x$HAVE_LIBSDL" = xyes; then
  AC_DEFINE(HAVE_LIBSDL)
fi

if test "x$HAVE_LIBHERMES" = "xyes"; then
  AC_DEFINE(HAVE_LIBHERMES)
fi

if test "x$HAVE_LIBDVDREAD" = xyes; then
  AC_DEFINE(HAVE_LIBDVDREAD)
fi

if test "x$HAVE_LINUX_CDROM" = xyes; then
  AC_DEFINE(HAVE_LINUX_CDROM)
fi

if test "x$HAVE_LINUX_VIDEODEV" = xyes; then
  AC_DEFINE(HAVE_LINUX_VIDEODEV)
fi

if test "x$HAVE_MPEG2DEC" = xyes; then
  AC_DEFINE(HAVE_MPEG2DEC)
fi

if test "x$HAVE_A52DEC" = xyes; then
  AC_DEFINE(HAVE_A52DEC)
fi

if test "x$HAVE_FLACLIB" = xyes; then
  AC_DEFINE(HAVE_FLACLIB)
fi

if test "x$HAVE_LIBGSM" = xyes; then
  AC_DEFINE(HAVE_LIBGSM)
fi

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

AM_CONDITIONAL(HAVE_OSS,            test "x$HAVE_OSS" = "xyes")
AM_CONDITIONAL(HAVE_XAUDIO,         test "x$HAVE_XAUDIO" = "xyes")
AM_CONDITIONAL(HAVE_LIBMAD,         test "x$HAVE_LIBMAD" = "xyes")
AM_CONDITIONAL(HAVE_LIBMIKMOD,      test "x$HAVE_LIBMIKMOD" = "xyes")
AM_CONDITIONAL(HAVE_LINUX_CDROM,    test "x$HAVE_LINUX_CDROM" = "xyes")
AM_CONDITIONAL(HAVE_LINUX_VIDEODEV, test "x$HAVE_LINUX_VIDEODEV" = "xyes")
AM_CONDITIONAL(HAVE_LIBDVDREAD,     test "x$HAVE_LIBDVDREAD" = "xyes")
AM_CONDITIONAL(HAVE_VORBIS,         test "x$HAVE_VORBIS" = "xyes")
AM_CONDITIONAL(HAVE_LIBJPEG,        test "x$HAVE_LIBJPEG" = "xyes")
AM_CONDITIONAL(HAVE_LIBSDL,         test "x$HAVE_LIBSDL" = "xyes")
AM_CONDITIONAL(HAVE_LIBHERMES,      test "x$HAVE_LIBHERMES" = "xyes")
AM_CONDITIONAL(HAVE_NASM,           test "x$HAVE_NASM" = "xyes")
AM_CONDITIONAL(HAVE_LIBGLADE_GNOME, test "x$HAVE_LIBGLADE_GNOME" = "xyes")
AM_CONDITIONAL(HAVE_GNOME,          test "x$HAVE_GNOME" = "xyes")
AM_CONDITIONAL(HAVE_LIBXV,          test "x$HAVE_LIBXV" = "xyes")
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
AM_CONDITIONAL(HAVE_CDPARANOIA,     test "x$HAVE_CDPARANOIA" = "xyes")
AM_CONDITIONAL(HAVE_LIBLAME,        test "x$HAVE_LIBLAME" = "xyes")
AM_CONDITIONAL(HAVE_LIBSHOUT,       test "x$HAVE_LIBSHOUT" = "xyes")
AM_CONDITIONAL(HAVE_MPEG2DEC,       test "x$HAVE_MPEG2DEC" = "xyes")
AM_CONDITIONAL(HAVE_A52DEC,         test "x$HAVE_A52DEC" = "xyes")
dnl thomas : the next line gives errors, this is how it is in CVS
dnl AM_CONDITIONAL(HAVE_FLAC,      	    test "x$HAVE_FLAC" = "xyes")
dnl thomas : the next line gives errors as well, I commented it
dnl AM_CONDITIONAL(HAVE_LAC,      	    test "x$HAVE_FAC" = "xyes")
dnl thomas: the next line doesn't give errors
AM_CONDITIONAL(HAVE_FLACLIB,        test "x$HAVE_FLACLIB" = "xyes")
AM_CONDITIONAL(HAVE_LIBRTP,         test "x$HAVE_LIBRTP" = "xyes")
AM_CONDITIONAL(HAVE_ARTS,           test "x$HAVE_ARTS" = "xyes")
AM_CONDITIONAL(HAVE_XMMS,           test "x$HAVE_XMMS" = "xyes")
AM_CONDITIONAL(HAVE_RAW1394,        test "x$HAVE_RAW1394" = "xyes")
AM_CONDITIONAL(HAVE_LIBDV,          test "x$HAVE_LIBDV" = "xyes")
AM_CONDITIONAL(HAVE_AALIB,          test "x$HAVE_AALIB" = "xyes")
AM_CONDITIONAL(HAVE_GNOME_VFS,      test "x$HAVE_GNOME_VFS" = "xyes")
AM_CONDITIONAL(HAVE_AVIFILE,        test "x$HAVE_AVIFILE" = "xyes")
AM_CONDITIONAL(HAVE_LIBOPENQUICKTIME, test "x$HAVE_LIBOPENQUICKTIME" = "xyes")
AM_CONDITIONAL(HAVE_LIBGSM,         test "x$HAVE_LIBGSM" = "xyes")
AM_CONDITIONAL(PLUGINS_USE_BUILDDIR,  test "x$PLUGINS_USE_BUILDDIR" = "xyes")
AM_CONDITIONAL(HAVE_SIDPLAY,        test "x$have_sidplay" = "xyes")


dnl ############################
dnl # Set up some more defines #
dnl ############################

dnl Set location of configuration dir.
dnl AC_DEFINE_UNQUOTED(GST_CONFIG_DIR,"$GST_CONFIG_DIR")
AC_SUBST(GST_CONFIG_DIR)

dnl Set location of windows dll dir.
AC_DEFINE_UNQUOTED(GST_WIN32_LIBDIR,"$GST_WIN32_LIBDIR")
AC_SUBST(GST_WIN32_LIBDIR)

dnl Set location of plugin directory
if test "x${prefix}" = "xNONE"; then
  PLUGINS_DIR=${ac_default_prefix}/lib/gst
else
  PLUGINS_DIR=${prefix}/lib/gst
fi
AC_DEFINE_UNQUOTED(PLUGINS_DIR,"$PLUGINS_DIR")
AC_SUBST(PLUGINS_DIR)

dnl Set location of uninstalled plugin directory
PLUGINS_BUILDDIR=${builddir}
AC_DEFINE_UNQUOTED(PLUGINS_BUILDDIR,"$PLUGINS_BUILDDIR")
AC_SUBST(PLUGINS_BUILDDIR)

dnl Private vars for libgst only
LIBGST_LIBS="$GST_LIBS $XML_LIBS $GLIB_LIBS"
LIBGST_CFLAGS="$GST_CFLAGS $XML_CFLAGS $GLIB_CFLAGS"
AC_SUBST(LIBGST_LIBS)
AC_SUBST(LIBGST_CFLAGS)

dnl Vars for everyone else
GST_LIBS="\$(top_builddir)/gst/libgst.la $LIBGST_LIBS"
GST_CFLAGS="-I\$(top_srcdir) -I\$(top_srcdir)/include $LIBGST_CFLAGS"
AC_SUBST(GST_LIBS)
AC_SUBST(GST_CFLAGS)

dnl #############################
dnl # Configure the subpackages #
dnl #############################

dnl AC_CONFIG_SUBDIRS(gist)
dnl AC_CONFIG_SUBDIRS(plugins/mp3decode/xing/libxing)

dnl ##################################################
dnl # Prepare informative messages to display at end #
dnl ##################################################

infomessages=

if test "x$PLUGINS_USE_BUILDDIR" = xyes; then
  infomessages="$infomessages
*** Warning: You have configured using the --enable-plugin-builddir option.

This option is for development purposes only: binaries built with
it should be used with code in the build tree only.  To build an
installable version, use ./configure without the --enable-plugin-builddir
option.  Note that the autogen.sh script supplies the plugin builddir
option automatically - it cannot be used to configure installable builds.

"
fi

dnl #########################
dnl # Make the output files #
dnl #########################

dnl until ffmpeg is handled by configure plugins/ffmpeg/Makefile
dnl components/bonobo-gstmediaplay/Makefile
dnl someone should fix this test/misc/Makefile
dnl wtay fix this: testsuite/threads/Makefile
dnl testsuite/refcounting/Makefile
AC_OUTPUT(
Makefile
include/Makefile
gst/Makefile
gst/gstversion.h
gst/types/Makefile
gst/elements/Makefile
gst/autoplug/Makefile
gst/schedulers/Makefile
libs/Makefile
libs/riff/Makefile
libs/getbits/Makefile
libs/putbits/Makefile
libs/idct/Makefile
libs/audio/Makefile
libs/bytestream/Makefile
libs/control/Makefile
libs/resample/Makefile
plugins/Makefile
plugins/a52dec/Makefile
plugins/aasink/Makefile
plugins/alsa/Makefile
plugins/au/Makefile
plugins/audiofile/Makefile
plugins/audioscale/Makefile
plugins/avi/Makefile
plugins/avi/wincodec/Makefile
plugins/avi/winaudio/Makefile
plugins/flx/Makefile
plugins/festival/Makefile
plugins/flac/Makefile
plugins/jpeg/Makefile
plugins/mp3decode/Makefile
plugins/mp3decode/types/Makefile
plugins/mp3decode/xa/Makefile
plugins/mp3decode/mpg123/Makefile
plugins/mp3decode/mad/Makefile
plugins/mp3decode/parse/Makefile
plugins/mp3encode/Makefile
plugins/mp3encode/lame/Makefile
plugins/mikmod/Makefile
plugins/mpeg2/Makefile
plugins/mpeg2/parse/Makefile
plugins/mpeg2/ac3parse/Makefile
plugins/mpeg2/ac3dec/Makefile
plugins/mpeg2/video/Makefile
plugins/mpeg2/mpeg2enc/Makefile
plugins/mpeg2/mpeg2dec/Makefile
plugins/mpeg2/subtitles/Makefile
plugins/mpeg2/videoparse/Makefile
plugins/mpeg2/mpegtypes/Makefile
plugins/mpeg1/Makefile
plugins/mpeg1/mpegtypes/Makefile
plugins/mpeg1/mpegaudio/Makefile
plugins/mpeg1/system_encode/Makefile
plugins/mpeg1/mpeg1encoder/Makefile
plugins/mpeg1video/Makefile
plugins/mpeg1video/parse/Makefile
plugins/oss/Makefile
plugins/rtp/Makefile
plugins/mulaw/Makefile
plugins/alaw/Makefile
plugins/filters/Makefile
plugins/filters/smooth/Makefile
plugins/filters/median/Makefile
plugins/filters/ladspa/Makefile
plugins/filters/stereo2mono/Makefile
plugins/filters/mono2stereo/Makefile
plugins/filters/intfloatconvert/Makefile
plugins/filters/passthrough/Makefile
plugins/filters/adder/Makefile
plugins/filters/colorspace/Makefile
plugins/filters/volenv/Makefile
plugins/filters/level/Makefile
plugins/filters/lav/Makefile
plugins/filters/cutter/Makefile
plugins/filters/deinterlace/Makefile
plugins/filters/volume/Makefile
plugins/filters/speed/Makefile
plugins/filters/playondemand/Makefile
plugins/gnomevfs/Makefile
plugins/icecast/Makefile
plugins/icecast/icecastsend/Makefile
plugins/effects/Makefile
plugins/effects/stereo/Makefile
plugins/effects/volume/Makefile
plugins/udp/Makefile
plugins/visualization/Makefile
plugins/visualization/spectrum/Makefile
plugins/visualization/vumeter/Makefile
plugins/visualization/synaesthesia/Makefile
plugins/visualization/smoothwave/Makefile
plugins/visualization/chart/Makefile
plugins/videoscale/Makefile
plugins/xvideosink/Makefile
plugins/wav/Makefile
plugins/dvdsrc/Makefile
plugins/vcdsrc/Makefile
plugins/rtjpeg/Makefile
plugins/SID/Makefile
plugins/vorbis/Makefile
plugins/capture/Makefile
plugins/capture/v4l/Makefile
plugins/capture/qcam/Makefile
plugins/cdparanoia/Makefile
plugins/esd/Makefile
plugins/esd/esdsink/Makefile
plugins/artsd/Makefile
plugins/quicktime/Makefile
plugins/xmms/Makefile
plugins/arts/Makefile
plugins/gsm/Makefile
plugins/1394/Makefile
plugins/sdlsink/Makefile
plugins/dv/Makefile
plugins/synthesis/Makefile
plugins/synthesis/sinesrc/Makefile
plugins/silence/Makefile
gstplay/Makefile
test/Makefile
test/xml/Makefile
test/bindings/Makefile
test/memchunk/Makefile
test/events/Makefile
tests/Makefile
tests/sched/Makefile
tests/eos/Makefile
tests/muxing/Makefile
testsuite/Makefile
testsuite/bytestream/Makefile
testsuite/capsnego/Makefile
testsuite/plugin/Makefile
tests/nego/Makefile
examples/Makefile
examples/autoplug/Makefile
examples/helloworld/Makefile
examples/helloworld2/Makefile
examples/launch/Makefile
examples/queue/Makefile
examples/queue2/Makefile
examples/queue3/Makefile
examples/queue4/Makefile
examples/thread/Makefile
examples/mixer/Makefile
examples/cutter/Makefile
examples/launch/Makefile
examples/xml/Makefile
examples/plugins/Makefile
examples/typefind/Makefile
examples/mixer/Makefile
editor/Makefile
editor/pixmaps/Makefile
tools/Makefile
docs/Makefile
docs/gst/Makefile
docs/gst/gstreamer.types
docs/libs/Makefile
docs/plugins/Makefile
docs/plugins/gstreamer-plugins.types
docs/manual/Makefile
docs/fwg/Makefile
debian/Makefile
stamp.h
gstreamer-config
gstreamer.pc
gstreamer-uninstalled.pc
gstreamer.spec,
chmod +x gstreamer-config
echo "$infomessages", infomessages="$infomessages"
)

echo -e "configure: *** Plugins that will be built : $GST_PLUGINS_YES"
echo
echo -e "configure: *** Plugins that will not be built : $GST_PLUGINS_NO"
echo
