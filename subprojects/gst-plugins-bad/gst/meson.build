foreach plugin : ['accurip', 'adpcmdec', 'adpcmenc', 'aiff', 'asfmux',
                  'audiobuffersplit', 'audiofxbad', 'audiomixmatrix',
                  'audiolatency', 'audiovisualizers', 'autoconvert', 'bayer',
                  'camerabin2', 'codecalpha', 'codectimestamper', 'coloreffects',
                  'debugutils', 'dvbsubenc',
                  'dvbsuboverlay', 'dvdspu', 'faceoverlay', 'festival',
                  'fieldanalysis', 'freeverb', 'frei0r', 'gaudieffects', 'gdp',
                  'geometrictransform', 'id3tag', 'inter', 'interlace',
                  'ivfparse', 'ivtc', 'jp2kdecimator', 'jpegformat', 'librfb',
                  'midi', 'mpegdemux', 'mpegpsmux', 'mpegtsdemux', 'mpegtsmux',
                  'mxf', 'netsim', 'onvif', 'pcapparse', 'pnm', 'proxy',
                  'rawparse', 'removesilence', 'rist', 'rtmp2', 'rtp', 'sdp',
                  'segmentclip', 'siren', 'smooth', 'speed', 'subenc', 'switchbin',
                  'timecode', 'transcode', 'videofilters',
                  'videoframe_audiolevel', 'videoparsers', 'videosignal',
                  'vmnc', 'y4m']
  if not get_option(plugin).disabled()
    subdir(plugin)
  endif
endforeach
