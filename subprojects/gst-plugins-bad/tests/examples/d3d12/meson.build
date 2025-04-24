if host_system != 'windows'
  subdir_done()
endif

have_d2d_h = cc.has_header('d2d1_3.h')
have_dwrite_h = cc.has_header('dwrite.h')
have_d3d12video_h = cc.has_header('d3d12video.h')
have_dcomp_h = cc.has_header('dcomp.h')
have_d3d11_h = cc.has_header('d3d11.h')
have_dxgi_h = cc.has_header('dxgi.h')
have_d3dcompile_h = cc.has_header('d3dcompiler.h')
dwrite_dep = cc.find_library('dwrite', required: false)
dcomp_dep = cc.find_library('dcomp', required: false)
d3d11_dep = cc.find_library('d3d11', required: false)
dxgi_dep = cc.find_library('dxgi', required: false)
runtimeobject_dep = cc.find_library('runtimeobject', required: false)
coremessaging_lib = cc.find_library('coremessaging', required: false)
d3dcompile_lib = cc.find_library('d3dcompiler', required: false)

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

  if have_d3dcompile_h and d3dcompile_lib.found() and have_dx_math and dx_headers_dep.found()
    extra_args = ['-DGST_USE_UNSTABLE_API', '-DGST_D3D12_USE_DIRECTX_HEADERS']
    extra_args += cc.get_supported_arguments([
        '/wd4062', # 'identifier' : unreferenced local variable
    ])
    executable('d3d12remap-fisheye',
      ['d3d12remap-fisheye.cpp', '../key-handler.c'],
      c_args : gst_plugins_bad_args + extra_args,
      cpp_args : gst_plugins_bad_args + extra_args,
      include_directories : [configinc],
      dependencies: [gst_dep, gstd3d12_dep, gstvideo_dep, d3dcompile_lib, dx_headers_dep],
      install: false
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
