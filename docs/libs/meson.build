types = configure_file(input : 'ges.types',
  output : 'ges.types',
  copy: true)

doc_deps_names = ['glib-2.0',
                  'gstreamer-@0@'.format(apiversion),
                  'gstreamer-plugins-base-@0@'.format(apiversion)]

doc_deps = []
foreach doc_dep : doc_deps_names
    # TODO: Use get_pkgconfig_variable()
    runcmd = run_command('pkg-config', '--variable=prefix', doc_dep)
    if runcmd.returncode() == 0
        tmp = '--extra-dir=' + runcmd.stdout().strip() + '/share/gtk-doc/html/'
        tmp.strip()
        doc_deps = doc_deps + [tmp]
    endif
endforeach

gnome.gtkdoc('ges',
  main_sgml : 'ges-docs.sgml',
  src_dir : '@0@/../../ges'.format(meson.current_source_dir()),
  scan_args : ['--deprecated-guards=GST_DISABLE_DEPRECATED',
            '--ignore-decorators=GES_API',
            '--ignore-headers=gesmarshal.h ges-internal.h ges-auto-transition.h ges-structured-interface.h ges-structure-parser.h ges-smart-video-mixer.h gstframepositioner.h'
            ],
  scanobjs_args : ['--type-init-func="gst_init(NULL,NULL)"'],
  gobject_typesfile : types,
  dependencies : [ges_dep],
  fixxref_args: doc_deps + ['--html-dir=' + get_option('prefix') + '/share/gtk-doc/html/'],
  content_files : ['architecture.xml', 'ges-sections.txt', version_entities],
  html_assets : 'layer_track_overview.png',
  install : true,
  install_dir : 'gstreamer-editing-services',
)
