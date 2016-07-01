noinst_PROGRAMS = \
	test-vaapisink  \
	$(NULL)

TEST_CFLAGS = \
	-DGST_USE_UNSTABLE_API		\
	-I$(top_srcdir)/gst-libs	\
	-I$(top_builddir)/gst-libs	\
	$(LIBVA_CFLAGS)			\
	$(GST_CFLAGS)			\
	$(GST_VIDEO_CFLAGS)		\
	$(NULL)

GST_VAAPI_LIBS = $(top_builddir)/gst-libs/gst/vaapi/libgstvaapi.la
TEST_LIBS = \
	$(LIBVA_LIBS)			\
	$(GST_LIBS)			\
	$(NULL)

if USE_DRM
GST_VAAPI_LIBS  += $(top_builddir)/gst-libs/gst/vaapi/libgstvaapi-drm.la
TEST_CFLAGS	+= $(LIBVA_DRM_CFLAGS)
TEST_LIBS	+= $(LIBVA_DRM_LIBS)
endif

if USE_X11
GST_VAAPI_LIBS  += $(top_builddir)/gst-libs/gst/vaapi/libgstvaapi-x11.la
TEST_CFLAGS	+= $(X11_CFLAGS)
TEST_LIBS	+= \
	$(LIBVA_X11_LIBS)		\
	$(X11_LIBS)			\
	$(NULL)
endif

if USE_GLX
GST_VAAPI_LIBS  += $(top_builddir)/gst-libs/gst/vaapi/libgstvaapi-glx.la
TEST_CFLAGS	+= $(X11_CFLAGS) $(GL_CFLAGS)
TEST_LIBS	+= \
	$(LIBVA_GLX_LIBS)		\
	$(X11_LIBS)			\
	$(GL_LIBS)			\
	$(NULL)
endif

if USE_EGL
GST_VAAPI_LIBS  += $(top_builddir)/gst-libs/gst/vaapi/libgstvaapi-egl.la
TEST_CFLAGS	+= $(EGL_CFLAGS)
TEST_LIBS	+= \
	$(LIBVA_EGL_LIBS)		\
	$(EGL_LIBS)			\
	$(NULL)
endif

if USE_WAYLAND
GST_VAAPI_LIBS  += $(top_builddir)/gst-libs/gst/vaapi/libgstvaapi-wayland.la
TEST_CFLAGS	+= $(WAYLAND_CFLAGS)
TEST_LIBS	+= \
	$(LIBVA_WAYLAND_LIBS)		\
	$(WAYLAND_LIBS)			\
	$(NULL)
endif

test_vaapisink_SOURCES	= test-vaapisink.c
test_vaapisink_CFLAGS	= $(TEST_CFLAGS)
test_vaapisink_LDFLAGS  = $(GST_VAAPI_LIBS)
test_vaapisink_LDADD	= $(TEST_LIBS)

-include $(top_srcdir)/git.mk
