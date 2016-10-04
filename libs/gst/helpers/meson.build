gst_plugin_scanner = executable('gst-plugin-scanner',
  'gst-plugin-scanner.c',
  c_args : gst_c_args,
  include_directories : [configinc],
  dependencies : [gobject_dep, gmodule_dep, glib_dep, mathlib, gst_dep],
  link_with : [printf_lib],
  install_dir : helpers_install_dir,
  install: true,
)

gst_scanner_dir = meson.current_build_dir()

# FIXME: gst-ptp-helper
# FIXME: gst-completion-helper
