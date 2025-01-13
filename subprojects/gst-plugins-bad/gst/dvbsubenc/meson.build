subenc_sources = [
  'gstdvbsubenc.c',
  'gstdvbsubenc-util.c',
]

libimagequant_sources = [
  'libimagequant/blur.c',
  'libimagequant/libimagequant.c',
  'libimagequant/mediancut.c',
  'libimagequant/mempool.c',
  'libimagequant/nearest.c',
  'libimagequant/pam.c',
  'libimagequant/viter.c'
]

subenc_headers = [
  'gstdvbsubenc.h',
]

doc_sources = []
foreach s: subenc_sources + subenc_headers
  doc_sources += meson.current_source_dir() / s
endforeach

plugin_sources += {
  'dvbsubenc': pathsep.join(doc_sources)
}

if get_option('dvbsubenc').disabled()
  subdir_done()
endif

local_c_args = ['-DLIQ_PRIVATE='] # GStreamer already has the right hidden symbol visibility
local_c_args += cc.get_supported_arguments(['-Wno-unknown-pragmas', '-Wno-aggregate-return'])

gstdvbsubenc = library('gstdvbsubenc',
  subenc_sources + libimagequant_sources,
  c_args : gst_plugins_bad_args + local_c_args,
  include_directories : [configinc, libsinc],
  dependencies : [gstbase_dep, gstvideo_dep, libm],
  install : true,
  install_dir : plugins_install_dir,
)
plugins += [gstdvbsubenc]
