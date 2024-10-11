if host_system != 'windows'
  subdir_done()
endif

have_d2d_h = cc.has_header('d2d1_3.h')
have_dwrite_h = cc.has_header('dwrite.h')
have_d3d12video_h = cc.has_header('d3d12video.h')
have_dcomp_h = cc.has_header('dcomp.h')
have_d3d11_h = cc.has_header('d3d11.h')
have_dxgi_h = cc.has_header('dxgi.h')
dwrite_dep = cc.find_library('dwrite', required: false)
dcomp_dep = cc.find_library('dcomp', required: false)
d3d11_dep = cc.find_library('d3d11', required: false)
dxgi_dep = cc.find_library('dxgi', required: false)
runtimeobject_dep = cc.find_library('runtimeobject', required: false)
coremessaging_lib = cc.find_library('coremessaging', required: false)

executable('d3d12enc-dynamic-reconfigure',
  ['d3d12enc-dynamic-reconfigure.c', '../key-handler.c'],
  include_directories : [configinc],
  dependencies: [gst_dep, gstbase_dep, gstvideo_dep],
  c_args : gst_plugins_bad_args,
  install: false
)

executable('d3d12videosink-switch',
  ['d3d12videosink-switch.cpp', '../key-handler.c'],
  include_directories : [configinc],
  dependencies: [gst_dep, gstbase_dep, gstvideo_dep],
  c_args : gst_plugins_bad_args,
  install: false
)

if gstd3d12_dep.found()
  if have_d2d_h and have_dwrite_h and have_d3d12video_h and dwrite_dep.found()
    executable('d3d12videosink-overlay', ['d3d12videosink-overlay.cpp'],
      c_args : gst_plugins_bad_args + ['-DGST_USE_UNSTABLE_API'],
      cpp_args : gst_plugins_bad_args + ['-DGST_USE_UNSTABLE_API'],
      include_directories : [configinc, libsinc],
      dependencies: [gst_dep, gstvideo_dep, gstd3d12_dep, dwrite_dep],
      install: false,
    )
  endif
endif

if cc.get_id() == 'msvc' and have_dcomp_h and dcomp_dep.found() and \
    have_d3d11_h and d3d11_dep.found() and have_dxgi_h and dxgi_dep.found()
  executable('d3d12swapchainsink-win32', 'd3d12swapchainsink-win32.cpp',
    c_args : gst_plugins_bad_args + ['-DGST_USE_UNSTABLE_API'],
    cpp_args : gst_plugins_bad_args + ['-DGST_USE_UNSTABLE_API'],
    include_directories : [configinc, libsinc],
    dependencies: [gst_dep, gstvideo_dep, dcomp_dep, d3d11_dep, dxgi_dep],
    install: false,
  )
endif

have_winrt_comp_headers = true
winrt_comp_headers = [
  'winstring.h',
  'roapi.h',
  'dispatcherqueue.h',
  'windows.system.h',
  'windows.ui.composition.h',
  'windows.ui.composition.interop.h',
  'windows.ui.composition.desktop.h',
]

foreach h: winrt_comp_headers
  if not cc.has_header(h)
    have_winrt_comp_headers = false
  endif
endforeach

if cc.get_id() == 'msvc' and have_winrt_comp_headers and \
    runtimeobject_dep.found() and coremessaging_lib.found()
  executable('d3d12swapchainsink-winrt', 'd3d12swapchainsink-winrt.cpp',
    c_args : gst_plugins_bad_args + ['-DGST_USE_UNSTABLE_API'],
    cpp_args : gst_plugins_bad_args + ['-DGST_USE_UNSTABLE_API'],
    include_directories : [configinc, libsinc],
    dependencies: [gst_dep, gstvideo_dep, runtimeobject_dep, coremessaging_lib],
    install: false,
  )
endif
