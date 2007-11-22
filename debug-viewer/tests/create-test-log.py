#!/usr/bin/env python

def main ():

    import sys
    import os.path
    sys.path.append (os.path.dirname (os.path.dirname (sys.argv[0])))

    from GstDebugViewer import Data

    count = 100000

    ts = 0
    pid = 12345
    thread = int ("89abcdef", 16)
    level = Data.debug_level_log
    category = "GST_DUMMY"
    filename = "gstdummyfilename.c"
    file_line = 1
    function = "gst_dummy_function"
    object_ = "dummyobj0"
    message = "dummy message with no content"

    levels = (Data.debug_level_log,
              Data.debug_level_debug,
              Data.debug_level_info,)

    shift = 0
    for i in xrange (count):

        ts = i * 10000
        shift += i % (count // 100)
        level = levels[(i + shift) % 3]
        line = Data.LogLine ([ts, pid, thread, level, category, filename, file_line, function, object_, message])
        print line.line_string ()

if __name__ == "__main__":
    main ()
