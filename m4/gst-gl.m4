dnl --------------------------------------------------------------------------
dnl GStreamer OpenGL library checks (gst-libs/gst/gl)
dnl --------------------------------------------------------------------------
AC_DEFUN([AG_GST_GL_CHECKS],
[
dnl define an ERROR_OBJCFLAGS Makefile variable
dnl FIXME: make check conditional on Apple OS?
AG_GST_SET_ERROR_OBJCFLAGS($FATAL_WARNINGS, [
    -Wmissing-declarations -Wredundant-decls
    -Wwrite-strings -Wformat-nonliteral -Wformat-security
    -Winit-self -Wmissing-include-dirs -Wno-multichar $NO_WARNINGS])

AC_CHECK_HEADER(MobileCoreServices/MobileCoreServices.h, HAVE_IOS="yes", HAVE_IOS="no", [-])

AM_CONDITIONAL(HAVE_IOS, test "x$HAVE_IOS" = "xyes")
if test "x$HAVE_IOS" = "xyes"; then
  AC_DEFINE(HAVE_IOS, 1, [Define if building for Apple iOS])
fi

dnl *** opengl ***
AC_ARG_ENABLE([opengl],
     [  --enable-opengl         Enable Desktop OpenGL support @<:@default=auto@:>@],
     [case "${enableval}" in
       yes)  NEED_GL=yes ;;
       no)   NEED_GL=no ;;
       auto) NEED_GL=auto ;;
       *) AC_MSG_ERROR([bad value ${enableval} for --enable-opengl]) ;;
     esac],[NEED_GL=auto])

AC_ARG_WITH([opengl-module-name],
  AS_HELP_STRING([--with-opengl-module-name],[library module name for OpenGL (default: libGL)]))
if test x$with_opengl_module_name != x; then
  AC_DEFINE_UNQUOTED(GST_GL_LIBGL_MODULE_NAME, "$with_opengl_module_name", [OpenGL module name])
fi

AC_ARG_ENABLE([gles2],
     [  --enable-gles2          Enable OpenGL|ES 2.0 support @<:@default=auto@:>@],
     [case "${enableval}" in
       yes)  NEED_GLES2=yes ;;
       no)   NEED_GLES2=no ;;
       auto) NEED_GLES2=auto ;;
       *) AC_MSG_ERROR([bad value ${enableval} for --enable-gles2]) ;;
     esac],[NEED_GLES2=auto])

AC_ARG_WITH([gles2-module-name],
  AS_HELP_STRING([--with-gles2-module-name],[library module name for GLES2 (default: libGLESv2)]))
if test x$with_gles2_module_name != x; then
  AC_DEFINE_UNQUOTED(GST_GL_LIBGLESV2_MODULE_NAME, "$with_gles2_module_name", [GLES2 module name])
fi

AC_ARG_ENABLE([egl],
     [  --enable-egl            Enable EGL support @<:@default=auto@:>@],
     [case "${enableval}" in
       yes)  NEED_EGL=yes ;;
       no)   NEED_EGL=no ;;
       auto) NEED_EGL=auto ;;
       *) AC_MSG_ERROR([bad value ${enableval} for --enable-egl]) ;;
     esac],[NEED_EGL=auto])

AC_ARG_WITH([egl-module-name],
  AS_HELP_STRING([--with-egl-module-name],[library module name for EGL (default: libEGL)]))
if test x$with_egl_module_name != x; then
  AC_DEFINE_UNQUOTED(GST_GL_LIBEGL_MODULE_NAME, "$with_egl_module_name", [EGL module name])
fi

AC_ARG_ENABLE([wgl],
     [  --enable-wgl            Enable WGL support @<:@default=auto@:>@],
     [case "${enableval}" in
       yes)  NEED_WGL=yes ;;
       no)   NEED_WGL=no ;;
       auto) NEED_WGL=auto ;;
       *) AC_MSG_ERROR([bad value ${enableval} for --enable-wgl]) ;;
     esac],[NEED_WGL=auto])

AC_ARG_ENABLE([glx],
     [  --enable-glx            Enable GLX support @<:@default=auto@:>@],
     [case "${enableval}" in
       yes)  NEED_GLX=yes ;;
       no)   NEED_GLX=no ;;
       auto) NEED_GLX=auto ;;
       *) AC_MSG_ERROR([bad value ${enableval} for --enable-glx]) ;;
     esac],[NEED_GLX=auto])

AC_ARG_ENABLE([cocoa],
     [  --enable-cocoa          Enable Cocoa support @<:@default=auto@:>@],
     [case "${enableval}" in
       yes)  NEED_COCOA=yes ;;
       no)   NEED_COCOA=no ;;
       auto) NEED_COCOA=auto ;;
       *) AC_MSG_ERROR([bad value ${enableval} for --enable-cocoa]) ;;
     esac],[NEED_COCOA=auto])

AC_ARG_ENABLE([x11],
     [  --enable-x11            Enable x11 support @<:@default=auto@:>@],
     [case "${enableval}" in
       yes)  NEED_X11=yes ;;
       no)   NEED_X11=no ;;
       auto) NEED_X11=auto ;;
       *) AC_MSG_ERROR([bad value ${enableval} for --enable-x11]) ;;
     esac],[NEED_X11=auto])

AC_ARG_ENABLE([wayland],
     [  --enable-wayland        Enable Wayland support (requires EGL) @<:@default=auto@:>@],
     [case "${enableval}" in
       yes)  NEED_WAYLAND_EGL=yes ;;
       no)   NEED_WAYLAND_EGL=no ;;
       auto) NEED_WAYLAND_EGL=auto ;;
       *) AC_MSG_ERROR([bad value ${enableval} for --enable-wayland]) ;;
     esac],[NEED_WAYLAND_EGL=auto])

AC_ARG_ENABLE([dispmanx],
     [  --enable-dispmanx        Enable Dispmanx support (requires EGL) @<:@default=auto@:>@],
     [case "${enableval}" in
       yes)  NEED_DISPMANX=yes ;;
       no)   NEED_DISPMANX=no ;;
       auto) NEED_DISPMANX=auto ;;
       *) AC_MSG_ERROR([bad value ${enableval} for --enable-dispmanx]) ;;
     esac],[NEED_DISPMANX=auto])

