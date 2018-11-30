ges_sources = [
    'ges.c',
    'ges-enums.c',
    'ges-meta-container.c',
    'ges-timeline.c',
    'ges-layer.c',
    'ges-clip.c',
    'ges-pipeline.c',
    'ges-source-clip.c',
    'ges-base-effect-clip.c',
    'ges-effect-clip.c',
    'ges-uri-clip.c',
    'ges-operation-clip.c',
    'ges-base-transition-clip.c',
    'ges-transition-clip.c',
    'ges-test-clip.c',
    'ges-title-clip.c',
    'ges-overlay-clip.c',
    'ges-text-overlay-clip.c',
    'ges-track.c',
    'ges-audio-track.c',
    'ges-video-track.c',
    'ges-track-element.c',
    'ges-source.c',
    'ges-operation.c',
    'ges-video-source.c',
    'ges-audio-source.c',
    'ges-video-uri-source.c',
    'ges-audio-uri-source.c',
    'ges-image-source.c',
    'ges-multi-file-source.c',
    'ges-transition.c',
    'ges-audio-transition.c',
    'ges-video-transition.c',
    'ges-video-test-source.c',
    'ges-audio-test-source.c',
    'ges-title-source.c',
    'ges-text-overlay.c',
    'ges-base-effect.c',
    'ges-effect.c',
    'ges-screenshot.c',
    'ges-formatter.c',
    'ges-asset.c',
    'ges-uri-asset.c',
    'ges-clip-asset.c',
    'ges-track-element-asset.c',
    'ges-extractable.c',
    'ges-project.c',
    'ges-base-xml-formatter.c',
    'ges-xml-formatter.c',
    'ges-command-line-formatter.c',
    'ges-auto-transition.c',
    'ges-timeline-element.c',
    'ges-container.c',
    'ges-effect-asset.c',
    'ges-smart-adder.c',
    'ges-smart-video-mixer.c',
    'ges-utils.c',
    'ges-group.c',
    'ges-validate.c',
    'ges-structured-interface.c',
    'ges-structure-parser.c',
    'gstframepositioner.c'
]

ges_headers = [
    'ges-types.h',
    'ges.h',
    'ges-prelude.h',
    'ges-enums.h',
    'ges-gerror.h',
    'ges-meta-container.h',
    'ges-timeline.h',
    'ges-layer.h',
    'ges-clip.h',
    'ges-pipeline.h',
    'ges-source-clip.h',
    'ges-uri-clip.h',
    'ges-base-effect-clip.h',
    'ges-effect-clip.h',
    'ges-operation-clip.h',
    'ges-base-transition-clip.h',
    'ges-transition-clip.h',
    'ges-test-clip.h',
    'ges-title-clip.h',
    'ges-overlay-clip.h',
    'ges-text-overlay-clip.h',
    'ges-base-effect.h',
    'ges-effect.h',
    'ges-track.h',
    'ges-audio-track.h',
    'ges-video-track.h',
    'ges-track-element.h',
    'ges-source.h',
    'ges-operation.h',
    'ges-video-source.h',
    'ges-audio-source.h',
    'ges-video-uri-source.h',
    'ges-audio-uri-source.h',
    'ges-image-source.h',
    'ges-multi-file-source.h',
    'ges-transition.h',
    'ges-audio-transition.h',
    'ges-video-transition.h',
    'ges-video-test-source.h',
    'ges-audio-test-source.h',
    'ges-title-source.h',
    'ges-text-overlay.h',
    'ges-screenshot.h',
    'ges-formatter.h',
    'ges-asset.h',
    'ges-uri-asset.h',
    'ges-clip-asset.h',
    'ges-track-element-asset.h',
    'ges-extractable.h',
    'ges-project.h',
    'ges-base-xml-formatter.h',
    'ges-xml-formatter.h',
    'ges-command-line-formatter.h',
    'ges-timeline-element.h',
    'ges-container.h',
    'ges-effect-asset.h',
    'ges-utils.h',
    'ges-group.h'
]

if libxml_dep.found()
  ges_sources += ['ges-pitivi-formatter.c']
  ges_headers += ['ges-pitivi-formatter.h']
endif

version_data = configuration_data()
version_data.set('GES_VERSION_MAJOR', gst_version_major)
version_data.set('GES_VERSION_MINOR', gst_version_minor)
version_data.set('GES_VERSION_MICRO', gst_version_micro)
version_data.set('GES_VERSION_NANO', gst_version_nano)

configure_file(input : 'ges-version.h.in',
  output : 'ges-version.h',
  install_dir : join_paths(get_option('includedir'), 'gstreamer-1.0/ges'),
  configuration : version_data)

install_headers(ges_headers, subdir : 'gstreamer-1.0/ges')

flex = find_program('flex', required : false)
if not flex.found()
  flex = find_program('win_flex', required : false)
  if not flex.found()
    error('flex not found')
  endif
endif

parser = custom_target('gesparselex',
  input : 'parse.l',
  output : ['lex.priv_ges_parse_yy.c', 'ges-parse-lex.h'],
  command : [flex, '-Ppriv_ges_parse_yy', '--header-file=@OUTPUT1@', '-o', '@OUTPUT0@', '@INPUT@']
)

libges = shared_library('ges-1.0', ges_sources, parser,
    version : libversion,
    soversion : soversion,
    darwin_versions : osxversion,
    c_args : [ges_c_args],
    include_directories : [configinc],
    install : true,
    dependencies : libges_deps)

ges_gen_sources = []
if build_gir
    ges_gir_extra_args = gir_init_section + [ '--c-include=ges/ges.h' ]
    if meson.is_subproject()
      # FIXME: There must be a better way to do this
      # Need to pass the include path to find gst/gst.h and gst/gstenumtypes.h (built)
      ges_gir_extra_args += ['--cflags-begin',
      '-I' + meson.current_source_dir() + '/..',
      '-I' + meson.current_build_dir() + '/..',
      '--cflags-end']
    endif
    ges_gen_sources += [gnome.generate_gir(libges,
        sources : ges_sources + ges_headers,
        namespace : 'GES',
        nsversion : apiversion,
        identifier_prefix : 'GES',
        symbol_prefix : 'ges',
        export_packages : 'gst-editing-services-1.0',
        includes : ['Gst-1.0', 'GstPbutils-1.0', 'GstVideo-1.0', 'Gio-2.0', 'GObject-2.0'],
        install : true,
        dependencies : libges_deps,
        extra_args : ges_gir_extra_args
    )]
endif

ges_dep = declare_dependency(link_with : libges,
  include_directories : [configinc],
  sources : ges_gen_sources,
  dependencies : libges_deps,
)
