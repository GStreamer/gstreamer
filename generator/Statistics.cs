// Statistics.cs : Generation statistics class implementation
//
// Author: Mike Kestner  <mkestner@ximian.com>
//
// Copyright (c) 2002 Mike Kestner
// Copyright (c) 2004 Novell, Inc.
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of version 2 of the GNU General Public
// License as published by the Free Software Foundation.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// General Public License for more details.
//
// You should have received a copy of the GNU General Public
// License along with this program; if not, write to the
// Free Software Foundation, Inc., 59 Temple Place - Suite 330,
// Boston, MA 02111-1307, USA.


namespace GtkSharp.Generation {
	
	using System;
	using System.Collections;
	
	public class Statistics {
		
		static int cbs = 0;
		static int enums = 0;
		static int objects = 0;
		static int structs = 0;
		static int boxed = 0;
		static int opaques = 0;
		static int interfaces = 0;
		static int methods = 0;
		static int ctors = 0;
		static int props = 0;
		static int sigs = 0;
		static int throttled = 0;
		static int ignored = 0;
		static bool vm_ignored = false;
		
		public static int CBCount {
			get {
				return cbs;
			}
			set {
				cbs = value;
			}
		}

		public static int EnumCount {
			get {
				return enums;
			}
			set {
				enums = value;
			}
		}

		public static int ObjectCount {
			get {
				return objects;
			}
			set {
				objects = value;
			}
		}

		public static int StructCount {
			get {
				return structs;
			}
			set {
				structs = value;
			}
		}

		public static int BoxedCount {
			get {
				return boxed;
			}
			set {
				boxed = value;
			}
		}

		public static int OpaqueCount {
			get {
				return opaques;
			}
			set {
				opaques = value;
			}
		}

		public static int CtorCount {
			get {
				return ctors;
			}
			set {
				ctors = value;
			}
		}
		
		public static int MethodCount {
			get {
				return methods;
			}
			set {
				methods = value;
			}
		}

		public static int PropCount {
			get {
				return props;
			}
			set {
				props = value;
			}
		}

		public static int SignalCount {
			get {
				return sigs;
			}
			set {
				sigs = value;
			}
		}

		public static int IFaceCount {
			get {
				return interfaces;
			}
			set {
				interfaces = value;
			}
		}

		public static int ThrottledCount {
			get {
				return throttled;
			}
			set {
				throttled = value;
			}
		}
		
		public static int IgnoreCount {
			get {
				return ignored;
			}
			set {
				ignored = value;
			}
		}
		
		public static bool VMIgnored {
			get {
				return vm_ignored;
			}
			set {
				if (value)
					vm_ignored = value;
			}
		}
		
		public static void Report()
		{
			if (VMIgnored) {
				Console.WriteLine();
				Console.WriteLine("Warning: Generation throttled for Virtual Methods.");
				Console.WriteLine("  Consider regenerating with --gluelib-name and --glue-filename.");
			}
			Console.WriteLine();
			Console.WriteLine("Generation Summary:");
			Console.Write("  Enums: " + enums);
			Console.Write("  Structs: " + structs);
			Console.Write("  Boxed: " + boxed);
			Console.Write("  Opaques: " + opaques);
			Console.Write("  Interfaces: " + interfaces);
			Console.Write("  Objects: " + objects);
			Console.WriteLine("  Callbacks: " + cbs);
			Console.Write("  Properties: " + props);
			Console.Write("  Signals: " + sigs);
			Console.Write("  Methods: " + methods);
			Console.Write("  Constructors: " + ctors);
			Console.WriteLine("  Throttled: " + throttled);
			Console.WriteLine("Total Nodes: " + (enums+structs+boxed+opaques+interfaces+cbs+objects+props+sigs+methods+ctors+throttled));
			Console.WriteLine();
		}
	}
}