AC_ARG_ENABLE([gbm],
     [  --enable-gbm        Enable Mesa3D GBM support (requires EGL) @<:@default=auto@:>@],
     [case "${enableval}" in
       yes)  NEED_GBM=yes ;;
       no)   NEED_GBM=no ;;
       auto) NEED_GBM=auto ;;
       *) AC_MSG_ERROR([bad value ${enableval} for --enable-gbm]) ;;
     esac],[NEED_GBM=auto])

AC_ARG_ENABLE([png],
     [  --enable-png        Enable libpng support @<:@default=auto@:>@],
     [case "${enableval}" in
       yes)  NEED_PNG=yes ;;
       no)   NEED_PNG=no ;;
       auto) NEED_PNG=auto ;;
       *) AC_MSG_ERROR([bad value ${enableval} for --enable-png]) ;;
     esac],[NEED_PNG=auto])

AC_ARG_ENABLE([jpeg],
     [  --enable-jpeg        Enable libjpeg support @<:@default=auto@:>@],
     [case "${enableval}" in
       yes)  NEED_JPEG=yes ;;
       no)   NEED_JPEG=no ;;
       auto) NEED_JPEG=auto ;;
       *) AC_MSG_ERROR([bad value ${enableval} for --enable-jpeg]) ;;
     esac],[NEED_JPEG=auto])

AG_GST_PKG_CHECK_MODULES(X11_XCB, x11-xcb)
save_CPPFLAGS="$CPPFLAGS"
save_LIBS="$LIBS"

HAVE_GL=no
HAVE_GLES2=no
HAVE_GLES3_H=no
HAVE_WAYLAND_EGL=no
HAVE_VIV_FB_EGL=no
HAVE_GBM_EGL=no
HAVE_EGL_RPI=no

