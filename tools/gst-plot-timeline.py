#!/usr/bin/env python
#
# based on plot-timeline.py by Federico Mena-Quintero <federico at ximian dotcom>
# example:
#   GST_DEBUG_NO_COLOR=1 GST_DEBUG="*:3" gst-launch-0.10 2>debug.log audiotestsrc num-buffers=10 ! audioconvert ! alsasink
#   gst-plot-timeline.py debug.log --output=debug.png

import math
import optparse
import os
import re
import sys

import cairo

FONT_NAME = "Bitstream Vera Sans"
FONT_SIZE = 8
# how many pixels for a second on the timeline
PIXELS_PER_SECOND = 300
# how many pixels for one line of log
PIXELS_PER_LINE = 10
PLOT_WIDTH = 1400
TIME_SCALE_WIDTH = 20
SYSCALL_MARKER_WIDTH = 20
LOG_TEXT_XPOS = 400
LOG_MARKER_WIDTH = 20
BACKGROUND_COLOR = (0, 0, 0)

# assumes GST_DEBUG_LOG_COLOR=1
#                             timestamp          pid  thread        level   category,file,line,msg
mark_regex = re.compile (r'^(\d:\d\d:\d\d\.\d+) +\d+ +0?x?[0-9a-f]+ [A-Z]+ +([-a-zA-Z0-9_]+ )(.*)')
mark_timestamp_group = 1
mark_program_group = 2
mark_log_group = 3

success_result = "0"

skip_lines = 0
max_lines = 500
filter_regex = re.compile ('')
skip_regex = re.compile('')

class BaseMark:
    colors = 0, 0, 0
    def __init__(self, timestamp, log):
        self.timestamp = timestamp
        self.log = log
        self.timestamp_ypos = 0
        self.log_ypos = 0

class AccessMark(BaseMark):
    pass

class LastMark(BaseMark):
    colors = 1.0, 0, 0

class FirstMark(BaseMark):
    colors = 1.0, 0, 0

class ExecMark(BaseMark):
#    colors = 0.75, 0.33, 0.33
    colors = (1.0, 0.0, 0.0)
    def __init__(self, timestamp, log):
        BaseMark.__init__(self, timestamp,
                          'execve: ' + os.path.basename(log))

class Metrics:
    def __init__(self):
        self.width = 0
        self.height = 0

# don't use black or red
palette = [
    (0.12, 0.29, 0.49),
    (0.36, 0.51, 0.71),
    (0.75, 0.31, 0.30),
    (0.62, 0.73, 0.38),
    (0.50, 0.40, 0.63),
    (0.29, 0.67, 0.78),
    (0.96, 0.62, 0.34)
    ]

class SyscallParser:
    def __init__ (self):
        self.syscalls = []

    def add_line (self, str):
        m = mark_regex.search (str)
        if m:
            timestr = m.group (mark_timestamp_group)
            timestamp = float (timestr[5:]) + (float (timestr[2:3]) * 60.0) + (float (timestr[0]) * 60.0*60.0)
            program = m.group (mark_program_group)
            text = program + m.group (mark_log_group)
            if text == 'last':
                self.syscalls.append (LastMark (timestamp, text))
            elif text == 'first':
                self.syscalls.append (FirstMark (timestamp, text))
            else:
                s = AccessMark (timestamp, text)
                program_hash = program.__hash__ ()
                s.colors = palette[program_hash % len (palette)]
                self.syscalls.append (s)
        else:
            print 'No log in %s' % str
            return

def parse_strace(filename):
    parser = SyscallParser ()

    global skip_lines
    global max_lines
    global skip_regex

    skip_found = False

    for line in file(filename, "r").readlines():
        if line == "":
            break

        if not skip_found:
            if skip_regex.search(line):
                skip_found = True
            else:
                continue

        if skip_lines > 0:
            skip_lines -= 1
            continue

        if len(parser.syscalls) >= max_lines:
            break

        if filter_regex.search(line):
            parser.add_line (line)

    return parser.syscalls

def normalize_timestamps(syscalls):

    first_timestamp = syscalls[0].timestamp

    for syscall in syscalls:
        syscall.timestamp -= first_timestamp

def compute_syscall_metrics(syscalls):
    global PIXELS_PER_SECOND
    global PIXELS_PER_LINE
    
    num_syscalls = len(syscalls)

    metrics = Metrics()
    metrics.width = PLOT_WIDTH

    last_timestamp = syscalls[num_syscalls - 1].timestamp

    time_height = int(math.ceil(last_timestamp * PIXELS_PER_SECOND))
    line_height = num_syscalls * PIXELS_PER_LINE

    if time_height > line_height:
        metrics.height = time_height
        print "Adjusting PIXELS_PER_LINE = %d" % PIXELS_PER_LINE
        PIXELS_PER_LINE = metrics.height / num_syscalls
        print "          PIXELS_PER_LINE = %d" % PIXELS_PER_LINE
    else:
        metrics.height = line_height
        print "Adjusting PIXELS_PER_SECOND %d" % PIXELS_PER_SECOND
        PIXELS_PER_SECOND = int(math.ceil(metrics.height / last_timestamp))
        print "          PIXELS_PER_SECOND %d" % PIXELS_PER_SECOND

    text_ypos = 0

    for syscall in syscalls:
        syscall.timestamp_ypos = syscall.timestamp * PIXELS_PER_SECOND
        syscall.log_ypos = text_ypos + FONT_SIZE

        text_ypos += PIXELS_PER_LINE

    return metrics

