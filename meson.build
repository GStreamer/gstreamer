project('gstreamer uninstalled', 'c', version : '1.9.1.1')

gst_version = meson.project_version()
gst_branch = 'master'

glib_req = '>= 2.40.0'

subprojects = [
    'gstreamer',
    'gst-plugins-base',
    'gst-plugins-good',
    'gst-plugins-ugly',
    'gst-plugins-bad',
    'gst-devtools',
    'gst-editing-services'
]

foreach subproj: subprojects
    subproject(subproj, version: gst_version)
endforeach

setenv = find_program('gst-uninstalled.py')
run_target('uninstalled', setenv, '--builddir=@0@'.format(meson.current_build_dir()),
           '--gst-version=@0@'.format(gst_branch))
