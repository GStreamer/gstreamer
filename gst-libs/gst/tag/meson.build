tag_sources = [
  'gstvorbistag.c',
  'gstid3tag.c',
  'gstxmptag.c',
  'gstexiftag.c',
  'lang.c',
  'licenses.c',
  'tags.c',
  'gsttagdemux.c',
  'gsttagmux.c',
  'gsttageditingprivate.c',
  'id3v2.c',
  'id3v2frames.c',
  'xmpwriter.c',
]

tag_mkenum_headers = [
  'gsttagdemux.h',
  'tag.h',
]

tag_headers = tag_mkenum_headers + [
  'gsttagmux.h',
  'xmpwriter.h',
  'tag-prelude.h',
]
install_headers(tag_headers, subdir : 'gstreamer-1.0/gst/tag/')

tag_enums = gnome.mkenums_simple('tag-enumtypes',
  sources : tag_mkenum_headers,
  header_prefix : '#include <gst/tag/tag-prelude.h>',
  decorator : 'GST_TAG_API',
  install_header: true,
  install_dir : join_paths(get_option('includedir'), 'gstreamer-1.0/gst/tag'))
gsttag_c = tag_enums[0]
gsttag_h = tag_enums[1]
tag_gen_sources = [gsttag_h]

install_data('license-translations.dict',
  install_dir : join_paths(get_option('datadir'), 'gst-plugins-base', api_version))

gst_tag_args = [
  '-DLICENSE_TRANSLATIONS_PATH="@0@"'.format(
    join_paths(
      get_option('prefix'),
      get_option('datadir'),
      'gst-plugins-base',
      api_version,
      'license-translations.dict'))
]

# could drop optional zlib dep and use g_zlib_decompressor_new()
zlib_dep = dependency('zlib', required: false, fallback: ['zlib', 'zlib_dep'])
core_conf.set('HAVE_ZLIB', zlib_dep.found())

tag_deps = [gst_base_dep, libm, zlib_dep]
gsttag = library('gsttag-@0@'.format(api_version),
  tag_sources, gsttag_h, gsttag_c,
  c_args : gst_plugins_base_args + gst_tag_args,
  include_directories: [configinc, libsinc],
  version : libversion,
  soversion : soversion,
  install : true,
  dependencies : tag_deps,
)

if build_gir
  gst_gir_extra_args = gir_init_section + [ '--c-include=gst/tag/tag.h' ]
  tag_gen_sources += [gnome.generate_gir(gsttag,
    sources : tag_sources + tag_headers + [gsttag_h] + [gsttag_c],
    namespace : 'GstTag',
    nsversion : api_version,
    identifier_prefix : 'Gst',
    symbol_prefix : 'gst',
    export_packages : 'gstreamer-tag-1.0',
    includes : ['Gst-1.0', 'GstBase-1.0'],
    install : true,
    extra_args : gst_gir_extra_args,
    dependencies : tag_deps
  )]
endif

tag_dep = declare_dependency(link_with: gsttag,
  include_directories : [libsinc],
  dependencies : tag_deps,
  sources: tag_gen_sources)

# FIXME: iso-codes stuff
# little program that reads iso_639.xml and outputs tables for us as fallback
# for when iso-codes are not available (and so we don't have to read the xml
# just to map codes)
#executable('mklangtables', 'mklangtables.c',
#  dependencies : gst_dep,
#  install : false,
#)
executable('mklicensestables', 'mklicensestables.c',
  c_args : gst_plugins_base_args,
  include_directories: [configinc],
  dependencies : [tag_dep, gst_base_dep],
  install : false,
)

gst_tag_dir = meson.current_source_dir()
