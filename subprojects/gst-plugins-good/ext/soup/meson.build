soup_sources = [
  'gstsoup.c',
  'gstsoupelement.c',
  'gstsouphttpclientsink.c',
  'gstsouphttpsrc.c',
  'gstsouploader.c',
  'gstsouputils.c',
]

soup_opt = get_option('soup')
if soup_opt.disabled()
  subdir_done()
endif

gmodule_dep = dependency('gmodule-2.0', fallback: ['glib', 'libgmodule_dep'])
gobject_dep = dependency('gobject-2.0', fallback: ['glib', 'libgobject_dep'])

libdl_dep = cc.find_library('dl', required: false)

gstsouphttpsrc = library('gstsoup',
  soup_sources,
  c_args : gst_plugins_good_args,
  link_args : noseh_link_args,
  include_directories : [configinc, libsinc],
  dependencies : [gst_dep, gstbase_dep, gsttag_dep, gmodule_dep, gobject_dep, gio_dep, libdl_dep],
  install : true,
  install_dir : plugins_install_dir,
)
pkgconfig.generate(gstsouphttpsrc, install_dir : plugins_pkgconfig_install_dir)
plugins += [gstsouphttpsrc]
