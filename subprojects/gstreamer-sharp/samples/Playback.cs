// Copyright (C) 2013  Stephan Sundermann <stephansundermann@gmail.com>
// 
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Affero General Public License as
// published by the Free Software Foundation, either version 3 of the
// License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.

using System;
using Gst; 

namespace GstreamerSharp
{
	class Playback
	{
		static GLib.MainLoop Loop;
		static Element element;
 
		public static void Main (string[] args)
		{
			Loop = new GLib.MainLoop();
 			
			Application.Init(ref args);
			element = Gst.Parse.Launch("playbin uri=http://ftp.nluug.nl/ftp/graphics/blender/apricot/trailer/Sintel_Trailer1.1080p.DivX_Plus_HD.mkv");

			element.Bus.AddSignalWatch();
			element.Bus.Message += Handle;
			element.SetState(State.Playing);
			Loop.Run();
		}
 
		static void Handle (object e, MessageArgs args)
		{

			switch (args.Message.Type) {
			case MessageType.StateChanged:
				State oldstate, newstate, pendingstate;
				args.Message.ParseStateChanged (out oldstate, out newstate, out pendingstate);
				System.Console.WriteLine ("[StateChange] From " + oldstate + " to " + newstate + " pending at " + pendingstate);
				break;
			case MessageType.StreamStatus:
				Element owner;
				StreamStatusType type;
				args.Message.ParseStreamStatus (out type, out owner);
				System.Console.WriteLine ("[StreamStatus] Type" + type + " from " + owner);
				break;
			case MessageType.DurationChanged:
				long duration;
				element.QueryDuration (Format.Time, out duration);
				System.Console.WriteLine ("[DurationChanged] New duration is " + (duration / Constants.SECOND) + " seconds");
				break;
			case MessageType.ResetTime:
				ulong runningtime = args.Message.ParseResetTime ();
				System.Console.WriteLine ("[ResetTime] Running time is " + runningtime);
				break;
			case MessageType.AsyncDone:
				ulong desiredrunningtime = args.Message.ParseAsyncDone ();
				System.Console.WriteLine ("[AsyncDone] Running time is " + desiredrunningtime);
				break;
			case MessageType.NewClock:
				Clock clock = args.Message.ParseNewClock ();
				System.Console.WriteLine ("[NewClock] " + clock);
				break;
			case MessageType.Buffering:
				int percent = args.Message.ParseBuffering ();
				System.Console.WriteLine ("[Buffering] " + percent + " % done");
				break;
			case MessageType.Tag:
				TagList list = args.Message.ParseTag ();
				System.Console.WriteLine ("[Tag] Information in scope " + list.Scope + " is " + list.ToString());
				break;
			case MessageType.Error:
				GLib.GException gerror;
				string debug;
				args.Message.ParseError (out gerror, out debug);
				System.Console.WriteLine ("[Error] " + gerror.Message + " debug information " + debug + ". Exiting! ");
				Loop.Quit ();
				break;
			case MessageType.Eos:
				System.Console.WriteLine ("[Eos] Playback has ended. Exiting!");
				Loop.Quit ();
				break;
			default:
				System.Console.WriteLine ("[Recv] " + args.Message.Type);
				break;
			}
		}
	}
}