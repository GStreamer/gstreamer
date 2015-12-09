DOCS = \
	AUTHORS		\
	COPYING.LIB	\
	NEWS		\
	README		\
	$(NULL)

DEBIANFILES = \
	changelog.in						\
	compat							\
	control.in						\
	copyright						\
	gstreamer-vaapi-doc.install.in				\
	gstreamer-vaapi.install.in				\
	rules							\
	$(NULL)

DEBIANGENFILES = \
	changelog						\
	control							\
	gstreamer$(GST_API_VERSION)-vaapi-doc.install		\
	gstreamer$(GST_API_VERSION)-vaapi.install		\
	$(NULL)

EXTRA_DIST = $(DEBIANFILES)

dist_noinst_DATA = $(DEBIANGENFILES)
DISTCLEANFILES = $(DEBIANGENFILES)

-include $(top_srcdir)/git.mk
