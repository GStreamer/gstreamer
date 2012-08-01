DOCS = \
	AUTHORS		\
	COPYING		\
	COPYING.LIB	\
	NEWS		\
	README		\
	$(NULL)

DEBIANFILES = \
	changelog						\
	changelog.in						\
	compat							\
	control							\
	control.in						\
	copyright						\
	gstreamer-vaapi.install.in				\
	gstreamer$(GST_MAJORMINOR)-vaapi.install		\
	gstreamer-vaapi-doc.install.in				\
	gstreamer$(GST_MAJORMINOR)-vaapi-doc.install		\
	libgstvaapi.install.in					\
	libgstvaapi$(GST_VAAPI_MAJOR_VERSION).install		\
	libgstvaapi-dev.install.in				\
	libgstvaapi-dev.install					\
	libgstvaapi-drm.install.in				\
	libgstvaapi-drm-$(GST_VAAPI_MAJOR_VERSION).install	\
	libgstvaapi-x11.install.in				\
	libgstvaapi-x11-$(GST_VAAPI_MAJOR_VERSION).install	\
	libgstvaapi-glx.install.in				\
	libgstvaapi-glx-$(GST_VAAPI_MAJOR_VERSION).install	\
	libgstvaapi-wayland.install.in				\
	libgstvaapi-wayland-$(GST_VAAPI_MAJOR_VERSION).install	\
	rules							\
	$(NULL)

DEBIANGENFILES = \
	control							\
	changelog						\
	gstreamer$(GST_MAJORMINOR)-vaapi.install		\
	gstreamer$(GST_MAJORMINOR)-vaapi-doc.install		\
	libgstvaapi$(GST_VAAPI_MAJOR_VERSION).install		\
	libgstvaapi-dev.install					\
	libgstvaapi-drm-$(GST_VAAPI_MAJOR_VERSION).install	\
	libgstvaapi-x11-$(GST_VAAPI_MAJOR_VERSION).install	\
	libgstvaapi-glx-$(GST_VAAPI_MAJOR_VERSION).install	\
	libgstvaapi-wayland-$(GST_VAAPI_MAJOR_VERSION).install	\
	$(NULL)

EXTRA_DIST = \
	$(DEBIANFILES)

# Extra clean files so that maintainer-clean removes *everything*
MAINTAINERCLEANFILES = Makefile.in $(DEBIANGENFILES)
