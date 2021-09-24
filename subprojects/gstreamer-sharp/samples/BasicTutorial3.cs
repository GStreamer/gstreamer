// Authors
//   Copyright (C) 2014 Stephan Sundermann <stephansundermann@gmail.com>

using System;
using Gst; 

namespace GstreamerSharp
{
	class Playback
	{
		static Pipeline pipeline;
		static Element source;
		static Element convert;
		static Element sink;

		public static void Main (string[] args)
		{
			// Initialize Gstreamer
			Application.Init(ref args);

			// Create the elements
			source = ElementFactory.Make ("uridecodebin", "source");
			convert = ElementFactory.Make ("audioconvert", "convert");
			sink = ElementFactory.Make ("autoaudiosink", "sink");

			// Create the empty pipeline
			pipeline = new Pipeline ("test-pipeline");

			if (source == null || convert == null || sink == null || pipeline == null) {
				Console.WriteLine ("Not all elements could be created");
				return;
			}

			// Build the pipeline. Note that we are NOT linking the source at this point.
			// We will do it later.
			pipeline.Add (source, convert, sink);
			if (!convert.Link (sink)) {
				Console.WriteLine ("Elements could not be linked");
				return;
			}

			// Set the URI to play
			source ["uri"] = "http://download.blender.org/durian/trailer/sintel_trailer-1080p.mp4";

			// Connect to the pad-added signal
			source.PadAdded += HandlePadAdded;

			// Start playing
			var ret = pipeline.SetState (State.Playing);
			if (ret == StateChangeReturn.Failure) {
				Console.WriteLine ("Unable to set the pipeline to the playing state.");
				return;
			}

			// Listen to the bus
			var bus = pipeline.Bus;
			bool terminated = false;
			do {
				var msg = bus.TimedPopFiltered (Constants.CLOCK_TIME_NONE, MessageType.StateChanged | MessageType.Error | MessageType.Eos);

				if (msg != null) {
					switch (msg.Type) {
					case MessageType.Error:
						string debug;
						GLib.GException exc;
						msg.ParseError (out exc, out debug);
						Console.WriteLine (string.Format ("Error received from element {0}: {1}", msg.Src.Name, exc.Message));
						Console.WriteLine ("Debugging information: {0}", debug);
						terminated = true;
						break;
					case MessageType.Eos:
						Console.WriteLine("End-Of-Stream reached.");
						terminated = true;
						break;
					case MessageType.StateChanged:
						// We are only interested in state-changed messages from the pipeline
						if (msg.Src == pipeline) {
							State oldState, newState, pendingState;
							msg.ParseStateChanged(out oldState, out newState, out pendingState);
							Console.WriteLine ("Pipeline state changed from {0} to {1}:", Element.StateGetName(oldState), Element.StateGetName(newState));
						}
						break;
					default:
						// We should not reach here
						Console.WriteLine ("Unexpected message received.");
						break;
					}
				}
			} while (!terminated);

			pipeline.SetState (State.Null);
		}

		static void HandlePadAdded (object o, PadAddedArgs args)
		{
			var src = (Element)o;
			var newPad = args.NewPad;
			var sinkPad = convert.GetStaticPad ("sink");

			Console.WriteLine (string.Format ("Received new pad '{0}' from '{1}':", newPad.Name, src.Name));

			// If our converter is already linked, we have nothing to do here
			if (sinkPad.IsLinked) {
				Console.WriteLine ("We are already linked. Ignoring.");
				return;
			}

			// Check the new pad's type
			var newPadCaps = newPad.Caps;
			var newPadStruct = newPadCaps.GetStructure (0);
			var newPadType = newPadStruct.Name;
			if (!newPadType.StartsWith ("audio/x-raw")) {
				Console.WriteLine ("It has type '{0}' which is not raw audio. Ignoring.", newPadType);
				return;
			}

			// Attempt the link
			var ret = newPad.Link (sinkPad);
			if (ret != PadLinkReturn.Ok)
				Console.WriteLine ("Type is '{0} but link failed.", newPadType);
			else
				Console.WriteLine ("Link succeeded (type '{0}').", newPadType);
		}
	}
}