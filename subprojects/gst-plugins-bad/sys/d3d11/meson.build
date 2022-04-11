d3d11_sources = [
  'gstd3d11basefilter.cpp',
  'gstd3d11convert.cpp',
  'gstd3d11converter.cpp',
  'gstd3d11compositor.cpp',
  'gstd3d11compositorbin.cpp',
  'gstd3d11download.cpp',
  'gstd3d11overlaycompositor.cpp',
  'gstd3d11pluginutils.cpp',
  'gstd3d11shader.cpp',
  'gstd3d11upload.cpp',
  'gstd3d11videoprocessor.cpp',
  'gstd3d11videosink.cpp',
  'gstd3d11window.cpp',
  'gstd3d11window_dummy.cpp',
  'plugin.cpp',
]

d3d11_dec_sources = [
  'gstd3d11av1dec.cpp',
  'gstd3d11decoder.cpp',
  'gstd3d11h264dec.cpp',
  'gstd3d11vp9dec.cpp',
  'gstd3d11h265dec.cpp',
  'gstd3d11mpeg2dec.cpp',
  'gstd3d11vp8dec.cpp',
]

extra_c_args = ['-DCOBJMACROS']
extra_args = ['-DGST_USE_UNSTABLE_API']
extra_dep = []

d3d11_option = get_option('d3d11')
if host_system != 'windows' or d3d11_option.disabled()
  subdir_done()
endif

if not gstd3d11_dep.found()
  if d3d11_option.enabled()
    error('The d3d11 was enabled explicitly, but required dependencies were not found.')
  endif
  subdir_done()
endif

d3dcompiler_lib = cc.find_library('d3dcompiler', required: d3d11_option)
runtimeobject_lib = cc.find_library('runtimeobject', required : false)
winmm_lib = cc.find_library('winmm', required: false)
has_decoder = false

have_d3d11 = cc.has_header('d3dcompiler.h')
if not have_d3d11
  if d3d11_option.enabled()
    error('The d3d11 plugin was enabled explicitly, but required dependencies were not found.')
  endif
  subdir_done()
endif

# d3d11 video api uses dxva structure for decoding, and dxva.h needs d3d9 types
if cc.has_header('dxva.h') and cc.has_header('d3d9.h')
  d3d11_sources += d3d11_dec_sources
  extra_args += ['-DHAVE_DXVA_H']
  extra_dep += [gstcodecs_dep]
  has_decoder = true
endif

if d3d11_winapi_only_app and (not d3dcompiler_lib.found() or not runtimeobject_lib.found())
  if d3d11_option.enabled()
    error('The d3d11 plugin was enabled explicitly, but required dependencies were not found.')
  endif
  subdir_done()
endif

# if build target is Windows 10 and WINAPI_PARTITION_APP is allowed,
# we can build UWP only modules as well
if d3d11_winapi_app
  d3d11_sources += ['gstd3d11window_corewindow.cpp',
                    'gstd3d11window_swapchainpanel.cpp']
  extra_dep += [runtimeobject_lib, d3dcompiler_lib]
endif

if d3d11_winapi_desktop
  d3d11_sources += ['gstd3d11window_win32.cpp']
  if d3d11_conf.get('GST_D3D11_DXGI_HEADER_VERSION') >= 6
    # Desktop Duplication API is unavailable for UWP
    # and MinGW is not supported due to some missing headers
    extra_args += ['-DHAVE_DXGI_DESKTOP_DUP']
    d3d11_sources += ['gstd3d11screencapture.cpp',
                      'gstd3d11screencapturedevice.cpp',
                      'gstd3d11screencapturesrc.cpp']
    message('Enable D3D11 Desktop Duplication API')
  endif
  # multimedia clock is desktop only API
  if has_decoder and winmm_lib.found() and cc.has_header('mmsystem.h')
    extra_args += ['-DHAVE_WINMM']
    extra_dep += [winmm_lib]
  endif
endif

# need dxgi1_5.h for HDR10 processing and d3d11_4.h for ID3D11VideoContext2 interface
if d3d11_conf.get('GST_D3D11_DXGI_HEADER_VERSION') >= 5 and d3d11_conf.get('GST_D3D11_HEADER_VERSION') >= 4
  d3d11_sources += ['gstd3d11deinterlace.cpp']
  extra_args += ['-DHAVE_D3D11_VIDEO_PROC']
endif

# MinGW 32bits compiler seems to be complaining about redundant-decls
# when ComPtr is in use. Let's just disable the warning
if cc.get_id() != 'msvc'
  extra_mingw_args = cc.get_supported_arguments([
    '-Wno-redundant-decls',
  ])

  extra_args += extra_mingw_args
endif

gstd3d11 = library('gstd3d11',
  d3d11_sources,
  c_args : gst_plugins_bad_args + extra_c_args + extra_args,
  cpp_args: gst_plugins_bad_args + extra_args,
  include_directories : [configinc],
  dependencies : [gstbase_dep, gstvideo_dep, gmodule_dep, gstcontroller_dep, gstd3d11_dep] + extra_dep,
  install : true,
  install_dir : plugins_install_dir,
)
pkgconfig.generate(gstd3d11, install_dir : plugins_pkgconfig_install_dir)
plugins += [gstd3d11]
