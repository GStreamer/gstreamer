DOC=gstreamer-manual
MAIN=$(DOC).xml
XML=$(shell cd $(srcdir) && echo *.xml)
XSLFO=$(srcdir)/../xsl/fo.xsl
XSLFOMODS=$(srcdir)/../xsl/ulink.xsl $(srcdir)/../xsl/keycombo.xsl
XSLHTML=$(srcdir)/../xsl/html.xsl
XSLHTMLMODS=$(srcdir)/../xsl/fileext.xsl $(srcdir)/../xsl/admon.xsl \
	$(srcdir)/../xsl/keycombo.xsl $(srcdir)/../xsl/css.xsl
XSLS=$(XSLFO) $(XSLFOMODS) $(XSLHTML) $(XSLHTMLMODS)
FIGS=$(shell cd $(srcdir) && echo *.fig)
PNGS=$(FIGS:.fig=.png)
PDFS=$(FIGS:.fig=.pdf)
SRC=$(XML)
CSS=base.css

EXTRA_DIST = $(XML) $(FIGS) $(CSS) magic-png magic-pdf

include $(srcdir)/../manuals.mak
