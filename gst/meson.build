foreach plugin : ['adder', 'app', 'audioconvert', 'audiomixer', 'audiorate', 'audioresample',
                  'audiotestsrc', 'encoding', 'gio', 'pbtypes', 'playback', 'rawparse',
                  'subparse', 'tcp', 'typefind', 'videoconvert', 'videorate', 'videoscale',
                  'videotestsrc', 'volume']
  if not get_option(plugin).disabled()
    subdir(plugin)
  endif
endforeach
