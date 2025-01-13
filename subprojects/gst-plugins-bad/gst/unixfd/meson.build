unixfd_sources = [
  'gstunixfd.c',
  'gstunixfdsink.c',
  'gstunixfdsrc.c',
]

unixfd_headers = [
  'gstunixfd.h',
]

doc_sources = []
foreach s: unixfd_sources + unixfd_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'unixfd': pathsep.join(doc_sources)
}

if get_option('unixfd').disabled()
  subdir_done()
endif

gstallocators_dep = dependency('gstreamer-allocators-1.0')

gio_unix_dep = dependency('gio-unix-2.0', required: get_option('unixfd'))
if not gio_unix_dep.found()
  subdir_done()
endif

gstunixfd_plugin = library('gstunixfd',
  unixfd_sources,
  c_args: gst_plugins_bad_args,
  include_directories: [configinc],
  dependencies : [gstbase_dep, gstallocators_dep, gio_dep, gio_unix_dep],
  install : true,
  install_dir : plugins_install_dir,
)

plugins += [gstunixfd_plugin]
