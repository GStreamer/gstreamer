d3d11_sources = [
  'gstd3d11av1dec.cpp',
  'gstd3d11basefilter.cpp',
  'gstd3d11compositor.cpp',
  'gstd3d11convert.cpp',
  'gstd3d11decoder.cpp',
  'gstd3d11deinterlace.cpp',
  'gstd3d11download.cpp',
  'gstd3d11h264dec.cpp',
  'gstd3d11h265dec.cpp',
  'gstd3d11ipc.cpp',
  'gstd3d11ipcclient.cpp',
  'gstd3d11ipcserver.cpp',
  'gstd3d11ipcsink.cpp',
  'gstd3d11ipcsrc.cpp',
  'gstd3d11mpeg2dec.cpp',
  'gstd3d11overlay.cpp',
  'gstd3d11overlaycompositor.cpp',
  'gstd3d11pluginutils.cpp',
  'gstd3d11testsrc.cpp',
  'gstd3d11upload.cpp',
  'gstd3d11videosink.cpp',
  'gstd3d11vp8dec.cpp',
  'gstd3d11vp9dec.cpp',
  'gstd3d11window.cpp',
  'gstd3d11window_dummy.cpp',
  'plugin.cpp',
]

d3d11_headers = [
  'gstd3d11ipcsrc.h',
  'gstd3d11window_dummy.h',
  'gstd3d11videosink.h',
  'gstd3d11compositor.h',
  'gstd3d11ipcsink.h',
  'gstd3d11ipcserver.h',
  'gstd3d11vp8dec.h',
  'gstd3d11download.h',
  'gstd3d11screencapturesrc.h',
  'gstd3d11av1dec.h',
  'gstd3d11mpeg2dec.h',
  'gstd3d11testsrc.h',
  'gstd3d11window_swapchainpanel.h',
  'gstd3d11upload.h',
  'gstd3d11window_corewindow.h',
  'gstd3d11pluginutils.h',
  'gstd3d11deinterlace.h',
  'gstd3d11screencapturedevice.h',
  'gstd3d11dxgicapture.h',
  'gstd3d11h265dec.h',
  'gstd3d11winrtcapture.h',
  'gstd3d11ipcclient.h',
  'gstd3d11overlay.h',
  'gstd3d11window_win32.h',
  'gstd3d11screencapture.h',
  'gstd3d11window.h',
  'gstd3d11convert.h',
  'gstd3d11ipc.h',
  'gstd3d11overlaycompositor.h',
  'gstd3d11vp9dec.h',
  'gstd3d11decoder.h',
  'gstd3d11basefilter.h',
  'gstd3d11h264dec.h',
]

winapi_app_sources = [
  'gstd3d11window_corewindow.cpp',
  'gstd3d11window_swapchainpanel.cpp',
]

winapi_desktop_sources = [
  'gstd3d11dxgicapture.cpp',
  'gstd3d11screencapture.cpp',
  'gstd3d11screencapturedevice.cpp',
  'gstd3d11screencapturesrc.cpp',
  'gstd3d11window_win32.cpp',
]

winrtcapture_sources = [
  'gstd3d11winrtcapture.cpp'
]

doc_sources = []
foreach s: d3d11_sources + winapi_app_sources + winapi_desktop_sources + winrtcapture_sources + d3d11_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'd3d11': pathsep.join(doc_sources)
}

extra_c_args = ['-DCOBJMACROS']
extra_args = ['-DGST_USE_UNSTABLE_API']
extra_dep = []

d3d11_option = get_option('d3d11')
d3d11_wgc_option = get_option('d3d11-wgc')
if host_system != 'windows' or d3d11_option.disabled()
  subdir_done()
endif

if not gstd3d11_dep.found() or not gstdxva_dep.found()
  if d3d11_option.enabled()
    error('The d3d11 was enabled explicitly, but required dependencies were not found.')
  endif
  subdir_done()
