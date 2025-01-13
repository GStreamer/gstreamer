asio_sources = [
  'gstasiodeviceprovider.cpp',
  'gstasioobject.cpp',
  'gstasioringbuffer.cpp',
  'gstasiosink.cpp',
  'gstasiosrc.cpp',
  'gstasioutils.cpp',
  'plugin.c',
]

asio_headers = [
  'gstasioringbuffer.h',
  'asio.h',
  'gstasiodeviceprovider.h',
  'gstasiosink.h',
  'gstasioobject.h',
  'gstasiosrc.h',
  'gstasioutils.h',
]

doc_sources = []
foreach s: asio_sources + asio_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'asio': pathsep.join(doc_sources)
}

asio_option = get_option('asio')
if asio_option.disabled() or host_system != 'windows'
  subdir_done()
endif

winapi_desktop = cxx.compiles('''#include <winapifamily.h>
    #if !WINAPI_FAMILY_PARTITION(WINAPI_PARTITION_DESKTOP)
    #error "not win32"
    #endif''',
    name: 'building for Win32')

if not winapi_desktop
  if asio_option.enabled()
    error('asio plugin requires WINAPI_PARTITION_DESKTOP')
  else
    subdir_done ()
  endif
endif

avrt_lib = cc.find_library('avrt', required: asio_option)
if not avrt_lib.found()
  subdir_done ()
endif

gstasio = library('gstasio',
    asio_sources,
    include_directories : [configinc],
    dependencies : [gstaudio_dep, avrt_lib],
    c_args : gst_plugins_bad_args,
    cpp_args : gst_plugins_bad_args,
    install : true,
    install_dir : plugins_install_dir)
plugins += [gstasio]
