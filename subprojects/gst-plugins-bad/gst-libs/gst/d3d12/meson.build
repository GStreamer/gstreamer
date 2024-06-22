d3d12_sources = [
  'gstd3d12bufferpool.cpp',
  'gstd3d12commandallocatorpool.cpp',
  'gstd3d12commandlistpool.cpp',
  'gstd3d12commandqueue.cpp',
  'gstd3d12converter-builder.cpp',
  'gstd3d12converter-unpack.cpp',
  'gstd3d12converter-pack.cpp',
  'gstd3d12converter.cpp',
  'gstd3d12descriptorpool.cpp',
  'gstd3d12device.cpp',
  'gstd3d12fencedatapool.cpp',
  'gstd3d12format.cpp',
  'gstd3d12frame.cpp',
  'gstd3d12memory.cpp',
  'gstd3d12utils.cpp',
]

d3d12_headers = [
  'd3d12-prelude.h',
  'gstd3d12_fwd.h',
  'gstd3d12.h',
  'gstd3d12bufferpool.h',
  'gstd3d12commandallocatorpool.h',
  'gstd3d12commandqueue.h',
  'gstd3d12converter.h',
  'gstd3d12descriptorpool.h',
  'gstd3d12device.h',
  'gstd3d12fencedatapool.h',
  'gstd3d12format.h',
  'gstd3d12frame.h',
  'gstd3d12memory.h',
  'gstd3d12utils.h',
]

extra_args = [
  '-DGST_USE_UNSTABLE_API',
  '-DBUILDING_GST_D3D12',
  '-DG_LOG_DOMAIN="GStreamer-D3D12"',
]

extra_deps = []

# Disable this warning error. Otherwise d3dx12.h will break build
if cc.get_argument_syntax() == 'msvc'
  extra_args += cc.get_supported_arguments([
    '/wd4062', # 'identifier' : unreferenced local variable
  ])
else
  extra_args += cc.get_supported_arguments([
    '-Wno-misleading-indentation',
    # MinGW 32bits compiler seems to be complaining about redundant-decls
    # when ComPtr is in use. Let's just disable the warning
    '-Wno-redundant-decls',
  ])
endif

gstd3d12_dep = dependency('', required : false)
d3d12_option = get_option('d3d12')
if host_system != 'windows' or d3d12_option.disabled()
  subdir_done()
endif

d3d12_lib = cc.find_library('d3d12', required : d3d12_option)
dxgi_lib = cc.find_library('dxgi', required : d3d12_option)
dx_headers_dep = dependency('DirectX-Headers',
    version: '>= 1.611',
    allow_fallback: true,
    required: d3d12_option)

if not d3d12_lib.found() or not dxgi_lib.found() or not dx_headers_dep.found() \
  or not gstd3dshader_dep.found()
  if d3d12_option.enabled()
    error('The d3d12 was enabled explicitly, but required dependencies were not found.')
  endif
  subdir_done()
endif

sdk_headers = [
  'dxgi1_6.h',
  'd3d11_1.h',
  'd3d11on12.h',
]

have_d3d12_headers = true
foreach h: sdk_headers
  if not cc.has_header(h)
    have_d3d12_headers = false
  endif
endforeach

if not have_d3d12_headers
  if d3d12_option.enabled()
    error('The d3d12 was enabled explicitly, but required dependencies were not found.')
  endif
  subdir_done()
endif

have_dx_math = cxx.compiles('''
    #include <windows.h>
    #include <DirectXMath.h>
    using namespace DirectX;
    int main(int argc, char ** argv) {
      XMMATRIX matrix;
      XMFLOAT4X4 dump;
      matrix = XMMatrixIdentity ();
      XMStoreFloat4x4 (&dump, matrix);
      return 0;
    }
    ''',
    name: 'DirectXMath support in Windows SDK')

if not have_dx_math
  directxmath_dep = dependency('directxmath',
      allow_fallback: true,
      required: d3d12_option)
  if not directxmath_dep.found()
    subdir_done()
  endif
  extra_deps += [directxmath_dep]
endif

# https://learn.microsoft.com/en-us/windows/win32/dxmath/pg-xnamath-internals#windows-sse-versus-sse2
# x86 with Windows 7 or older may not support SSE2
if host_machine.cpu_family() != 'x86'
  extra_args += ['-DHAVE_DIRECTX_MATH_SIMD']
endif

if cc.compiles('''
  #include <dxgi.h>
  #include <dxgidebug.h>
  int main(int argc, char ** argv) {
    IDXGIDebug *debug = NULL;
    IDXGIInfoQueue *info_queue = NULL;
    return 0;
  }''',
  name: 'DXGI debug layer support in Windows SDK')
  extra_args += ['-DHAVE_DXGIDEBUG_H']
endif

pkg_name = 'gstreamer-d3d12-' + api_version
gstd3d12 = library('gstd3d12-' + api_version,
  d3d12_sources,
  c_args : gst_plugins_bad_args + extra_args,
  cpp_args: gst_plugins_bad_args + extra_args,
  include_directories : [configinc, libsinc],
  dependencies : [gstbase_dep, gstvideo_dep, gstd3dshader_dep, d3d12_lib,
                  dxgi_lib, dx_headers_dep, gmodule_dep] + extra_deps,
  version : libversion,
  install : true,
)

pkgconfig.generate(gstd3d12,
  libraries : [gstbase_dep, gstvideo_dep, d3d12_lib, dxgi_lib],
  variables : pkgconfig_variables,
  subdirs : pkgconfig_subdirs,
  name : pkg_name,
  description : 'GStreamer Direct3D12 library',
)

library_def = {'lib': gstd3d12}
gst_libraries += [[pkg_name, library_def]]

install_headers(d3d12_headers, subdir : 'gstreamer-1.0/gst/d3d12')

gstd3d12_dep = declare_dependency(link_with : gstd3d12,
  include_directories : [libsinc],
  dependencies : [gstbase_dep, gstvideo_dep, d3d12_lib, dxgi_lib])

meson.override_dependency(pkg_name, gstd3d12_dep)
