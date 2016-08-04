configure_file(input : 'ges.types',
  output : 'ges.types',
  configuration : configuration_data())

doc_deps_names = ['glib-2.0',
                  'gstreamer-@0@'.format(apiversion),
                  'gstreamer-plugins-base-@0@'.format(apiversion)]

doc_deps = []
foreach doc_dep : doc_deps_names
    runcmd = run_command('pkg-config', '--variable=prefix', doc_dep)
    if runcmd.returncode() == 0
        tmp = '--extra-dir=' + runcmd.stdout().strip() + '/share/gtk-doc/html/'
        tmp.strip()
        doc_deps = doc_deps + [tmp]
    endif
endforeach

gnome.gtkdoc('gst-editing-services-@0@'.format(apiversion),
  main_sgml : 'ges-docs.sgml',
  src_dir : '@0@/ges'.format(meson.source_root()),
  scan_args : ['--deprecated-guards=GST_DISABLE_DEPRECATED',
            '--ignore-decorators=GST_EXPORT',
            '--ignore-headers=gesmarshal.h ges-internal.h ges-auto-transition.h ges-structured-interface.h ges-structure-parser.h ges-smart-video-mixer.h gstframepositioner.h'
            ],
  fixxref_args: doc_deps + ['--html-dir=' + get_option('prefix') + '/share/gtk-doc/html/'],
  install : true)
