## Process this file with automake to produce Makefile.in

# FIXME: fix the docs then remove this variable
DOCS_ARE_INCOMPLETE_PLEASE_FIXME=yespleasedo

# The name of the module, e.g. 'glib'.
#DOC_MODULE=gst-plugins-libs-@GST_API_VERSION@
DOC_MODULE=gst-plugins-base-libs

# for upload-doc.mak
DOC=gst-plugins-base-libs
FORMATS=html
html: html-build.stamp
include $(top_srcdir)/common/upload-doc.mak

# The top-level SGML file. Change it if you want.
DOC_MAIN_SGML_FILE=$(DOC_MODULE)-docs.sgml

# The directory containing the source code.
# gtk-doc will search all .c & .h files beneath here for inline comments
# documenting functions and macros.
DOC_SOURCE_DIR=$(top_srcdir)/gst-libs/gst

# Extra options to supply to gtkdoc-scan.
SCAN_OPTIONS=--deprecated-guards="GST_DISABLE_DEPRECATED" \
  --ignore-decorators="GST_API|GST_BASE_API|GST_CHECK_API|GST_CONTROLLER_API|GST_NET_API|GST_ALLOCATORS_API|GST_APP_API|GST_AUDIO_API|GST_FFT_API|GST_GL_API|GST_PBUTILS_API|GST_RIFF_API|GST_RTP_API|GST_RTSP_API|GST_SDP_API|GST_TAG_API|GST_VIDEO_API"

# Extra options to supply to gtkdoc-mkdb.
MKDB_OPTIONS=--sgml-mode  --output-format=xml

# Extra options to supply to gtkdoc-fixref.
FIXXREF_OPTIONS=--extra-dir=$(GLIB_PREFIX)/share/gtk-doc/html \
	--extra-dir=$(GST_PREFIX)/share/gtk-doc/html

# Used for dependencies.
HFILE_GLOB=$(top_srcdir)/gst-libs/gst/*/*.h
CFILE_GLOB=$(top_srcdir)/gst-libs/gst/*/*.c

