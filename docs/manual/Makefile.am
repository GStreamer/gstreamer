manualname = gstreamer-manual
htmlname = index.html

sgml_files = $(shell echo $(srcdir)/*.sgml)
fig_files  = $(shell echo $(srcdir)/*.fig)
eps_files  = $(shell echo $(srcdir)/*.eps)
png_files  = $(shell echo $(srcdir)/*.png)

EXTRA_DIST = $(sgml_files) $(fig_files)

## FIXME if we don't do this they don't get cleaned up in make distcheck
## but maybe this is not the best way to do it
CONFIG_CLEAN_FILES = $(sgml_files) $(fig_files)

include $(srcdir)/../manuals.mak

