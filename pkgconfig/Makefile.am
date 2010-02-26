### all of the standard pc files we need to generate
if HAVE_CHECK
CHECK_PC_I = gstreamer-check-@GST_MAJORMINOR@.pc
CHECK_PC_U = gstreamer-check-@GST_MAJORMINOR@-uninstalled.pc
else
CHECK_PC_I =
CHECK_PC_U =
endif

pcfiles = 							\
	gstreamer-@GST_MAJORMINOR@.pc				\
	gstreamer-base-@GST_MAJORMINOR@.pc			\
	$(CHECK_PC_I)						\
	gstreamer-controller-@GST_MAJORMINOR@.pc		\
	gstreamer-dataprotocol-@GST_MAJORMINOR@.pc		\
	gstreamer-net-@GST_MAJORMINOR@.pc

pcfiles_uninstalled =						\
	gstreamer-@GST_MAJORMINOR@-uninstalled.pc		\
	gstreamer-base-@GST_MAJORMINOR@-uninstalled.pc		\
	$(CHECK_PC_U)						\
	gstreamer-controller-@GST_MAJORMINOR@-uninstalled.pc	\
	gstreamer-dataprotocol-@GST_MAJORMINOR@-uninstalled.pc	\
	gstreamer-net-@GST_MAJORMINOR@-uninstalled.pc

all-local: $(pcfiles) $(pcfiles_uninstalled)

cp_verbose = $(cp_verbose_$(V))
cp_verbose_ = $(cp_verbose_$(AM_DEFAULT_VERBOSITY))
cp_verbose_0 = @echo "  CP     $@";

### how to generate pc files
%-@GST_MAJORMINOR@.pc: %.pc
	$(cp_verbose_0)cp $< $@
%-@GST_MAJORMINOR@-uninstalled.pc: %-uninstalled.pc
	$(cp_verbose_0)cp $< $@

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = $(pcfiles)

EXTRA_DIST =							\
	gstreamer.pc.in						\
	gstreamer-uninstalled.pc.in				\
	gstreamer-base.pc.in					\
	gstreamer-base-uninstalled.pc.in			\
	gstreamer-check.pc.in					\
	gstreamer-check-uninstalled.pc.in			\
	gstreamer-controller.pc.in				\
	gstreamer-controller-uninstalled.pc.in			\
	gstreamer-dataprotocol.pc.in				\
	gstreamer-dataprotocol-uninstalled.pc.in		\
	gstreamer-net.pc.in					\
	gstreamer-net-uninstalled.pc.in

CLEANFILES = $(pcfiles) $(pcfiles_uninstalled)
