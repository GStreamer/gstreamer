gst_plugins_rtp_sources = [
  'plugin.c',
  'gstrtpsink.c',
  'gstrtpsrc.c',
  'gstrtp-utils.c',
]

gstrtp = library('gstrtpmanagerbad',
  gst_plugins_rtp_sources,
  dependencies: [gio_dep, gst_dep, gstbase_dep, gstrtp_dep, gstnet_dep, gstcontroller_dep],
  include_directories: [configinc],
  install: true,
  c_args: gst_plugins_bad_args,
  install_dir: plugins_install_dir,
)
pkgconfig.generate(gstrtp, install_dir : plugins_pkgconfig_install_dir)
plugins += [gstrtp]
