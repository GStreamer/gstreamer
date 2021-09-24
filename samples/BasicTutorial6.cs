// Authors
//   Copyright (C) 2014 Stephan Sundermann <stephansundermann@gmail.com>

using System;
using Gst;
using System.Runtime.InteropServices;

namespace GstreamerSharp
{
	class Playback
	{
		// Functions below print the capabilities in a human-friendly format
		static void PrintCaps (Caps caps, string pfx) {

			if (caps == null)
				return;

			if (caps.IsAny) {
				Console.WriteLine ("{0}ANY", pfx);
				return;
			}

			if (caps.IsEmpty) {
				Console.WriteLine ("{0}EMPTY", pfx);
				return;
			}

			for (uint i = 0; i < caps.Size; i++) {
				var structure = caps.GetStructure (i);

				Console.WriteLine ("{0}{1}", pfx, structure.Name);
				structure.Foreach ((field_id, value) => {
					var ptr = g_quark_to_string (field_id);
					var quark = GLib.Marshaller.Utf8PtrToString (ptr);
					Console.WriteLine ("{0}   {1}: {2}", pfx, quark, value.Val);
					return true;
				});
			}
		}

		// Prints information about a Pad Template, including its Capabilities*/
		static void PrintPadTemplateInformation (ElementFactory factory) {

			Console.WriteLine ("Pad Templates for {0}:", factory.Name);
			if (factory.NumPadTemplates == 0) {
				Console.WriteLine ("  none");
				return;
			}

			var pads = factory.StaticPadTemplates;
			foreach (var p in pads) {
				var pad = (StaticPadTemplate) p;

				if (pad.Direction == PadDirection.Src)
					Console.WriteLine ("  SRC template: '{0}'", pad.NameTemplate);
				else if (pad.Direction == PadDirection.Sink)
					Console.WriteLine ("  SINK template: '{0}'", pad.NameTemplate);
				else
					Console.WriteLine ("  UNKNOWN!!! template: '{0}'", pad.NameTemplate);

				if (pad.Presence == PadPresence.Always)
					Console.WriteLine ("    Availability: Always");
				else if (pad.Presence == PadPresence.Sometimes)
					Console.WriteLine ("    Availability: Sometimes");
				else if (pad.Presence == PadPresence.Request) {
					Console.WriteLine ("    Availability: On request");
				} else
					Console.WriteLine ("    Availability: UNKNOWN!!!");

				if (pad.StaticCaps.String != null) {
					Console.WriteLine ("    Capabilities:");
					PrintCaps (pad.StaticCaps.Get (), "      ");
				}

				Console.WriteLine ();
			}
		}

		// Shows the CURRENT capabilities of the requested pad in the given element */
		static void PrintPadCapabilities (Element element, string padName) {

			// Retrieve pad
			var pad = element.GetStaticPad (padName);
			if (pad == null) {
				Console.WriteLine ("Could not retrieve pad '{0}'", padName);
				return;
			}

			// Retrieve negotiated caps (or acceptable caps if negotiation is not finished yet)
			var caps = pad.CurrentCaps;
			if (caps == null)
				caps = pad.Caps;

			/* Print and free */
			Console.WriteLine ("Caps for the {0} pad:", padName);
			PrintCaps (caps, "      ");
		}

		public static void Main (string[] args)
		{
			// Initialize Gstreamer
			Gst.Application.Init(ref args);

			// Create the element factories
			var sourceFactory = ElementFactory.Find ("audiotestsrc");
			var sinkFactory = ElementFactory.Find ("autoaudiosink");



			if (sourceFactory == null || sinkFactory == null) {
				Console.WriteLine ("Not all element factories could be created.");
				return;
			}

			// Print information about the pad templates of these factories
			PrintPadTemplateInformation (sourceFactory);
			PrintPadTemplateInformation (sinkFactory);

			// Ask the factories to instantiate actual elements
			var source = sourceFactory.Create ("source");
			var sink = sinkFactory.Create ("sink");

			// Create the empty pipeline
			var pipeline = new Pipeline ("test-pipeline");

			if (pipeline == null || source == null || sink == null) {
				Console.WriteLine ("Not all elements could be created.");
				return;
			}

			// Build the pipeline
			pipeline.Add (source, sink);
			if (!source.Link (sink)) {
				Console.WriteLine ("Elements could not be linked.");
				return;
			}

			// Print initial negotiated caps (in NULL state)
			Console.WriteLine ("In NULL state:");
			PrintPadCapabilities (sink, "sink");

			// Start playing
			var ret = pipeline.SetState (State.Playing);
			if (ret == StateChangeReturn.Failure) {
				Console.WriteLine ("Unable to set the pipeline to the playing state (check the bus for error messages).");
			}

			// Wait until error, EOS or State Change
			var bus = pipeline.Bus;
			var terminate = false;

			do {
				var msg = bus.TimedPopFiltered (Constants.CLOCK_TIME_NONE, MessageType.Error | MessageType.Eos | MessageType.StateChanged);

				// Parse message
				if (msg != null) {
					switch (msg.Type) {
					case MessageType.Error:
						string debug;
						GLib.GException exc;
						msg.ParseError (out exc, out debug);
						Console.WriteLine ("Error received from element {0}: {1}", msg.Src.Name, exc.Message);
						Console.WriteLine ("Debugging information: {0}", debug != null ? debug : "none");
						terminate = true;
						break;
					case MessageType.Eos:
						Console.WriteLine ("End-Of-Stream reached.\n");
						terminate = true;
						break;
					case MessageType.StateChanged:
						// We are only interested in state-changed messages from the pipeline
						if (msg.Src == pipeline) {
							State oldState, newState, pendingState;
							msg.ParseStateChanged (out oldState, out newState, out pendingState);
							Console.WriteLine ("Pipeline state changed from {0} to {1}:",
								Element.StateGetName (oldState), Element.StateGetName (newState));
							// Print the current capabilities of the sink element
							PrintPadCapabilities (sink, "sink");
						}
						break;
					default:
						// We should not reach here because we only asked for ERRORs, EOS and STATE_CHANGED
						Console.WriteLine ("Unexpected message received.");
						break;
					}
				}
			} while (!terminate);

			// Free resources
			pipeline.SetState (State.Null);
		}

		[DllImport ("glib-2.0.dll")]
		static extern IntPtr g_quark_to_string (uint quark);
	}
}