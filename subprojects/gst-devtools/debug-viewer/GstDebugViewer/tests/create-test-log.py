#!/usr/bin/env python


def line_string(ts, pid, thread, level, category, filename, line, function,
                object_, message):

    # Replicates gstreamer/gst/gstinfo.c:gst_debug_log_default.

    # FIXME: Regarding object_, this doesn't fully replicate the formatting!
    return "%s %5d 0x%x %s %20s %s:%d:%s:<%s> %s" % (Data.time_args(ts), pid, thread,
                                                     level.name.ljust(
                                                         5), category,
                                                     filename, line, function,
                                                     object_, message,)


def main():

    import sys
    import os.path
    sys.path.append(os.path.dirname(os.path.dirname(sys.argv[0])))

    global Data
    from GstDebugViewer import Data

    count = 100000

    ts = 0
    pid = 12345
    thread = int("89abcdef", 16)
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
    for i in range(count):

        ts = i * 10000
        shift += i % (count // 100)
        level = levels[(i + shift) % 3]
        print(line_string(ts, pid, thread, level, category, filename, file_line,
                          function, object_, message))


if __name__ == "__main__":
    main()
