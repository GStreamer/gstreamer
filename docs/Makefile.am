XMLFILES = gst-python.xml
XSLFILES = common.xsl html.xsl
CSSFILES = gst-python.css

DOCS = gst-python.txt gst-python.html

all: $(DOCS)

gst-python.txt: gst-python.xml
	xmlto txt $<
#	cp gst-python.txt $(top_builddir)/../README

gst-python.html: gst-python.xml common.xsl html.xsl
	xmlto -m common.xsl -m html.xsl xhtml-nochunks $<

gst-python.pdf: gst-python.xml common.xsl pdf.xsl
	xmlto -m common.xsl -m pdf.xsl pdf $<

CLEANFILES = $(DOCS)

EXTRA_DIST = $(XMLFILES) $(XSLFILES) $(CSSFILES)
