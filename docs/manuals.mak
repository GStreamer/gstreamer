# rewritten by Thomas to be more simple and working

# SF username
USERNAME ?= thomasvs

### These are all generic; we set all the variables we need

# intermediary build path
BUILDDIR = build
# same for images
BUILDIMAGESDIR = $(BUILDDIR)/images

# images
# right now, we only allow .png and .fig as source
# we might add more later if we feel the need

# PNG's can be source or built from .fig
PNG = $(strip $(PNG_SRC) $(FIG_SRC:.fig=.png))
# EPS .ps files can be built from .png or .fig
EPS = $(strip $(FIG_SRC:.fig=.ps) $(PNG_SRC:.png=.ps))
# PDF .pdf files can be built from .png or .fig
PDF = $(strip $(FIG_SRC:.fig=.pdf) $(PNG_SRC:.png=.pdf))

# where we expect to find images during building, whether by copying
# or by generating them
PNG_BUILT = $(foreach file, $(PNG), $(BUILDIMAGESDIR)/$(file))
EPS_BUILT = $(foreach file, $(EPS), $(BUILDIMAGESDIR)/$(file))
PDF_BUILT = $(foreach file, $(PDF), $(BUILDIMAGESDIR)/$(file))

# everything considered source
SRC = $(XML) $(PNG_SRC) $(FIG_SRC) $(CSS)

# generate A4 docs
PAPER_LOCALE = nl_NL

### generate all documentation by default
all: html ps pdf

# can we generate HTML ?
if     DOC_HTML
HTML_DAT = html
html: html/index.html
else  #!DOC_HTML
HTML_DAT =
html:
endif #DOC_HTML

# can we generate PS ?
if     DOC_PS
PS_DAT = $(DOC).ps
ps: $(DOC).ps
else  #!DOC_PS
PS_DAT =
ps:
endif #DOC_PS

# can we generate PDF ?
if     DOC_PDF
PDF_DAT = $(DOC).pdf
pdf: $(DOC).pdf
else  #!DOC_PDF
PDF_DAT =
pdf:
endif #DOC_PDF

debug:
	@echo "outputting some useful debug information"
	@echo "Source XML:"
	@echo "XML: '$(XML)'"
	@echo "CSS: '$(CSS)'"
	@echo "Source image files:"
	@echo "PNG_SRC: '$(PNG_SRC)'"
	@echo "FIG_SRC: '$(FIG_SRC)'"
	@echo "All used image files:"
	@echo "PNG: '$(PNG)'"
	@echo "EPS: '$(EPS)'"
	@echo "PDF: '$(PDF)'"
	@echo "All used image files in their built path:"
	@echo "PNG_BUILT: '$(PNG_BUILT)'"
	@echo "EPS_BUILT: '$(EPS_BUILT)'"
	@echo "PDF_BUILT: '$(PDF_BUILT)'"
	@echo "End result products:"
	@echo "HTML_DAT: '$(HTML_DAT)'"
	@echo "PS_DAT:   '$(PS_DAT)'"
	@echo "PDF_DAT:  '$(PDF_DAT)'"

# a rule to copy all of the source for docs into $(builddir)/build
$(BUILDDIR)/$(MAIN): $(XML) $(CSS)
	@-mkdir -p $(BUILDDIR)
	@for a in $(XML); do cp $(srcdir)/$$a $(BUILDDIR); done
	@for a in $(CSS); do cp $(srcdir)/$$a $(BUILDDIR); done
	@cp ../version.entities $(BUILDDIR)

html/index.html: $(BUILDDIR)/$(MAIN) $(PNG_BUILT) $(FIG_SRC)
	@echo "*** Generating HTML output ***"
	@-mkdir -p html
	@cp $(srcdir)/../image-png $(BUILDDIR)/image.entities
	@cd $(BUILDDIR) && xmlto html -o ../html $(MAIN)
	@test "x$(CSS)" != "x" && \
          echo "Copying .css files: $(CSS)" && \
          cp $(srcdir)/$(CSS) html
	@test "x$(PNG)" != "x" && \
          echo "Copying .png images: $(PNG_BUILT)" && \
	  mkdir -p html/images && \
          cp $(PNG_BUILT) html/images || true

