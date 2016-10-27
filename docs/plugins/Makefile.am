GST_DOC_SCANOBJ = $(top_srcdir)/common/gstdoc-scangobj

## Process this file with automake to produce Makefile.in

# The name of the module, e.g. 'glib'.
MODULE=gstreamer-vaapi
DOC_MODULE=$(MODULE)-plugins

# for upload-doc.mak
DOC=$(MODULE)-plugins
FORMATS=html
html: html-build.stamp
include $(top_srcdir)/common/upload-doc.mak

# The top-level SGML file. You can change this if you want to.
DOC_MAIN_SGML_FILE = $(DOC_MODULE)-docs.xml

# The directory containing the source code. Relative to $(srcdir).
# gtk-doc will search all .c & .h files beneath here for inline comments
# documenting the functions and macros.
DOC_SOURCE_DIR = $(top_srcdir)/gst

# Extra options to supply to gtkdoc-scan.
SCAN_OPTIONS = --deprecated-guards="GST_VAAPI_DISABLE_DEPRECATED"

# Extra options to supply to gtkdoc-mkdb.
MKDB_OPTIONS = --sgml-mode --source-suffixes=c,h,cc,m

# Extra options to supply to gtkdoc-fixref.
FIXXREF_OPTIONS=--extra-dir=$(GLIB_PREFIX)/share/gtk-doc/html \
	--extra-dir=$(GST_PREFIX)/share/gtk-doc/html \
	--extra-dir=$(GSTPB_PREFIX)/share/gtk-doc/html

# Used for dependencies. The docs will be rebuilt if any of these change.
# e.g. HFILE_GLOB=$(top_srcdir)/gtk/*.h
# e.g. CFILE_GLOB=$(top_srcdir)/gtk/*.c
HFILE_GLOB = $(top_srcdir)/gst/*/*.h
CFILE_GLOB = $(top_srcdir)/gst/*/*.c

# Header files to ignore when scanning.
IGNORE_HFILES =
EXTRA_HFILES =

# we add all .h files of elements that have signals/args we want
# sadly this also pulls in the private methods - maybe we should
# move those around in the source ?
#
# also, we should add some stuff here conditionally based on whether
# or not the plugin will actually build
# but I'm not sure about that - it might be this Just Works given that
# the registry won't have the element
# -> it just works (TM) (ensonic)

# FIXME: not ported yet
#	$(top_srcdir)/ext/gnomevfs/gstgnomevfssink.c

# example code that needs to be converted to xml and placed in xml/
EXAMPLE_CFILES =

# Images to copy into HTML directory.
HTML_IMAGES =

# Extra SGML files that are included by $(DOC_MAIN_SGML_FILE).
content_files = running.xml

# Other files to distribute.
extra_files =

# CFLAGS and LDFLAGS for compiling scan program. Only needed if your app/lib
# contains GtkObjects/GObjects and you want to document signals and properties.
GTKDOC_CFLAGS = -DGST_USE_UNSTABLE_API $(GST_PLUGINS_BAD_CFLAGS) $(GST_BASE_CFLAGS) -I$(top_builddir) -I$(top_builddir)/gst-libs/
GTKDOC_LIBS = $(top_builddir)/gst-libs/gst/vaapi/libgstvaapi.la $(top_builddir)/gst/vaapi/libgstvaapi.la $(GST_BASE_LIBS)

# If you need to override some of the declarations, place them in this file
# and uncomment this line.
#DOC_OVERRIDES = $(DOC_MODULE)-overrides.txt
DOC_OVERRIDES =

# This includes the standard gtk-doc make rules, copied by gtkdocize.
include $(top_srcdir)/common/gtk-doc-plugins.mak

SUBDIRS =

# Extra clean files so that maintainer-clean removes *everything*
MAINTAINERCLEANFILES += \
        gstreamer-vaapi-plugins.* \
	*.stamp inspect \
	gstreamer-vaapi-plugins-overrides.txt \
	gstreamer-vaapi-plugins-sections.new \
	$(NULL)

-include $(top_srcdir)/git.mk
