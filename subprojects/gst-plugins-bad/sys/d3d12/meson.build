d3d12_sources = [
  'gstd3d12av1dec.cpp',
  'gstd3d12basefilter.cpp',
  'gstd3d12bufferpool.cpp',
  'gstd3d12decoder.cpp',
  'gstd3d12device.cpp',
  'gstd3d12download.cpp',
  'gstd3d12fence.cpp',
  'gstd3d12format.cpp',
  'gstd3d12h264dec.cpp',
  'gstd3d12h265dec.cpp',
  'gstd3d12memory.cpp',
  'gstd3d12utils.cpp',
  'gstd3d12vp9dec.cpp',
  'plugin.cpp',
]

extra_args = [
  '-DGST_USE_UNSTABLE_API',
  # Disable this warning error. Otherwise d3dx12.h will break build
  '/wd4062',
]

d3d12_option = get_option('d3d12')
if host_system != 'windows' or d3d12_option.disabled()
  subdir_done()
endif

if cc.get_id() != 'msvc'
  if d3d12_option.enabled()
    error('d3d12 plugin supports only MSVC build')
  endif
  subdir_done()
endif

d3d12_lib = cc.find_library('d3d12', required : d3d12_option)
dxgi_lib = cc.find_library('dxgi', required : d3d12_option)
dx_headers_dep = dependency('DirectX-Headers',
    version: '>= 1.611',
    allow_fallback: true,
    required: d3d12_option)

if not gstdxva_dep.found() or not gstd3d11_dep.found() or not d3d12_lib.found() or not dxgi_lib.found() or not dx_headers_dep.found()
  if d3d12_option.enabled()
    error('The d3d12 was enabled explicitly, but required GstD3D11 dependencies were not found.')
  endif
  subdir_done()
endif

d3d12_headers = [
  'dxgi1_6.h',
]

have_d3d12_headers = true
foreach h: d3d12_headers
  if not cc.has_header(h)
    have_d3d12_headers = false
  endif
endforeach

if not have_d3d12_headers
  if d3d12_option.enabled()
    error('The d3d12 plugin was enabled explicitly, but required dependencies were not found.')
  endif
  subdir_done()
endif

gstd3d12 = library('gstd3d12',
  d3d12_sources,
  c_args : gst_plugins_bad_args + extra_args,
  cpp_args: gst_plugins_bad_args + extra_args,
  include_directories : [configinc],
  dependencies : [gstbase_dep, gstvideo_dep, gstcodecs_dep,
                  gstdxva_dep, gstd3d11_dep, d3d12_lib, dxgi_lib,
                  dx_headers_dep],
  install : true,
  install_dir : plugins_install_dir,
)
plugins += [gstd3d12]
