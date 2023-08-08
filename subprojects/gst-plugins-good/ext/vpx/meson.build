vpx_sources = [
  'gstvp8dec.c',
  'gstvp8enc.c',
  'gstvp8utils.c',
  'gstvp9dec.c',
  'gstvp9enc.c',
  'gstvpxdec.c',
  'gstvpxenc.c',
  'gstvpxelement.c',
  'plugin.c',
]

vpx_features = [
  [ 'vpx/vp8cx.h', 'vpx_codec_vp8_cx_algo', '-DHAVE_VP8_ENCODER', 'VP8 encoder' ],
  [ 'vpx/vp8dx.h', 'vpx_codec_vp8_dx_algo', '-DHAVE_VP8_DECODER', 'VP8 decoder' ],
  [ 'vpx/vp8cx.h', 'vpx_codec_vp9_cx_algo', '-DHAVE_VP9_ENCODER', 'VP9 encoder' ],
  [ 'vpx/vp8dx.h', 'vpx_codec_vp9_dx_algo', '-DHAVE_VP9_DECODER', 'VP9 decoder' ],
]

vpx_option = get_option('vpx')
vpx_dep = dependency('vpx', version : '>=1.7.0', required : vpx_option, allow_fallback: true)

if vpx_dep.found()
  vpx_args = []
  foreach f : vpx_features
    header = f.get(0)
    codec_iface = f.get(1)
    extra_define = f.get(2)
    link_code = '''#include <@0@>
                   int main (int a, char ** g) {
                     const vpx_codec_iface_t *c = &@1@;
                     return c != 0;
                   }'''.format(header,codec_iface)
    # Link-time checks are only compatible with externally supplied libraries 
    if vpx_dep.type_name() == 'internal'
      vpx_codec = vpx_dep.get_variable(f.get(3).split(' ')[0].to_lower())
      vpx_feature = vpx_dep.get_variable(f.get(3).to_lower().underscorify())
      if vpx_feature == 'true' and vpx_codec == 'true'
        vpx_args += extra_define
        message('libvpx provides @0@ interface (@1@)'.format(f.get(3),f.get(1)))
        have_vpx_feature = true
      else
        message('libvpx does not provide @0@ interface (@1@)'.format(f.get(3),f.get(1)))
        have_vpx_feature = false
      endif
    else
      if cc.links(link_code, dependencies : vpx_dep)
        vpx_args += extra_define
        message('libvpx provides @0@ interface (@1@)'.format(f.get(3),f.get(1)))
        have_vpx_feature = true
      else
        message('libvpx does not provide @0@ interface (@1@)'.format(f.get(3),f.get(1)))
        have_vpx_feature = false
      endif
    endif
    set_variable('have_' + f.get(3).to_lower().underscorify(), have_vpx_feature)
  endforeach

  if vpx_args.length() == 0
    msg = 'libvpx was built without any encoder or decoder features!'
    # Error out if explicitly enabled, but continue with a warning if the
    # plugin is in auto-detect mode to reduce build-time friction.
    if vpx_option.enabled()
      error(msg)
    endif
    warning(msg)
  endif

  if vpx_dep.version().version_compare('>= 1.8.0')
    message('Found vpx >= 1.8.0')
    vpx_args += '-DHAVE_VPX_1_8'
  endif

  gnome = import('gnome')

  gstvpx_enums = gnome.mkenums_simple('gstvpx-enumtypes',
    sources : ['gstvpxenums.h'],
    decorator : 'G_GNUC_INTERNAL',
    install_header: false)

  gstvpx = library('gstvpx',
    vpx_sources, gstvpx_enums,
    c_args : gst_plugins_good_args + vpx_args,
    include_directories : [configinc],
    dependencies : [gstbase_dep, gsttag_dep, gstvideo_dep, vpx_dep],
    install : true,
    install_dir : plugins_install_dir,
  )
  plugins += [gstvpx]

  install_data(sources: ['GstVP8Enc.prs'], install_dir: presetdir)
  env = environment()
  env.prepend('GST_PRESET_PATH', meson.current_source_dir())
  meson.add_devenv(env)
endif
