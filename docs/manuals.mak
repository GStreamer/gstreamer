
# taken from selfdocbookx, http://cyberelk.net/tim/docbook/selfdocbookx/index.html

# modified by andy wingo <apwingo@eos.ncsu.edu> 14 dec 2001 for use by gstreamer
# and a little bit by thomas as well

all: html ps pdf

check:
	xmllint -noout -valid $(MAIN)

if     HAVE_XSLTPROC

if     HAVE_FIG2DEV_PNG
html: $(DOC)
else  #!HAVE_FIG2DEV_PNG
html:
endif #!HAVE_FIG2DEV_PNG

if     HAVE_FIG2DEV_PDF
if     HAVE_PDFXMLTEX
pdf: $(DOC).pdf

if     HAVE_PDFTOPS
ps: $(DOC).ps
else  #!HAVE_PDFTOPS
ps:
endif #!HAVE_PDFTOPS

else  #!HAVE_PDFXMLTEX
pdf:
ps:
endif #!HAVE_PDFXMLTEX

else  #!HAVE_FIG2DEV_PDF
pdf:
ps:
endif #!HAVE_FIG2DEV_PDF

else  #!HAVE_XSLTPROC
html:
ps:
pdf:
endif #!HAVE_XSLTPROC

#$(DOC).fo: $(XML) $(PDFS) $(XSLFO) $(XSLFOMODS)
#	cp magic-pdf magic
#	xsltproc $(XSLTPROC_OPTIONS) $(XSLFO) $(MAIN) > $@-t
#	mv -f $@-t $@
#
#$(DOC).pdf: $(DOC).fo
#	pdfxmltex $< || true
#	pdfxmltex $< || true
#
#$(DOC).ps: $(DOC).pdf
#	pdftops $< $@

# thomasvs: use db2 because it seems a lot better
# this ought to be checked for in configure, and the old stuff removed
$(DOC).pdf: $(DOC).xml
	db2pdf $(DOC).xml

$(DOC).ps: $(DOC).xml
	db2ps $(DOC).xml

$(DOC): $(XML) $(PNGS) $(XSLHTML) $(XSLHTMLMODS)
	-$(RM) *.html
	-$(RM) -r $@
	mkdir $@
	cp magic-png magic
	xsltproc $(XSLTPROC_OPTIONS) $(XSLHTML) $(MAIN)
	mv *.html $@
	cp $(CSS) $@
	test "x$(PNGS)" != "x" && mkdir $@/images && cp $(PNGS) $@/images || true

builddate:
	echo -n $$(date "+%e %B %Y") > $@

clean:
	-$(RM) -f *.log *.dvi *.aux *.tex *.out *-t
	-$(RM) -f $(PNGS) $(PDFS) builddate *.html
	-$(RM) -rf $(DOC) $(DOC).ps $(DOC).pdf $(DOC).fo
	-$(RM) -f magic

distclean: clean
	-$(RM) -f *~ $(DOC).tar.gz docbook.tar.gz
#	-$(RM) -r docbook

$(DOC).tar.gz: distclean
	(cd ..; tar zcf /tmp/$(DOC).tar.gz $(DOC) )
	mv /tmp/$(DOC).tar.gz .

#docbook: $(DOC).tar.gz all
#	-$(RM) -r $@
#	mkdir $@
#	cp $(DOC).tar.gz $(DOC).ps $(DOC).pdf $@
#	tar cf - $(DOC) | (cd $@; tar xf -)

#docbook.tar.gz: docbook
#	tar zcf docbook.tar.gz docbook

# Make png from xfig
%.png: %.fig
	fig2dev -Lpng $< $@

# Make pdf from xfig
%.pdf: %.fig
	fig2dev -Lpdf $< images/$@

.PHONY: distclean clean all builddate
