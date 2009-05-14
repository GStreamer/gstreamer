// GtkSharp.Generation.VMSignature.cs - The Virtual Method Signature Generation Class.
//
// Author: Mike Kestner <mkestner@ximian.com>
//
// Copyright (c) 2003-2004 Novell, Inc.
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
	using System.Xml;

	public class VMSignature  {
		
		private ArrayList parms = new ArrayList ();

		public VMSignature (Parameters parms) 
		{
			bool has_cb = parms.HideData;
			for (int i = 0; i < parms.Count; i++) {
				Parameter p = parms [i];

				if (i > 0 && p.IsLength && parms [i - 1].IsString)
					continue;

				if (p.IsCount && ((i > 0 && parms [i - 1].IsArray) || (i < parms.Count - 1 && parms [i + 1].IsArray)))
					continue;

				has_cb = has_cb || p.Generatable is CallbackGen;
				if (p.IsUserData && has_cb) 
					continue;

				if (p.CType == "GError**")
					continue;

				if (p.Scope == "notified")
					i += 2;

				this.parms.Add (p);
			}
		}

		public string GetCallString (bool use_place_holders)
		{
			if (parms.Count == 0)
				return "";

			string[] result = new string [parms.Count];
			int i = 0;
			foreach (Parameter p in parms) {
				result [i] = p.PassAs != "" ? p.PassAs + " " : "";
				result [i] += use_place_holders ? "{" + i + "}" : p.Name;
				i++;
			}

			return String.Join (", ", result);
		}

		public override string ToString ()
		{
			if (parms.Count == 0)
				return "";

			string[] result = new string [parms.Count];
			int i = 0;

			foreach (Parameter p in parms) {
				result [i] = p.PassAs != "" ? p.PassAs + " " : "";
				result [i++] += p.CSType + " " + p.Name;
			}

			return String.Join (", ", result);
		}
	}
}

