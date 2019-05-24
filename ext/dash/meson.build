dash_sources = [
  'gstdashdemux.c',
  'gstmpdrootnode.c',
  'gstmpdbaseurlnode.c',
  'gstmpdutctimingnode.c',
  'gstmpdmetricsnode.c',
  'gstmpdmetricsrangenode.c',
  'gstmpdsnode.c',
  'gstmpdsegmenttimelinenode.c',
  'gstmpdsegmenttemplatenode.c',
  'gstmpdsegmenturlnode.c',
  'gstmpdsegmentlistnode.c',
  'gstmpdperiodnode.c',
  'gstmpdrepresentationnode.c',
  'gstmpdsubrepresentationnode.c',
  'gstmpdcontentcomponentnode.c',
  'gstmpdadaptationsetnode.c',
  'gstmpdsubsetnode.c',
  'gstmpdprograminformationnode.c',
  'gstxmlhelper.c',
  'gstmpdhelper.c',
  'gstmpdparser.c',
  'gstmpdclient.c',
  'gstplugin.c',
]

xml2_dep = dependency('libxml-2.0',
  version : '>= 2.8',
  fallback : ['libxml2', 'xml2lib_dep'],
  required : get_option('dash')
)

if xml2_dep.found()
  gstdashdemux = library('gstdashdemux',
    dash_sources,
    c_args : gst_plugins_bad_args + ['-DGST_USE_UNSTABLE_API'],
    link_args : noseh_link_args,
    include_directories : [configinc, libsinc],
    dependencies : [gstadaptivedemux_dep, gsturidownloader_dep, gsttag_dep,
                    gstnet_dep, gstbase_dep, gstisoff_dep, gio_dep, xml2_dep],
    install : true,
    install_dir : plugins_install_dir,
  )
  pkgconfig.generate(gstdashdemux, install_dir : plugins_pkgconfig_install_dir)
  plugins += [gstdashdemux]
endif
