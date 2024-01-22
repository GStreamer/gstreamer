using Gst;
using Gst.RtspServer;
using System;

namespace GstreamerSharp
{
  class RtspServerExample
  {
    private const string RtspPort = "8554";
    private const string Mount = "/test";
    private const bool DisableRtcp = false;

    public static void Main(string[] args)
    {
      Application.Init(ref args);
      var loop = new GLib.MainLoop();

      var server = new RTSPServer();
      server.Service = RtspPort;

      var factory = new RTSPMediaFactory();
      factory.Launch = "videotestsrc ! x264enc ! rtph264pay name=pay0 pt=96";
      factory.Shared = true;
      factory.EnableRtcp = !DisableRtcp;

      var mounts = server.MountPoints;
      mounts.AddFactory(Mount, factory);
      server.Attach();

      Console.WriteLine($"Stream ready at rtsp://127.0.0.1:{RtspPort}{Mount}");
      loop.Run();
    }
  }
}
