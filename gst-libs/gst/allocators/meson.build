gst_allocators_headers = [
  'allocators.h',
  'gstfdmemory.h',
  'gstdmabuf.h',
]
install_headers(gst_allocators_headers, subdir : 'gstreamer-1.0/gst/allocators/')

gst_allocators_sources = [ 'gstdmabuf.c', 'gstfdmemory.c' ]
gstallocators = library('gstallocators-@0@'.format(api_version),
  gst_allocators_sources,
  c_args : gst_plugins_base_args,
  include_directories: [configinc, libsinc],
  version : libversion,
  soversion : soversion,
  install : true,
  dependencies : [gst_dep],
  vs_module_defs: vs_module_defs_dir + 'libgstallocators.def',
)

allocators_gen_sources  = []
if build_gir
  gst_gir_extra_args = gir_init_section + [ '--c-include=gst/allocators/allocators.h' ]
  allocators_gen_sources += [gnome.generate_gir(gstallocators,
    sources : gst_allocators_sources + gst_allocators_headers,
    namespace : 'GstAllocators',
    nsversion : api_version,
    identifier_prefix : 'Gst',
    symbol_prefix : 'gst',
    export_packages : 'gstreamer-allocators-1.0',
    includes : ['Gst-1.0'],
    install : true,
    extra_args : gst_gir_extra_args,
    dependencies : [gst_dep]
  )]
endif

allocators_dep = declare_dependency(link_with: gstallocators,
  include_directories : [libsinc],
  dependencies : [gst_dep],
  sources : allocators_gen_sources)
