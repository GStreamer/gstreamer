### this is the part you can customize if you need to

# base name of doc
DOC = manual
# formats defined for upload.mak
FORMATS=html ps pdf

# main xml file
MAIN = $(DOC).xml
# all xml sources
XML = $(notdir $(wildcard $(srcdir)/*.xml))
# base style sheet
CSS = base.css

# image sources
PNG_SRC = 
FIG_SRC = $(notdir $(wildcard $(srcdir)/*.fig))

# extra sources to copy in build directory
EXTRA_SOURCES = 

### this is the generic bit and you shouln't need to change this

# get the generic docbuilding Makefile stuff
include $(srcdir)/../manuals.mak
# get the generic upload target
include $(srcdir)/../upload.mak

### this is standard automake stuff

# package up all the source
EXTRA_DIST = $(SRC)

# install documentation
docdir = $(datadir)/doc/$(PACKAGE)-$(VERSION)/$(DOC)
doc_DATA = $(PDF_DAT) $(PS_DAT)

include $(srcdir)/../htmlinstall.mak
