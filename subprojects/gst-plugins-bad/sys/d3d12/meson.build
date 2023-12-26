d3d12_sources = [
  'gstd3d12av1dec.cpp',
  'gstd3d12basefilter.cpp',
  'gstd3d12bufferpool.cpp',
  'gstd3d12commandallocatorpool.cpp',
  'gstd3d12commandlistpool.cpp',
  'gstd3d12commandqueue.cpp',
  'gstd3d12converter-builder.cpp',
  'gstd3d12converter.cpp',
  'gstd3d12convert.cpp',
  'gstd3d12decoder.cpp',
  'gstd3d12descriptorpool.cpp',
  'gstd3d12device.cpp',
  'gstd3d12download.cpp',
  'gstd3d12fencedatapool.cpp',
  'gstd3d12format.cpp',
  'gstd3d12h264dec.cpp',
  'gstd3d12h265dec.cpp',
  'gstd3d12memory.cpp',
  'gstd3d12overlaycompositor.cpp',
  'gstd3d12pluginutils.cpp',
  'gstd3d12upload.cpp',
  'gstd3d12utils.cpp',
  'gstd3d12videosink.cpp',
  'gstd3d12vp9dec.cpp',
  'gstd3d12window.cpp',
  'plugin.cpp',
]

hlsl_precompiled = []

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
d3dcompiler_lib = cc.find_library('d3dcompiler', required : d3d12_option)
dx_headers_dep = dependency('DirectX-Headers',
    version: '>= 1.611',
    allow_fallback: true,
    required: d3d12_option)
dxc = find_program('dxc', required : d3d12_option)

if not gstdxva_dep.found() or not d3d12_lib.found() or not dxgi_lib.found() \
  or not dx_headers_dep.found() or not d3dcompiler_lib.found() \
  or not dxc.found()
  if d3d12_option.enabled()
    error('The d3d12 was enabled explicitly, but required GstD3D11 dependencies were not found.')
  endif
  subdir_done()
endif

d3d12_headers = [
  'dxgi1_6.h',
  'd3dcompiler.h',
  'DirectXMath.h',
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

hlsl_precompiled = []
subdir('hlsl')

# https://learn.microsoft.com/en-us/windows/win32/dxmath/pg-xnamath-internals#windows-sse-versus-sse2
# x86 with Windows 7 or older may not support SSE2
if host_machine.cpu_family() != 'x86'
  extra_args += ['-DHAVE_DIRECTX_MATH_SIMD']
endif

gstd3d12 = library('gstd3d12',
  d3d12_sources + hlsl_precompiled,
  c_args : gst_plugins_bad_args + extra_args,
  cpp_args: gst_plugins_bad_args + extra_args,
  include_directories : [configinc],
  dependencies : [gstbase_dep, gstvideo_dep, gstcodecs_dep,
                  gstdxva_dep, d3d12_lib, dxgi_lib, d3dcompiler_lib,
                  dx_headers_dep],
  install : true,
  install_dir : plugins_install_dir,
)
plugins += [gstd3d12]
