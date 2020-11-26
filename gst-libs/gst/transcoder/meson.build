sources = files(['gsttranscoder.c', 'gsttranscoder-signal-adapter.c'])
headers = files(['gsttranscoder.h', 'transcoder-prelude.h', 'gsttranscoder-signal-adapter.h'])

install_headers(headers, subdir : 'gstreamer-' + api_version + '/gst/transcoder')

transcoder_enums = gnome.mkenums_simple('transcoder-enumtypes',
  sources : headers,
  body_prefix : '#ifdef HAVE_CONFIG_H\n#include "config.h"\n#endif',
  header_prefix : '#include <gst/transcoder/transcoder-prelude.h>',
  decorator: 'GST_TRANSCODER_API',
  install_header: true,
  install_dir : join_paths(get_option('includedir'), 'gstreamer-1.0/gst/transcoder'))

gsttranscoder_c = transcoder_enums[0]
gsttranscoder_h = transcoder_enums[1]

transcoder_gen_sources = [gsttranscoder_h]

gst_transcoder = library('gsttranscoder-' + api_version,
  sources + [gsttranscoder_c]  + transcoder_gen_sources,
  install: true,
  include_directories : [configinc, libsinc],
  dependencies: [gst_dep, gstpbutils_dep],
  c_args: gst_plugins_bad_args + ['-DGST_USE_UNSTABLE_API', '-DBUILDING_GST_TRANSCODER'],
  soversion : soversion,
)

pkgconfig.generate(gst_transcoder,
  libraries : [gst_dep, gstbase_dep],
  variables : pkgconfig_variables,
  subdirs : pkgconfig_subdirs,
  name : 'gstreamer-transcoder-1.0',
  description : 'High level API for transcoding using GStreamer',
)

if build_gir
  transcoder_gir = gnome.generate_gir(gst_transcoder,
    sources : sources + headers + [gsttranscoder_h],
    nsversion : api_version,
    namespace : 'GstTranscoder',
    identifier_prefix : 'Gst',
    symbol_prefix : 'gst_',
    includes : ['GObject-2.0',
                'Gst-' + api_version,
                'GstPbutils-' + api_version],
    dependencies: [gst_dep, gstpbutils_dep],
    install : true,
    extra_args : gir_init_section
  )
  transcoder_gen_sources += transcoder_gir
endif

gst_transcoder_dep = declare_dependency(link_with: gst_transcoder,
  dependencies : [gst_dep, gstpbutils_dep],
  include_directories : [libsinc],
  sources: transcoder_gen_sources,
)

meson.override_dependency('gstreamer-transcoder-1.0', gst_transcoder_dep)