endif

runtimeobject_lib = cc.find_library('runtimeobject', required : false)
winmm_lib = cc.find_library('winmm', required: false)

if d3d11_winapi_only_app and (not d3dcompiler_lib.found() or not runtimeobject_lib.found())
  if d3d11_option.enabled()
    error('The d3d11 plugin was enabled explicitly, but required dependencies were not found.')
  endif
  subdir_done()
endif

d2d_dep = cc.find_library('d2d1', required: d3d11_option)
have_d2d_h = cc.has_header('d2d1.h', required: d3d11_option)
dwmapi_lib = cc.find_library('dwmapi', required: d3d11_wgc_option)
have_wgc = false

if not d3d11_wgc_option.disabled()
  have_wgc = cxx.compiles('''
      #include<windows.h>
      #include<winstring.h>
      #include<roapi.h>
      #include<windows.graphics.capture.h>,
      #include<windows.graphics.capture.interop.h>
      #include<windows.graphics.directx.direct3d11.h>
      #include<windows.graphics.directx.direct3d11.interop.h>
      #include<wrl.h>
      using namespace Microsoft::WRL;
      using namespace ABI::Windows::Graphics::Capture;
      ComPtr<IDirect3D11CaptureFramePoolStatics> pool_statics;
      ComPtr<IDirect3D11CaptureFramePoolStatics2> pool_statics2;
      ComPtr<IDirect3D11CaptureFramePool> pool;
      ComPtr<IGraphicsCaptureSession> session;
      ComPtr<IGraphicsCaptureSession2> session2;
      ComPtr<IGraphicsCaptureSession3> session3;
      ''',
      name: 'Windows Graphics Capture support in Windows SDK')

  if d3d11_wgc_option.enabled() and not have_wgc
    error('The Windows Graphics Capture feature is enabled but not supported by Windows SDK')
  endif
endif

# if build target is Windows 10 and WINAPI_PARTITION_APP is allowed,
# we can build UWP only modules as well
if d3d11_winapi_app
  d3d11_sources += winapi_app_sources
  extra_dep += [runtimeobject_lib]
  if cc.get_id() == 'msvc' and get_option('b_sanitize') == 'address'
    extra_args += ['/bigobj']
  endif
endif

if d3d11_winapi_desktop
  d3d11_sources += winapi_desktop_sources

  # multimedia clock is desktop only API
  if winmm_lib.found() and cc.has_header('mmsystem.h')
    extra_args += ['-DHAVE_WINMM']
    extra_dep += [winmm_lib]
  endif

  if have_wgc and dwmapi_lib.found()
    d3d11_sources += winrtcapture_sources
    extra_args += ['-DHAVE_WINRT_CAPTURE']
    extra_dep += [dwmapi_lib]
  endif
endif

# MinGW 32bits compiler seems to be complaining about redundant-decls
# when ComPtr is in use. Let's just disable the warning
if cc.get_id() != 'msvc'
  extra_mingw_args = cc.get_supported_arguments([
    '-Wno-redundant-decls',
    # include/directxmath/DirectXMathMatrix.inl:1161:16: error: variable 'aa' set but not used
    '-Wno-unused-but-set-variable',
  ])

  extra_args += extra_mingw_args
endif

# Definition issues
extra_args += cc.get_supported_arguments(['-Wno-undef'])

gstd3d11 = library('gstd3d11',
  d3d11_sources,
  c_args : gst_plugins_bad_args + extra_c_args + extra_args,
  cpp_args: gst_plugins_bad_args + extra_args,
  include_directories : [configinc],
  dependencies : [gstbase_dep, gstvideo_dep, gmodule_dep, gstcontroller_dep,
                  gstd3d11_dep, gstd3dshader_dep, gstdxva_dep, d2d_dep, directxmath_dep] + extra_dep,
  install : true,
  install_dir : plugins_install_dir,
)
plugins += [gstd3d11]
