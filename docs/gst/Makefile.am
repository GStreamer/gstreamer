## Process this file with automake to produce Makefile.in

# The name of the module, e.g. 'glib'.
# DOC_MODULE=gstreamer-@GST_API_VERSION@
DOC_MODULE=gstreamer

# don't want $(DOC_MODULE)-scan.c to be built with -Werror
ERROR_CFLAGS=

BUILT_SOURCES=gstreamer.types

# for upload-doc.mak
DOC=gstreamer
FORMATS=html
html: html-build.stamp
include $(top_srcdir)/common/upload-doc.mak

# The top-level SGML file. Change it if you want.
DOC_MAIN_SGML_FILE=$(DOC_MODULE)-docs.sgml

# The files containing the source code. Relative to $(top_srcdir).
# gtk-doc will search all .c & .h files beneath here for inline comments
# documenting functions and macros.
DOC_SOURCE_DIR = $(top_srcdir)/gst

# Extra options to supply to gtkdoc-scan.
SCAN_OPTIONS=--deprecated-guards="GST_DISABLE_DEPRECATED" \
  --ignore-decorators='GST_EXPORT'

# Extra options to supply to gtkdoc-mkdb.
MKDB_OPTIONS=--sgml-mode --output-format=xml --ignore-files=parse

# Extra options to supply to gtkdoc-fixref.
FIXXREF_OPTIONS=--extra-dir=$(GLIB_PREFIX)/share/gtk-doc/html

# Used for dependencies.
HFILE_GLOB=$(top_srcdir)/gst/*.h
CFILE_GLOB=$(top_srcdir)/gst/*.c

# Header files to ignore when scanning. Use base file name, no paths
IGNORE_HFILES= \
	gettext.h \
	glib-compat-private.h \
	glib-compat.h \
	gst-i18n-app.h \
	gst-i18n-lib.h \
	gst_private.h \
	gstelementdetails.h \
	gstmacros.h \
	gstmarshal.h \
	math-compat.h \
	\
	grammar.tab.h \
	grammar.tab.pre.h \
  parse_lex.h \
	types.h \
	\
	gst-printf.h \
	printf-args.h \
	printf-extension.h \
	printf-parse.h \
	vasnprintf.h

gst-universe.svg: gst-universe.dot
	-dot -Tsvg $< | sed 's/\(font-size:[0-9]*\.[0-9]*\);/\1px;/g' >$@

# Images to copy into HTML directory.
HTML_IMAGES =

# Extra SGML files that are included by $(DOC_MAIN_SGML_FILE).
content_files = building.xml running.xml gst-universe.svg

# Other files to distribute.
extra_files = gst-universe.dot

# CFLAGS and LDFLAGS for compiling scan program. Only needed if your app/lib
# contains GtkObjects/GObjects and you want to document signals and properties.
GTKDOC_CFLAGS = $(GST_OBJ_CFLAGS) -DGST_USE_UNSTABLE_API
GTKDOC_LIBS = $(GST_OBJ_LIBS) \
	$(top_builddir)/gst/libgstreamer-@GST_API_VERSION@.la \
	$(top_builddir)/libs/gst/base/libgstbase-@GST_API_VERSION@.la \
	$(top_builddir)/libs/gst/controller/libgstcontroller-@GST_API_VERSION@.la \
	$(top_builddir)/libs/gst/net/libgstnet-@GST_API_VERSION@.la

GTKDOC_EXTRA_ENVIRONMENT= \
    GST_PLUGIN_SCANNER_1_0=$(top_builddir)/libs/gst/helpers/gst-plugin-scanner

# If you need to override some of the declarations, place them in this file
# and uncomment this line.
DOC_OVERRIDES = $(DOC_MODULE)-overrides.txt

include $(top_srcdir)/common/gtk-doc.mak

# gtk-doc.mak defines CLEANFILES
CLEANFILES += gst-universe.svg

