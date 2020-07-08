d3d11_sources = [
  'gstd3d11bufferpool.c',
  'gstd3d11device.c',
  'gstd3d11memory.c',
  'gstd3d11utils.c',
  'gstd3d11videosink.c',
  'gstd3d11window.cpp',
  'plugin.c',
  'gstd3d11format.c',
  'gstd3d11basefilter.c',
  'gstd3d11upload.c',
  'gstd3d11download.c',
  'gstd3d11colorconvert.c',
  'gstd3d11videosinkbin.c',
  'gstd3d11shader.c',
  'gstd3d11colorconverter.c',
  'gstd3d11overlaycompositor.c',
  'gstd3d11videoprocessor.c',
]

d3d11_dec_sources = [
  'gstd3d11decoder.c',
  'gstd3d11h264dec.c',
  'gstd3d11vp9dec.c',
  'gstd3d11h265dec.c',
  'gstd3d11vp8dec.c',
]

dxgi_headers = [
  ['dxgi1_6.h', 6],
  ['dxgi1_5.h', 5],
  ['dxgi1_4.h', 4],
  ['dxgi1_3.h', 3],
  ['dxgi1_2.h', 2],
  ['dxgi.h', 1]
]

d3d11_headers = [
  ['d3d11_4.h', 4],
  ['d3d11_3.h', 3],
  ['d3d11_2.h', 2],
  ['d3d11_1.h', 1],
  ['d3d11.h', 0]
]

have_d3d11 = false
extra_c_args = ['-DCOBJMACROS']
have_dxgi_header = false
have_d3d11_header = false
have_d3d11sdk_h = false
have_dxgidebug_h = false
winapi_desktop = false
winapi_app = false
extra_dep = []
d3d11_conf = configuration_data()

d3d11_option = get_option('d3d11')
if host_system != 'windows' or d3d11_option.disabled()
  subdir_done()
endif

d3d11_lib = cc.find_library('d3d11', required : d3d11_option)
dxgi_lib = cc.find_library('dxgi', required : d3d11_option)
d3dcompiler_lib = cc.find_library('d3dcompiler', required: d3d11_option)
runtimeobject_lib = cc.find_library('runtimeobject', required : false)

foreach dxgi_h: dxgi_headers
  if not have_dxgi_header and cc.has_header(dxgi_h[0])
    d3d11_conf.set('DXGI_HEADER_VERSION', dxgi_h[1])
    have_dxgi_header = true
  endif
endforeach

foreach d3d11_h: d3d11_headers
  if not have_d3d11_header and cc.has_header(d3d11_h[0])
    d3d11_conf.set('D3D11_HEADER_VERSION', d3d11_h[1])
    have_d3d11_header = true
  endif
endforeach

have_d3d11 = d3d11_lib.found() and dxgi_lib.found() and have_d3d11_header and have_dxgi_header and cc.has_header('d3dcompiler.h')
if not have_d3d11
  if d3d11_option.enabled()
    error('The d3d11 plugin was enabled explicitly, but required dependencies were not found.')
  endif
  subdir_done()
endif

# d3d11 video api uses dxva structure for decoding, and dxva.h needs d3d9 types
if cc.has_header('dxva.h') and cc.has_header('d3d9.h')
  d3d11_conf.set('HAVE_DXVA_H', 1)
  d3d11_sources += d3d11_dec_sources
  extra_c_args += ['-DGST_USE_UNSTABLE_API']
  extra_dep += [gstcodecs_dep]
endif

winapi_desktop = cxx.compiles('''#include <winapifamily.h>
    #if !WINAPI_FAMILY_PARTITION(WINAPI_PARTITION_DESKTOP)
    #error "not win32"
    #endif''',
    dependencies: [d3d11_lib, dxgi_lib],
    name: 'checking if building for Win32')

if runtimeobject_lib.found() and d3dcompiler_lib.found()
  winapi_app = cxx.compiles('''#include <winapifamily.h>
      #include <windows.applicationmodel.core.h>
      #include <wrl.h>
      #include <wrl/wrappers/corewrappers.h>
      #include <d3d11.h>
      #include <dxgi1_2.h>
      #if !WINAPI_FAMILY_PARTITION(WINAPI_PARTITION_APP)
      #error "not winrt"
      #endif''',
      dependencies: [d3d11_lib, dxgi_lib, runtimeobject_lib],
      name: 'checking if building for WinRT')
