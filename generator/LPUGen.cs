// GtkSharp.Generation.LPUGen.cs - unsugned long/pointer generatable.
//
// Author: Mike Kestner <mkestner@novell.com>
//
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
	using System.IO;

	public class LPUGen : SimpleGen, IAccessor {
		
		public LPUGen (string ctype) : base (ctype, "ulong", "0") {}

		public override string MarshalType {
			get {
				return "UIntPtr";
			}
		}

		public override string CallByName (string var_name)
		{
			return "new UIntPtr (" + var_name + ")";
		}
		
		public override string FromNative(string var)
		{
			return "(ulong) " + var;
		}

		public void WriteAccessors (StreamWriter sw, string indent, string var)
		{
			sw.WriteLine (indent + "get {");
			sw.WriteLine (indent + "\treturn " + FromNative (var) + ";");
			sw.WriteLine (indent + "}");
			sw.WriteLine (indent + "set {");
			sw.WriteLine (indent + "\t" + var + " = " + CallByName ("value") + ";");
			sw.WriteLine (indent + "}");
		}
	}
}

