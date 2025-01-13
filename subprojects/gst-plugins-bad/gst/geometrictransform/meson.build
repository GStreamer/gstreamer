geotr_sources = [
  'plugin.c',
  'gstgeometrictransform.c',
  'gstcirclegeometrictransform.c',
  'geometricmath.c',
  'gstcircle.c',
  'gstdiffuse.c',
  'gstkaleidoscope.c',
  'gstmarble.c',
  'gstpinch.c',
  'gstrotate.c',
  'gstsphere.c',
  'gsttwirl.c',
  'gstwaterripple.c',
  'gststretch.c',
  'gstbulge.c',
  'gsttunnel.c',
  'gstsquare.c',
  'gstmirror.c',
  'gstfisheye.c',
  'gstperspective.c',
]

geotr_headers = [
  'gstkaleidoscope.h',
  'gstwaterripple.h',
  'gstpinch.h',
  'gstmarble.h',
  'gsttunnel.h',
  'gstcircle.h',
  'gstgeometrictransform.h',
  'gstsphere.h',
  'gstrotate.h',
  'gstperspective.h',
  'gstbulge.h',
  'gsttwirl.h',
  'gststretch.h',
  'gstdiffuse.h',
  'gstfisheye.h',
  'gstsquare.h',
  'geometricmath.h',
  'gstcirclegeometrictransform.h',
  'gstmirror.h',
]

doc_sources = []
foreach s: geotr_sources + geotr_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'geometrictransform': pathsep.join(doc_sources)
}

if get_option('geometrictransform').disabled()
  subdir_done()
endif

gstgeometrictransform = library('gstgeometrictransform',
  geotr_sources,
  c_args : gst_plugins_bad_args,
  include_directories : [configinc],
  dependencies : [gstbase_dep, gstvideo_dep, libm],
  install : true,
  install_dir : plugins_install_dir,
)
plugins += [gstgeometrictransform]
