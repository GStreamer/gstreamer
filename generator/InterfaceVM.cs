 
// GtkSharp.Generation.InterfaceVM.cs - interface-specific part of VM creation
//
// Author: Christian Hoff <christian_hoff@gmx.net>
//
// Copyright (c) 2009 Christian Hoff
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
	using System.IO;
	using System.Xml;

	public class InterfaceVM : VirtualMethod
	{
		private Method target;

		public InterfaceVM (XmlElement elem, Method target, ObjectBase container_type) : base (elem, container_type)
		{
			this.target = target;
			parms.HideData = true;
			this.Protection = "public";
		}

		public bool IsGetter {
			get {
				return HasGetterName && ((!retval.IsVoid && parms.Count == 0) || (retval.IsVoid && parms.Count == 1 && parms [0].PassAs == "out"));
			}
		}
	
		public bool IsSetter {
			get {
				if (!HasSetterName || !retval.IsVoid)
					return false;

				if (parms.Count == 1 || (parms.Count == 3 && parms [0].Scope == "notified"))
					return true;
				else
					return false;
			}
		}

		protected override string CallString {
			get {
				if (IsGetter)
					return (target.Name.StartsWith ("Get") ? target.Name.Substring (3) : target.Name);
				else if (IsSetter)
					return target.Name.Substring (3) + " = " + call;
				else
					return target.Name + " (" + call + ")";
			}
		}

		public void GenerateDeclaration (StreamWriter sw, InterfaceVM complement)
		{
			if (IsGetter) {
				string name = Name.StartsWith ("Get") ? Name.Substring (3) : Name;
				string type = retval.IsVoid ? parms [0].CSType : retval.CSType;
				if (complement != null && complement.parms [0].CSType == type)
					sw.WriteLine ("\t\t" + type + " " + name + " { get; set; }");
				else {
					sw.WriteLine ("\t\t" + type + " " + name + " { get; }");
					if (complement != null)
						sw.WriteLine ("\t\t" + complement.retval.CSType + " " + complement.Name + " (" + complement.Signature + ");");
				}
			} else if (IsSetter) 
				sw.WriteLine ("\t\t" + parms[0].CSType + " " + Name.Substring (3) + " { set; }");
			else
				sw.WriteLine ("\t\t" + retval.CSType + " " + Name + " (" + Signature + ");");
		}

		public override bool Validate ()
		{
			if (target == null) {
				Console.WriteLine ("Virtual method {0}->{1} has no matching target to invoke", container_type.CName, CName);
				return false;
			}
			return base.Validate ();
		}
	}
}