KISS_FFT_HFILES=$(wildcard $(top_srcdir)/gst-libs/gst/fft/*kiss_fft*.h)

# Header files to ignore when scanning.
# Note that you can't use glob, so all headers to be ignored must be specified
IGNORE_HFILES = \
	$(top_srcdir)/gst-libs/gst/gettext.h \
	$(top_srcdir)/gst-libs/gst/gst-i18n-app.h \
	$(top_srcdir)/gst-libs/gst/gst-i18n-plugin.h \
	$(top_srcdir)/gst-libs/gst/glib-compat-private.h \
	$(top_srcdir)/gst-libs/gst/audio/gstaudioutilsprivate.h \
	$(top_srcdir)/gst-libs/gst/audio/gstaudiopack.h \
	$(top_srcdir)/gst-libs/gst/audio/gstaudiopack-dist.h \
	$(top_srcdir)/gst-libs/gst/audio/audio-resampler-private.h \
	$(top_srcdir)/gst-libs/gst/audio/audio-resampler-macros.h \
	$(top_srcdir)/gst-libs/gst/audio/audio-resampler-x86.h \
	$(top_srcdir)/gst-libs/gst/audio/audio-resampler-x86-sse.h \
	$(top_srcdir)/gst-libs/gst/audio/audio-resampler-x86-sse2.h \
	$(top_srcdir)/gst-libs/gst/audio/audio-resampler-x86-sse41.h \
	$(top_srcdir)/gst-libs/gst/audio/audio-resampler-neon.h \
	$(top_srcdir)/gst-libs/gst/gl/gstglcontext_private.h \
	$(top_srcdir)/gst-libs/gst/gl/gstglfeature_private.h \
	$(top_srcdir)/gst-libs/gst/gl/gstglfuncs.h \
	$(top_srcdir)/gst-libs/gst/gl/gstglsl_private.h \
	$(top_srcdir)/gst-libs/gst/gl/gstglutil_private.h \
	$(top_srcdir)/gst-libs/gst/gl/gstglwindow_private.h \
	$(top_srcdir)/gst-libs/gst/gl/android/gstglwindow_android_egl.h \
	$(top_srcdir)/gst-libs/gst/gl/cocoa/gstglcontext_cocoa.h \
	$(top_srcdir)/gst-libs/gst/gl/cocoa/gstglwindow_cocoa.h \
	$(top_srcdir)/gst-libs/gst/gl/cocoa/gstgl_cocoa_private.h \
	$(top_srcdir)/gst-libs/gst/gl/dispmanx/gstglwindow_dispmanx_egl.h \
	$(top_srcdir)/gst-libs/gst/gl/eagl/gstglwindow_eagl.h \
	$(top_srcdir)/gst-libs/gst/gl/eagl/gstglcontext_eagl.h \
	$(top_srcdir)/gst-libs/gst/gl/egl/gstglcontext_egl.h \
	$(top_srcdir)/gst-libs/gst/gl/gbm/gstgl_gbm_utils.h \
	$(top_srcdir)/gst-libs/gst/gl/gbm/gstgldisplay_gbm.h \
	$(top_srcdir)/gst-libs/gst/gl/gbm/gstglwindow_gbm_egl.h \
	$(top_srcdir)/gst-libs/gst/gl/utils/opengl_versions.h \
	$(top_srcdir)/gst-libs/gst/gl/utils/gles_versions.h \
	$(top_srcdir)/gst-libs/gst/gl/viv-fb/gstgldisplay_viv_fb.h \
	$(top_srcdir)/gst-libs/gst/gl/viv-fb/gstglwindow_viv_fb_egl.h \
	$(top_srcdir)/gst-libs/gst/gl/wayland/gstglwindow_wayland_egl.h \
	$(top_srcdir)/gst-libs/gst/gl/wayland/wayland_event_source.h \
	$(top_srcdir)/gst-libs/gst/gl/win32/gstglcontext_wgl.h \
	$(top_srcdir)/gst-libs/gst/gl/win32/gstglwindow_win32.h \
	$(top_srcdir)/gst-libs/gst/gl/win32/win32_message_source.h \
	$(top_srcdir)/gst-libs/gst/gl/x11/gstglwindow_x11.h \
	$(top_srcdir)/gst-libs/gst/gl/x11/gstglcontext_glx.h \
	$(top_srcdir)/gst-libs/gst/gl/x11/xcb_event_source.h \
	$(KISS_FFT_HFILES) \
	$(top_srcdir)/gst-libs/gst/pbutils/pbutils-private.h \
	$(top_srcdir)/gst-libs/gst/tag/gsttageditingprivate.h \
	$(top_srcdir)/gst-libs/gst/tag/id3v2.h \
	$(top_srcdir)/gst-libs/gst/video/gstvideoutilsprivate.h \
	$(top_srcdir)/gst-libs/gst/video/video-orc.h \
	$(top_srcdir)/gst-libs/gst/video/video-orc-dist.h


# Images to copy into HTML directory.
HTML_IMAGES =

# Extra SGML files that are included by $(DOC_MAIN_SGML_FILE).
content_files = compiling.sgml

# Other files to distribute.
extra_files =

# CFLAGS and LDFLAGS for compiling scan program. Only needed if your app/lib
# contains GtkObjects/GObjects and you want to document signals and properties.
GTKDOC_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GL_CFLAGS)
GTKDOC_LIBS = \
	$(top_builddir)/gst-libs/gst/allocators/libgstallocators-@GST_API_VERSION@.la \
	$(top_builddir)/gst-libs/gst/audio/libgstaudio-@GST_API_VERSION@.la \
	$(top_builddir)/gst-libs/gst/fft/libgstfft-@GST_API_VERSION@.la \
	$(top_builddir)/gst-libs/gst/rtp/libgstrtp-@GST_API_VERSION@.la \
	$(top_builddir)/gst-libs/gst/rtsp/libgstrtsp-@GST_API_VERSION@.la \
	$(top_builddir)/gst-libs/gst/sdp/libgstsdp-@GST_API_VERSION@.la \
	$(top_builddir)/gst-libs/gst/tag/libgsttag-@GST_API_VERSION@.la \
	$(top_builddir)/gst-libs/gst/video/libgstvideo-@GST_API_VERSION@.la \
	$(top_builddir)/gst-libs/gst/app/libgstapp-@GST_API_VERSION@.la \
	$(top_builddir)/gst-libs/gst/pbutils/libgstpbutils-@GST_API_VERSION@.la \
	$(top_builddir)/gst-libs/gst/riff/libgstriff-@GST_API_VERSION@.la \
	$(top_builddir)/gst-libs/gst/gl/libgstgl-@GST_API_VERSION@.la \
	$(GST_BASE_LIBS)

# If you need to override some of the declarations, place them in this file
DOC_OVERRIDES = $(DOC_MODULE)-overrides.txt

include $(top_srcdir)/common/gtk-doc.mak
