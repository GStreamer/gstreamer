#!/usr/bin/env python3
'''
Plot buffer pts and events in relation to the wall clock. The plots can be used
to spot anomalies, such as processing gaps.

How to run:
1) generate a log
GST_DEBUG="GST_TRACER:7" GST_TRACERS=stats GST_DEBUG_FILE=trace.log <application>

2) generate the images
python3 gsttr-tsplot.py trace.log <outdir>
eog <outdir>/*.png
'''

# TODO:
# - improve event plot
#   - ideally each event is a vertical line
#     http://stackoverflow.com/questions/35105672/vertical-lines-from-data-in-file-in-time-series-plot-using-gnuplot
#   - this won't work well if the event is e.g. 'qos'
#   - we could sort them by event type and separate them by double new-lines,
#     we'd then use 'index <x>' to plot them in different colors with
# - buffer-pts should be ahead of clock time of the pipeline
#   - we don't have the clock ts in the log though

import logging
import os
from subprocess import Popen, PIPE, DEVNULL
from string import Template
from tracer.analysis_runner import AnalysisRunner
from tracer.analyzer import Analyzer
from tracer.parser import Parser
from tracer.structure import Structure


logging.basicConfig(level=logging.WARNING)
logger = logging.getLogger('gsttr-tsplot')

_HANDLED_CLASSES = ('buffer', 'event', 'new-pad', 'new-element')

_GST_BUFFER_FLAG_DISCONT = (1<<6)

_PLOT_SCRIPT_HEAD = Template(
    '''set term png truecolor size $width,$height
    ''')
_PLOT_SCRIPT_BODY = Template(
    '''set output '$png_file_name'
       set multiplot layout 3,1 title '$pad_name'
       set style line 100 lc rgb '#dddddd' lt 0 lw 1
       set grid back ls 100

       set xlabel "Clock Time (sec.msec)"
       set ylabel "Buffer Time (sec.msec)"
       set yrange [*:*]
       set ytics
       plot '$buf_file_name' using 1:2 with linespoints notitle

        set ylabel "Duration (sec.msec)"
       plot '$buf_file_name' using 1:3 with linespoints title "cycle", \
            '' using 1:4 with linespoints title "duration"

       set ylabel "Events"
       set yrange [$ypos_max:10]
       set ytics format ""
       plot '$ev_file_name' using 1:4:3:(0) with vectors heads size screen 0.008,90 notitle, \
            '' using 2:4 with points notitle, \
            '' using 2:4:5 with labels font ',7' offset char 0,-0.5 notitle
       unset multiplot
       ''')

