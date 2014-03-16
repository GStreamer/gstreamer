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
IGNORE_HFILES = $(top_srcdir)/gst-libs/gst/mpegts/gstmpegts-private.h

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
	$(top_builddir)/gst-libs/gst/codecparsers/libgstcodecparsers-@GST_API_VERSION@.la \
	$(top_builddir)/gst-libs/gst/basecamerabinsrc/libgstbasecamerabinsrc-@GST_API_VERSION@.la \
	$(top_builddir)/gst-libs/gst/interfaces/libgstphotography-@GST_API_VERSION@.la \
	$(top_builddir)/gst-libs/gst/insertbin/libgstinsertbin-@GST_API_VERSION@.la \
	$(top_builddir)/gst-libs/gst/uridownloader/libgsturidownloader-@GST_API_VERSION@.la \
	$(top_builddir)/gst-libs/gst/mpegts/libgstmpegts-@GST_API_VERSION@.la \
	$(top_builddir)/gst-libs/gst/gl/libgstgl-@GST_API_VERSION@.la \
  $(GST_BASE_LIBS)

GTKDOC_CC=$(LIBTOOL) --tag=CC --mode=compile $(CC)
GTKDOC_LD=$(LIBTOOL) --tag=CC --mode=link $(CC)

# If you need to override some of the declarations, place them in this file
# and uncomment this line.
DOC_OVERRIDES = $(DOC_MODULE)-overrides.txt

include $(top_srcdir)/common/gtk-doc.mak

