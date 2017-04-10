gst_net_sources = [
  'gstnetaddressmeta.c',
  'gstnetclientclock.c',
  'gstnetcontrolmessagemeta.c',
  'gstnettimepacket.c',
  'gstnettimeprovider.c',
  'gstptpclock.c',
  'gstntppacket.c',
]

gst_net_headers = [
 'gstnet.h',
 'gstnetaddressmeta.h',
 'gstnetclientclock.h',
 'gstnetcontrolmessagemeta.h',
 'gstnettimepacket.h',
 'gstnettimeprovider.h',
 'gstptpclock.h',
 'net.h',
]
install_headers(gst_net_headers, subdir : 'gstreamer-1.0/gst/net/')

if libtype != 'shared'
  gst_net_static = static_library('gstnet-@0@'.format(apiversion),
    gst_net_sources,
    c_args : gst_c_args,
    include_directories : [configinc, libsinc],
    install : true,
    dependencies : [gio_dep, gst_base_dep],
  )
  gst_net = gst_net_static
endif

gst_net_gen_sources = []
if libtype != 'static'
  gst_net_shared = shared_library('gstnet-@0@'.format(apiversion),
    gst_net_sources,
    c_args : gst_c_args,
    include_directories : [configinc, libsinc],
    version : libversion,
    soversion : soversion,
    install : true,
    dependencies : [gio_dep, gst_base_dep],
    vs_module_defs: vs_module_defs_dir + 'libgstnet.def',
  )
  gst_net = gst_net_shared
  if build_gir
    gst_gir_extra_args = gir_init_section + [ '--c-include=gst/net/net.h' ]
    gst_net_gir = gnome.generate_gir(gst_net_shared,
      sources : gst_net_sources + gst_net_headers,
      namespace : 'GstNet',
      nsversion : apiversion,
      identifier_prefix : 'Gst',
      symbol_prefix : 'gst',
      export_packages : 'gstreamer-net-1.0',
      dependencies : [gst_base_dep],
      include_directories : [configinc, libsinc],
      includes : ['GLib-2.0', 'GObject-2.0', 'GModule-2.0', 'Gio-2.0', 'Gst-1.0'],
      install : true,
      extra_args : gst_gir_extra_args,
    )
    gst_net_gen_sources += [gst_net_gir]
  endif
endif

gst_net_dep = declare_dependency(link_with : gst_net,
  include_directories : [libsinc],
  sources: gst_net_gen_sources,
  dependencies : [gst_base_dep])
