noinst_PROGRAMS = \
	test-vaapisink  \
	test-vaapipostproc  \
	test-roi  \
	$(NULL)

TEST_CFLAGS = \
	-DGST_USE_UNSTABLE_API		\
	$(GST_CFLAGS)			\
	$(GST_VIDEO_CFLAGS)		\
	$(NULL)

TEST_LIBS = \
	$(GST_LIBS) 			\
	$(GST_VIDEO_LIBS)		\
	$(NULL)

test_vaapisink_SOURCES	= test-vaapisink.c
test_vaapisink_CFLAGS	= $(TEST_CFLAGS)
test_vaapisink_LDADD	= $(TEST_LIBS)

test_vaapipostproc_SOURCES	= test-vaapipostproc.c
test_vaapipostproc_CFLAGS	= $(TEST_CFLAGS)
test_vaapipostproc_LDADD	= $(TEST_LIBS)

test_roi_SOURCES = test-roi.c
test_roi_CFLAGS  = $(TEST_CFLAGS)
test_roi_LDADD   = $(TEST_LIBS)

if USE_GTK
noinst_PROGRAMS += test-vaapicontext

test_vaapicontext_SOURCES = test-vaapicontext.c
test_vaapicontext_CFLAGS = \
	$(TEST_CFLAGS) 		\
	$(GTK3_CFLAGS) 		\
	$(X11_CFLAGS)  		\
	$(LIBVA_CFLAGS)		\
	$(LIBVA_X11_CFLAGS)	\
	$(NULL)
test_vaapicontext_LDADD = \
	$(TEST_LIBS) 		\
	$(GTK3_LIBS) 		\
	$(X11_LIBS) 		\
	$(LIBVA_LIBS)		\
	$(LIBVA_X11_LIBS)	\
	$(NULL)
endif

-include $(top_srcdir)/git.mk
