/* config.h.in.  Generated from configure.ac by autoheader.  */
/* This copy of config.h.in is specifically for win32 Visual Studio builds */

/* defined if cdda headers are in a cdda/ directory */
#undef CDPARANOIA_HEADERS_IN_DIR

/* Default audio sink */
#define DEFAULT_AUDIOSINK "directaudiosink"

/* Default audio source */
#undef DEFAULT_AUDIOSRC

/* Default video sink */
#define DEFAULT_VIDEOSINK "directdrawsink"

/* Default video source */
#undef DEFAULT_VIDEOSRC

/* Default visualizer */
#define DEFAULT_VISUALIZER "goom"

/* Define to 1 if translation of program messages to the user's native
   language is requested. */
#undef ENABLE_NLS

/* gettext package name */
#define GETTEXT_PACKAGE "gst-plugins-base-0.10"

/* macro to use to show function name */
#define GST_FUNCTION "(function)"

/* Defined if gcov is enabled to force a rebuild due to config.h changing */
#undef GST_GCOV_ENABLED

/* Default errorlevel to use */
#undef GST_LEVEL_DEFAULT

/* GStreamer license */
#define GST_LICENSE "LGPL"

/* package name in plugins */
#define GST_PACKAGE_NAME "GStreamer Base Plug-ins source release"

/* package origin */
#define GST_PACKAGE_ORIGIN "Unknown package origin"

/* support for features: gstalsa */
#undef HAVE_ALSA

/* support for features: cdparanoia */
#undef HAVE_CDPARANOIA

/* Define if the host CPU is an Alpha */
#undef HAVE_CPU_ALPHA

/* Define if the host CPU is an ARM */
#undef HAVE_CPU_ARM

/* Define if the host CPU is a HPPA */
#undef HAVE_CPU_HPPA

/* Define if the host CPU is an x86 */
#undef HAVE_CPU_I386

/* Define if the host CPU is a IA64 */
#undef HAVE_CPU_IA64

/* Define if the host CPU is a M68K */
#undef HAVE_CPU_M68K

/* Define if the host CPU is a MIPS */
#undef HAVE_CPU_MIPS

/* Define if the host CPU is a PowerPC */
#undef HAVE_CPU_PPC

/* Define if the host CPU is a S390 */
#undef HAVE_CPU_S390

/* Define if the host CPU is a SPARC */
#undef HAVE_CPU_SPARC

/* Define if the host CPU is a x86_64 */
#undef HAVE_CPU_X86_64

/* Define if the GNU dcgettext() function is already present or preinstalled.
   */
#undef HAVE_DCGETTEXT

/* Define to 1 if you have the <dlfcn.h> header file. */
#undef HAVE_DLFCN_H

/* support for features: */
#undef HAVE_EXTERNAL

/* FIONREAD ioctl found in sys/filio.h */
#undef HAVE_FIONREAD_IN_SYS_FILIO

/* FIONREAD ioctl found in sys/ioclt.h */
#undef HAVE_FIONREAD_IN_SYS_IOCTL

/* defined if the compiler implements __func__ */
#undef HAVE_FUNC

/* defined if the compiler implements __FUNCTION__ */
#undef HAVE_FUNCTION

/* Define if the GNU gettext() function is already present or preinstalled. */
#undef HAVE_GETTEXT

/* support for features: gnomevfssrc */
#undef HAVE_GNOME_VFS

/* support for features: v4lsrc v4lmjpegsrc v4lmjpegsink */
#undef HAVE_GST_V4L

/* Define if you have the iconv() function. */
#undef HAVE_ICONV

/* Define to 1 if you have the <inttypes.h> header file. */
#undef HAVE_INTTYPES_H

/* Define to 1 if you have the `asound' library (-lasound). */
#undef HAVE_LIBASOUND

/* support for features: libvisual */
#undef HAVE_LIBVISUAL

/* Define if you have C99's lrint function. */
#undef HAVE_LRINT

/* Define if you have C99's lrintf function. */
#undef HAVE_LRINTF