def plot_time_scale(surface, ctx, metrics):
    num_seconds = (metrics.height + PIXELS_PER_SECOND - 1) / PIXELS_PER_SECOND

    ctx.set_source_rgb(0.5, 0.5, 0.5)
    ctx.set_line_width(1.0)

    for i in range(num_seconds):
        ypos = i * PIXELS_PER_SECOND

        ctx.move_to(0, ypos + 0.5)
        ctx.line_to(TIME_SCALE_WIDTH, ypos + 0.5)
        ctx.stroke()

        ctx.move_to(0, ypos + 2 + FONT_SIZE)
        ctx.show_text("%d s" % i)

def plot_syscall(surface, ctx, syscall):
    ctx.set_source_rgb(*syscall.colors)

    # Line

    ctx.move_to(TIME_SCALE_WIDTH, syscall.timestamp_ypos)
    ctx.line_to(TIME_SCALE_WIDTH + SYSCALL_MARKER_WIDTH, syscall.timestamp_ypos)
    ctx.line_to(LOG_TEXT_XPOS - LOG_MARKER_WIDTH, syscall.log_ypos - FONT_SIZE / 2 + 0.5)
    ctx.line_to(LOG_TEXT_XPOS, syscall.log_ypos - FONT_SIZE / 2 + 0.5)
    ctx.stroke()

    # Log text

    ctx.move_to(LOG_TEXT_XPOS, syscall.log_ypos)
    ctx.show_text("%8.5f: %s" % (syscall.timestamp, syscall.log))

def plot_syscalls_to_surface(syscalls, metrics):
    num_syscalls = len(syscalls)

    print 'picture size: %d x %d' % (metrics.width, metrics.height);

    surface = cairo.ImageSurface(cairo.FORMAT_RGB24,
                                 metrics.width, metrics.height)

    ctx = cairo.Context(surface)
    ctx.select_font_face(FONT_NAME)
    ctx.set_font_size(FONT_SIZE)

    # Background

    ctx.set_source_rgb (*BACKGROUND_COLOR)
    ctx.rectangle(0, 0, metrics.width, metrics.height)
    ctx.fill()

    # Time scale

    plot_time_scale(surface, ctx, metrics)

    # Contents

    ctx.set_line_width(1.0)

    for syscall in syscalls:
        plot_syscall(surface, ctx, syscall)

    return surface

def main(args):

    global skip_lines
    global max_lines
    global filter_regex
    global skip_regex

    option_parser = optparse.OptionParser(
        usage="usage: %prog -o output.png <debug.log>")
    option_parser.add_option("-o",
                             "--output", dest="output",
                             metavar="FILE",
                             help="Name of output file (output is a PNG file)")
    option_parser.add_option("-s",
                             "--skip", dest="skip",
                             metavar="LINES",
                             help="Skip a number of loglines at the beginning of the file or wait till a regular expression happens")
    option_parser.add_option("-m",
                             "--max-lines", dest="max",
                             help="max lines that need to be plotted")
    option_parser.add_option("-f",
                             "--filter", dest="filter",
                             help="filter the log lines on a regular expression")

    options, args = option_parser.parse_args()

    if not options.output:
        print 'Please specify an output filename with "-o file.png" or "--output=file.png".'
        return 1

    if len(args) != 1:
        print 'Please specify only one input filename, which is an debug log taken with "GST_DEBUG_NO_COLOR=1 GST_DEBUG=XXX <application>"'
        return 1

    in_filename = args[0]
    out_filename = options.output

    if options.skip:
        try:
            skip_lines = int(options.skip)
        except:
            skip_regex = re.compile(options.skip)
            skip_lines = 0

    if options.max:
        max_lines = int(options.max)

    if options.filter:
        filter_regex = re.compile(options.filter)

    syscalls = []
    for syscall in parse_strace(in_filename):
        syscalls.append(syscall)
        if isinstance(syscall, FirstMark):
            syscalls = []
        elif isinstance(syscall, LastMark):
            break

    if not syscalls:
        print 'No logs in %s' % in_filename
        return 1

    normalize_timestamps(syscalls)
    metrics = compute_syscall_metrics(syscalls)

    surface = plot_syscalls_to_surface(syscalls, metrics)
    surface.write_to_png(out_filename)

    return 0

if __name__ == "__main__":
    sys.exit(main(sys.argv))
