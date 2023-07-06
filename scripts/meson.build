fs = import('fs')

# scripts checks
release_modules = [
  'gstreamer',
  'gst-plugins-base',
  'gst-plugins-good',
  'gst-plugins-ugly',
  'gst-plugins-bad',
  'gst-libav',
  'gst-rtsp-server',
  'gst-editing-services',
  'gst-devtools',
  'gst-python',
  'gstreamer-vaapi',
  'gst-docs',
  'gstreamer-sharp',
]

# Make sure the files are all identical to avoid divergence
gen_cl_hash = fs.hash(files('gen-changelog.py'), 'md5')

out_of_sync_list = []

foreach m : release_modules
  module_gen_cl_hash = fs.hash(f'../subprojects/@m@/scripts/gen-changelog.py', 'md5')

  if module_gen_cl_hash != gen_cl_hash
    out_of_sync_list += [f'subprojects/@m@/scripts/gen-changelog.py']
  endif
endforeach

if out_of_sync_list.length() > 0
  module_list = ' '.join(release_modules)
  out_of_sync_msg = '\n          '.join(out_of_sync_list)
  error('''

    ==============================================================================================================

        The following subproject scripts are out of sync with scripts/gen-changelog.py:

          @0@

        Run

          for m in @1@; do cp scripts/gen-changelog.py subprojects/$m/scripts/gen-changelog.py; done 

        from the top-level git source directory to sync them up.

    ==============================================================================================================
    '''.format(out_of_sync_msg, module_list))
endif
