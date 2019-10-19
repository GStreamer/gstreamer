sources = files(['gsttranscoder.c'])
headers = files(['gsttranscoder.h', 'transcoder-prelude.h'])

install_headers(headers, subdir : 'gstreamer-' + api_version + '/gst/transcoder')

gst_transcoder = library('gsttranscoder-' + api_version,
  sources,
  install: true,
  include_directories : [configinc, libsinc],
  dependencies: [gst_dep, gstpbutils_dep],
  c_args: gst_plugins_bad_args + ['-DGST_USE_UNSTABLE_API', '-DBUILDING_GST_TRANSCODER'],
  soversion : soversion,
)
if build_gir
  transcoder_gir = gnome.generate_gir(gst_transcoder,
    sources : sources + headers,
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
endif

gst_transcoder_dep = declare_dependency(link_with: gst_transcoder,
  dependencies : [gst_dep, gstpbutils_dep],
  include_directories : [libsinc]
)