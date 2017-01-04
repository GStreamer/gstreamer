### all of the standard pc files we need to generate
if HAVE_CHECK
CHECK_PC_I = gstreamer-check-@GST_API_VERSION@.pc
CHECK_PC_U = gstreamer-check-@GST_API_VERSION@-uninstalled.pc
else
CHECK_PC_I =
CHECK_PC_U =
endif

pcfiles = 							\
	gstreamer-@GST_API_VERSION@.pc				\
	gstreamer-base-@GST_API_VERSION@.pc			\
	$(CHECK_PC_I)						\
	gstreamer-controller-@GST_API_VERSION@.pc		\
	gstreamer-net-@GST_API_VERSION@.pc

pcfiles_uninstalled =						\
	gstreamer-@GST_API_VERSION@-uninstalled.pc		\
	gstreamer-base-@GST_API_VERSION@-uninstalled.pc		\
	$(CHECK_PC_U)						\
	gstreamer-controller-@GST_API_VERSION@-uninstalled.pc	\
	gstreamer-net-@GST_API_VERSION@-uninstalled.pc

all-local: $(pcfiles) $(pcfiles_uninstalled)

cp_verbose = $(cp_verbose_$(V))
cp_verbose_ = $(cp_verbose_$(AM_DEFAULT_VERBOSITY))
cp_verbose_0 = @echo "  CP     $@";

### how to generate pc files
%-@GST_API_VERSION@.pc: %.pc
	$(cp_verbose_0)cp $< $@
%-@GST_API_VERSION@-uninstalled.pc: %-uninstalled.pc
### the uninstalled libdir is depend of the build system used so set it here
### rather than hardcoding it in the file directly.
	$(AM_V_GEN) sed \
		-e "s|[@]gstlibdir[@]|$(abs_top_builddir)/gst/.libs|" \
		-e "s|[@]checklibdir[@]|$(abs_top_builddir)/libs/gst/check/.libs|" \
		-e "s|[@]baselibdir[@]|$(abs_top_builddir)/libs/gst/base/.libs|" \
		-e "s|[@]netlibdir[@]|$(abs_top_builddir)/libs/gst/net/.libs|" \
		-e "s|[@]controllerlibdir[@]|$(abs_top_builddir)/libs/gst/controller/.libs|" \
		$< > $@.tmp && mv $@.tmp $@

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
	gstreamer-net.pc.in					\
	gstreamer-net-uninstalled.pc.in

CLEANFILES = $(pcfiles) $(pcfiles_uninstalled)
