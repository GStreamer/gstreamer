gst_net_sources = files(
  'gstnetaddressmeta.c',
  'gstnetclientclock.c',
  'gstnetcontrolmessagemeta.c',
  'gstnettimepacket.c',
  'gstnettimeprovider.c',
  'gstptpclock.c',
  'gstntppacket.c',
  'gstnetutils.c',
)

gst_net_headers = files(
 'gstnet.h',
 'gstnetaddressmeta.h',
 'gstnetclientclock.h',
 'gstnetcontrolmessagemeta.h',
 'gstnettimepacket.h',
 'gstnettimeprovider.h',
 'gstnetutils.h',
 'gstptpclock.h',
 'net-prelude.h',
 'net.h',
)
install_headers(gst_net_headers, subdir : 'gstreamer-1.0/gst/net/')

gst_net_gen_sources = []
gst_net = library('gstnet-@0@'.format(apiversion),
  gst_net_sources,
  c_args : gst_c_args + ['-DBUILDING_GST_NET', '-DG_LOG_DOMAIN="GStreamer-Net"'],
  include_directories : [configinc, libsinc],
  version : libversion,
  soversion : soversion,
  darwin_versions : osxversion,
  install : true,
  dependencies : [gio_dep, gst_base_dep, dl_dep],
)

library_def = {'lib': gst_net}
pkg_name = 'gstreamer-net-1.0'
pkgconfig.generate(gst_net,
  libraries : [libgst],
  variables : pkgconfig_variables,
  subdirs : pkgconfig_subdirs,
  name : pkg_name,
  description : 'Network-enabled GStreamer plug-ins and clocking',
)

if build_gir
  gst_gir_extra_args = gir_init_section + [ '--c-include=gst/net/net.h' ]
  gir = {
    'sources' : gst_net_sources + gst_net_headers,
    'namespace' : 'GstNet',
    'nsversion' : apiversion,
    'identifier_prefix' : 'Gst',
    'symbol_prefix' : 'gst',
    'export_packages' : pkg_name,
    'dependencies' : [gst_base_dep],
    'include_directories' : [configinc, libsinc],
    'includes' : ['GLib-2.0', 'GObject-2.0', 'GModule-2.0', 'Gio-2.0', 'Gst-1.0'],
    'install' : true,
    'extra_args' : gst_gir_extra_args,
  }
  library_def += {'gir': [gir]}
  if not static_build
    gst_net_gir = gnome.generate_gir(gst_net, kwargs: gir)
    library_def += {'gir_targets':  library_def.get('gir_targets', []) + [gst_net_gir]}
    gst_net_gen_sources += [gst_net_gir]
  endif
endif
gst_libraries += [[pkg_name, library_def]]

gst_net_dep = declare_dependency(link_with : gst_net,
  include_directories : [libsinc],
  sources: gst_net_gen_sources,
  dependencies : [gst_base_dep])

meson.override_dependency(pkg_name, gst_net_dep)
