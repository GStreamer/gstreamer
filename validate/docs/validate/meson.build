types = configure_file(input : 'gst-validate.types',
  output : 'gst-validate.types',
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

if gtkdoc.found()
  gnome.gtkdoc('gst-validate',
    main_sgml : 'gst-validate-docs.sgml',
    src_dir : '@0@/../../gst/validate'.format(meson.current_source_dir()),
    scan_args : ['--deprecated-guards=GST_DISABLE_DEPRECATED',
                '--ignore-decorators=GST_EXPORT',
                '--ignore-headers=gettext.h gst-validate-internal.h gst-validate-monitor.h gst-validate-bin-monitor.h gst-validate-element-monitor.h gst-validate-pad-monitor.h gst-validate-override.h gst-validate-override-registry.h gst-validate-utils.h gst-validate-media-info.h  gst-validate-report.h  media-descriptor.h media-descriptor-parser.h media-descriptor-writer.h gst-validate-i18n-lib.h'
            ],
    scanobj_args : ['--type-init-func="gst_init(NULL,NULL)'],
    gobject_typesfile : types,
    dependencies : [validate_dep],
    content_files : ['gst-validate.xml', 'gst-validate-transcoding.xml', 'gst-validate-media-check.xml', 'gst-validate-launcher.xml', 'envvariables.xml', 'scenarios.xml'],
    fixxref_args: doc_deps + ['--html-dir=' + get_option('prefix') + '/share/gtk-doc/html/'],
    install : true)
endif
