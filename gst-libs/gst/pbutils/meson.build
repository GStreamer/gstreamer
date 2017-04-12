pbutils_sources = [
  'gstpluginsbaseversion.c',
  'pbutils.c',
  'codec-utils.c',
  'descriptions.c',
  'encoding-profile.c',
  'encoding-target.c',
  'install-plugins.c',
  'missing-plugins.c',
  'gstaudiovisualizer.c',
  'gstdiscoverer.c',
  'gstdiscoverer-types.c'
  ]

pbconf = configuration_data()
pbconf.set('PACKAGE_VERSION_MAJOR', gst_version_major)
pbconf.set('PACKAGE_VERSION_MINOR', gst_version_minor)
pbconf.set('PACKAGE_VERSION_MICRO', gst_version_micro)
pbconf.set('PACKAGE_VERSION_NANO', gst_version_nano)
configure_file(input : 'gstpluginsbaseversion.h.in',
  output: 'gstpluginsbaseversion.h',
  install_dir : 'include/gstreamer-1.0/gst/pbutils/',
  configuration: pbconf)

pbutils_headers = [
  'pbutils.h',
  'codec-utils.h',
  'descriptions.h',
  'encoding-profile.h',
  'encoding-target.h',
  'install-plugins.h',
  'missing-plugins.h',
  'gstdiscoverer.h',
  'gstaudiovisualizer.h',
]
install_headers(pbutils_headers, subdir : 'gstreamer-1.0/gst/pbutils/')

pbutils_mkenum_headers = pbutils_headers

mkenums = find_program('pbutils_mkenum.py')
gstpbutils_h = custom_target('gstpbutilsenum_h',
  output : 'pbutils-enumtypes.h',
  input : pbutils_mkenum_headers,
  install : true,
  install_dir : 'include/gstreamer-1.0/gst/pbutils/',
  command : [mkenums, glib_mkenums, '@OUTPUT@', '@INPUT@'])

gstpbutils_c = custom_target('gstpbutilsenum_c',
  output : 'pbutils-enumtypes.c',
  input : pbutils_mkenum_headers,
  depends : [gstaudio_h, gstvideo_h, gstpbutils_h],
  command : [mkenums, glib_mkenums, '@OUTPUT@', '@INPUT@'])

gstpbutils_deps = [video_dep, audio_dep, tag_dep]
pbutils = library('gstpbutils-@0@'.format(api_version),
  pbutils_sources, gstpbutils_c, gstpbutils_h,
  c_args : gst_plugins_base_args,
  include_directories: [configinc, libsinc],
  version : libversion,
  soversion : soversion,
  install : true,
  dependencies : gstpbutils_deps,
  vs_module_defs: vs_module_defs_dir + 'libgstpbutils.def',
)

pbutils_gen_sources = [gstpbutils_h]
if build_gir
  gst_gir_extra_args = gir_init_section + [ '--c-include=gst/pbutils/pbutils.h' ]
  pbutils_gen_sources += [gnome.generate_gir(pbutils,
    sources : pbutils_sources + pbutils_headers + [gstpbutils_h],
    namespace : 'GstPbutils',
    nsversion : api_version,
    identifier_prefix : 'Gst',
    symbol_prefix : 'gst',
    export_packages : 'gstreamer-pbutils-1.0',
    includes : ['Gst-1.0', 'GstBase-1.0'],
    install : true,
    extra_args : gst_gir_extra_args,
    dependencies : gstpbutils_deps
  )]
endif

pbutils_dep = declare_dependency(link_with : pbutils,
  include_directories : [libsinc],
  dependencies : gstpbutils_deps,
  sources : pbutils_gen_sources)
