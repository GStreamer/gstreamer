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
	libgstvaapi-dev.install.in				\
	libgstvaapi-drm.install.in				\
	libgstvaapi-glx.install.in				\
	libgstvaapi-wayland.install.in				\
	libgstvaapi-x11.install.in				\
	libgstvaapi.install.in					\
	rules							\
	$(NULL)

DEBIANGENFILES = \
	changelog						\
	control							\
	gstreamer$(GST_API_VERSION)-vaapi-doc.install		\
	gstreamer$(GST_API_VERSION)-vaapi.install		\
	libgstvaapi$(GST_VAAPI_MAJOR_VERSION).install		\
	libgstvaapi-dev.install					\
	libgstvaapi-drm-$(GST_VAAPI_MAJOR_VERSION).install	\
	libgstvaapi-glx-$(GST_VAAPI_MAJOR_VERSION).install	\
	libgstvaapi-wayland-$(GST_VAAPI_MAJOR_VERSION).install	\
	libgstvaapi-x11-$(GST_VAAPI_MAJOR_VERSION).install	\
	$(NULL)

EXTRA_DIST = $(DEBIANFILES)

dist_noinst_DATA = $(DEBIANGENFILES)

# Extra clean files so that maintainer-clean removes *everything*
MAINTAINERCLEANFILES = Makefile.in $(DEBIANGENFILES)
