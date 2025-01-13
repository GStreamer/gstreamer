camerabin_sources = [
  'gstdigitalzoom.c',
  'gstviewfinderbin.c',
  'camerabingeneral.c',
  'gstwrappercamerabinsrc.c',
  'gstcamerabin2.c',
  'gstplugin.c',
]

camerabin_headers = [
  'gstviewfinderbin.h',
  'gstdigitalzoom.h',
  'gstwrappercamerabinsrc.h',
  'camerabingeneral.h',
  'gstcamerabin2.h',
]

doc_sources = []
foreach s: camerabin_sources + camerabin_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'camerabin': pathsep.join(doc_sources)
}

if get_option('camerabin2').disabled()
  subdir_done()
endif

gstcamerabin = library('gstcamerabin',
  camerabin_sources,
  c_args : gst_plugins_bad_args + ['-DGST_USE_UNSTABLE_API'],
  include_directories : [configinc, libsinc],
  link_with : gstbasecamerabin,
  dependencies : [gstbasecamerabin_dep, gstphotography_dep, gsttag_dep, gstapp_dep, gstpbutils_dep, gstbase_dep],
  install : true,
  install_dir : plugins_install_dir,
)
plugins += [gstcamerabin]
