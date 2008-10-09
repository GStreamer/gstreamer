dnl AG_GST_SET_PLUGINDIR

dnl AC_DEFINE PLUGINDIR to the full location where plug-ins will be installed
dnl AC_SUBST plugindir, to be used in Makefile.am's

AC_DEFUN([AG_GST_SET_PLUGINDIR],
[
  dnl define location of plugin directory
  AS_AC_EXPAND(PLUGINDIR, ${libdir}/gstreamer-$GST_MAJORMINOR)
  AC_DEFINE_UNQUOTED(PLUGINDIR, "$PLUGINDIR",
    [directory where plugins are located])
  AC_MSG_NOTICE([Using $PLUGINDIR as the plugin install location])

  dnl plugin directory configure-time variable for use in Makefile.am
  plugindir="\$(libdir)/gstreamer-$GST_MAJORMINOR"
  AC_SUBST(plugindir)
])
