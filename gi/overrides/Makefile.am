common_cflags = $(PYGOBJECT_CFLAGS) $(GST_CFLAGS) -fno-strict-aliasing
common_libadd = $(GST_LIBS) $(PYGOBJECT_LIBS)
common_ldflags = -module -avoid-version -shrext $(PYTHON_SO)

# We install everything in the gi/overrides folder
pygioverridesdir = $(PYGI_OVERRIDES_DIR)
pygioverrides_PYTHON = Gst.py GstPbutils.py

pygioverridesexecdir = $(PYGI_OVERRIDES_DIR)

EXTRA_DIST = Gst.py

INCLUDES = $(PYTHON_INCLUDES)

pygioverridesexec_LTLIBRARIES = _gi_gst.la

_gi_gst_la_SOURCES = gstmodule.c
_gi_gst_la_CFLAGS = $(common_cflags)
_gi_gst_la_LIBADD = $(common_libadd)
_gi_gst_la_LDFLAGS = $(common_ldflags)

# if we build in a separate tree, we need to symlink the *.so files from the
# source tree; Python does not accept the extensions and modules in different
# paths
build_pylinks:
	for f in $(pygioverrides_PYTHON) __init__.py; do \
	    [ -e $(builddir)/$$f ] || $(LN_S) -f $(srcdir)/$$f $(builddir)/$$f; \
	done

all: $(pygioverridesexec_LTLIBRARIES:.la=$(PYTHON_SO)) build_pylinks
check-local: $(pygioverridesexec_LTLIBRARIES:.la=$(PYTHON_SO)) build_pylinks
clean-local:
	rm -f $(pygioverridesexec_LTLIBRARIES:.la=$(PYTHON_SO))

%$(PYTHON_SO): %.la
	$(LN_S) -f .libs/$@ $@

