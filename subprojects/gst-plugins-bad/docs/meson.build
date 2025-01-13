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


hotdoc_p = find_program('hotdoc', required: get_option('doc'))
if not hotdoc_p.found()
    message('Hotdoc not found, not building the documentation')
    subdir_done()
endif

hotdoc_req = '>= 0.11.0'
hotdoc_version = run_command(hotdoc_p, '--version', check: false).stdout()
if not hotdoc_version.version_compare(hotdoc_req)
    if get_option('doc').enabled()
        error('Hotdoc version @0@ not found, got @1@'.format(hotdoc_req, hotdoc_version))
    else
        message('Hotdoc version @0@ not found, got @1@'.format(hotdoc_req, hotdoc_version))
        subdir_done()
    endif
endif

hotdoc = import('hotdoc')
required_hotdoc_extensions = ['gi-extension', 'c-extension', 'gst-extension']
foreach extension: required_hotdoc_extensions
    if not hotdoc.has_extensions(extension)
        if get_option('doc').enabled()
            error('Documentation enabled but @0@ missing'.format(extension))
        endif

        message('@0@ extensions not found, not building documentation requiring it'.format(extension))
    endif
endforeach

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

libs = []
if build_gir
    libs = [
        {'name': 'mpegts', 'gir': mpegts_gir, 'lib': gstmpegts_dep},
        {'name': 'play', 'gir': play_gir, 'lib': gstplay_dep},
        {'name': 'player', 'gir': player_gir, 'lib': gstplayer_dep},
        {'name': 'insertbin', 'gir': insertbin_gir, 'lib': gstinsertbin_dep},
        {'name': 'codecparsers', 'lib': gstcodecparsers_dep},
        {'name': 'adaptivedemux', 'lib': gstadaptivedemux_dep},
        {'name': 'basecamerabinsrc', 'gir': basecamerabin_gir, 'lib': gstbasecamerabin_dep},
        {'name': 'webrtc', 'gir': webrtc_gir, 'lib': gstwebrtc_dep, 'suffix': 'lib'},
        {'name': 'audio', 'gir': audio_gir, 'lib': gstbadaudio_dep, 'prefix': 'bad-'},
        {'name': 'transcoder', 'gir': transcoder_gir, 'lib': gst_transcoder_dep},
        {'name': 'codecs', 'gir': codecs_gir, 'lib': gstcodecs_dep},
        {'name': 'dxva', 'gir': dxva_gir, 'lib': gstdxva_dep, 'c_source_patterns': ['*.h', '*.cpp']},
        {'name': 'mse', 'gir': mse_gir, 'lib': gstmse_dep, 'suffix': 'lib'},
        {'name': 'analytics', 'gir': analytics_gir, 'lib': gstanalytics_dep},
    ]

    if get_variable('gst_cuda_gir', []).length() > 0
        libs += [{'name': 'cuda', 'gir': gst_cuda_gir, 'lib': gstcuda_dep, 'c_source_patterns': ['*.h', '*.cpp']}]
    endif

    if gstopencv_dep.found()
        libs += [
            {'name': 'opencv', 'lib': gstopencv_dep, 'c_source_patterns': ['*.h', '*.cpp'], 'extra_c_flags': ['-x c++']},
        ]
    endif

    if gstva_dep.found()
        libs += [{'name': 'va', 'gir': va_gir, 'lib': gstva_dep, 'suffix': 'lib'}]
    else
        libs += [{'name': 'va', 'gir-file': join_paths(meson.global_source_root(), 'girs', 'GstVa-1.0.gir'), 'lib': []}]
    endif

    if gstvulkan_dep.found()
        libs += [
             {'name': 'vulkan', 'gir': vulkan_gir, 'lib': gstvulkan_dep, 'suffix': 'lib'},
        ]
        if enabled_vulkan_winsys.contains('xcb')
            libs += [
                {'name': 'vulkan-xcb', 'gir': vulkan_xcb_gir, 'lib': gstvulkanxcb_dep, 'extra_sources' : [
                    join_paths(root_rel, 'gst-libs/gst/vulkan/xcb/gstvkdisplay_xcb.[ch]'),
                ]},
            ]
        endif
        if enabled_vulkan_winsys.contains('wayland')
            libs += [
                {'name': 'vulkan-wayland', 'gir': vulkan_wayland_gir, 'lib': gstvulkanwayland_dep, 'extra_sources' : [
                    join_paths(root_rel, 'gst-libs/gst/vulkan/wayland/gstvkdisplay_wayland.[ch]'),
                ]},
            ]
        endif
    endif
endif

has_gi_extension = hotdoc.has_extensions('gi-extension')
has_c_extension = hotdoc.has_extensions('c-extension')
libs_doc = []
foreach lib_def: libs
  name = lib_def['name']
  lib = lib_def['lib']
  extra_sources = lib_def.get('extra_sources', [])

  c_source_patterns = lib_def.get('c_source_patterns', ['*.[hc]'])
  c_sources = []
  foreach pattern: c_source_patterns
    c_sources += join_paths(root_rel, 'gst-libs/gst', name, pattern)
  endforeach
  c_sources += extra_sources

    if lib_def.has_key('gir') or lib_def.has_key('gir-file')
        if has_gi_extension
            if lib_def.has_key('gir')
                gir_targets = lib_def['gir']
                gir = gir_targets[0]
                gir_file = gir[0].full_path()
            else
                gir_targets = []
                gir = []
                gir_file = lib_def['gir-file']
            endif

            prefix = lib_def.get('prefix', '')
            suffix = lib_def.get('suffix', '')
            libs_doc += [hotdoc.generate_doc(prefix + name + suffix,
                project_version: api_version,
                gi_c_sources: c_sources,
                gi_sources: gir_file,
                gi_c_source_filters: excludes,
                sitemap: join_paths('libs', name, 'sitemap.txt'),
                index: join_paths('libs/', name, 'index.md'),
                gi_index: join_paths('libs/', name, 'index.md'),
                gi_smart_index: true,
                gi_order_generated_subpages: true,
                dependencies: [lib, gir_targets],
                c_flags: '-DGST_USE_UNSTABLE_API',
                install: false,
                depends: gir,
            )]
        endif
    else
        if has_c_extension
            libs_doc += [hotdoc.generate_doc('gst-plugins-bad-' + name,
                sitemap: join_paths('libs', name, 'sitemap.txt'),
                index: join_paths('libs', name, 'index.md'),
                project_version: api_version,
                c_sources: c_sources,
                c_source_filters: excludes,
                c_index: join_paths('libs', name, 'index.md'),
                c_smart_index: true,
                c_order_generated_subpages: true,
                extra_c_flags: ['-DGST_USE_UNSTABLE_API'] + lib_def.get('extra_c_flags', []),
                dependencies: lib,
                install: false,
            )]
        endif
    endif
endforeach

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
