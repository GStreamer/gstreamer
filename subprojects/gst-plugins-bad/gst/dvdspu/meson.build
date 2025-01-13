dvdspu_sources = [
  'gstdvdspu.c',
  'gstspu-vobsub.c',
  'gstspu-vobsub-render.c',
  'gstspu-pgs.c',
]

dvdspu_headers = [
  'gstspu-pgs.h',
  'gstdvdspu.h',
  'gstspu-common.h',
  'gstspu-vobsub.h',
]

doc_sources = []
foreach s: dvdspu_sources + dvdspu_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'dvdspu': pathsep.join(doc_sources)
}

if get_option('dvdspu').disabled()
  subdir_done()
endif

gstdvdspu = library('gstdvdspu',
  dvdspu_sources,
  c_args : gst_plugins_bad_args,
  include_directories : [configinc, libsinc],
  dependencies : [gstbase_dep, gstvideo_dep],
  install : true,
  install_dir : plugins_install_dir,
)
plugins += [gstdvdspu]
