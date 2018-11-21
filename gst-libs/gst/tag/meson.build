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
  body_prefix : '#ifdef HAVE_CONFIG_H\n#include "config.h"\n#endif',
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

# iso-codes (optional, used for language names lookup and translation)
have_iso_codes = false
iso_codes_dep = dependency('iso-codes', required: get_option('iso-codes'))
if iso_codes_dep.found()
  if iso_codes_dep.get_pkgconfig_variable('domains').split(' ').contains('iso_639-2')
    iso_codes_prefix = iso_codes_dep.get_pkgconfig_variable('prefix')
    if iso_codes_prefix != ''
      core_conf.set_quoted('ISO_CODES_PREFIX', iso_codes_prefix)
      core_conf.set_quoted('ISO_CODES_VERSION', iso_codes_dep.version())
      have_iso_codes = true
    elif get_option('iso-codes').enabled()
      error('iso-codes was requested for libgsttag, but could not determine prefix')
    endif
  elif get_option('iso-codes').enabled()
    error('iso-codes was requested for libgsttag, but does not provide iso 639')
  endif
endif
core_conf.set('HAVE_ISO_CODES', have_iso_codes)

# could drop optional zlib dep and use g_zlib_decompressor_new()
zlib_dep = dependency('zlib', required : false)
if not zlib_dep.found()
  zlib_dep = cc.find_library('z', required : false)
  if not zlib_dep.found() or not cc.has_header('zlib.h')
    zlib_dep = subproject('zlib').get_variable('zlib_dep')
  endif
endif
core_conf.set('HAVE_ZLIB', true)

tag_deps = [gst_base_dep, libm, zlib_dep]
gsttag = library('gsttag-@0@'.format(api_version),
  tag_sources, gsttag_h, gsttag_c,
  c_args : gst_plugins_base_args + gst_tag_args + ['-DBUILDING_GST_TAG'],
  include_directories: [configinc, libsinc],
  version : libversion,
  soversion : soversion,
  darwin_versions : osxversion,
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

# Little program that reads iso_639.xml and outputs tables for us as fallback
# for when iso-codes are not available (and so we don't have to read the xml
# just to map codes)
if have_iso_codes
  executable('mklangtables', 'mklangtables.c',
    c_args: ['-DHAVE_CONFIG_H'],
    include_directories: [configinc, libsinc],
    dependencies: gst_dep,
    install: false)
endif

executable('mklicensestables', 'mklicensestables.c',
  c_args : gst_plugins_base_args,
  include_directories: [configinc],
  dependencies : [tag_dep, gst_base_dep],
  install : false)

gst_tag_dir = meson.current_source_dir()
