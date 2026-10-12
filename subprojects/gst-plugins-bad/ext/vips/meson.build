vips_sources = [
  'gstvipsenumtypes.c',
  'gstvipsimage.c',
  'gstvipssmartcrop.c',
  'gstvipsplugin.c',
]

vips_headers = [
  'gstvipsenumtypes.h',
  'gstvipsimage.h',
  'gstvipssmartcrop.h',
]

doc_sources = []
foreach s: vips_sources + vips_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'vips': pathsep.join(doc_sources)
}

if get_option('vips').disabled()
  subdir_done()
endif

vips_dep = dependency('vips', required : get_option('vips'))

if not vips_dep.found()
  subdir_done()
endif

gstvips = library('gstvips',
    vips_sources,
    c_args : gst_plugins_bad_args + ['-DGST_USE_UNSTABLE_API'],
    include_directories : [configinc],
    dependencies : [gstvideo_dep, gstbase_dep, gst_dep, vips_dep],
    install : true,
    install_dir : plugins_install_dir,
  )
  pkgconfig.generate(gstvips, install_dir : plugins_pkgconfig_install_dir)
  plugins += [gstvips]
