DOC=gstreamer-manual
MAIN=$(DOC).xml
XML=$(shell echo $(srcdir)/*.xml)
XSLFO=$(srcdir)/../xsl/fo.xsl
XSLFOMODS=$(srcdir)/../xsl/ulink.xsl $(srcdir)/../xsl/keycombo.xsl
XSLHTML=$(srcdir)/../xsl/html.xsl
XSLHTMLMODS=$(srcdir)/../xsl/fileext.xsl $(srcdir)/../xsl/admon.xsl \
	$(srcdir)/../xsl/keycombo.xsl $(srcdir)/../xsl/css.xsl
XSLS=$(XSLFO) $(XSLFOMODS) $(XSLHTML) $(XSLHTMLMODS)
FIGS=$(shell echo $(srcdir)/*.fig)
PNGS=$(FIGS:.fig=.png)
PDFS=$(FIGS:.fig=.pdf)
SRC=$(XML)
CSS=base.css

EXTRA_DIST = $(XML) $(FIGS)

## FIXME if we don't do this they don't get cleaned up in make distcheck
## but maybe this is not the best way to do it
CONFIG_CLEAN_FILES = $(XML) $(FIGS)

include $(srcdir)/../manuals.mak
