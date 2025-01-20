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
endif

if gst_dep.type_name() == 'internal'
    gst_proj = subproject('gstreamer')
    plugins_cache_generator = gst_proj.get_variable('plugins_cache_generator')
else
    plugins_cache_generator = find_program('gst-plugins-doc-cache-generator',
        dirs: [join_paths(gst_dep.get_variable('libexecdir', default_value: ''), 'gstreamer-' + api_version)],
        required: false)
endif

plugins_cache = join_paths(meson.current_source_dir(), 'plugins', 'gst_plugins_cache.json')

if plugins.length() == 0
    message('All bad plugins have been disabled')
elif plugins_cache_generator.found()
    gst_plugins_doc_dep = custom_target('bad-plugins-doc-cache',
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

configure_file(input : 'gst_api_version.in',
               output : 'gst_api_version.md',
               configuration : docconf)

root_rel = '..'
excludes = []
foreach f: [
        'gst/frei0r/frei0r.h',
        'gst/mxf/mxful.c',
        'gst-libs/gst/*/*-private.h',
        'gst-libs/gst/codecparsers/nalutils.h',
        'ext/closedcaption/bcd.h',
        'ext/closedcaption/bit_slicer.[ch]',
        'ext/closedcaption/decoder.[ch]',
        'ext/closedcaption/macros.h',
        'ext/closedcaption/misc.h',
        'ext/closedcaption/raw_decoder.[ch]',
        'ext/closedcaption/sampling_par.[ch]',
        'ext/closedcaption/sliced.[ch]',
        'ext/lv2/gstlv2utils.h',
        'ext/closedcaption/hamm.h',
        'ext/closedcaption/io-sim.h',
        'ext/avtp/gstavtpcrfbase.h',
        'sys/nvcodec/nvEncodeAPI.h',
        'sys/nvcodec/cuviddec.h',
        'sys/nvcodec/nvcuvid.h',
        'sys/winks/ksvideohelpers.h',
        'gst/videoparsers/gstjpeg2000parse.h',
        'gst-libs/gst/*/*-prelude.h',
        'gst-libs/gst/vulkan/vulkan_fwd.h',
        'gst-libs/gst/dxva/gstdxvatypedef.h',
    ]
    excludes += [join_paths(meson.current_source_dir(), root_rel, f)]
endforeach

libs_doc_source_file = configure_file(
  output: 'libs_doc_sources.json',
  configuration: libs_sources,
  output_format: 'json')

libs_doc_c_source_file = configure_file(
  output: 'libs_doc_c_sources.json',
  configuration: libs_c_sources,
  output_format: 'json')

libs_doc_gi_conf = {}

libs_doc_depends = []

if build_gir
    libs_doc_gi_conf += {
      'mpegts': mpegts_gir[0].full_path(),
      'play': play_gir[0].full_path(),
      'player': player_gir[0].full_path(),
      'insertbin': insertbin_gir[0].full_path(),
      'basecamerabinsrc': basecamerabin_gir[0].full_path(),
      'webrtc': webrtc_gir[0].full_path(),
      'audio': audio_gir[0].full_path(),
      'transcoder': transcoder_gir[0].full_path(),
      'codecs': codecs_gir[0].full_path(),
      'dxva': dxva_gir[0].full_path(),
      'mse': mse_gir[0].full_path(),
      'analytics': analytics_gir[0].full_path(),
    }

    libs_doc_depends += [
      mpegts_gir[0],
      play_gir[0],
      player_gir[0],
      insertbin_gir[0],
      basecamerabin_gir[0],
      webrtc_gir[0],
      audio_gir[0],
      transcoder_gir[0],
      codecs_gir[0],
      dxva_gir[0],
      mse_gir[0],
      analytics_gir[0],
    ]

    if get_variable('gst_cuda_gir', []).length() > 0
        libs_doc_gi_conf += {'cuda': gst_cuda_gir[0].full_path()}
        libs_doc_depends += [gst_cuda_gir[0]]
    endif

    if gstva_dep.found()
        libs_doc_gi_conf += {'va': va_gir[0].full_path()}
        libs_doc_depends += [va_gir[0]]
    else
        libs_doc_gi_conf += {'va': join_paths(meson.global_source_root(), 'girs', 'GstVa-1.0.gir')}
    endif

    if gstvulkan_dep.found()
        libs_doc_gi_conf += {'vulkan': vulkan_gir[0].full_path()}
        libs_doc_depends += [vulkan_gir[0]]
        if enabled_vulkan_winsys.contains('xcb')
            libs_doc_gi_conf += {'vulkan-xcb': vulkan_xcb_gir[0].full_path()}
            libs_doc_depends += [vulkan_xcb_gir[0]]
        endif
        if enabled_vulkan_winsys.contains('wayland')
            libs_doc_gi_conf += {'vulkan-wayland': vulkan_wayland_gir[0].full_path()}
            libs_doc_depends += [vulkan_wayland_gir[0]]
        endif
    endif
endif

cdir = meson.current_source_dir()

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
    '--gi_c_source_filters', excludes,
    '--c_source_filters', excludes,
    '--c_source_file', libs_doc_c_source_file,
    '--source_root', cdir / 'libs',
    '--gi_source_root', cdir / '..' / 'gst-libs' / 'gst',
    '--output', '@OUTPUT@',
  ],
  output: 'hotdoc-libs-configs.json',
  depends: libs_doc_depends,
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

cdir = meson.current_source_dir()
plugin_hotdoc_configs = custom_target(
  'build-hotdoc-configs',
  command: [
    plugins_cache_generator,
    'hotdoc-config',
    '--builddir', meson.current_build_dir(),
    '--project_version', api_version,
    '--sitemap', cdir / 'plugins/sitemap.txt',
    '--index', cdir / 'plugins/index.md',
    '--c_flags', '\-DGST_USE_UNSTABLE_API',
    '--gst_index', 'plugins/index.md',
    '--gst_c_source_filters', excludes,
    '--gst_cache_file', '@INPUT@',
    '--include_paths', cdir / '..',
    '--output', '@OUTPUT@',
    '--gst_c_source_file', doc_source_file,
    '--gst_plugin_libraries_file', doc_plugin_libs_file,
  ],
  input: plugins_cache,
  output: 'hotdoc-configs.json',
)
