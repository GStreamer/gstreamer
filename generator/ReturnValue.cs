// GtkSharp.Generation.ReturnValue.cs - The ReturnValue Generatable.
//
// Author: Mike Kestner <mkestner@novell.com>
//
// Copyright (c) 2004-2005 Novell, Inc.
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
	using System.Xml;

	public class ReturnValue  {

		
		bool is_null_term;
		bool is_array;
		bool elements_owned;
		bool owned;
		string ctype = String.Empty;
		string element_ctype = String.Empty;

		public ReturnValue (XmlElement elem) 
		{
			if (elem != null) {
				is_null_term = elem.HasAttribute ("null_term_array");
				is_array = elem.HasAttribute ("array");
				elements_owned = elem.GetAttribute ("elements_owned") == "true";
				owned = elem.GetAttribute ("owned") == "true";
				ctype = elem.GetAttribute("type");
				element_ctype = elem.GetAttribute ("element_type");
			}
		}

		public string CType {
			get {
				return ctype;
			}
		}

		public string CSType {
			get {
				if (IGen == null)
					return String.Empty;

				if (ElementType != String.Empty)
					return ElementType + "[]";

				return IGen.QualifiedName + (is_array || is_null_term ? "[]" : String.Empty);
			}
		}

		public string DefaultValue {
			get {
				if (IGen == null)
					return String.Empty;
				return IGen.DefaultValue;
			}
		}

		string ElementType {
			get {
				if (element_ctype.Length > 0)
					return SymbolTable.Table.GetCSType (element_ctype);

				return String.Empty;
			}
		}

		IGeneratable igen;
		public IGeneratable IGen {
			get {
				if (igen == null)
					igen = SymbolTable.Table [CType];
				return igen;
			}
		}

		public bool IsVoid {
			get {
				return CSType == "void";
			}
		}

		public string MarshalType {
			get {
				if (IGen == null)
					return String.Empty;
				else if (is_null_term)
					return "IntPtr";
				return IGen.MarshalReturnType + (is_array ? "[]" : String.Empty);
			}
		}

		public string ToNativeType {
			get {
				if (IGen == null)
					return String.Empty;
				return IGen.ToNativeReturnType + (is_array || is_null_term ? "[]" : String.Empty);
			}
		}

		public string FromNative (string var)
		{
			if (IGen == null)
				return String.Empty;

			if (ElementType != String.Empty) {
				string args = (owned ? "true" : "false") + ", " + (elements_owned ? "true" : "false");
				if (IGen.QualifiedName == "GLib.PtrArray")
					return String.Format ("({0}[]) GLib.Marshaller.PtrArrayToArray ({1}, {2}, typeof({0}))", ElementType, var, args);
				else
					return String.Format ("({0}[]) GLib.Marshaller.ListPtrToArray ({1}, typeof({2}), {3}, typeof({4}))", ElementType, var, IGen.QualifiedName, args, element_ctype == "gfilename*" ? "GLib.ListBase.FilenameString" : ElementType);
			} else if (IGen is HandleBase)
				return ((HandleBase)IGen).FromNative (var, owned);
			else if (is_null_term)
				return String.Format ("GLib.Marshaller.NullTermPtrToStringArray ({0}, {1})", var, owned ? "true" : "false");
			else
				return IGen.FromNativeReturn (var);
		}
			
		public string ToNative (string var)
		{
			if (IGen == null)
				return String.Empty;

			if (ElementType.Length > 0) {
				string args = ", typeof (" + ElementType + "), " + (owned ? "true" : "false") + ", " + (elements_owned ? "true" : "false");
				var = "new " + IGen.QualifiedName + "(" + var + args + ")";
			} else if (is_null_term)
				return String.Format ("GLib.Marshaller.StringArrayToNullTermPointer ({0})", var);

			if (IGen is IManualMarshaler)
				return (IGen as IManualMarshaler).AllocNative (var);
			else if (IGen is ObjectGen && owned)
				return var + " == null ? IntPtr.Zero : " + var + ".OwnedHandle";
			else if (IGen is OpaqueGen && owned)
				return var + " == null ? IntPtr.Zero : " + var + ".OwnedCopy";
			else
				return IGen.ToNativeReturn (var);
		}

		public bool Validate ()
		{
			if (MarshalType == "" || CSType == "") {
				Console.Write("rettype: " + CType);
				return false;
			}

			return true;
		}
	}
}

