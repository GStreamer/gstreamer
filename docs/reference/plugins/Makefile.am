## Process this file with automake to produce Makefile.in

# We require automake 1.6 at least.
AUTOMAKE_OPTIONS = 1.6

# This is a blank Makefile.am for using gtk-doc.
# Copy this to your project's API docs directory and modify the variables to
# suit your project. See the GTK+ Makefiles in gtk+/docs/reference for examples
# of using the various options.

# The name of the module, e.g. 'glib'.
DOC_MODULE = plugins

# The top-level SGML file. You can change this if you want to.
DOC_MAIN_SGML_FILE = $(DOC_MODULE)-docs.xml

# The directory containing the source code. Relative to $(srcdir).
# gtk-doc will search all .c & .h files beneath here for inline comments
# documenting the functions and macros.
# e.g. DOC_SOURCE_DIR=../../../gtk
DOC_SOURCE_DIR = $(top_srcdir)/gst

# Extra options to pass to gtkdoc-scangobj. Not normally needed.
SCANGOBJ_OPTIONS = --type-init-func="gst_init(NULL, NULL)"

# Extra options to supply to gtkdoc-scan.
# e.g. SCAN_OPTIONS=--deprecated-guards="GTK_DISABLE_DEPRECATED"
SCAN_OPTIONS = --deprecated-guards="GST_VAAPI_DISABLE_DEPRECATED"

# Extra options to supply to gtkdoc-mkdb.
# e.g. MKDB_OPTIONS=--sgml-mode --output-format=xml
MKDB_OPTIONS = --sgml-mode --output-format=xml --name-space=$(DOC_MODULE)

# Extra options to supply to gtkdoc-mktmpl
# e.g. MKTMPL_OPTIONS=--only-section-tmpl
MKTMPL_OPTIONS =

# Extra options to supply to gtkdoc-fixref. Not normally needed.
# e.g. FIXXREF_OPTIONS=--extra-dir=../gdk-pixbuf/html --extra-dir=../gdk/html
FIXXREF_OPTIONS = \
	--extra-dir=$(GLIB_PREFIX)/share/gtk-doc/html/glib \
	--extra-dir=$(GLIB_PREFIX)/share/gtk-doc/html/gobject \
	--extra-dir=$(CAIRO_PREFIX)/share/gtk-doc/html/cairo \
	--extra-dir=$(PANGO_PREFIX)/share/gtk-doc/html/pango

# Used for dependencies. The docs will be rebuilt if any of these change.
# e.g. HFILE_GLOB=$(top_srcdir)/gtk/*.h
# e.g. CFILE_GLOB=$(top_srcdir)/gtk/*.c
HFILE_GLOB = $(top_srcdir)/gst/*/*.h
CFILE_GLOB = $(top_srcdir)/gst/*/*.c

# Header files to ignore when scanning.
# e.g. IGNORE_HFILES=gtkdebug.h gtkintl.h
IGNORE_HFILES = \
	$(NULL)

EXTRA_HFILES = \
	$(NULL)

# Images to copy into HTML directory.
# e.g. HTML_IMAGES=$(top_srcdir)/gtk/stock-icons/stock_about_24.png
HTML_IMAGES = \
	$(NULL)

# Extra SGML files that are included by $(DOC_MAIN_SGML_FILE).
# e.g. content_files=running.sgml building.sgml changes-2.0.sgml
content_files = \
	$(NULL)

# SGML files where gtk-doc abbrevations (#GtkWidget) are expanded
# These files must be listed here *and* in content_files
# e.g. expand_content_files=running.sgml
expand_content_files = \
	$(NULL)

# CFLAGS and LDFLAGS for compiling gtkdoc-scangobj with your library.
# Only needed if you are using gtkdoc-scangobj to dynamically query widget
# signals and properties.
# e.g. INCLUDES=-I$(top_srcdir) -I$(top_builddir) $(GTK_DEBUG_FLAGS)
# e.g. GTKDOC_LIBS=$(top_builddir)/gtk/$(gtktargetlib)

INCLUDES = \
	-I$(top_srcdir) \
	-I$(top_srcdir)/gst-libs \
	-I$(top_srcdir)/gst-libs/gst/vaapi \
	$(GLIB_CFLAGS) \
	$(GST_CFLAGS) \
	$(NULL)

GTKDOC_LIBS = \
	$(top_builddir)/gst-libs/gst/vaapi/libgstvaapi-$(GST_MAJORMINOR).la \
	$(top_builddir)/gst-libs/gst/vaapi/libgstvaapi-x11-$(GST_MAJORMINOR).la \
	$(top_builddir)/gst/vaapi/libgstvaapi.la \
	$(GLIB_LIBS) \
	$(GST_LIBS) \
	$(NULL)

# This includes the standard gtk-doc make rules, copied by gtkdocize.
include $(top_srcdir)/gtk-doc.make

# Other files to distribute
# e.g. EXTRA_DIST += version.xml.in
EXTRA_DIST += \
	$(DOC_MODULE)-docs.xml.in \
	$(NULL)

# Extra clean files so that maintainer-clean removes *everything*
MAINTAINERCLEANFILES = Makefile.in $(DOC_MODULE)-docs.xml
