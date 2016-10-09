HOTDOC ?= hotdoc
prefix ?= /usr
datadir ?= ${prefix}/share
devhelpdir ?= ${datadir}/devhelp/books
docdir ?= ${datadir}/doc/${PACKAGE}
htmldir ?= ${docdir}

# emulate Automake well enough for hotdoc.mk
AM_V_GEN ?=
AMTAR ?= tar
mkinstalldirs ?= install -d
srcdir = $(CURDIR)
top_srcdir = $(CURDIR)
builddir = $(CURDIR)
top_builddir = $(CURDIR)

HOTDOC_PROJECTS = gst-docs

gst_docs_HOTDOC_FLAGS = \
	--conf-file hotdoc.json \
	$(NULL)

all:

install:

clean:

.PHONY: all install clean

-include $(shell $(HOTDOC) --makefile-path)
