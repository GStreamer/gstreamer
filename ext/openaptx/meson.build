openaptx_sources = [
  'openaptx-plugin.c',
  'gstopenaptxdec.c',
  'gstopenaptxenc.c',
]

openaptx_dep = dependency('libopenaptx', version : '== 0.2.0', required : get_option('openaptx'))

if openaptx_dep.found()
  gstopenaptx = library('gstopenaptx',
    openaptx_sources,
    c_args : gst_plugins_bad_args,
    include_directories : [configinc],
    dependencies : [gstaudio_dep, openaptx_dep],
    install : true,
    install_dir : plugins_install_dir,
  )
  pkgconfig.generate(gstopenaptx, install_dir : plugins_pkgconfig_install_dir)
  plugins += [gstopenaptx]
endif
