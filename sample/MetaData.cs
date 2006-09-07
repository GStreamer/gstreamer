//
// Authors
//   Khaled Mohammed (khaled.mohammed@gmail.com)
//
// (C) 2006
//

using Gst;
using System;

public class MetaData 
{

	static Element pipeline = null;
	static Element source = null;

	static void PrintTag( TagList list, string tag) {
		uint count = list.GetTagSize(tag);
		Console.WriteLine("Tags found = " + count);
		for(uint i =0; i < count; i++) 
		{
			string str;
			if(Tag.GetGType(tag) == GLib.GType.String) {
				if(!list.GetStringIndex(tag, i, out str))
					Console.Error.WriteLine("g_assert_not_reached()???");
			} else {
				str = (String) list.GetValueIndex(tag, i).Val; 
			}

			if(i == 0) 
				Console.WriteLine("{0}:\t {1}", Tag.GetNick(tag), str);
			else
				Console.WriteLine("\t{0}", str);
		}
	}

	static bool MessageLoop(Element element, ref TagList tags) 
	{
		Bus bus = element.Bus;
		bool done = false;

		while(!done) {
			Message message = bus.Pop();
			if(message == null)
				break;

			switch(message.Type) {
				case MessageType.Error:
					string error;
					message.ParseError(out error);
					message.Dispose();
					return true;
				case MessageType.Eos:
					message.Dispose();
					return true;
				case MessageType.Tag: {
					TagList new_tags = new TagList();
					message.ParseTag(new_tags);
					if(tags != null) {
						tags = tags.Merge(new_tags, TagMergeMode.KeepAll);
					}
					else {
						tags = new_tags;
					}
					//tags.Foreach(PrintTag);
					//new_tags.Dispose();
					break;
				}
				default:
					break;
			}	
			message.Dispose();
		}
		bus.Dispose();
		return true;
	}

	static void MakePipeline() 
	{
		Element decodebin;

		if(pipeline != null) {
			pipeline.Dispose();
		}
		
		pipeline = new Pipeline(String.Empty);
		source = ElementFactory.Make("filesrc", "source");
		decodebin = ElementFactory.Make("decodebin", "decodebin");

		if(pipeline == null) Console.Error.WriteLine("Pipeline count not be created");
		if(source == null) Console.Error.WriteLine("Element filesrc could not be created");
		if(decodebin == null) Console.Error.WriteLine("Element decodebin coult not be created");

		Bin bin = (Bin) pipeline;
		bin.AddMany(source, decodebin);
		if(!source.Link(decodebin))
			Console.Error.WriteLine("filesrc could not be linked with decodebin");
		decodebin.Dispose();
	}

	public static void Main(string [] args) 
	{
		Application.Init();

		if(args.Length < 1) 
		{
			Console.Error.WriteLine("Please give filenames to read metadata from\n\n");
			return;
		}

		MakePipeline();		

		int i=-1;
		while(++i < args.Length)
		{
			State state, pending;
			TagList tags = null;

			string filename = args[i];
			source.SetProperty("location", filename);

			StateChangeReturn sret = pipeline.SetState(State.Paused);

			if(sret == StateChangeReturn.Async) {
				if(StateChangeReturn.Success != pipeline.GetState(out state, out pending, Clock.Second * 5)) {
					Console.Error.WriteLine("State change failed for {0}. Aborting\n", filename);
					break;
				}
			} else if(sret != StateChangeReturn.Success) {
				Console.Error.WriteLine("{0} - Could not read file\n", filename);
				continue;
			}

			if(!MessageLoop(pipeline, ref tags)) {
				Console.Error.WriteLine("Failed in message reading for {0}", args[i]);
			}

			if(tags != null) {
				Console.WriteLine("Metadata for {0}:", args[i]);
				tags.Foreach(new TagForeachFunc(PrintTag));
				tags.Dispose();
				tags = null;
			} else Console.Error.WriteLine("No metadata found for {0}", args[0]);

			sret = pipeline.SetState(State.Null);

			if(StateChangeReturn.Async == sret) {
				if(StateChangeReturn.Failure == pipeline.GetState(out state, out pending, Clock.TimeNone)) {
					Console.Error.WriteLine("State change failed. Aborting");
				}
			}
		}

		if(pipeline != null) 
		{
			pipeline.Dispose();
		}

	}
}