endif

if not winapi_desktop and not winapi_app
  error('Neither Desktop partition nor App partition')
endif

winapi_app_only = winapi_app and not winapi_desktop

if winapi_app_only
  d3d11_sources += ['gstd3d11window_corewindow.cpp',
                    'gstd3d11window_swapchainpanel.cpp']
  extra_dep += [runtimeobject_lib, d3dcompiler_lib]
else
  d3d11_sources += ['gstd3d11window_win32.cpp']
endif

d3d11_conf.set10('GST_D3D11_WINAPI_ONLY_APP', winapi_app_only)

# for enabling debug layer
# NOTE: Disable d3d11/dxgi debug layer in case of [UWP build + release CRT]
# WACK (Windows App Certification Kit) doesn't seem to be happy with
# the DXGIGetDebugInterface1 symbol.

# FIXME: Probably DXGIGetDebugInterface1 might be used on UWP app for development
# purpose. So, I suspect one possible reason why WACK is complaining about
# DXGIGetDebugInterface1 is that debugging APIs couldn't be used for
# Windows store app, but couldn't find any reference about that.
#
# [IDXGIDebug1]
# https://docs.microsoft.com/en-us/windows/win32/api/dxgidebug/nn-dxgidebug-idxgidebug1
# is saying that the IDXGIDebug1 interface is available for both desktop app and
# UWP. And then the *DXGIGetDebugInterface1* method need to be called to obtain
# the IDXGIDebug1 interface.
#
# [DXGIGetDebugInterface1]
# https://docs.microsoft.com/en-us/windows/win32/api/dxgi1_3/nf-dxgi1_3-dxgigetdebuginterface1
# is mentioning that DXGIGetDebugInterface1 is desktop app only.
#
# PLEASE LET US KNOW A CORRECT WAY TO OBTAIN IDXGIDebug1 ON UWP, MICROSOFT
if get_option('debug') and not (winapi_app_only and get_option('b_vscrt') == 'md')
  d3d11_debug_libs = [
    ['d3d11sdklayers.h', 'ID3D11Debug', 'ID3D11InfoQueue', 'have_d3d11sdk_h'],
    ['dxgidebug.h', 'IDXGIDebug', 'IDXGIInfoQueue', 'have_dxgidebug_h'],
  ]

  foreach f : d3d11_debug_libs
    header = f.get(0)
    debug_obj = f.get(1)
    info_obj = f.get(2)
    compile_code = '''
                   #include <d3d11.h>
                   #include <dxgi.h>
                   #include <@0@>
                   int main(int arc, char ** argv) {
                     @1@ *debug = NULL;
                     @2@ *info_queue = NULL;
                     return 0;
                   }'''.format(header, debug_obj, info_obj)
    if cc.compiles(compile_code, dependencies: [d3d11_lib, dxgi_lib], name: debug_obj)
      set_variable(f.get(3), true)
    endif
  endforeach
else
  message('Disable D3D11Debug and DXGIDebug layers')
endif

d3d11_conf.set10('HAVE_D3D11SDKLAYERS_H', have_d3d11sdk_h)
d3d11_conf.set10('HAVE_DXGIDEBUG_H', have_dxgidebug_h)

configure_file(
  output: 'gstd3d11config.h',
  configuration: d3d11_conf,
)

gstd3d11 = library('gstd3d11',
  d3d11_sources,
  c_args : gst_plugins_bad_args + extra_c_args,
  cpp_args: gst_plugins_bad_args,
  include_directories : [configinc],
  dependencies : [gstbase_dep, gstvideo_dep, gmodule_dep, d3d11_lib, dxgi_lib] + extra_dep,
  install : true,
  install_dir : plugins_install_dir,
)
pkgconfig.generate(gstd3d11, install_dir : plugins_pkgconfig_install_dir)
plugins += [gstd3d11]
