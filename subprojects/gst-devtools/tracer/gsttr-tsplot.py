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

_GST_BUFFER_FLAG_DISCONT = (1 << 6)

_PLOT_SCRIPT_HEAD = Template(
    '''
    set term pngcairo truecolor size $width,$height font "Helvetica,14"
    set style line 1 lc rgb '#8b1a0e' pt 1 ps 1 lt 1 lw 1 # --- red
    set style line 2 lc rgb '#5e9c36' pt 6 ps 1 lt 1 lw 1 # --- green
    set style line 100 lc rgb '#999999' lt 0 lw 1
    set grid back ls 100
    set key font ",10"
    set label font ",10"
    set tics font ",10"
    set xlabel font ",10"
    set ylabel font ",10"
    ''')
_PLOT_SCRIPT_BODY = Template(
    '''
    set output '$png_file_name'
    set multiplot layout 3,1 title "$title\\n$subtitle"

    set xlabel ""
    set xrange [*:*] writeback
    set xtics format ""
    set ylabel "Buffer Time (sec.msec)" offset 1,0
    set yrange [*:*]
    set ytics
    plot '$buf_file_name' using 1:2 with linespoints ls 1 notitle

    set xrange restore
    set ylabel "Duration (sec.msec)" offset 1,0
    plot '$buf_file_name' using 1:3 with linespoints ls 1title "cycle", \
         '' using 1:4 with linespoints ls 2 title "duration"

    set xrange restore
    set xtics format "%g" scale .5 offset 0,.5
    set xlabel "Clock Time (sec.msec)" offset 0,1
    set ylabel "Events" offset 1,0
    set yrange [$ypos_max:10]
    set ytics format ""
    plot '$ev_file_name' using 1:4:3:(0) with vectors heads size screen 0.008,90 ls 1 notitle, \
         '' using 2:4 with points ls 1 notitle, \
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
        self.element_info = {}
        self.pad_names = {}
        self.pad_info = {}
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
        line = self.ev_labels[ix]
        ct = data['ct']
        x1 = data['first-ts']
        # TODO: scale 'y' according to max-y of buf or do a multiplot
        y = (1 + data['ypos']) * -10
        if ct == 1:
            pad_file.write('%f %f %f %f "%s"\n' % (x1, x1, 0.0, y, line))
        else:
            x2 = data['last-ts']
            xd = (x2 - x1)
            xm = x1 + xd / 2
            pad_file.write('%f %f %f %f "%s (%d)"\n' % (x1, xm, xd, y, line, ct))

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
        line = s.values['name']
        if line == self.ev_labels.get(ix):
            # count lines and track last ts
            data = self.ev_data[ix]
            data['ct'] += 1
            data['last-ts'] = x
        else:
            self._log_event_data(pad_file, ix)
            # start new data, assign a -y coord by event type
            if ix not in self.ev_ypos:
                ypos = {}
                self.ev_ypos[ix] = ypos
            else:
                ypos = self.ev_ypos[ix]
            if line in ypos:
                y = ypos[line]
            else:
                y = len(ypos)
                ypos[line] = y
            self.ev_labels[ix] = line
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
        if ix not in self.buf_cts:
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
            self.element_info[ix] = 'Element Type: %s' % s.values['type']
        elif entry_name == 'new-pad':
            pad_type = s.values['type']
            if self.show_ghost_pads or pad_type not in ['GstGhostPad', 'GstProxyPad']:
                parent_ix = int(s.values['parent-ix'])
                parent_name = self.element_names.get(parent_ix, '')
                ix = int(s.values['ix'])
                self.pad_names[ix] = '%s.%s' % (parent_name, s.values['name'])
                self.pad_info[ix] = '(%s, Pad Type: %s)' % (
                    self.element_info.get(parent_ix, ''), pad_type)
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
            sub_title = self.pad_info[ix]
            ypos_max = (2 + len(self.ev_ypos[ix])) * -10
            script += _PLOT_SCRIPT_BODY.substitute(self.params, title=name,
                subtitle=sub_title, buf_file_name=buf_file_name,
                ev_file_name=ev_file_name, png_file_name=png_file_name,
                ypos_max=ypos_max)
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
