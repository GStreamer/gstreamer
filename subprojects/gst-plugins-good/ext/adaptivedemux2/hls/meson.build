hls_sources = [
  'hls/gsthlsdemux.c',
  'hls/gsthlsdemux-stream.c',
  'hls/gsthlsdemux-playlist-loader.c',
  'hls/gsthlsdemux-preloader.c',
  'hls/gsthlsdemux-util.c',
  'hls/gsthlselement.c',
  'hls/m3u8.c',
]

hls_cargs = []

hls_crypto = get_option('hls-crypto')
hls_crypto_dep = dependency('', required : false)

if ['auto', 'nettle'].contains(hls_crypto)
  hls_crypto_dep = dependency('nettle', version : '>= 3.0', required : false)
  if hls_crypto_dep.found()
    hls_cargs += ['-DHAVE_NETTLE']
  endif
endif

if not hls_crypto_dep.found() and ['auto', 'libgcrypt'].contains(hls_crypto)
  hls_crypto_dep = cc.find_library('gcrypt', required : false)
  if hls_crypto_dep.found()
    hls_cargs += ['-DHAVE_LIBGCRYPT']
  endif
endif

if not hls_crypto_dep.found() and ['auto', 'openssl'].contains(hls_crypto)
  hls_crypto_dep = dependency('openssl', required : false)
  if hls_crypto_dep.found()
    hls_cargs += ['-DHAVE_OPENSSL']
  endif
endif

if not hls_crypto_dep.found()
  if hls_crypto == 'auto'
    message('Could not find a supported crypto library for HLS support')
  else
    error('HLS crypto support library "@0@" not found'.format(hls_crypto))
  endif
endif
hls_dep = declare_dependency(include_directories : include_directories('.'))
