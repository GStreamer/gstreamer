using GLib;
using Gst;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Text;

namespace GstreamerSharp
{
    class Program
    {
        static void Main(string[] args)
        {
            Gst.Application.Init(ref args);

            Element src = ElementFactory.Make("audiotestsrc");
            Element convert = ElementFactory.Make("audioconvert");
            Element volume = new ExampleVolume();
            Element sink = ElementFactory.Make("autoaudiosink");

            Pipeline pipeline = new Pipeline();
            pipeline.Add(src, convert, volume, sink);
            Element.Link(src, convert, volume, sink);

            pipeline.SetState(State.Playing);

            MainLoop loop = new MainLoop();
            loop.Run();

            pipeline.SetState(State.Null);

            Console.ReadLine();
        }
    }
    class ExampleVolume : Element
    {
        public ExampleVolume()
        {
            Volume = 0.5;

            _sink = new Pad(__sinkTemplate, "sink");
            _sink.ChainFunctionFull = Chain;
            _sink.Flags |= PadFlags.ProxyCaps;
            AddPad(_sink);

            _src = new Pad(__srcTemplate, "src");
            _src.Flags |= PadFlags.ProxyCaps;
            AddPad(_src);
        }
        
        public double Volume { get; set; }

        FlowReturn Chain(Pad pad, Gst.Object parent, Gst.Buffer buffer)
        {
            if (Volume == 1.0)
            {
                return _src.Push(buffer);
            }

            buffer.MakeWritable();

            MapInfo mapInfo;
            buffer.Map(out mapInfo, MapFlags.Read | MapFlags.Write);

            ScaleInt16(mapInfo.DataPtr, mapInfo.Size / 2, Volume);

            buffer.Unmap(mapInfo);
            
            return _src.Push(buffer);
        }

        private unsafe void ScaleInt16(IntPtr data, ulong size, double volume)
        {
            Int16* sample = (Int16*)data;
            for (ulong i = 0; i < size; i++)
            {
                *sample = ClampInt16(*sample * volume);
                sample++;
            }
        }

        private Int16 ClampInt16(double d)
        {
            int i = (int)Math.Round(d);

            if (i > Int16.MaxValue)
            {
                return Int16.MaxValue;
            }
            else if (i < Int16.MinValue)
            {
                return Int16.MinValue;
            }
            else
            {
                return (Int16)i;
            }
        }

        Pad _src;
        Pad _sink;

        static ExampleVolume()
        {
            Caps audioCaps = Caps.FromString("audio/x-raw, format=(string) S16LE, rate=(int) [1, MAX], channels=(int) 2, layout=(string) interleaved");
            __srcTemplate = new PadTemplate("src", PadDirection.Src, PadPresence.Always, audioCaps);
            __sinkTemplate = new PadTemplate("sink", PadDirection.Sink, PadPresence.Always, audioCaps);
        }

        static PadTemplate __srcTemplate;
        static PadTemplate __sinkTemplate;
    }
}
