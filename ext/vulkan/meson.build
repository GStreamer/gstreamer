if get_option('vulkan').disabled()
  subdir_done()
endif

vulkan_sources = [
  'gstvulkan.c',
  'vksink.c',
  'vkswapper.c',
  'vktrash.c',
  'vkupload.c',
]

gstvulkan_plugin = library('gstvulkan',
  vulkan_sources,
  c_args : gst_plugins_bad_args,
  objc_args : gst_plugins_bad_args,
  link_args : noseh_link_args,
  include_directories : [configinc],
  dependencies : [gstvideo_dep, gstbase_dep, gstvulkan_dep, vulkan_dep],
  install : true,
  install_dir : plugins_install_dir,
)
pkgconfig.generate(gstvulkan_plugin, install_dir : plugins_pkgconfig_install_dir)
plugins += [gstvulkan_plugin]
