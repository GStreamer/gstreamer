ACLOCAL_AMFLAGS = -I m4 ${ACLOCAL_FLAGS}

AUTOMAKE_OPTIONS = foreign

SUBDIRS = gst-libs gst tests docs

# Extra clean files so that maintainer-clean removes *everything*
MAINTAINERCLEANFILES = \
	$(GITIGNORE_MAINTAINERCLEANFILES_TOPLEVEL)	\
	$(GITIGNORE_MAINTAINERCLEANFILES_MAKEFILE_IN)	\
	$(GITIGNORE_MAINTAINERCLEANFILES_M4_LIBTOOL)	\
	$(srcdir)/gtk-doc.make $(srcdir)/m4/gtk-doc.m4	\
	$(NULL)

-include $(top_srcdir)/git.mk
