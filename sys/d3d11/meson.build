d3d11_sources = [
  'gstd3d11bufferpool.c',
  'gstd3d11device.c',
  'gstd3d11memory.c',
  'gstd3d11utils.c',
  'gstd3d11videosink.c',
  'gstd3d11window.c',
  'plugin.c',
]

have_d3d11 = false
extra_c_args = []
extra_dep = []

d3d11_option = get_option('d3d11')
if host_system != 'windows' or d3d11_option.disabled()
  subdir_done()
endif

d3d11_lib = cc.find_library('d3d11', required : d3d11_option)
dxgi_lib = cc.find_library('dxgi', required : d3d11_option)

have_d3d11 = d3d11_lib.found() and dxgi_lib.found() and cc.has_header('d3d11.h') and cc.has_header('dxgi.h')
if not have_d3d11
  if d3d11_option.enabled()
    error('The d3d11 plugin was enabled explicitly, but required dependencies were not found.')
  endif
  subdir_done()
endif

# required for HDR metadata
if cc.has_header('dxgi1_5.h')
  extra_c_args += ['-DHAVE_DXGI_1_5_H']
endif

# for enabling debug layer
if cc.has_header('d3d11sdklayers.h')
  extra_c_args += ['-DHAVE_D3D11SDKLAYER_H']
  extra_dep += [gmodule_dep]
endif

gstd3d11 = library('gstd3d11',
  d3d11_sources,
  c_args : gst_plugins_bad_args + extra_c_args,
  include_directories : [configinc],
  dependencies : [gstbase_dep, gstvideo_dep, d3d11_lib, dxgi_lib] + extra_dep,
  install : true,
  install_dir : plugins_install_dir,
)
pkgconfig.generate(gstd3d11, install_dir : plugins_pkgconfig_install_dir)
plugins += [gstd3d11]
