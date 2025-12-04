win32ipc_sources = [
  'gstwin32ipc.cpp',
  'gstwin32ipcmmf.cpp',
  'gstwin32ipcclient.cpp',
  'gstwin32ipcserver.cpp',
  'gstwin32ipcprotocol.cpp',
  'gstwin32ipcbufferpool.cpp',
  'gstwin32ipcmemory.cpp',
  'gstwin32ipcvideosink.cpp',
  'gstwin32ipcvideosrc.cpp',
  'gstwin32ipcbasesink.cpp',
  'gstwin32ipcbasesrc.cpp',
  'gstwin32ipcsink.cpp',
  'gstwin32ipcsrc.cpp',
  'plugin.cpp',
]

win32ipc_headers = [
  'gstwin32ipc.h',
  'gstwin32ipcvideosink.h',
  'gstwin32ipcmemory.h',
  'gstwin32ipcvideosrc.h',
  'gstwin32ipcbufferpool.h',
  'gstwin32ipcsrc.h',
  'gstwin32ipcsink.h',
]

doc_sources = []
foreach s: win32ipc_sources + win32ipc_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'win32ipc': pathsep.join(doc_sources)
}

if host_system != 'windows' or get_option('win32ipc').disabled()
  subdir_done()
endif

code = '''
#include <windows.h>
#if !(WINAPI_FAMILY_PARTITION(WINAPI_PARTITION_APP) && !WINAPI_FAMILY_PARTITION(WINAPI_PARTITION_DESKTOP))
#error "Not building for UWP"
#endif'''
if cc.compiles(code, name : 'building for UWP')
  if get_option('win32ipc').enabled()
    error('win32ipc plugin does not support UWP')
  else
    subdir_done()
  endif
endif

gstwin32ipc = library('gstwin32ipc',
  win32ipc_sources,
  c_args : gst_plugins_bad_args,
  cpp_args: gst_plugins_bad_args,
  include_directories : [configinc],
  dependencies : [gstbase_dep, gstvideo_dep],
  install : true,
  install_dir : plugins_install_dir,
)
plugins += [gstwin32ipc]
