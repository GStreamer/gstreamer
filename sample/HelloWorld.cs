using System;
using Gst;
using GLib;
using System.Reflection;

public class HelloWorld {

	MainLoop loop;
	Element pipeline, source, parser, decoder, conv, identity, sink;
	
	bool BusCall(Bus bus, Message message) {
		
		switch(message.Type) {
			case MessageType.Error:
				string err = String.Empty;
				message.ParseError(out err);
				Console.WriteLine ("Gstreamer error: {0}", err);
				loop.Quit();
				break;
			case MessageType.Eos:
				Console.WriteLine("End-of-stream");
				loop.Quit();
				break;
			default:
				//Console.WriteLine("Entered BusCall:\t" + message.Type);
				break;
		}
		return true;
	}

	public static void Main(string [] args) {
		new HelloWorld(args);
	}

	public HelloWorld(string [] args) {

// Initializes Gstreamer library
		Application.Init();

		loop = new MainLoop();

		// create elements
		if((pipeline = new Pipeline("audio-player")) == null)
		{
			Console.WriteLine("Could not create audio player pipeline");
		}
		
		if((source = ElementFactory.Make("filesrc", "file-source")) == null) 
		{
			Console.WriteLine("Could not create file-source");
		}
		
		parser = ElementFactory.Make("oggdemux", "ogg-parser");
		decoder = ElementFactory.Make("vorbisdec", "vorbis-decoder");
		conv = ElementFactory.Make("audioconvert", "converter");
		identity = ElementFactory.Make("identity", "identitye");
		sink = ElementFactory.Make("alsasink", "alsa-output");
		
		// set source to read the filename from command line argdument
		source.SetProperty("location", args[0]);
		
		Bin bin = (Bin) pipeline;
		bin.Bus.AddWatch(new BusFunc(BusCall));

		bin.Add(source);
		bin.Add(parser);
		bin.Add(decoder);
		bin.Add(conv);
		bin.Add(identity);
		bin.Add(sink);


		if(!source.Link(parser))
			Console.WriteLine("link failed");
		if(!decoder.Link(conv))
			Console.WriteLine("link failed between decoder and converter");
		if(!conv.Link(identity))
			Console.WriteLine("link failed between converter and identity");
		if(!identity.Link(sink))
			Console.Error.WriteLine("link failed between identity and sink");

		parser.PadAdded += new PadAddedHandler(OnPadAdded);

		Console.WriteLine("Adding custom event");


		identity.AddCustomEvent("handoff", new MyEventHandler(handoff));

		pipeline.SetState(State.Playing);
		Console.WriteLine("Playing [" + args[0] + "]");


		loop.Run();

		pipeline.SetState(State.Null);

		pipeline.Dispose();
	}

	delegate void MyEventHandler(object sender, Gst.Buffer buf);

	ulong count = 0;


	void handoff(object i, Gst.Buffer buf) {
		Console.WriteLine(buf.Duration + "\t" + buf.Timestamp);
		/*
		ulong newcount = buf.Timestamp / buf.Duration * 20;
		if(newcount > count)
		{
			Console.Write("*");
			count = newcount;
		}
		*/
	}
	void OnPadAdded(object sender, PadAddedArgs e) {
		Console.WriteLine("Entered OnPadAdded");
		Pad sinkpad = decoder.GetPad("sink");
		e.Pad.Link(sinkpad);
	}
}
