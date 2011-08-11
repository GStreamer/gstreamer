SUBDIRS = codegen examples testsuite
pkgpyexecdir = $(pyexecdir)

# we install everything in pyexecdir; otherwise you end up with a mess for
# multilib
pygesdir = $(pkgpyexecdir)
pyges_PYTHON =

pygesexecdir = $(pkgpyexecdir)
pygesexec_LTLIBRARIES = ges.la

DEFS = $(srcdir)/ges-types.defs $(srcdir)/ges.defs
defs_DATA = $(DEFS)
defsdir = $(pkgdatadir)/$(GST_MAJORMINOR)/defs
OVERRIDES = ges.override

INCLUDES = -I$(top_srcdir) -I$(srcdir) $(PYTHON_INCLUDES)

ges_la_CFLAGS = -I$(top_srcdir)\
		$(PYGOBJECT_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS) -Wno-write-strings
ges_la_LDFLAGS = -export-symbols-regex "^(initges|_PyGObject_API).*" \
	-module -avoid-version $(GST_PLUGIN_LDFLAGS)
ges_la_LIBADD = $(top_builddir)/ges/libges-@GST_MAJORMINOR@.la \
		$(GST_PLUGINS_BASE_LIBS) $(GST_BASE_LIBS) \
		-lges-@GST_MAJORMINOR@ -lges-@GST_MAJORMINOR@ \
		-lges-@GST_MAJORMINOR@ $(GST_LIBS) $(LIBM)
ges_la_SOURCES = gesmodule.c
nodist_ges_la_SOURCES = ges.c

EXTRA_DIST = $(defs_DATA) $(OVERRIDES) arg-types.py

CLEANFILES = ges.c

.defs.c: 
	($(PYTHON) $(srcdir)/codegen/codegen.py \
		--load-types $(srcdir)/arg-types.py \
		--register $(srcdir)/ges-types.defs \
		--override $(srcdir)/$*.override \
		--prefix pyges $<) > gen-$*.c \
	&& cp gen-$*.c $*.c \
	&& rm -f gen-$*.c

