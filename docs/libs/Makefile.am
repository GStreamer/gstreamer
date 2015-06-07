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
SCAN_OPTIONS=--deprecated-guards="GST_DISABLE_DEPRECATED"

# Extra options to supply to gtkdoc-mkdb.
MKDB_OPTIONS=--sgml-mode  --output-format=xml

# Extra options to supply to gtkdoc-fixref.
FIXXREF_OPTIONS=--extra-dir=$(GLIB_PREFIX)/share/gtk-doc/html \
	--extra-dir=$(GST_PREFIX)/share/gtk-doc/html

# Used for dependencies.
HFILE_GLOB=$(top_srcdir)/gst-libs/gst/*/*.h
CFILE_GLOB=$(top_srcdir)/gst-libs/gst/*/*.c

# Header files to ignore when scanning.
IGNORE_HFILES = pbutils-private.h gsttageditingprivate.h id3v2.h \
	kiss_fft_f32.h kiss_fft_f64.h kiss_fftr_f32.h kiss_fftr_f64.h \
	kiss_fftr_s16.h kiss_fftr_s32.h kiss_fft_s16.h kiss_fft_s32.h \
	_kiss_fft_guts_f32.h _kiss_fft_guts_f64.h _kiss_fft_guts_s16.h \
	_kiss_fft_guts_s16.h _kiss_fft_guts_s32.h _kiss_fft_guts_s32.h \
	pbutils-marshal.h


# Images to copy into HTML directory.
HTML_IMAGES =

# Extra SGML files that are included by $(DOC_MAIN_SGML_FILE).
content_files = compiling.sgml

# Other files to distribute.
extra_files =

# CFLAGS and LDFLAGS for compiling scan program. Only needed if your app/lib
# contains GtkObjects/GObjects and you want to document signals and properties.
GTKDOC_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS)
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
	$(GST_BASE_LIBS)

# If you need to override some of the declarations, place them in this file
DOC_OVERRIDES = $(DOC_MODULE)-overrides.txt

include $(top_srcdir)/common/gtk-doc.mak
