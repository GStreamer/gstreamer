DOC=gstreamer-faq
MAIN=$(DOC).xml
XML=$(wildcard *.xml)
XSLFO=$(srcdir)/../xsl/fo.xsl
XSLFOMODS=$(srcdir)/../xsl/ulink.xsl $(srcdir)/../xsl/keycombo.xsl
XSLHTML=$(srcdir)/../xsl/html.xsl
XSLHTMLMODS=$(srcdir)/../xsl/fileext.xsl $(srcdir)/../xsl/admon.xsl \
	$(srcdir)/../xsl/keycombo.xsl $(srcdir)/../xsl/css.xsl
XSLS=$(XSLFO) $(XSLFOMODS) $(XSLHTML) $(XSLHTMLMODS)
SRC=$(XML)
CSS=base.css

EXTRA_DIST = $(XML) $(CSS)

include $(srcdir)/../manuals.mak