$(DOC).ps: $(BUILDDIR)/$(MAIN) $(EPS_BUILT) $(PNG_SRC) $(FIG_SRC)
	@echo "*** Generating PS output ***"
	@cp $(srcdir)/../image-eps $(BUILDDIR)/image.entities
	export LC_PAPER=$(PAPER_LOCALE) && cd $(BUILDDIR) && xmlto ps -o .. $(MAIN)

$(DOC).pdf: $(DOC).ps
	@echo "*** Generating PDF output ***"
	@ps2pdf $(DOC).ps

#$(DOC).pdf: $(MAIN) $(PDF) $(FIG_SRC)
#	@echo "*** Generating PDF output ***"
#	@cp $(srcdir)/../image-pdf image.entities
#	@export LC_PAPER=$(PAPER_LOCALE) && xmlto pdf $(MAIN)
#	@rm image.entities

clean-local:
	-$(RM) -r $(BUILDDIR)
	-$(RM) -r html
	-$(RM) $(DOC).ps
	-$(RM) $(DOC).pdf
	-$(RM) -r www

### image generation

# copy png from source dir png
$(BUILDIMAGESDIR)/%.png: $(srcdir)/%.png
	@echo "Copying $< to $@"
	@mkdir -p $(BUILDIMAGESDIR)
	@cp $< $@
# make png from fig
$(BUILDIMAGESDIR)/%.png: %.fig
	@echo "Generating $@ from $<"
	@mkdir -p $(BUILDIMAGESDIR)
	@fig2dev -Lpng $< $@

# make ps(EPS) from fig
$(BUILDIMAGESDIR)/%.ps: %.fig
	@echo "Generating $@ from $<"
	@mkdir -p $(BUILDIMAGESDIR)
	@fig2dev -Leps $< $@

# make pdf from fig
$(BUILDIMAGESDIR)/%.pdf: %.fig
	@echo "Generating $@ from $<"
	@mkdir -p $(BUILDIMAGESDIR)
	@fig2dev -Lpdf $< $@

# make pdf from png
$(BUILDIMAGESDIR)/%.pdf: %.png
	@echo "Generating $@ from $<"
	@mkdir -p $(BUILDIMAGESDIR)
	@cat $< | pngtopnm | pnmtops -noturn 2> /dev/null | epstopdf --filter --outfile $@ 2> /dev/null

# make ps(EPS) from png
$(BUILDIMAGESDIR)/%.ps: %.png
	@echo "Generating $@ from $<"
	@mkdir -p $(BUILDIMAGESDIR)
	@cat $< | pngtopnm | pnmtops -noturn > $@ 2> /dev/null

# make sure xml validates properly
#check-local:
#	xmllint -noout -valid $(srcdir)/$(MAIN)

### this is a website upload target

upload: html ps pdf
	@export RSYNC_RSH=ssh; \
	if test "x$$GST_PLUGINS_VERSION_NANO" = x0; then \
            export DOCVERSION=$(VERSION); \
        else export DOCVERSION=cvs; \
        fi; \
	echo Uploading docs to shell.sf.net/home/groups/g/gs/gstreamer/htdocs/docs/$$DOCVERSION; \
	ssh $(USERNAME)@shell.sf.net mkdir -p /home/groups/g/gs/gstreamer/htdocs/docs/$$DOCVERSION/$(DOC); \
	rsync -arv $(DOC).ps $(DOC).pdf html $(USERNAME)@shell.sf.net:/home/groups/g/gs/gstreamer/htdocs/docs/$$DOCVERSION/$(DOC)

