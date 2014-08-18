GST_DOC_SCANOBJ = $(top_srcdir)/common/gstdoc-scangobj
## Process this file with automake to produce Makefile.in

# The name of the module, e.g. 'glib'.
MODULE=gst-validate
DOC_MODULE=gst-validate

# don't want $(DOC_MODULE)-scan.c to be built with -Werror
ERROR_CFLAGS=

# for upload-doc.mak
DOC=gst-validate
FORMATS=html
html: html-build.stamp
include $(top_srcdir)/common/upload-doc.mak

# The top-level SGML file. Change it if you want.
DOC_MAIN_SGML_FILE=$(DOC_MODULE)-docs.sgml

# The directory containing the source code.
# gtk-doc will search all .c & .h files beneath here for inline comments
# documenting functions and macros.
DOC_SOURCE_DIR=$(top_srcdir)/gst/validate/

# Extra options to supply to gtkdoc-scan.
SCAN_OPTIONS=--deprecated-guards="GST_DISABLE_DEPRECATED"

# Extra options to supply to gtkdoc-mkdb.
MKDB_OPTIONS=--sgml-mode --output-format=xml

# Extra options to supply to gtkdoc-fixref.
FIXXREF_OPTIONS=--extra-dir=$(top_builddir)/docs/gst/html \
	--extra-dir=$(GLIB_PREFIX)/share/gtk-doc/html \
	--extra-dir=$(datadir)/gtk-doc/html

# Used for dependencies.
HFILE_GLOB=$(top_srcdir)/gst/validate/gst-validate-scenario.h
CFILE_GLOB=$(top_srcdir)/gst/validate/gst-validate-scenario.c

# Extra options to pass to gtkdoc-scanobj or gtkdoc-scangobj.
SCANOBJ_OPTIONS=--type-init-func="gst_init(&argc,&argv); gst_validate_init()"

# Header files to ignore when scanning.
IGNORE_HFILES = \
	gettext.h \
	gst-validate-internal.h \
	gst-validate-monitor.h \
	gst-validate-bin-monitor.h \
	gst-validate-element-monitor.h \
	gst-validate-pad-monitor.h \
	gst-validate-override.h \
 	gst-validate-override-registry.h \
	gst-validate-utils.h \
	gst-validate-media-info.h  \
	gst-validate-report.h  \
	media-descriptor.h \
	media-descriptor-parser.h \
	media-descriptor-writer.h \
	gst-validate-i18n-lib.h

IGNORE_CFILES = \
	gst-validate-monitor.c \
	gst-validate-bin-monitor.c \
	gst-validate-pad-monitor.c \
	gst-validate-element-monitor.c \
	gst-validate-override.c \
 	gst-validate-override-registry.c \
	gst-validate-utils.c \
	gst-validate-report.c  \
	gst-validate-media-info.c \
	media-descriptor.c \
	media-descriptor-parser.c \
	media-descriptor-writer.c \
	gst-validate-i18n-lib.c

# Images to copy into HTML directory.
# HTML_IMAGES = gdp-header.png

# Extra SGML files that are included by $(DOC_MAIN_SGML_FILE).
content_files =

# Other files to distribute.
extra_files =

# CFLAGS and LDFLAGS for compiling scan program. Only needed if your app/lib
# contains GtkObjects/GObjects and you want to document signals and properties.
GTKDOC_CFLAGS = -I$(top_srcdir) $(GST_PBUTILS_CFLAGS) $(GST_BASE_CFLAGS) \
	$(GST_CFLAGS)  $(GIO_CFLAGS) $(GCOV_CFLAGS)
GTKDOC_LIBS = \
	$(top_builddir)/gst/validate/libgstvalidate-@GST_API_VERSION@.la \
	$(GST_BASE_LIBS) $(GST_LIBS) $(GIO_LIBS) $(GCOV_LIBS)

GTKDOC_CC=$(LIBTOOL) --tag=CC --mode=compile $(CC)
GTKDOC_LD=$(LIBTOOL) --tag=CC --mode=link $(CC)

# If you need to override some of the declarations, place them in this file
# and uncomment this line.
DOC_OVERRIDES = $(DOC_MODULE)-overrides.txt

include $(top_srcdir)/common/gtk-doc.mak