/* Define to 1 if you have the <malloc.h> header file. */
#undef HAVE_MALLOC_H

/* Define to 1 if you have the <memory.h> header file. */
#undef HAVE_MEMORY_H

/* support for features: oggdemux oggmux */
#undef HAVE_OGG

/* support for features: pango */
#undef HAVE_PANGO

/* defined if the compiler implements __PRETTY_FUNCTION__ */
#undef HAVE_PRETTY_FUNCTION

/* Define if RDTSC is available */
#undef HAVE_RDTSC

/* Define to 1 if you have the <regex.h> header file. */
#undef HAVE_REGEX_H

/* Define to 1 if you have the <stdint.h> header file. */
#undef HAVE_STDINT_H

/* Define to 1 if you have the <stdlib.h> header file. */
#define HAVE_STDLIB_H 1

/* Define to 1 if you have the <strings.h> header file. */
#undef HAVE_STRINGS_H

/* Define to 1 if you have the <string.h> header file. */
#define HAVE_STRING_H 1

/* Define to 1 if you have the <sys/socket.h> header file. */
#undef HAVE_SYS_SOCKET_H

/* Define to 1 if you have the <sys/stat.h> header file. */
#undef HAVE_SYS_STAT_H

/* Define to 1 if you have the <sys/types.h> header file. */
#undef HAVE_SYS_TYPES_H

/* support for features: theoradec theoraenc */
#undef HAVE_THEORA

/* Define to 1 if you have the <unistd.h> header file. */
#undef HAVE_UNISTD_H

/* Define if valgrind should be used */
#undef HAVE_VALGRIND

/* support for features: vorbisenc vorbisdec */
#undef HAVE_VORBIS

/* defined if vorbis_synthesis_restart is present */
#undef HAVE_VORBIS_SYNTHESIS_RESTART

/* support for features: ximagesink */
#undef HAVE_X

/* support for features: xshm */
#undef HAVE_XSHM

/* support for features: xvimagesink */
#undef HAVE_XVIDEO

/* gettext locale dir */
#define LOCALEDIR PREFIX "\\share\\locale"

/* Name of package */
#define PACKAGE "gst-plugins-base"

/* Define to the address where bug reports for this package should be sent. */
#define PACKAGE_BUGREPORT "http://bugzilla.gnome.org/enter_bug.cgi?product=GStreamer"

/* Define to the full name of this package. */
#define PACKAGE_NAME "GStreamer Base Plug-ins"

/* Define to the full name and version of this package. */
#define PACKAGE_STRING "GStreamer Base Plug-ins 0.10.18"

/* Define to the one symbol short name of this package. */
#define PACKAGE_TARNAME "gst-plugins-base"

/* Define to the version of this package. */
#define PACKAGE_VERSION "0.10.18"

/* directory where plugins are located */
#undef PLUGINDIR

/* The size of a `char', as computed by sizeof. */
#undef SIZEOF_CHAR

/* The size of a `int', as computed by sizeof. */
#undef SIZEOF_INT

/* The size of a `long', as computed by sizeof. */
#undef SIZEOF_LONG

/* The size of a `short', as computed by sizeof. */
#undef SIZEOF_SHORT

/* The size of a `void*', as computed by sizeof. */
#undef SIZEOF_VOIDP

/* Define to 1 if you have the ANSI C header files. */
#undef STDC_HEADERS

/* Version number of package */
#define VERSION "0.10.18"

/* Define to 1 if your processor stores words with the most significant byte
   first (like Motorola and SPARC, unlike Intel and VAX). */
#undef WORDS_BIGENDIAN

/* Define to 1 if the X Window System is missing or not being used. */
#undef X_DISPLAY_MISSING

/* Define to `__inline__' or `__inline' if that's what the C compiler
   calls it, or to nothing if 'inline' is not supported under any name.  */
#ifndef __cplusplus
#undef inline
#endif

/* FIXME: this should probably be hard-coded to some win32 system path */
#define GST_INSTALL_PLUGINS_HELPER "/usr/local/libexec/gst-install-plugins-helper"

