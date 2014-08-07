// Authors
//   Copyright (C) 2014 Stephan Sundermann <stephansundermann@gmail.com>

using System;
using Gst;
using Gst.PbUtils;
using System.Runtime.InteropServices;

namespace GstreamerSharp
{
	class Playback
	{
		static Discoverer Discoverer;
		static GLib.MainLoop MainLoop;

		// Print a tag in a human-readable format (name: value)
		static void PrintTagForeach (TagList tags, string tag, int depth) {
			var val = GLib.Value.Empty;

			TagList.CopyValue (ref val, tags, tag);

			string str;
			if (val.Val is string)
				str = (string)val.Val;
			else
				str = Value.Serialize (val);

			Console.WriteLine ("{0}{1}: {2}", new string(' ', 2 * depth), Tag.GetNick (tag), str);	
		}

		// Print information regarding a stream
		static void PrintStreamInfo (DiscovererStreamInfo info, int depth) {

			var caps = info.Caps;

			string desc = null;
			if (caps != null) {
				if (caps.IsFixed)
					desc = Gst.PbUtils.Global.PbUtilsGetCodecDescription (caps);
				else
					desc = caps.ToString ();
			}

			Console.WriteLine ("{0}{1}: {2}", new string (' ', 2 * depth), info.StreamTypeNick, (desc != null ? desc : ""));

			var tags = info.Tags;
			if (tags != null) {
				Console.WriteLine ("{0}Tags:", new string (' ', 2 * (depth + 1)));
				tags.Foreach ((TagForeachFunc)delegate (TagList list, string tag) {
					PrintTagForeach (list, tag, depth + 2);
				});
			}
		}

		// Print information regarding a stream and its substreams, if any
		static void PrintTopology (DiscovererStreamInfo info, int depth) {

			if (info == null)
				return;

			PrintStreamInfo (info, depth);

			var next = info.Next;
			if (next != null) {
				PrintTopology (next, depth + 1);
			} else if (info is DiscovererContainerInfo) {
				var streams = ((DiscovererContainerInfo)info).Streams;
				foreach (var stream in streams) {
					PrintTopology (stream, depth + 1);
				}
			}
		}

		//This function is called every time the discoverer has information regarding one of the URIs we provided.
		static void HandleDiscovered (object disc, DiscoveredArgs args) {
			var info = args.Info;
			var uri = info.Uri;
			var result = info.Result;
			var discoverer = (Discoverer)disc;

			switch (result) {
			case DiscovererResult.UriInvalid:
				Console.WriteLine ("Invalid URI '{0}'", uri);
				break;
			case DiscovererResult.Error:
				var err = new GLib.GException (args.Error);
				Console.WriteLine ("Discoverer error: {0}", err.Message);
				break;
			case DiscovererResult.Timeout:
				Console.WriteLine ("Timeout");
				break;
			case DiscovererResult.Busy:
				Console.WriteLine ("Busy");
				break;
			case DiscovererResult.MissingPlugins:{
					var s = info.Misc;

					if (s != null) {
						Console.WriteLine ("Missing plugins: {0}", s);
					}
					break;
				}
			case DiscovererResult.Ok:
				Console.WriteLine ("Discovered '{0}'", uri);
				break;
			}

			if (result != DiscovererResult.Ok) {
				Console.WriteLine ("This URI cannot be played");
				return;
			}

			// If we got no error, show the retrieved information
			Console.WriteLine ("\nDuration: {0}", new TimeSpan((long)info.Duration));

			var tags = info.Tags;
			if (tags != null) {
				Console.WriteLine ("Tags:");
				tags.Foreach ((TagForeachFunc)delegate (TagList list, string tag) {
					PrintTagForeach (list, tag, 1);
				});
			}

			Console.WriteLine ("Seekable: {0}", (info.Seekable ? "yes" : "no"));

			Console.WriteLine ();

			var sinfo = info.StreamInfo;
			if (sinfo == null)
				return;

			Console.WriteLine ("Stream information:");

			PrintTopology (sinfo, 1);

			Console.WriteLine ();
		}

		public static void Main (string[] args)
		{
			var uri = "http://download.blender.org/durian/trailer/sintel_trailer-1080p.mp4";

			// if a URI was provided, use it instead of the default one
			if (args.Length > 1) {
				uri = args[0];
			}

			// Initialize GStreamer
			Gst.Application.Init (ref args);

			Console.WriteLine ("Discovering '{0}'", uri);

			// Instantiate the Discoverer
			Discoverer = new Discoverer (5L * Gst.Constants.SECOND);

			// Connect to the interesting signals
			Discoverer.Discovered += HandleDiscovered;
			Discoverer.Finished += (sender, e) => {
				Console.WriteLine ("Finished discovering");
				MainLoop.Quit ();
			};

			// Start the discoverer process (nothing to do yet)
			Discoverer.Start ();

			// Add a request to process asynchronously the URI passed through the command line
			if (!Discoverer.DiscoverUriAsync (uri)) {
				Console.WriteLine ("Failed to start discovering URI '{0}'", uri);
				return;
			}

			// Create a GLib Main Loop and set it to run, so we can wait for the signals
			MainLoop = new GLib.MainLoop ();
			MainLoop.Run ();

			// Stop the discoverer process
			Discoverer.Stop ();
		}
	}
}