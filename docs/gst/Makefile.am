## Process this file with automake to produce Makefile.in

# The name of the module.
DOC_MODULE=gstreamer

# The top-level SGML file.
DOC_MAIN_SGML_FILE=gstreamer-docs.sgml

# The directory containing the source code (if it contains documentation).
DOC_SOURCE_DIR=$(top_srcdir)/gst

CFLAGS = $(GLIB_CFLAGS) $(GTK_CFLAGS) -I$(top_srcdir)
LDFLAGS = $(GLIB_LIBS) $(GTK_LIBS) $(top_srcdir)/gst/libgst.la

HTML_DIR=$(datadir)/gstreamer/gst/html

TARGET_DIR=$(HTML_DIR)/$(DOC_MODULE)

tmpl_sources =				\
	tmpl/gst.sgml			\
	tmpl/gstbin.sgml		\
	tmpl/gstbuffer.sgml		\
	tmpl/gstconnection.sgml		\
	tmpl/gstelement.sgml		\
	tmpl/gstfilter.sgml		\
	tmpl/gstmeta.sgml		\
	tmpl/gstobject.sgml		\
	tmpl/gstpad.sgml		\
	tmpl/gstpipeline.sgml		\
	tmpl/gstplugin.sgml		\
	tmpl/gstqueue.sgml		\
	tmpl/gstsink.sgml		\
	tmpl/gstsrc.sgml		\
	tmpl/gsttee.sgml		\
	tmpl/gstthread.sgml		\
	tmpl/gsttrace.sgml		\
	tmpl/gsttype.sgml		\
	tmpl/gstutils.sgml		\
	tmpl/gstxml.sgml		\
	tmpl/plugin.sgml		

SCANOBJS_FILES =		\
	$(DOC_MODULE).signals	\
	$(DOC_MODULE).hierarchy	\
	$(DOC_MODULE).args

scanobj:
	env CC="$(LIBTOOL) --mode=link $(CC)" CFLAGS="$(CFLAGS)" LDFLAGS="$(LDFLAGS)" \
		gtkdoc-scanobj --module=$(DOC_MODULE) \
		--types=$(srcdir)/$(DOC_MODULE).types

scan:
	gtkdoc-scan --module=$(DOC_MODULE) --source-dir=$(DOC_SOURCE_DIR) --ignore-headers="gsttypefind.h"

tmpl: scan
	gtkdoc-mktmpl --module=$(DOC_MODULE)

sgml:
	gtkdoc-mkdb --module=$(DOC_MODULE) --source-dir=$(DOC_SOURCE_DIR)

html:
	if ! test -d html ; then mkdir html ; fi
	-cd html && gtkdoc-mkhtml $(DOC_MODULE) ../$(DOC_MAIN_SGML_FILE)

clean-local:
	rm -f *~ *.bak *.hierarchy *.signals *.args *-unused.txt

maintainer-clean-local: clean
	rm -rf sgml html $(DOC_MODULE)-decl-list.txt $(DOC_MODULE)-decl.txt 

install-data-local:
	install -d -m 0755 $(TARGET_DIR)
	install -m 0644 html/*.html $(TARGET_DIR)
	install -m 0644 html/index.sgml $(TARGET_DIR)
	gtkdoc-fixxref --module=$(DOC_MODULE) --html-dir=$(HTML_DIR)

.PHONY : html sgml templates scan
