all:

install:

clean:

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

theme.stamp: less/variables.less
	+make -C hotdoc_bootstrap_theme LESS_INCLUDE_PATH=$$PWD/less
	@rm -rf hotdoc-private*
	@touch theme.stamp

clean_theme:
	rm -f theme.stamp
	+make -C hotdoc_bootstrap_theme clean

clean: clean_theme

gst_docs_HOTDOC_EXTRA_DEPS = theme.stamp

.PHONY: all install clean

-include $(shell $(HOTDOC) --makefile-path)
