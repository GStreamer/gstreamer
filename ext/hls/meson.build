hls_sources = [
  'gsthlsdemux.c',
  'gsthlsdemux-util.c',
  'gsthlsplugin.c',
  'gsthlssink.c',
  'gsthlssink2.c',
  'gstm3u8playlist.c',
  'm3u8.c',
]

hls_cargs = ['-DGST_USE_UNSTABLE_API']

hls_crypto = get_option('hls-crypto')
hls_option = get_option('hls')

have_hls_crypto = false
if not hls_option.disabled()
  if ['auto', 'nettle'].contains(hls_crypto)
    hls_crypto_dep = dependency('nettle', required : false)
    if hls_crypto_dep.found()
      have_hls_crypto = true
      hls_cargs += ['-DHAVE_NETTLE']
    endif
  endif

  if not hls_crypto_dep.found() and ['auto', 'libgcrypt'].contains(hls_crypto)
    hls_crypto_dep = cc.find_library('gcrypt', required : false)
    if hls_crypto_dep.found()
      have_hls_crypto = true
      hls_cargs += ['-DHAVE_LIBGCRYPT']
    endif
  endif

  if not hls_crypto_dep.found() and ['auto', 'openssl'].contains(hls_crypto)
    hls_crypto_dep = dependency('openssl', required : false)
    if hls_crypto_dep.found()
      have_hls_crypto = true
      hls_cargs += ['-DHAVE_OPENSSL']
    endif
  endif

  if not have_hls_crypto and hls_option.enabled()
    if hls_crypto == 'auto'
      error('HLS plugin enabled, but found none of nettle, libgcrypt, or openssl')
    else
      error('HLS plugin enabled, but crypto library "@0@" not found'.format(hls_crypto))
    endif
  endif
endif

if have_hls_crypto
  gsthls = library('gsthls',
    hls_sources,
    c_args : gst_plugins_bad_args + hls_cargs,
    link_args : noseh_link_args,
    include_directories : [configinc],
    dependencies : [gstpbutils_dep, gsttag_dep, gstvideo_dep,
		    gstadaptivedemux_dep, gsturidownloader_dep,
		    hls_crypto_dep, libm],
    install : true,
    install_dir : plugins_install_dir,
  )
  pkgconfig.generate(gsthls, install_dir : plugins_pkgconfig_install_dir)
endif
