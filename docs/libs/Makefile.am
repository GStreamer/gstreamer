## Process this file with automake to produce Makefile.in

# FIXME: fix the docs then remove this variable
DOCS_ARE_INCOMPLETE_PLEASE_FIXME=yespleasedo

# The name of the module, e.g. 'glib'.
#DOC_MODULE=gst-plugins-libs-@GST_API_VERSION@
DOC_MODULE=gst-plugins-bad-libs

# for upload-doc.mak
DOC=gst-plugins-bad-libs
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
  --ignore-decorators="GST_API|GST_BASE_API|GST_CHECK_API|GST_CONTROLLER_API|GST_NET_API|GST_ALLOCATORS_API|GST_APP_API|GST_AUDIO_API|GST_FFT_API|GST_GL_API|GST_PBUTILS_API|GST_RIFF_API|GST_RTP_API|GST_RTSP_API|GST_SDP_API|GST_TAG_API|GST_VIDEO_API|GST_ADAPTIVE_DEMUX_API|GST_AUDIO_BAD_API|GST_BASE_CAMERA_BIN_SRC_API|GST_CODEC_PARSERS_API|GST_INSERT_BIN_API|GST_ISOFF_API|GST_PHOTOGRAPHY_API|GST_MPEGTS_API|GST_OPENCV_API|GST_PLAYER_API|GST_URI_DOWNLOADER_API|GST_VIDEO_BAD_API|GST_WAYLAND_API|GST_WEBRTC_API"

# Extra options to supply to gtkdoc-mkdb.
MKDB_OPTIONS=--sgml-mode  --output-format=xml

# Extra options to supply to gtkdoc-fixref.
FIXXREF_OPTIONS=--extra-dir=$(GLIB_PREFIX)/share/gtk-doc/html \
	--extra-dir=$(GST_PREFIX)/share/gtk-doc/html

# Used for dependencies.
HFILE_GLOB=$(top_srcdir)/gst-libs/gst/*/*.h
CFILE_GLOB=$(top_srcdir)/gst-libs/gst/*/*.c

# Header files to ignore when scanning.
IGNORE_HFILES = \
	$(top_srcdir)/gst-libs/gst/mpegts/gstmpegts-private.h \
	$(top_srcdir)/gst-libs/gst/player/gstplayer-signal-dispatcher-private.h \
	$(top_srcdir)/gst-libs/gst/player/gstplayer-video-renderer-private.h \
	$(top_srcdir)/gst-libs/gst/player/gstplayer-media-info-private.h

# Images to copy into HTML directory.
HTML_IMAGES =

# Extra SGML files that are included by $(DOC_MAIN_SGML_FILE).
content_files = compiling.sgml

# Other files to distribute.
extra_files =

# CFLAGS and LDFLAGS for compiling scan program. Only needed if your app/lib
# contains GtkObjects/GObjects and you want to document signals and properties.
GTKDOC_CFLAGS = -DGST_USE_UNSTABLE_API $(GST_PLUGINS_BAD_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS)
GTKDOC_LIBS = \
	$(top_builddir)/gst-libs/gst/audio/libgstbadaudio-@GST_API_VERSION@.la \
	$(top_builddir)/gst-libs/gst/video/libgstbadvideo-@GST_API_VERSION@.la \
	$(top_builddir)/gst-libs/gst/codecparsers/libgstcodecparsers-@GST_API_VERSION@.la \
	$(top_builddir)/gst-libs/gst/insertbin/libgstinsertbin-@GST_API_VERSION@.la \
	$(top_builddir)/gst-libs/gst/mpegts/libgstmpegts-@GST_API_VERSION@.la \
	$(top_builddir)/gst-libs/gst/player/libgstplayer-@GST_API_VERSION@.la \
	$(top_builddir)/gst-libs/gst/webrtc/libgstwebrtc-@GST_API_VERSION@.la \
  $(GST_BASE_LIBS)

# If you need to override some of the declarations, place them in this file
# and uncomment this line.
DOC_OVERRIDES = $(DOC_MODULE)-overrides.txt

include $(top_srcdir)/common/gtk-doc.mak

