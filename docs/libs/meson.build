configure_file(input : 'gstreamer-libs.types',
  output : 'gstreamer-libs.types',
  configuration : configuration_data())

gnome.gtkdoc('gstreamer-libs',
  main_sgml : 'gstreamer-libs-docs.sgml',
  src_dir : '@0@/libs/gst'.format(meson.source_root()),
  scan_args : ['--deprecated-guards=GST_DISABLE_DEPRECATED',
            '--ignore-decorators=GST_EXPORT',
            '--ignore-headers=gettext.h glib-compat-private.h glib-compat.h gst-i18n-app.h gst-i18n-lib.h gst_private.h gstelementdetails.h gstmacros.h grammar.tab.h grammar.tab.pre.h math-compat.h types.h'
            ],
  install : true)
