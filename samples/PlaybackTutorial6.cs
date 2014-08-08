// Authors
//   Copyright (C) 2014 Stephan Sundermann <stephansundermann@gmail.com>

using System;
using Gst;
using System.Runtime.InteropServices;
using System.Text;

namespace GstreamerSharp
{
	class Playback
	{
		const uint PlayFlagsVis = (1 << 3);

		static Element Pipeline;

		// Return TRUE if this is a Visualization element
		static bool FilterVisFeatures (PluginFeature feature) {

			if (!(feature is ElementFactory))
				return false;
			var factory = (ElementFactory)feature;
			if (!factory.GetMetadata (Gst.Constants.ELEMENT_METADATA_KLASS).Contains ("Visualization"))
				return false;

			return true;
		}

		public static void Main (string[] args)
		{
			ElementFactory selectedFactory = null;

			// Initialize GStreamer
			Application.Init (ref args);

			// Get a list of all visualization plugins
			var list = Registry.Get().FeatureFilter (FilterVisFeatures, false);

			// Print their names
			Console.WriteLine ("Available visualization plugins:");
			foreach (var walk in list) {
				var factory = (ElementFactory)walk;
				var name = factory.Name;
				Console.WriteLine("  {0}", name);

				if (selectedFactory == null && name.StartsWith ("goom")) {
					selectedFactory = factory;
				}
			}

			// Don't use the factory if it's still empty
			// e.g. no visualization plugins found
			if (selectedFactory == null) {
				Console.WriteLine ("No visualization plugins found!");
				return;
			}

			// We have now selected a factory for the visualization element
			Console.WriteLine ("Selected '{0}'", selectedFactory.Name);
			var visPlugin = selectedFactory.Create ();
			if (visPlugin == null)
				return;


			// Build the pipeline
			Pipeline = Parse.Launch ("playbin uri=http://1live.akacast.akamaistream.net/7/706/119434/v1/gnl.akacast.akamaistream.net/1live");

			// Set the visualization flag
			var flags = (uint)Pipeline ["flags"];
			flags |= PlayFlagsVis;
			Pipeline ["flags"] = flags;

			// set vis plugin for playbin
			Pipeline ["vis-plugin"] = visPlugin;

			// Start playing 
			var ret = Pipeline.SetState (State.Playing);
			if (ret == StateChangeReturn.Failure) {
				Console.WriteLine ("Unable to set the pipeline to the playing state.");
				return;
			}

			// Wait until error or EOS 
			var bus = Pipeline.Bus;
			var msg = bus.TimedPopFiltered (Constants.CLOCK_TIME_NONE, MessageType.Error | MessageType.Eos);

			// Free resources 
			Pipeline.SetState (State.Null);
		}
	}
}