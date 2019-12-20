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
  'gstd3d11overlaycompositor.c',
]

d3d11_dec_sources = [
  'gsth264picture.c',
  'gsth264decoder.c',
  'gstd3d11decoder.c',
  'gstd3d11h264dec.c',
  'gstvp9picture.c',
  'gstvp9decoder.c',
  'gstd3d11vp9dec.c',
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
have_d3d11sdk_h = false
have_dxgidebug_h = false
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
if get_option('buildtype').startswith('debug')
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

  if have_d3d11sdk_h or have_dxgidebug_h
    extra_dep += [gmodule_dep]
  endif
endif

d3d11_conf.set10('HAVE_D3D11SDKLAYERS_H', have_d3d11sdk_h)
d3d11_conf.set10('HAVE_DXGIDEBUG_H', have_dxgidebug_h)

# d3d11 video api uses dxva structure for decoding, and dxva.h needs d3d9 types
if cc.has_header('dxva.h') and cc.has_header('d3d9.h')
  d3d11_conf.set('HAVE_DXVA_H', 1)
  d3d11_sources += d3d11_dec_sources
  extra_c_args += ['-DGST_USE_UNSTABLE_API']
  extra_dep += [gstcodecparsers_dep]
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
