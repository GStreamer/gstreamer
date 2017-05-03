hls_sources = [
  'gsthlsdemux.c',
  'gsthlsdemux-util.c',
  'gsthlsplugin.c',
  'gsthlssink.c',
  'gstm3u8playlist.c',
  'm3u8.c',
]

hls_cargs = ['-DGST_USE_UNSTABLE_API']

# FIXME: Add an option for selecting the library, and fail if it's not found
hls_crypto_dep = dependency('nettle', required : false)
if hls_crypto_dep.found()
  hls_cargs += ['-DHAVE_NETTLE']
else
  hls_crypto_dep = cc.find_library('gcrypt', required : false)
  if hls_crypto_dep.found()
    hls_cargs += ['-DHAVE_LIBGCRYPT']
  else
    hls_crypto_dep = openssl_dep
    if hls_crypto_dep.found()
      hls_cargs += ['-DHAVE_OPENSSL']
    endif
  endif
endif

if hls_crypto_dep.found()
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
endif
