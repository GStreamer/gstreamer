openaptx_sources = [
  'openaptx-plugin.c',
  'gstopenaptxdec.c',
  'gstopenaptxenc.c',
]

openaptx_headers = [
  'gstopenaptxenc.h',
  'openaptx-plugin.h',
  'gstopenaptxdec.h',
]

doc_sources = []
foreach s: openaptx_sources + openaptx_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'openaptx': pathsep.join(doc_sources)
}

if not get_option('openaptx').allowed()
  subdir_done()
endif

openaptx_defines = []

openaptx_dep = dependency('libfreeaptx', version : '>= 0.1.1', required : false)
if openaptx_dep.found()
  openaptx_defines += ['-DUSE_FREEAPTX']
else
  openaptx_dep = dependency('libopenaptx', version : '== 0.2.0', required : get_option('openaptx'))
endif

if openaptx_dep.found()
  gstopenaptx = library('gstopenaptx',
    openaptx_sources,
    c_args : gst_plugins_bad_args + openaptx_defines,
    include_directories : [configinc],
    dependencies : [gstaudio_dep, openaptx_dep],
    install : true,
    install_dir : plugins_install_dir,
  )
  plugins += [gstopenaptx]
endif
