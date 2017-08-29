// Authors
//   Copyright (C) 2017 Thibault Saunier <thibault.saunier@osg-samsung.com>
//
// This library is free software; you can redistribute it and/or
// modify it under the terms of the GNU Lesser General Public
// License as published by the Free Software Foundation; either
// version 2.1 of the License, or (at your option) any later version.
//
// This library is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public
// License along with this library; if not, write to the Free Software
// Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
// 02110-1301  USA


using System;
using Gst;
using System.Diagnostics;

namespace GESSharp
{
    class GESExample
    {
        public static void Main (string[] args)
        {
            // Initialize Gstreamer
            Gst.Application.Init();

            // Build the pipeline
            GES.Global.Init();
            var pipeline = new GES.Pipeline();
            var timeline = GES.Timeline.NewAudioVideo();
            var layer = timeline.AppendLayer();

            pipeline["timeline"] = timeline;

            var clip = new GES.TitleClip();
            clip.Duration = Constants.SECOND * 5;
            layer.AddClip(clip);
            clip.SetChildProperty("text", new GLib.Value("Clip 1"));

            var clip1 = new GES.TitleClip();
            clip1.Start = Constants.SECOND * 5;
            clip1.Duration = Constants.SECOND * 5;
            layer.AddClip(clip1);
            clip1.SetChildProperty("text", new GLib.Value("Clip 2"));

            timeline.Commit();

            pipeline.SetState(State.Playing);
            //// Wait until error or EOS
            var bus = pipeline.Bus;
            Message msg = null;
            while (msg == null) {
                var format = Format.Time;
                long position;
                msg = bus.TimedPopFiltered (Gst.Constants.SECOND, MessageType.Eos | MessageType.Error);

                pipeline.QueryPosition (format, out position);
                Console.WriteLine("position: " + Global.TimeFormat(position)
                        + " / " + Global.TimeFormat(timeline.Duration));
            }
            pipeline.SetState(State.Null);
        }
    }
}