case $host in
  *-mingw32* )
    LIBS="$LIBS -lgdi32"
    AG_GST_CHECK_LIBHEADER(GL, opengl32, glTexImage2D,, GL/gl.h)
    AC_CHECK_HEADER(GL/wglext.h, HAVE_WGLEXT="yes", HAVE_WGLEXT="no", [#include <GL/gl.h>])
    if test "x$HAVE_WGLEXT" = "xyes"; then
      HAVE_WGL=yes
      HAVE_GL=yes
    fi
  ;;
  *)
    if test "x$NEED_GL" != "xno"; then
      AG_GST_PKG_CHECK_MODULES(GL, gl)
      if test "x$HAVE_GL" != "xyes"; then
        AG_GST_CHECK_LIBHEADER(GL, GL, glTexImage2D,, GL/gl.h)
      fi
    fi
    if test "x$NEED_GLES2" != "xno"; then
      AG_GST_PKG_CHECK_MODULES(GLES2, glesv2)
      if test "x$HAVE_GLES2" != "xyes"; then
        AG_GST_CHECK_LIBHEADER(GLES2, GLESv2, glTexImage2D,, GLES2/gl2.h)
      fi
      AC_CHECK_HEADER([GLES3/gl3.h], [HAVE_GLES3_H=yes])
      AS_IF([test "x$HAVE_GLES3_H" == "xyes"],
            [
             AC_CHECK_HEADER([GLES3/gl3ext.h], [HAVE_GLES3EXT3_H=yes], [HAVE_GLES3EXT3_H=no], [#include <GLES3/gl3.h>])
            ])
    fi
    if test "x$NEED_EGL" != "xno"; then
      AG_GST_PKG_CHECK_MODULES(EGL, egl)
      if test "x$HAVE_EGL" != "xyes"; then
        AG_GST_CHECK_LIBHEADER(EGL, EGL, eglGetError,, EGL/egl.h)
      fi
    fi

    old_LIBS=$LIBS
    old_CFLAGS=$CFLAGS

    dnl imx6 / Vivante specifics
    if test "x$HAVE_EGL" = "xyes"; then
        AC_CHECK_LIB([EGL], [fbGetDisplay], [HAVE_VIV_FB_EGL=yes])
    fi

    if test "x$NEED_GBM" != "xno"; then
      if test "x$HAVE_EGL" = "xyes"; then
        PKG_CHECK_MODULES(DRM, libdrm >= 2.4.55, HAVE_DRM=yes, HAVE_DRM=no)
        AC_SUBST(DRM_CFLAGS)
        AC_SUBST(DRM_LIBS)
        if test "x$NEED_GBM" = "xyes"; then
          if test "x$HAVE_DRM" = "xno"; then
            AC_MSG_ERROR([GBM support requested but libdrm is not available])
          fi
          if test "x$HAVE_GUDEV" = "xno"; then
            AC_MSG_ERROR([GBM support requested but gudev is not available])
          fi
        fi
        if test "x$HAVE_DRM" = "xyes" -a "x$HAVE_GUDEV" = "xyes"; then
          PKG_CHECK_MODULES(GBM, gbm, HAVE_GBM_EGL=yes, HAVE_GBM_EGL=no)
          if test "x$HAVE_GBM_EGL" = "xno" -a "x$NEED_GBM" = "xyes"; then
            AC_MSG_ERROR([GBM support requested but gbm library is not available])
          fi
          AC_SUBST(GBM_CFLAGS)
          AC_SUBST(GBM_LIBS)
        fi
      elif test "x$NEED_GBM" = "xyes"; then
        AC_MSG_ERROR([GBM support requested but EGL is not available])
      else
        AC_MSG_NOTICE([GBM support requested but EGL is not available; not enabling GBM support])
      fi
    fi

    dnl FIXME: Mali EGL depends on GLESv1 or GLESv2
    AC_CHECK_HEADER([EGL/fbdev_window.h],
      [
        LIBS="$LIBS -lUMP"
        AC_CHECK_LIB([Mali], [mali_image_create],
          [
            LIBS="$LIBS -lMali"
            AC_CHECK_LIB([GLESv2], [glEnable],
              [
                AC_CHECK_HEADER([GLES2/gl2.h],
                  [
                    AC_CHECK_LIB([EGL], [eglGetProcAddress],
                      [
                        AC_CHECK_HEADER([EGL/egl.h],
                          [
                            HAVE_EGL=yes
                            HAVE_GLES2=yes
                            EGL_LIBS="-lMali -lUMP"
                            EGL_CFLAGS=""
                            AC_DEFINE(USE_EGL_MALI_FB, [1], [Use Mali FB EGL platform])
                          ])
                      ])
                  ])
              ])
          ])
      ])

    dnl FIXME: EGL of RPi depends on GLESv1 or GLESv2
    dnl FIXME: GLESv2 of RPi depends on EGL... WTF!
    LIBS="$LIBS -lvcos -lvchiq_arm"
    AC_CHECK_LIB([bcm_host], [bcm_host_init],
      [
        LIBS="$LIBS -lbcm_host"
        AC_CHECK_HEADER(bcm_host.h,
          [
            LIBS="$LIBS -lGLESv2"
            AC_CHECK_LIB([EGL], [eglGetProcAddress],
              [
                LIBS="$LIBS -lEGL"
                AC_CHECK_HEADER([EGL/egl.h],
                  [
                    AC_CHECK_LIB([GLESv2], [glEnable],
                      [
                        AC_CHECK_HEADER([GLES2/gl2.h],
                          [
                            HAVE_EGL=yes
                            HAVE_GLES2=yes
                            HAVE_EGL_RPI=yes
                            EGL_LIBS="-lbcm_host -lvcos -lvchiq_arm"
                            EGL_CFLAGS=""
                            AC_DEFINE(USE_EGL_RPI, [1], [Use RPi platform])
                          ])
                      ])
                  ])
              ])
          ])
      ])

    LIBS=$old_LIBS
    CFLAGS=$old_CFLAGS

    PKG_CHECK_MODULES(WAYLAND_EGL, wayland-client >= 1.0 wayland-cursor >= 1.0 wayland-egl >= 9.0, HAVE_WAYLAND_EGL=yes, HAVE_WAYLAND_EGL=no)

    # OS X and iOS always have GL available
    case $host in
      *-darwin*)
        if test "x$HAVE_IOS" = "xyes"; then
          HAVE_GLES2=yes
        else
          HAVE_GL=yes
        fi
      ;;
    esac
  ;;
esac

CPPFLAGS="$save_CPPFLAGS"
LIBS="$save_LIBS"

USE_OPENGL=no
USE_GLES2=no
USE_GLX=no
USE_COCOA=no
USE_WGL=no
USE_X11=no
USE_EAGL=no
GL_LIBS=
GL_CFLAGS=
GL_OBJCFLAGS=

dnl Check for what the user asked for and what we could find
if test "x$HAVE_EGL" = "xno"; then
  if test "x$NEED_EGL" = "xyes"; then
    AC_MSG_ERROR([Could not find the required EGL libraries])
  fi
fi

if test "x$HAVE_GL" = "xno"; then
  if test "x$NEED_GL" = "xyes"; then
    AC_MSG_ERROR([Could not find the required OpenGL libraries])
  fi
fi

if test "x$HAVE_GLES2" = "xno"; then
  if test "x$NEED_GLES2" = "xyes"; then
    AC_MSG_ERROR([Could not find the required OpenGL|ES 2.0 libraries])
  fi
fi

dnl X, GLX and OpenGL
if test "x$HAVE_X11_XCB" = "xno"; then
  if test "x$NEED_GLX" = "xyes"; then
    AC_MSG_ERROR([Building the GLX backend without X11 is unsupported])
  fi
  if test "x$NEED_X11" = "xyes"; then
    AC_MSG_ERROR([Could not find X11 development libraries])
  fi
else
  if test "x$NEED_GL" != "xno"; then
    if test "x$HAVE_GL" = "xno"; then
      if test "x$NEED_GLX" = "xyes"; then
        AC_MSG_ERROR([Building the GLX backend without the OpenGL backend is unsupported])
      fi
    else dnl HAVE_GL=yes
      USE_OPENGL=yes
      if test "x$NEED_GLX" != "xno"; then
        USE_GLX=yes
      fi
    fi
  fi
fi

dnl check for DMABUF support
HAVE_DRM_FOURCC_HEADER=no
AC_CHECK_HEADER(libdrm/drm_fourcc.h,
  HAVE_DRM_FOURCC_HEADER=yes, )

GST_GL_HAVE_DMABUF=0
if test "x$HAVE_DRM_FOURCC_HEADER" = "xyes" -a \
        "x$HAVE_EGL" = "xyes"; then
          GST_GL_HAVE_DMABUF=1
fi

dnl check for Vivante DirectVIV support
AC_CHECK_LIB(GLESv2, glTexDirectVIV, [HAVE_VIV_DIRECTVIV=yes], [HAVE_VIV_DIRECTVIV=no])

GST_GL_HAVE_VIV_DIRECTVIV=0
if test "x$HAVE_VIV_DIRECTVIV" = "xyes"; then
          GST_GL_HAVE_VIV_DIRECTVIV=1
fi

dnl check if we can include both GL and GLES2 at the same time
if test "x$HAVE_GL" = "xyes" -a "x$HAVE_GLES2" = "xyes"; then
  GLES3_H_DEFINE=0
  GLES3EXT3_H_DEFINE=0
  if test "x$HAVE_GLES3_H" == "xyes"; then
    GLES3_H_DEFINE=1
  fi
  if test "x$HAVE_GLES3EXT3_H" == "xyes"; then
    GLES3EXT3_H_DEFINE=1
  fi
  GL_INCLUDES="
#ifdef __GNUC__
#  pragma GCC diagnostic push
#  pragma GCC diagnostic ignored \"-Wredundant-decls\"
#endif
#ifndef GL_GLEXT_PROTOTYPES
#define GL_GLEXT_PROTOTYPES 1
#endif
# ifdef HAVE_IOS
#  include <OpenGLES/ES2/gl.h>
#  include <OpenGLES/ES2/glext.h>
# else
#  if $GLES3_H_DEFINE
#   include <GLES3/gl3.h>
#   if $GLES3EXT3_H_DEFINE
#     include <GLES3/gl3ext.h>
#   endif
#   include <GLES2/gl2ext.h>
#  else
#   include <GLES2/gl2.h>
#   include <GLES2/gl2ext.h>
#  endif
# endif
# ifdef __APPLE__
#  include <OpenGL/OpenGL.h>
#  include <OpenGL/gl.h>
#  if MAC_OS_X_VERSION_MAX_ALLOWED >= 1070
#   define GL_DO_NOT_WARN_IF_MULTI_GL_VERSION_HEADERS_INCLUDED
#   include <OpenGL/gl3.h>
#  endif
# else
#  include <GL/gl.h>
#  if __WIN32__ || _WIN32
#   include <GL/glext.h>
#  endif
# endif
int main (int argc, char **argv) { return 0; }
"
  AC_MSG_CHECKING([whether it is possible to include both GL and GLES2 headers])
  save_CFLAGS="$CFLAGS"
  CFLAGS="$CFLAGS $GL_CFLAGS $GLES2_CFLAGS $WARNING_CFLAGS $ERROR_CFLAGS"
  AC_COMPILE_IFELSE([AC_LANG_SOURCE([[$GL_INCLUDES]], [[
    #if !defined(GL_FALSE)
    #error Failed to include GL headers
    #endif
    ]])],[ AC_MSG_RESULT(yes)
    ],[AC_MSG_RESULT(no)
    if test "x$NEED_GLES2" = "xyes"; then
      if test "x$NEED_GL" = "xyes"; then
        AC_MSG_ERROR([Cannot seem to include both GL and GLES2 headers. Try disabling one API])
      fi
      AC_MSG_WARN([Disabling Desktop GL support])
      HAVE_GL=no
      USE_OPENGL=no
    else
      AC_MSG_WARN([Disabling GL|ES 2.0 support])
      HAVE_GLES2=no
      HAVE_GLES3_H=no
    fi
  ])
  CFLAGS="$save_CFLAGS"
fi

#dnl Check for OpenGL
echo host is $host
case $host in
  *-android*)
    if test "x$NEED_WGL" = "xyes"; then
      AC_MSG_ERROR([WGL is not available on Android])
    fi
    if test "x$NEED_GLX" = "xyes"; then
      AC_MSG_ERROR([GLX is not available on Android])
    fi
    if test "x$NEED_GL" = "xyes"; then
      AC_MSG_ERROR([GL is not available on Android])
    fi
    if test "x$NEED_X11" = "xyes"; then
      AC_MSG_ERROR([X11 is not available on Android])
    fi
    if test "x$NEED_COCOA" = "xyes"; then
      AC_MSG_ERROR([Cocoa is not available on Android])
    fi

    dnl OpenGL|ES 2.0
    if test "x$HAVE_GLES2" = "xyes"; then
      if test "x$NEED_GLES2" != "xno"; then
        GL_LIBS="$GL_LIBS -lGLESv2"
        USE_GLES2=yes
      fi
    fi

    dnl EGL
    if test "x$HAVE_EGL" = "xyes"; then
      if test "x$NEED_EGL" != "xno"; then
        GL_LIBS="$GL_LIBS -lEGL"
        USE_EGL=yes
      fi
    fi

    if test "x$USE_EGL" != "xyes"; then
      AC_MSG_ERROR([Need EGL on Android])
    fi

    if test "x$USE_GLES2" != "xyes"; then
      AC_MSG_ERROR([Need OpenGL|ES 2.0 on Android])
    fi

    HAVE_WINDOW_ANDROID=yes
    ;;
  *-linux* | *-cygwin* | *-solaris* | *-netbsd* | *-freebsd* | *-openbsd* | *-kfreebsd* | *-dragonflybsd* | *-gnu* )
    if test "x$NEED_WGL" = "xyes"; then
      AC_MSG_ERROR([WGL is not available on unix])
    fi

    if test "x$HAVE_X11_XCB" = "xno"; then
      if test "x$HAVE_WAYLAND_EGL" = "xno"; then
        AC_MSG_WARN([X or Wayland is required for OpenGL support])
      fi
    fi

    dnl check Desktop OpenGL
    if test "x$HAVE_GL" = "xyes"; then
      if test "x$NEED_GL" != "xno"; then
        GL_LIBS="$GL_LIBS -lGL"
      fi
    fi

    dnl OpenGL|ES 2.0
    if test "x$HAVE_GLES2" = "xyes"; then
      if test "x$NEED_GLES2" != "xno"; then
        GL_LIBS="$GL_LIBS -lGLESv2"
        USE_GLES2=yes
      fi
    fi

    if test "x$HAVE_GBM_EGL" = "xyes"; then
      if test "x$NEED_EGL" = "xno" -o "x$HAVE_EGL" = "xno"; then
        AC_MSG_WARN([EGL is required by the Mesa GBM EGL backend])
      else
        HAVE_WINDOW_GBM=yes
        GL_CFLAGS="$GL_CFLAGS $DRM_CFLAGS $GBM_CFLAGS"
      fi
    fi

    if test "x$HAVE_X11_XCB" = "xyes" -a "x$HAVE_EGL_RPI" = "xno"; then
      if test "x$NEED_X11" != "xno"; then
        GL_LIBS="$GL_LIBS $X11_XCB_LIBS"
        GL_CFLAGS="$GL_CFLAGS $X11_XCB_CFLAGS"
        HAVE_WINDOW_X11=yes
      fi
    fi

    if test "x$HAVE_WAYLAND_EGL" = "xyes"; then
      if test "x$NEED_EGL" = "xno" -o "x$HAVE_EGL" = "xno"; then
        AC_MSG_WARN([EGL is required by the Wayland backend for OpenGL support])
      else
        if test "x$NEED_WAYLAND_EGL" != "xno"; then
          HAVE_WINDOW_WAYLAND=yes
          GL_LIBS="$GL_LIBS $WAYLAND_EGL_LIBS"
          GL_CFLAGS="$GL_CFLAGS $WAYLAND_EGL_CFLAGS"
        fi
      fi
    fi

    if test "x$HAVE_VIV_FB_EGL" = "xyes"; then
      if test "x$NEED_EGL" = "xno" -o "x$HAVE_EGL" = "xno"; then
        AC_MSG_WARN([EGL is required by the Vivante EGL FB backend])
      else
        HAVE_WINDOW_VIV_FB=yes
        GL_LIBS="$GL_LIBS"
        GL_CFLAGS="$GL_CFLAGS"
      fi
    fi

    if test "x$HAVE_EGL_RPI" = "xyes"; then
      if test "x$NEED_DISPMANX" != "xno"; then
        HAVE_WINDOW_DISPMANX=yes
        USE_EGL=yes
      fi
    fi

    dnl EGL
    if test "x$HAVE_EGL" = "xno"; then
      if test "x$HAVE_GL" = "xno"; then
        AC_MSG_WARN([Building requires either EGL or GLX for OpenGL support])
      fi
    else
      if test "x$NEED_EGL" != "xno"; then
        if test "x$HAVE_WINDOW_WAYLAND" = "xyes" -o "x$HAVE_WINDOW_X11" = "xyes" -o "x$HAVE_WINDOW_DISPMANX" = "xyes" -o "x$HAVE_WINDOW_VIV_FB" = "xyes" -o "x$HAVE_WINDOW_GBM" = "xyes"; then
          GL_LIBS="$GL_LIBS -lEGL $EGL_LIBS"
          GL_CFLAGS="$GL_CFLAGS $EGL_CFLAGS"
          USE_EGL=yes
        fi
      fi
    fi
    ;;
  *-darwin*)
    if test "x$HAVE_IOS" = "xyes"; then
      if test "x$NEED_WGL" = "xyes"; then
        AC_MSG_ERROR([WGL is not available on iOS])
      fi
      if test "x$NEED_GLX" = "xyes"; then
        AC_MSG_ERROR([GLX is not available on iOS])
      fi
      if test "x$NEED_GL" = "xyes"; then
        AC_MSG_ERROR([GL is not available on iOS])
      fi
      if test "x$NEED_X11" = "xyes"; then
        AC_MSG_ERROR([X11 is not available on iOS])
      fi
      if test "x$NEED_COCOA" = "xyes"; then
        AC_MSG_ERROR([Cocoa is not available on iOS])
      fi
      if test "x$NEED_EGL" = "xyes"; then
        AC_MSG_ERROR([EGL is not available on iOS])
      fi

      GL_LIBS="$LIBS -framework OpenGLES -framework QuartzCore -framework UIKit -framework CoreGraphics -framework CoreFoundation -framework Foundation"
      GL_CFLAGS="$GL_CFLAGS"
      USE_GLES2=yes
      USE_EAGL=yes
      HAVE_WINDOW_EAGL=yes

      ac_cv_type_GLsizeiptr=yes
      ac_cv_type_GLintptr=yes
      ac_cv_type_GLchar=yes
    else
      dnl Only osx supports cocoa.
      if test "x$NEED_WGL" = "xyes"; then
        AC_MSG_ERROR([WGL is not available on Mac OS X])
      fi

      if test "x$NEED_COCOA" != "xno"; then
        GL_LIBS="$LIBS -framework OpenGL -framework Cocoa -framework QuartzCore -framework CoreFoundation"
        GL_CFLAGS="$GL_CFLAGS"
        USE_COCOA=yes
        HAVE_WINDOW_COCOA=yes
        USE_OPENGL=yes
      fi

      if test "x$USE_GLX" = "xyes"; then
        if test "x$HAVE_X11_XCB" = "xyes"; then
          if test "x$NEED_X11" != "xno"; then
            GL_LIBS="$GL_LIBS $X11_XCB_LIBS"
            GL_CFLAGS="$GL_CFLAGS $X11_XCB_CFLAGS"
            HAVE_WINDOW_X11=yes
          fi
        fi

        if test "x$HAVE_GL" = "xyes"; then
          if test "x$NEED_GL" != "xno"; then
            GL_LIBS="$GL_LIBS -lGL"
          fi
          USE_OPENGL=yes
        fi
      fi

      if test "x$HAVE_EGL" = "xyes"; then
        if test "x$NEED_EGL" != "xno"; then
          if test "x$HAVE_WINDOW_X11" = "xyes"; then
            GL_LIBS="$GL_LIBS -lEGL $EGL_LIBS"
            GL_CFLAGS="$GL_CFLAGS $EGL_CFLAGS"
            USE_EGL=yes
          fi
        fi
      fi

      dnl OpenGL|ES 2.0
      if test "x$HAVE_GLES2" = "xyes"; then
        if test "x$NEED_GLES2" != "xno"; then
          GL_LIBS="$GL_LIBS -lGLESv2"
          USE_GLES2=yes
        fi
      fi
    fi
    ;;
  *-mingw32*)
    if test "x$NEED_GLX" = "xyes"; then
      AC_MSG_ERROR([GLX is not available on Windows])
    fi
    if test "x$NEED_GLES2" = "xyes"; then
      AC_MSG_ERROR([OpenGL|ES 2.0 is not supported on your platform yet])
    fi

    if test "x$HAVE_GL" = "xyes"; then
      if test "x$NEED_GL" != "xno"; then
        if test "x$HAVE_WGL" = "xyes"; then
          if test "$NEED_WGL" != "xno"; then
            GL_LIBS="$GL_LIBS -lgdi32 -lopengl32"
            HAVE_WINDOW_WIN32=yes
            USE_OPENGL=yes
            USE_WGL=yes
          fi
        fi
      fi
    fi
    ;;
  *)
    AC_MSG_WARN([Don't know how to check for OpenGL on your platform.])
    ;;
esac

GL_PLATFORMS=
GL_WINDOWS=
GL_APIS=
GL_CONFIG_DEFINES=

dnl APIs

GST_GL_HAVE_OPENGL=0
GST_GL_HAVE_GLES2=0
GST_GL_HAVE_GLES3=0
GST_GL_HAVE_GLES3EXT3_H=0

if test "x$USE_OPENGL" = "xyes"; then
  GL_APIS="gl $GL_APIS"
  GST_GL_HAVE_OPENGL=1
fi
if test "x$USE_GLES2" = "xyes"; then
  GL_APIS="gles2 $GL_APIS"
  GST_GL_HAVE_GLES2=1
  if test "x$HAVE_GLES3_H" = "xyes"; then
    GST_GL_HAVE_GLES3=1
    if test "x$HAVE_GLES3EXT3_H" = "xyes"; then
      GST_GL_HAVE_GLES3EXT3_H=1
    fi
  fi
fi

GL_CONFIG_DEFINES="$GL_CONFIG_DEFINES
#define GST_GL_HAVE_OPENGL $GST_GL_HAVE_OPENGL
#define GST_GL_HAVE_GLES2 $GST_GL_HAVE_GLES2
#define GST_GL_HAVE_GLES3 $GST_GL_HAVE_GLES3
#define GST_GL_HAVE_GLES3EXT3_H $GST_GL_HAVE_GLES3EXT3_H
"

dnl WINDOW's

GST_GL_HAVE_WINDOW_X11=0
GST_GL_HAVE_WINDOW_COCOA=0
GST_GL_HAVE_WINDOW_WIN32=0
GST_GL_HAVE_WINDOW_WAYLAND=0
GST_GL_HAVE_WINDOW_ANDROID=0
GST_GL_HAVE_WINDOW_DISPMANX=0
GST_GL_HAVE_WINDOW_EAGL=0
GST_GL_HAVE_WINDOW_VIV_FB=0
GST_GL_HAVE_WINDOW_GBM=0

if test "x$HAVE_WINDOW_X11" = "xyes"; then
  GL_WINDOWS="x11 $GL_WINDOWS"
  GST_GL_HAVE_WINDOW_X11=1
fi
if test "x$HAVE_WINDOW_COCOA" = "xyes"; then
  GL_WINDOWS="cocoa $GL_WINDOWS"
  GST_GL_HAVE_WINDOW_COCOA=1
fi
if test "x$HAVE_WINDOW_WIN32" = "xyes"; then
  GL_WINDOWS="win32 $GL_WINDOWS"
  GST_GL_HAVE_WINDOW_WIN32=1
fi
if test "x$HAVE_WINDOW_WAYLAND" = "xyes"; then
  GL_WINDOWS="wayland $GL_WINDOWS"
  GST_GL_HAVE_WINDOW_WAYLAND=1
fi
if test "x$HAVE_WINDOW_ANDROID" = "xyes"; then
  GL_WINDOWS="android $GL_WINDOWS"
  GST_GL_HAVE_WINDOW_ANDROID=1
fi
if test "x$HAVE_WINDOW_DISPMANX" = "xyes"; then
  GL_WINDOWS="dispmanx $GL_WINDOWS"
  GST_GL_HAVE_WINDOW_DISPMANX=1
fi
if test "x$HAVE_WINDOW_EAGL" = "xyes"; then
  GL_WINDOWS="eagl $GL_WINDOWS"
  GST_GL_HAVE_WINDOW_EAGL=1
fi
if test "x$HAVE_WINDOW_VIV_FB" = "xyes"; then
  GL_WINDOWS="viv-fb $GL_WINDOWS"
  GST_GL_HAVE_WINDOW_VIV_FB=1
fi
if test "x$HAVE_WINDOW_GBM" = "xyes"; then
  GL_WINDOWS="gbm $GL_WINDOWS"
  GST_GL_HAVE_WINDOW_GBM=1
fi

GL_CONFIG_DEFINES="$GL_CONFIG_DEFINES
#define GST_GL_HAVE_WINDOW_X11 $GST_GL_HAVE_WINDOW_X11
#define GST_GL_HAVE_WINDOW_COCOA $GST_GL_HAVE_WINDOW_COCOA
#define GST_GL_HAVE_WINDOW_WIN32 $GST_GL_HAVE_WINDOW_WIN32
#define GST_GL_HAVE_WINDOW_WAYLAND $GST_GL_HAVE_WINDOW_WAYLAND
#define GST_GL_HAVE_WINDOW_ANDROID $GST_GL_HAVE_WINDOW_ANDROID
#define GST_GL_HAVE_WINDOW_DISPMANX $GST_GL_HAVE_WINDOW_DISPMANX
#define GST_GL_HAVE_WINDOW_EAGL $GST_GL_HAVE_WINDOW_EAGL
#define GST_GL_HAVE_WINDOW_VIV_FB $GST_GL_HAVE_WINDOW_VIV_FB
#define GST_GL_HAVE_WINDOW_GBM $GST_GL_HAVE_WINDOW_GBM
"

dnl PLATFORM's

GST_GL_HAVE_PLATFORM_EGL=0
GST_GL_HAVE_PLATFORM_GLX=0
GST_GL_HAVE_PLATFORM_WGL=0
GST_GL_HAVE_PLATFORM_CGL=0
GST_GL_HAVE_PLATFORM_EAGL=0

if test "x$USE_EGL" = "xyes"; then
  GL_PLATFORMS="egl $GL_PLATFORMS"
  GST_GL_HAVE_PLATFORM_EGL=1
fi
if test "x$USE_GLX" = "xyes"; then
  GL_PLATFORMS="glx $GL_PLATFORMS"
  GST_GL_HAVE_PLATFORM_GLX=1
fi
if test "x$USE_WGL" = "xyes"; then
  GL_PLATFORMS="wgl $GL_PLATFORMS"
  GST_GL_HAVE_PLATFORM_WGL=1
fi
if test "x$USE_COCOA" = "xyes"; then
  GL_PLATFORMS="cgl $GL_PLATFORMS"
  GST_GL_HAVE_PLATFORM_CGL=1
fi
if test "x$USE_EAGL" = "xyes"; then
  GL_PLATFORMS="eagl $GL_PLATFORMS"
  GST_GL_HAVE_PLATFORM_EAGL=1
fi

GL_CONFIG_DEFINES="$GL_CONFIG_DEFINES
#define GST_GL_HAVE_PLATFORM_EGL $GST_GL_HAVE_PLATFORM_EGL
#define GST_GL_HAVE_PLATFORM_GLX $GST_GL_HAVE_PLATFORM_GLX
#define GST_GL_HAVE_PLATFORM_WGL $GST_GL_HAVE_PLATFORM_WGL
#define GST_GL_HAVE_PLATFORM_CGL $GST_GL_HAVE_PLATFORM_CGL
#define GST_GL_HAVE_PLATFORM_EAGL $GST_GL_HAVE_PLATFORM_EAGL
"

GL_CONFIG_DEFINES="$GL_CONFIG_DEFINES
#define GST_GL_HAVE_DMABUF $GST_GL_HAVE_DMABUF
#define GST_GL_HAVE_VIV_DIRECTVIV $GST_GL_HAVE_VIV_DIRECTVIV
"

dnl Check for no platforms/window systems
if test "x$GL_APIS" = "x"; then
  AC_MSG_WARN([Either OpenGL or OpenGL|ES is required for OpenGL support])
fi
if test "x$GL_PLATFORMS" = "x"; then
  AC_MSG_WARN([Could not find any OpenGL platforms to use such as CGL, WGL or GLX])
fi
if test "x$GL_WINDOWS" = "x"; then
  AC_MSG_WARN([Could not find any window systems to use such as Cocoa, Win32API or X11])
fi

if test "x$GL_APIS" = "x" -o "x$GL_PLATFORMS" = "x" -o "x$GL_WINDOWS" = "x"; then
  GL_LIBS=
  GL_CFLAGS=
  GL_OBJCFLAGS=
  USE_OPENGL=no
  USE_GLES2=no
  USE_GLX=no
  USE_EGL=no
  USE_WGL=no
  USE_COCOA=no
  USE_EGL_MALI=no
  USE_EGL_RPI=no
  USE_EAGL=no

  HAVE_WINDOW_X11=no
  HAVE_WINDOW_WIN32=no
  HAVE_WINDOW_DISPMANX=no
  HAVE_WINDOW_WAYLAND=no
  HAVE_WINDOW_ANDROID=no
  HAVE_WINDOW_COCOA=no
  HAVE_WINDOW_EAGL=no
  HAVE_WINDOW_VIV_FB=no
  HAVE_WINDOW_GBM=no
fi

AC_SUBST(GL_APIS)
AC_SUBST(GL_PLATFORMS)
AC_SUBST(GL_WINDOWS)
AC_SUBST(GL_LIBS)
AC_SUBST(GL_CFLAGS)
AC_SUBST(GL_OBJCFLAGS)
AC_SUBST(USE_OPENGL)
AC_SUBST(USE_GLES2)

AM_CONDITIONAL(HAVE_WINDOW_X11, test "x$HAVE_WINDOW_X11" = "xyes")
AM_CONDITIONAL(HAVE_WINDOW_COCOA, test "x$HAVE_WINDOW_COCOA" = "xyes")
AM_CONDITIONAL(HAVE_WINDOW_WIN32, test "x$HAVE_WINDOW_WIN32" = "xyes")
AM_CONDITIONAL(HAVE_WINDOW_DISPMANX, test "x$HAVE_WINDOW_DISPMANX" = "xyes")
AM_CONDITIONAL(HAVE_WINDOW_WAYLAND, test "x$HAVE_WINDOW_WAYLAND" = "xyes")
AM_CONDITIONAL(HAVE_WINDOW_ANDROID, test "x$HAVE_WINDOW_ANDROID" = "xyes")
AM_CONDITIONAL(HAVE_WINDOW_EAGL, test "x$HAVE_WINDOW_EAGL" = "xyes")
AM_CONDITIONAL(HAVE_WINDOW_VIV_FB, test "x$HAVE_WINDOW_VIV_FB" = "xyes")
AM_CONDITIONAL(HAVE_WINDOW_GBM, test "x$HAVE_WINDOW_GBM" = "xyes")

AM_CONDITIONAL(USE_OPENGL, test "x$USE_OPENGL" = "xyes")
AM_CONDITIONAL(USE_GLES2, test "x$USE_GLES2" = "xyes")
AM_CONDITIONAL(USE_GLX, test "x$USE_GLX" = "xyes")
AM_CONDITIONAL(USE_EGL, test "x$USE_EGL" = "xyes")
AM_CONDITIONAL(USE_WGL, test "x$USE_WGL" = "xyes")
AM_CONDITIONAL(USE_COCOA, test "x$USE_COCOA" = "xyes")
AM_CONDITIONAL(USE_EGL_MALI, test "x$USE_EGL_MALI" = "xyes")
AM_CONDITIONAL(USE_EGL_RPI, test "x$USE_EGL_RPI" = "xyes")
AM_CONDITIONAL(USE_EAGL, test "x$USE_EAGL" = "xyes")

AM_CONDITIONAL(HAVE_GST_GL, test "x$USE_OPENGL" = "xyes" -o "x$USE_GLES2" = "xyes")

dnl Check for some types that are not always present
GL_INCLUDES=""
if test "x$USE_GLES2" = "xyes"; then
  GL_INCLUDES="$GL_INCLUDES
#ifndef GL_GLEXT_PROTOTYPES
#define GL_GLEXT_PROTOTYPES 1
#endif
# ifdef HAVE_IOS
#  include <OpenGLES/ES2/gl.h>
#  include <OpenGLES/ES2/glext.h>
# else
#  if $GST_GL_HAVE_GLES3
#   include <GLES3/gl3.h>
#   if $GST_GL_HAVE_GLES3EXT3_H
#    include <GLES3/gl3ext.h>
#   endif
#   include <GLES2/gl2ext.h>
#  else
#   include <GLES2/gl2.h>
#   include <GLES2/gl2ext.h>
#  endif
# endif
"
fi

if test "x$USE_OPENGL" = "xyes"; then
  GL_INCLUDES="$GL_INCLUDES
# ifdef __APPLE__
#  include <OpenGL/OpenGL.h>
#  include <OpenGL/gl.h>
# else
#  include <GL/gl.h>
#  if __WIN32__ || _WIN32
#   include <GL/glext.h>
#  endif
# endif
"
fi

GST_GL_HAVE_GLEGLIMAGEOES=0
GST_GL_HAVE_GLCHAR=0
GST_GL_HAVE_GLSIZEIPTR=0
GST_GL_HAVE_GLINTPTR=0
GST_GL_HAVE_GLSYNC=0
GST_GL_HAVE_GLUINT64=0
GST_GL_HAVE_GLINT64=0
GST_GL_HAVE_EGLATTRIB=0
GST_GL_HAVE_EGLUINT64KHR=0

old_CFLAGS=$CFLAGS
CFLAGS="$GL_CFLAGS $CFLAGS"

AC_CHECK_TYPES(GLeglImageOES, [], [], [[$GL_INCLUDES]])
if test "x$ac_cv_type_GLeglImageOES" = "xyes"; then
  GST_GL_HAVE_GLEGLIMAGEOES=1
fi

AC_CHECK_TYPES(GLchar, [], [], [[$GL_INCLUDES]])
if test "x$ac_cv_type_GLchar" = "xyes"; then
  GST_GL_HAVE_GLCHAR=1
fi

AC_CHECK_TYPES(GLsizeiptr, [], [], [[$GL_INCLUDES]])
if test "x$ac_cv_type_GLsizeiptr" = "xyes"; then
  GST_GL_HAVE_GLSIZEIPTR=1
fi

AC_CHECK_TYPES(GLintptr, [], [], [[$GL_INCLUDES]])
if test "x$ac_cv_type_GLintptr" = "xyes"; then
  GST_GL_HAVE_GLINTPTR=1
fi

AC_CHECK_TYPES(GLsync, [], [], [[$GL_INCLUDES]])
if test "x$ac_cv_type_GLsync" = "xyes"; then
  GST_GL_HAVE_GLSYNC=1
fi

AC_CHECK_TYPES(GLuint64, [], [], [[$GL_INCLUDES]])
if test "x$ac_cv_type_GLuint64" = "xyes"; then
  GST_GL_HAVE_GLUINT64=1
fi

AC_CHECK_TYPES(GLint64, [], [], [[$GL_INCLUDES]])
if test "x$ac_cv_type_GLint64" = "xyes"; then
  GST_GL_HAVE_GLINT64=1
fi

if test "x$USE_EGL" = "xyes"; then
  EGL_INCLUDES="$GL_INCLUDES
  #include <EGL/egl.h>
  #include <EGL/eglext.h>
  "
  AC_CHECK_TYPES(EGLAttrib, [], [], [[$EGL_INCLUDES]])
  if test "x$ac_cv_type_EGLAttrib" = "xyes"; then
    GST_GL_HAVE_EGLATTRIB=1
  fi

  AC_CHECK_TYPES(EGLuint64KHR, [], [], [[$EGL_INCLUDES]])
  if test "x$ac_cv_type_EGLuint64KHR" = "xyes"; then
    GST_GL_HAVE_EGLUINT64KHR=1
  fi
fi

CFLAGS=$old_CFLAGS

GL_CONFIG_DEFINES="$GL_CONFIG_DEFINES
#define GST_GL_HAVE_GLEGLIMAGEOES $GST_GL_HAVE_GLEGLIMAGEOES
#define GST_GL_HAVE_GLCHAR $GST_GL_HAVE_GLCHAR
#define GST_GL_HAVE_GLSIZEIPTR $GST_GL_HAVE_GLSIZEIPTR
#define GST_GL_HAVE_GLINTPTR $GST_GL_HAVE_GLINTPTR
#define GST_GL_HAVE_GLSYNC $GST_GL_HAVE_GLSYNC
#define GST_GL_HAVE_GLUINT64 $GST_GL_HAVE_GLUINT64
#define GST_GL_HAVE_GLINT64 $GST_GL_HAVE_GLINT64
#define GST_GL_HAVE_EGLATTRIB $GST_GL_HAVE_EGLATTRIB
#define GST_GL_HAVE_EGLUINT64KHR $GST_GL_HAVE_EGLUINT64KHR
"

AC_CONFIG_COMMANDS([gst-libs/gst/gl/gstglconfig.h], [
	outfile=gstglconfig.h-tmp
	cat > $outfile <<\_______EOF
/* gstglconfig.h
 *
 * This is a generated file.  Please modify `configure.ac'
 */

#ifndef __GST_GL_CONFIG_H__
#define __GST_GL_CONFIG_H__

#include <gst/gst.h>

G_BEGIN_DECLS

_______EOF

	cat >>$outfile <<_______EOF
$gl_config_defines
_______EOF

	cat >>$outfile <<_______EOF

G_END_DECLS

#endif  /* __GST_GL_CONFIG_H__ */
_______EOF


	if cmp -s $outfile gst-libs/gst/gl/gstglconfig.h; then
          AC_MSG_NOTICE([gst-libs/gst/gl/gstglconfig.h is unchanged])
	  rm -f $outfile
	else
	  mv $outfile gst-libs/gst/gl/gstglconfig.h
	fi
],[
gl_config_defines='$GL_CONFIG_DEFINES'
])

])

