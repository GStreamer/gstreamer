app_sources = ['gstappsrc.c', 'gstappsink.c']

app_mkenum_headers = [
  'gstappsrc.h',
]

app_headers = app_mkenum_headers + [ 'app.h', 'app-prelude.h', 'gstappsrc.h', 'gstappsink.h' ]
install_headers(app_headers, subdir : 'gstreamer-1.0/gst/app/')

app_enums = gnome.mkenums_simple('app-enumtypes',
  sources : app_mkenum_headers,
  body_prefix : '#ifdef HAVE_CONFIG_H\n#include "config.h"\n#endif',
  header_prefix : '#include <gst/app/app-prelude.h>',
  decorator : 'GST_APP_API',
  install_header: true,
  install_dir : join_paths(get_option('includedir'), 'gstreamer-1.0/gst/app'))
gstapp_c = app_enums[0]
gstapp_h = app_enums[1]

app_gen_sources = [gstapp_h]

gstapp = library('gstapp-@0@'.format(api_version),
  app_sources, gstapp_h, gstapp_c,
  c_args : gst_plugins_base_args + ['-DBUILDING_GST_APP'],
  include_directories: [configinc, libsinc],
  version : libversion,
  soversion : soversion,
  darwin_versions : osxversion,
  install : true,
  dependencies : [gst_base_dep],
)

if build_gir
  gst_gir_extra_args = gir_init_section + [ '--c-include=gst/app/app.h' ]
  app_gir = gnome.generate_gir(gstapp,
    sources : app_sources + app_headers + [gstapp_c] + [gstapp_h],
    namespace : 'GstApp',
    nsversion : api_version,
    identifier_prefix : 'Gst',
    symbol_prefix : 'gst',
    export_packages : 'gstreamer-app-1.0',
    includes : ['Gst-1.0', 'GstBase-1.0'],
    install : true,
    extra_args : gst_gir_extra_args,
    dependencies : [gst_dep, gst_base_dep]
  )

  app_gen_sources += app_gir
endif

app_dep = declare_dependency(link_with: gstapp,
  include_directories : [libsinc],
  dependencies : [gst_base_dep],
  sources : app_gen_sources)