class TsPlot(Analyzer):
    '''Generate a timestamp plots from a tracer log.

    These show the buffer pts on the y-axis and the wall-clock time the buffer
    was produced on the x-axis. This helps to spot timing issues, such as
    stalled elements.
    '''

    def __init__(self, outdir, show_ghost_pads, size):
        super(TsPlot, self).__init__()
        self.outdir = outdir
        self.show_ghost_pads = show_ghost_pads
        self.params = {
            'width': size[0],
            'height': size[1],
        }
        self.buf_files = {}
        self.buf_cts = {}
        self.ev_files = {}
        self.element_names = {}
        self.pad_names = {}
        self.ev_labels = {}
        self.ev_data = {}
        self.ev_ypos = {}

    def _get_data_file(self, files, key, name_template):
        data_file = files.get(key)
        if not data_file:
            pad_name = self.pad_names.get(key)
            if pad_name:
                file_name = name_template % (self.outdir, key, pad_name)
                data_file = open(file_name, 'w')
                files[key] = data_file
        return data_file

    def _log_event_data(self, pad_file, ix):
        data = self.ev_data.get(ix)
        if not data:
            return
        l = self.ev_labels[ix]
        ct = data['ct']
        x1 = data['first-ts']
        # TODO: scale 'y' according to max-y of buf or do a multiplot
        y = (1 + data['ypos']) * -10
        if ct == 1:
            pad_file.write('%f %f %f %f "%s"\n' % (x1, x1, 0.0, y, l))
        else:
            x2 = data['last-ts']
            xd = (x2 - x1)
            xm = x1 + xd / 2
            pad_file.write('%f %f %f %f "%s (%d)"\n' % (x1, xm, xd, y, l, ct))

    def _log_event(self, s):
        # build a [ts, event-name] data file
        ix = int(s.values['pad-ix'])
        pad_file = self._get_data_file(self.ev_files, ix, '%s/ev_%d_%s.dat')
        if not pad_file:
            return
        # convert timestamps to seconds
        x = int(s.values['ts']) / 1e9
        # some events fire often, labeling each would be unreadable
        # so we aggregate a series of events of the same type
        l = s.values['name']
        if l == self.ev_labels.get(ix):
            # count lines and track last ts
            data = self.ev_data[ix]
            data['ct'] += 1
            data['last-ts'] = x
        else:
            self._log_event_data(pad_file, ix)
            # start new data, assign a -y coord by event type
            if not ix in self.ev_ypos:
                ypos = {}
                self.ev_ypos[ix] = ypos
            else:
                ypos = self.ev_ypos[ix]
            if l in ypos:
                y = ypos[l]
            else:
                y = len(ypos)
                ypos[l] = y
            self.ev_labels[ix] = l
            self.ev_data[ix] = {
                'ct': 1,
                'first-ts': x,
                'ypos': y,
            }

    def _log_buffer(self, s):
        if not int(s.values['have-buffer-pts']):
            return
        # build a [ts, buffer-pts] data file
        ix = int(s.values['pad-ix'])
        pad_file = self._get_data_file(self.buf_files, ix, '%s/buf_%d_%s.dat')
        if not pad_file:
            return
        flags = int(s.values['buffer-flags'])
        if flags & _GST_BUFFER_FLAG_DISCONT:
            pad_file.write('\n')
        # convert timestamps to e.g. seconds
        cts = int(s.values['ts']) / 1e9
        pts = int(s.values['buffer-pts']) / 1e9
        dur = int(s.values['buffer-duration']) / 1e9
        if not ix in self.buf_cts:
            dcts = 0
        else:
            dcts = cts - self.buf_cts[ix]
        self.buf_cts[ix] = cts
        pad_file.write('%f %f %f %f\n' % (cts, pts, dcts, dur))

    def handle_tracer_entry(self, event):
        if event[Parser.F_FUNCTION]:
            return

        msg = event[Parser.F_MESSAGE]
        p = msg.find(',')
        if p == -1:
            return
        entry_name = msg[:p]
        if entry_name not in _HANDLED_CLASSES:
            return

        try:
            s = Structure(msg)
        except ValueError:
            logger.warning("failed to parse: '%s'", msg)
            return

        if entry_name == 'new-element':
            ix = int(s.values['ix'])
            self.element_names[ix] = s.values['name']
        elif entry_name == 'new-pad':
            pad_type = s.values['type']
            if pad_type not in ['GstGhostPad', 'GstProxyPad']:
                parent_ix = int(s.values['parent-ix'])
                parent_name = self.element_names.get(parent_ix, '')
                ix = int(s.values['ix'])
                self.pad_names[ix] = "%s.%s" % (parent_name, s.values['name'])
        elif entry_name == 'event':
            self._log_event(s)
        else:  # 'buffer'
            self._log_buffer(s)

    def report(self):
        for ix, pad_file in self.ev_files.items():
            self._log_event_data(pad_file, ix)
            pad_file.close()

        script = _PLOT_SCRIPT_HEAD.substitute(self.params)
        for ix, pad_file in self.buf_files.items():
            pad_file.close()
            name = self.pad_names[ix]
            buf_file_name = '%s/buf_%d_%s.dat' % (self.outdir, ix, name)
            ev_file_name = '%s/ev_%d_%s.dat' % (self.outdir, ix, name)
            png_file_name = '%s/%d_%s.png' % (self.outdir, ix, name)
            ypos_max = (2 + len(self.ev_ypos[ix])) * -10
            script += _PLOT_SCRIPT_BODY.substitute(self.params, pad_name=name,
                buf_file_name=buf_file_name, ev_file_name=ev_file_name,
                png_file_name=png_file_name, ypos_max=ypos_max)
        # plot PNGs
        p = Popen(['gnuplot'], stdout=DEVNULL, stdin=PIPE)
        p.communicate(input=script.encode('utf-8'))

        # cleanup
        for ix, pad_file in self.buf_files.items():
            name = self.pad_names[ix]
            buf_file_name = '%s/buf_%d_%s.dat' % (self.outdir, ix, name)
            os.unlink(buf_file_name)
        for ix, pad_file in self.ev_files.items():
            name = self.pad_names[ix]
            ev_file_name = '%s/ev_%d_%s.dat' % (self.outdir, ix, name)
            os.unlink(ev_file_name)


if __name__ == '__main__':
    import argparse
    parser = argparse.ArgumentParser()
    parser.add_argument('file', nargs='?', default='debug.log')
    parser.add_argument('outdir', nargs='?', default='tsplot')
    parser.add_argument('-g', '--ghost-pads', action='store_true',
                        help='also plot data for ghost-pads')
    parser.add_argument('-s', '--size', action='store', default='1600x600',
                        help='graph size as WxH')
    args = parser.parse_args()

    os.makedirs(args.outdir, exist_ok=True)
    size = [int(s) for s in args.size.split('x')]

    with Parser(args.file) as log:
        tsplot = TsPlot(args.outdir, args.ghost_pads, size)
        runner = AnalysisRunner(log)
        runner.add_analyzer(tsplot)
        runner.run()

        tsplot.report()