dnl --------------------------------------------------------------------------
dnl GStreamer OpenGL plugin-related checks (ext/opengl)
dnl --------------------------------------------------------------------------
dnl FIXME: make these checks conditional to the opengl plugin being enabled

AC_DEFUN([AG_GST_GL_PLUGIN_CHECKS],
[

dnl graphene-1.0 is optional and used in gltransformation
HAVE_GRAPHENE=NO
PKG_CHECK_MODULES(GRAPHENE, graphene-1.0 >= 1.4.0, HAVE_GRAPHENE=yes, HAVE_GRAPHENE=no)
if test "x$HAVE_GRAPHENE" = "xyes"; then
  AC_DEFINE(HAVE_GRAPHENE, [1] , [Use graphene])
fi
AC_SUBST(HAVE_GRAPHENE)
AC_SUBST(GRAPHENE_LIBS)
AC_SUBST(GRAPHENE_CFLAGS)

dnl Needed by plugins that use g_module_*() API
PKG_CHECK_MODULES(GMODULE_NO_EXPORT, gmodule-no-export-2.0)

dnl libpng is optional
if test "x$NEED_PNG" != "xno"; then
  PKG_CHECK_MODULES(LIBPNG, libpng >= 1.0, HAVE_PNG=yes, HAVE_PNG=no)
  if test "x$HAVE_PNG" = "xyes"; then
    AC_DEFINE(HAVE_PNG, [1] , [Use libpng])
  elif test "x$NEED_PNG" = "xyes"; then
    AC_MSG_ERROR([libpng support requested but libpng is not available])
  fi
fi
AC_SUBST(HAVE_PNG)
AC_SUBST(LIBPNG_LIBS)
AC_SUBST(LIBPNG_CFLAGS)

dnl libjpeg is optional
AC_ARG_WITH(jpeg-mmx, [  --with-jpeg-mmx, path to MMX'ified JPEG library])
if test "x$NEED_JPEG" != "xno"; then
  OLD_LIBS="$LIBS"
  if test x$with_jpeg_mmx != x; then
    LIBS="$LIBS -L$with_jpeg_mmx"
  fi
  AC_CHECK_LIB(jpeg-mmx, jpeg_set_defaults, HAVE_JPEG="yes", HAVE_JPEG="no")
  JPEG_LIBS="$LIBS -ljpeg-mmx"
  LIBS="$OLD_LIBS"
  if test x$HAVE_JPEG != xyes; then
    JPEG_LIBS="-ljpeg"
    AC_CHECK_LIB(jpeg, jpeg_set_defaults, HAVE_JPEG="yes", HAVE_JPEG="no")
  fi

  if test x$HAVE_JPEG = xyes; then
    AC_DEFINE(HAVE_JPEG, [1], [Use libjpeg])
  elif test "x$NEED_JPEG" = "xyes"; then
    AC_MSG_ERROR([libjpeg support requested but libjpeg is not available])
  else
    JPEG_LIBS=
  fi
  AC_SUBST(JPEG_LIBS)
  AC_SUBST(HAVE_JPEG)
fi
])

dnl --------------------------------------------------------------------------
dnl GStreamer OpenGL examples-related checks (tests/examples/gl)
dnl --------------------------------------------------------------------------

AC_DEFUN([AG_GST_GL_EXAMPLES_CHECKS],
[

dnl sdl is optional and used in examples
HAVE_SDL=NO
if test "x$BUILD_EXAMPLES" = "xyes"; then
  PKG_CHECK_MODULES(SDL, sdl >= 1.2.0, HAVE_SDL=yes, HAVE_SDL=no)
  AC_SUBST(SDL_LIBS)
  AC_SUBST(SDL_CFLAGS)
fi
AM_CONDITIONAL(HAVE_SDL, test "x$HAVE_SDL" = "xyes")

])
