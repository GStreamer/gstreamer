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
SRC = $(XML) $(PNG_SRC) $(FIG_SRC) $(CSS) $(EXTRA_SOURCES)

# generate A4 docs
PAPER_LOCALE = nl_NL

### generate all documentation by default
all: html ps pdf

# can we generate HTML ?
if     DOC_HTML
HTML_DAT = html
HTML_TARGET = html/index.html
else  #!DOC_HTML
HTML_DAT =
HTML_TARGET =
endif #DOC_HTML
html: $(HTML_TARGET)

# can we generate PS ?
if     DOC_PS
PS_DAT = $(DOC).ps
else  #!DOC_PS
PS_DAT =
endif #DOC_PS
ps: $(PS_DAT)

# can we generate PDF ?
if     DOC_PDF
PDF_DAT = $(DOC).pdf
else  #!DOC_PDF
PDF_DAT =
endif #DOC_PDF
pdf: $(PDF_DAT)

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
$(BUILDDIR)/$(MAIN): $(XML) $(CSS) $(EXTRA_SOURCES)
	@-mkdir -p $(BUILDDIR)
	@if test "x$(EXTRA_SOURCES)" != "x"; then for a in $(EXTRA_SOURCES); do cp $(srcdir)/$$a $(BUILDDIR); done; fi
	@for a in $(XML); do cp $(srcdir)/$$a $(BUILDDIR); done
	@for a in $(CSS); do cp $(srcdir)/$$a $(BUILDDIR); done
	@cp ../version.entities $(BUILDDIR)

html/index.html: $(BUILDDIR)/$(MAIN) $(PNG_BUILT) $(FIG_SRC)
	@make check-local
	@echo "*** Generating HTML output ***"
	@-mkdir -p html
	@cp -f $(srcdir)/../image-png $(BUILDDIR)/image.entities
	@cd $(BUILDDIR) && docbook2html -o ../html -V '%use-id-as-filename%' $(MAIN)
	@test "x$(CSS)" != "x" && \
          echo "Copying .css files: $(CSS)" && \
          cp $(srcdir)/$(CSS) html
	@test "x$(PNG)" != "x" && \
          echo "Copying .png images: $(PNG_BUILT)" && \
	  mkdir -p html/images && \
          cp $(PNG_BUILT) html/images || true

$(DOC).ps: $(BUILDDIR)/$(MAIN) $(EPS_BUILT) $(PNG_SRC) $(FIG_SRC)
	@make check-local
	@echo "*** Generating PS output ***"
	@cp -f $(srcdir)/../image-eps $(BUILDDIR)/image.entities
	cd $(BUILDDIR) && docbook2ps -o .. $(MAIN)
#	export LC_PAPER=$(PAPER_LOCALE) && cd $(BUILDDIR) && xmlto ps -o .. $(MAIN)

$(DOC).pdf: $(DOC).ps
	@make check-local
	@echo "*** Generating PDF output ***"
	@ps2pdf $(DOC).ps

#$(DOC).pdf: $(MAIN) $(PDF) $(FIG_SRC)
#	@echo "*** Generating PDF output ***"
#	@cp -f $(srcdir)/../image-pdf image.entities
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
check-local: $(BUILDDIR)/$(MAIN)
	@cp -f $(srcdir)/../image-png $(BUILDDIR)/image.entities
	cd $(BUILDDIR) && xmllint -noout -valid $(MAIN)
