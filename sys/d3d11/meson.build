d3d11_sources = [
  'gstd3d11bufferpool.c',
  'gstd3d11device.c',
  'gstd3d11memory.c',
  'gstd3d11utils.c',
  'gstd3d11videosink.c',
  'gstd3d11window.c',
  'plugin.c',
  'gstd3d11format.c',
  'gstd3d11basefilter.c',
  'gstd3d11upload.c',
  'gstd3d11download.c',
  'gstd3d11colorconvert.c',
  'gstd3d11videosinkbin.c',
  'gstd3d11shader.c',
  'gstd3d11colorconverter.c',
]

dxgi_headers = [
  ['dxgi1_6.h', 6],
  ['dxgi1_5.h', 5],
  ['dxgi1_4.h', 4],
  ['dxgi1_3.h', 3],
  ['dxgi1_2.h', 2],
  ['dxgi.h', 1]
]

have_d3d11 = false
extra_c_args = ['-DCOBJMACROS']
have_dxgi_header = false
extra_dep = []
d3d11_conf = configuration_data()

d3d11_option = get_option('d3d11')
if host_system != 'windows' or d3d11_option.disabled()
  subdir_done()
endif

d3d11_lib = cc.find_library('d3d11', required : d3d11_option)
dxgi_lib = cc.find_library('dxgi', required : d3d11_option)
d3dcompiler_lib = cc.find_library('d3dcompiler', required: d3d11_option)

foreach dxgi_h: dxgi_headers
  if not have_dxgi_header and cc.has_header(dxgi_h[0])
    d3d11_conf.set('DXGI_HEADER_VERSION', dxgi_h[1])
    have_dxgi_header = true
  endif
endforeach

have_d3d11 = d3d11_lib.found() and dxgi_lib.found() and cc.has_header('d3d11.h') and have_dxgi_header and cc.has_header('d3dcompiler.h')
if not have_d3d11
  if d3d11_option.enabled()
    error('The d3d11 plugin was enabled explicitly, but required dependencies were not found.')
  endif
  subdir_done()
endif

# for enabling debug layer
if cc.has_header('d3d11sdklayers.h')
  d3d11_conf.set('HAVE_D3D11SDKLAYER_H', 1)
  extra_dep += [gmodule_dep]
endif

configure_file(
  output: 'd3d11config.h',
  configuration: d3d11_conf,
)

gstd3d11 = library('gstd3d11',
  d3d11_sources,
  c_args : gst_plugins_bad_args + extra_c_args,
  include_directories : [configinc],
  dependencies : [gstbase_dep, gstvideo_dep, d3d11_lib, dxgi_lib, d3dcompiler_lib] + extra_dep,
  install : true,
  install_dir : plugins_install_dir,
)
pkgconfig.generate(gstd3d11, install_dir : plugins_pkgconfig_install_dir)
plugins += [gstd3d11]
