READMEXMLFILES = README.xml NEWS.xml TODO.xml AUTHORS.xml
READMEFILES = $(READMEXMLFILES) gst-python.ent

RELNOTESXMLFILES = RELNOTES.xml
RELNOTESFILES = $(RELNOTESXMLFILES) $(READMEXMLFILES) gst-python.ent

XMLFILES = $(READMEXMLFILES) $(RELNOTESXMLFILES)
XSLFILES = common.xsl full.xsl part.xsl html.xsl pdf.xsl
CSSFILES = gst-python.css

EXTRA_DIST = $(XMLFILES) $(XSLFILES) $(CSSFILES)

DOCS = README.txt README.html #gst-python.pdf
TOPDOCS = README.txt NEWS.txt TODO.txt AUTHORS.txt
RELNOTES = RELNOTES.txt

CLEANFILES = $(DOCS) $(TOPDOCS) $(RELNOTES) tmpbuildcatalog

if BUILD_DOCS

XMLTO = xmlto
XMLTOFLAGS = --skip-validation -o $(CURDIR)
XMLLINT = xmllint
XMLLINTFLAGS = --xinclude --noout --loaddtd --catalogs --postvalid

all: $(DOCS)

tmpbuildcatalog: gst-python.ent
	xmlcatalog --noout --create $@
	xmlcatalog --noout --add system `cd $(srcdir) && pwd`/gst-python.ent $(CURDIR)/gst-python.ent $@
	xmlcatalog --noout --add system $(srcdir)/gst-python.ent $(CURDIR)/gst-python.ent $@

check: tmpbuildcatalog
	for d in $(XMLFILES); do \
		$(XMLLINT) $(XMLLINTFLAGS) $(srcdir)/$$d; \
	done

dist-docs: $(TOPDOCS)

dist-hook: dist-docs
	for d in $(TOPDOCS); do \
		install -m 444 $$d $(distdir)/../`basename $$d .txt`; \
	done

# full docs

# filter out xsl deps and make xslto flags
xmltoxslflags = $(foreach xsl,$(filter %xsl,$(filter-out $(1),$(2))),-m $(srcdir)/$(xsl))
FULLDEPS = common.xsl full.xsl tmpbuildcatalog

README.txt: $(READMEFILES) $(FULLDEPS)
	$(XMLTO) $(XMLTOFLAGS) $(call xmltoxslflags,$<,$+) txt $<

README.html: $(READMEFILES) $(FULLDEPS) html.xsl
	$(XMLTO) $(XMLTOFLAGS) $(call xmltoxslflags,$<,$+) xhtml-nochunks $<

README.pdf: $(READMEFILES) $(FULLDEPS) pdf.xsl
	$(XMLTO) $(XMLTOFLAGS) $(call xmltoxslflags,$<,$+) pdf $<

# partial docs

PARTIALDEPS = gst-python.ent common.xsl part.xsl tmpbuildcatalog

RELNOTES.txt: $(RELNOTESFILES) $(PARTIALDEPS)
	$(XMLTO) $(XMLTOFLAGS) $(call xmltoxslflags,$<,$+) txt $<

%.txt: %.xml $(PARTIALDEPS)
	$(XMLTO) $(XMLTOFLAGS) $(call xmltoxslflags,$<,$+) txt $<

else

dist-hook:
	@echo "Error: Doc building must be enabled for autogeneration of README, NEWS, TODO, and AUTHORS for distribution."
	@false

endif
