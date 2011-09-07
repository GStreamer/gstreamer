## Process this file with automake to produce Makefile.in

# The name of the module, e.g. 'glib'.
#DOC_MODULE=gstreamer-libs-@GST_MAJORMINOR@
DOC_MODULE=gstreamer-libs

# don't want $(DOC_MODULE)-scan.c to be built with -Werror
ERROR_CFLAGS=

# for upload-doc.mak
DOC=gstreamer-libs
FORMATS=html
html: html-build.stamp
include $(top_srcdir)/common/upload-doc.mak

# The top-level SGML file. Change it if you want.
DOC_MAIN_SGML_FILE=$(DOC_MODULE)-docs.sgml

# The directory containing the source code.
# gtk-doc will search all .c & .h files beneath here for inline comments
# documenting functions and macros.
DOC_SOURCE_DIR=$(top_srcdir)/libs/gst

# Extra options to supply to gtkdoc-scan.
SCAN_OPTIONS=--deprecated-guards="GST_DISABLE_DEPRECATED"

# Extra options to supply to gtkdoc-mkdb.
MKDB_OPTIONS=--sgml-mode --output-format=xml

# Extra options to supply to gtkdoc-fixref.
FIXXREF_OPTIONS=--extra-dir=$(top_builddir)/docs/gst/html \
	--extra-dir=$(GLIB_PREFIX)/share/gtk-doc/html \
	--extra-dir=$(datadir)/gtk-doc/html

# Used for dependencies.
HFILE_GLOB=$(top_srcdir)/libs/gst/*/*.h
CFILE_GLOB=$(top_srcdir)/libs/gst/*/*.c

# Extra options to pass to gtkdoc-scanobj or gtkdoc-scangobj.
SCANOBJ_OPTIONS=--type-init-func="g_type_init();gst_init(&argc,&argv)"

# Header files to ignore when scanning.
IGNORE_HFILES = \
	gstinterpolationcontrolsourceprivate.h \
	gstlfocontrolsourceprivate.h \
	gstcontrollerprivate.h \
	gstgetbits_inl.h \
	dp-private.h

# Images to copy into HTML directory.
HTML_IMAGES = gdp-header.png

# Extra SGML files that are included by $(DOC_MAIN_SGML_FILE).
content_files =

# Other files to distribute.
extra_files =

# CFLAGS and LDFLAGS for compiling scan program. Only needed if your app/lib
# contains GtkObjects/GObjects and you want to document signals and properties.
GTKDOC_CFLAGS = -I$(top_builddir) -I$(top_builddir)/libs $(GST_OBJ_CFLAGS)
GTKDOC_LIBS = \
	$(top_builddir)/libs/gst/controller/libgstcontroller-@GST_MAJORMINOR@.la \
	$(top_builddir)/libs/gst/base/libgstbase-@GST_MAJORMINOR@.la \
	$(top_builddir)/libs/gst/net/libgstnet-@GST_MAJORMINOR@.la \
	$(GST_OBJ_LIBS)

GTKDOC_CC=$(LIBTOOL) --tag=CC --mode=compile $(CC)
GTKDOC_LD=$(LIBTOOL) --tag=CC --mode=link $(CC)

GTKDOC_EXTRA_ENVIRONMENT= \
    GST_PLUGIN_SCANNER=$(top_builddir)/libs/gst/helpers/gst-plugin-scanner

# If you need to override some of the declarations, place them in this file
# and uncomment this line.
DOC_OVERRIDES = $(DOC_MODULE)-overrides.txt

include $(top_srcdir)/common/gtk-doc.mak
