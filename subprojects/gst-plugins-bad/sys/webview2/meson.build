webview2_sources = [
  'gstwebview2object.cpp',
  'gstwebview2src.cpp',
  'plugin.cpp',
]

extra_args = ['-DGST_USE_UNSTABLE_API']

webview2_option = get_option('webview2')
if host_system != 'windows' or webview2_option.disabled()
  subdir_done()
endif

if not gstd3d11_dep.found() or not gstd3d12_dep.found()
  if webview2_option.enabled()
    error('The webview2 was enabled explicitly, but required dependencies were not found.')
  endif
  subdir_done()
endif

if cc.get_id() != 'msvc'
  if webview2_option.enabled()
    error('webview2 plugin supports only MSVC build.')
  endif
  subdir_done()
endif

if d3d11_winapi_only_app
  if webview2_option.enabled()
    error('UWP only build is not supported.')
  endif
  subdir_done()
endif

have_wgc = cxx.compiles('''
    #include<windows.h>
    #include<winstring.h>
    #include<roapi.h>
    #include <dispatcherqueue.h>
    #include<windows.graphics.capture.h>,
    #include<windows.graphics.directx.direct3d11.h>
    #include<windows.graphics.directx.direct3d11.interop.h>
    #include<wrl.h>
    using namespace Microsoft::WRL;
    using namespace ABI::Windows::Graphics::Capture;
    ComPtr<IDirect3D11CaptureFramePoolStatics> pool_statics;
    ComPtr<IDirect3D11CaptureFramePool> pool;
    ComPtr<IGraphicsCaptureSession> session;
    ''',
    name: 'Windows Graphics Capture support in Windows SDK')

if not have_wgc
  if webview2_option.enabled()
    error('Windows Graphics Capture API is unavailable.')
  endif
  subdir_done()
endif

building_for_win10 = cxx.compiles('''#include <windows.h>
    #ifndef WINVER
    #error "unknown minimum supported OS version"
    #endif
    #if (WINVER < 0x0A00)
    #error "Windows 10 API is not guaranteed"
    #endif
    ''',
    name: 'building for Windows 10')

if not building_for_win10
  message('Bumping target Windows version to Windows 10 for building webview2 plugin')
  extra_args += ['-DWINVER=0x0A00', '-D_WIN32_WINNT=0x0A00', '-DNTDDI_VERSION=WDK_NTDDI_VERSION']
endif

dwmapi_lib = cc.find_library('dwmapi', required : webview2_option)
dcomp_lib = cc.find_library('dcomp', required : webview2_option)
runtimeobject_dep = cc.find_library('runtimeobject', required : webview2_option)
coremessaging_lib = cc.find_library('coremessaging', required : webview2_option)

if not dwmapi_lib.found() or not dcomp_lib.found() or not runtimeobject_dep.found() \
  or not coremessaging_lib.found()
  subdir_done()
endif

loader_lib = cc.find_library('WebView2LoaderStatic', required: false)
sdk_deps = []
if loader_lib.found() and cc.has_header('WebView2.h') and cc.has_header('WebView2EnvironmentOptions.h')
  sdk_deps += [loader_lib]
else
  webview2_dep = dependency('webview2', required : webview2_option,
      fallback: ['webview2', 'webview2_dep'])
  if not webview2_dep.found()
    subdir_done()
  endif

  sdk_deps += [webview2_dep]
endif

gstwebview2 = library('gstwebview2',
  webview2_sources,
  c_args : gst_plugins_bad_args + extra_args,
  cpp_args : gst_plugins_bad_args + extra_args,
  include_directories : [configinc],
  dependencies : [gstbase_dep, gstvideo_dep, coremessaging_lib,
                  gstd3d11_dep, gstd3d12_dep, runtimeobject_dep, dwmapi_lib, dcomp_lib] + sdk_deps,
  install : true,
  install_dir : plugins_install_dir,
)
plugins += [gstwebview2]
