build_hotdoc = false

if meson.is_cross_build()
    if get_option('doc').enabled()
        error('Documentation enabled but building the doc while cross building is not supported yet.')
    endif

    message('Documentation not built as building it while cross building is not supported yet.')
    subdir_done()
endif

if static_build
    if get_option('doc').enabled()
        error('Documentation enabled but not supported when building statically.')
    endif

    message('Building statically, can\'t build the documentation')
    subdir_done()
endif

if not build_gir
    if get_option('doc').enabled()
        error('Documentation enabled but introspection not built.')
    endif

    message('Introspection not built, won\'t build documentation requiring it')
    subdir_done()
endif


if gst_dep.type_name() == 'internal'
    gst_proj = subproject('gstreamer')
    plugins_cache_generator = gst_proj.get_variable('plugins_cache_generator')
else
    plugins_cache_generator = find_program('gst-plugins-doc-cache-generator',
        dirs: [join_paths(gst_dep.get_variable('libexecdir', default_value: ''), 'gstreamer-' + api_version)],
        required: false)
endif

cdir = meson.current_source_dir()
plugins_cache = cdir / 'plugins/gst_plugins_cache.json'
if plugins.length() == 0
    message('All base plugins have been disabled')
elif plugins_cache_generator.found()
    gst_plugins_doc_dep = custom_target('base-plugins-doc-cache',
        command: [plugins_cache_generator, plugins_cache, '@OUTPUT@', '@INPUT@'],
        input: plugins,
        output: 'gst_plugins_cache.json',
        build_always_stale: true,
    )
else
    warning('GStreamer plugin inspector for documentation not found, can\'t update the cache')
endif

if get_option('doc').disabled()
  subdir_done()
endif

build_hotdoc = true

docconf = configuration_data()
docconf.set('GST_API_VERSION', api_version)

version_entities = configure_file(input : 'version.in',
    output : 'gst_api_version.md',
    configuration : docconf)

libs_excludes = []
foreach h: ['pbutils-private.h', 'gsttageditingprivate.h', 'id3v2.h',
  'kiss_fft_f32.h', 'kiss_fft_f64.h', 'kiss_fftr_f32.h', 'kiss_fftr_f64.h',
  'kiss_fftr_s16.h', 'kiss_fftr_s32.h', 'kiss_fft_s16.h', 'kiss_fft_s32.h',
  '_kiss_fft_guts_f32.h', '_kiss_fft_guts_f64.h', '_kiss_fft_guts_s16.h',
  '_kiss_fft_guts_s16.h', '_kiss_fft_guts_s32.h', '_kiss_fft_guts_s32.h',
  'pbutils-marshal.h', 'audio-resampler-private.h', '*orc-dist.*',
  '*-neon.h', 'audio-resampler-macros.[ch]', '*-prelude.h', '*_private.h',
  'gstglfuncs.[ch]', 'gstgl_fwd.h'
  ]

  libs_excludes += [join_paths(meson.current_source_dir(), '..', 'gst-libs/gst/*/', h)]
endforeach

libs_doc_source_file = configure_file(
  output: 'libs_doc_sources.json',
  configuration: libs_sources,
  output_format: 'json')

libs_doc_c_source_file = configure_file(
  output: 'libs_doc_c_sources.json',
  configuration: libs_c_sources,
  output_format: 'json')

libs_doc_gi_conf = {
  'allocators': allocators_gir[0].full_path(),
  'app': app_gir[0].full_path(),
  'audio': audio_gir[0].full_path(),
  'pbutils': pbutils_gir[0].full_path(),
  'rtp': rtp_gir[0].full_path(),
  'rtsp': rtsp_gir[0].full_path(),
  'sdp': sdp_gir[0].full_path(),
  'tag': tag_gir[0].full_path(),
  'video': video_gir[0].full_path(),
}

if build_gstgl
  libs_doc_gi_conf += {'gl': gl_gir[0].full_path()}
  if enabled_gl_platforms.contains('egl')
    libs_doc_gi_conf += {'gl-egl': gl_egl_gir[0].full_path()}
  endif
  if enabled_gl_winsys.contains('x11')
    libs_doc_gi_conf += {'gl-x11': gl_x11_gir[0].full_path()}
  endif
  if enabled_gl_winsys.contains('wayland')
    libs_doc_gi_conf += {'gl-wayland': gl_wayland_gir[0].full_path()}
  endif
endif

libs_doc_gi_source_file = configure_file(
  output: 'libs_doc_gi_sources.json',
  configuration: libs_doc_gi_conf,
  output_format: 'json')

libs_hotdoc_configs = custom_target(
  'build-libs-hotdoc-configs',
  command: [
    plugins_cache_generator,
    'hotdoc-lib-config',
    '--srcdir', cdir,
    '--builddir', meson.current_build_dir(),
    '--buildroot', meson.global_build_root(),
    '--project_version', api_version,
    '--gi_source_file', libs_doc_gi_source_file,
    '--gi_c_source_file', libs_doc_source_file,
    '--gi_c_source_filters', libs_excludes,
    '--c_source_filters', libs_excludes,
    '--c_source_file', libs_doc_c_source_file,
    '--source_root', cdir / 'libs',
    '--gi_source_root', cdir / '..' / 'gst-libs' / 'gst',
    '--output', '@OUTPUT@',
  ],
  output: 'hotdoc-libs-configs.json',
)

doc_source_file = configure_file(output: 'doc_sources.json', configuration: plugin_sources, output_format: 'json')

plugin_libraries = {}

foreach plugin: plugins
  if plugin.name().startswith('gst')
    plugin_name = plugin.name().substring(3)
  else
    plugin_name = plugin.name()
  endif

  plugin_libraries += {
    plugin_name: plugin.full_path()
  }
endforeach

doc_plugin_libs_file = configure_file(output: 'doc_plugin_libs.json', configuration: plugin_libraries, output_format: 'json')

plugin_hotdoc_configs = custom_target(
  'build-hotdoc-configs',
  command: [
    plugins_cache_generator,
    'hotdoc-config',
    '--builddir', meson.current_build_dir(),
    '--project_version', api_version,
    '--sitemap', cdir / 'plugins/sitemap.txt',
    '--index', cdir / 'plugins/index.md',
    '--gst_index', cdir / 'plugins/index.md',
    '--gst_cache_file', '@INPUT@',
    '--output', '@OUTPUT@',
    '--gst_c_source_file', doc_source_file,
    '--gst_plugin_libraries_file', doc_plugin_libs_file,
  ],
  input: plugins_cache,
  output: 'hotdoc-configs.json',
)
