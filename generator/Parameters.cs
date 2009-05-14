// GtkSharp.Generation.Parameters.cs - The Parameters Generation Class.
//
// Author: Mike Kestner <mkestner@speakeasy.net>
//
// Copyright (c) 2001-2003 Mike Kestner
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
	using System.IO;
	using System.Xml;

	public class Parameter {

		private XmlElement elem;

		public Parameter (XmlElement e)
		{
			elem = e;
		}

		string call_name;
		public string CallName {
			get {
				if (call_name == null)
					return Name;
				else
					return call_name;
			}
			set {
				call_name = value;
			}
		}

		public string CType {
			get {
				string type = elem.GetAttribute("type");
				if (type == "void*")
					type = "gpointer";
				return type;
			}
		}

		public string CSType {
			get {
				string cstype = SymbolTable.Table.GetCSType( elem.GetAttribute("type"));
				if (cstype == "void")
					cstype = "System.IntPtr";
				if (IsArray) {
					if (IsParams)
						cstype = "params " + cstype;
					cstype += "[]";
					cstype = cstype.Replace ("ref ", "");
				}
				return cstype;
			}
		}

		public IGeneratable Generatable {
			get {
				return SymbolTable.Table[CType];
			}
		}

		public bool IsArray {
			get {
				return elem.HasAttribute("array") || elem.HasAttribute("null_term_array");
			}
		}

		public bool IsEllipsis {
			get {
				return elem.HasAttribute("ellipsis");
			}
		}

		public bool IsCount {
			get {
				
				if (Name.StartsWith("n_"))
					switch (CSType) {
					case "int":
					case "uint":
					case "long":
					case "ulong":
					case "short":
					case "ushort": 
						return true;
					default:
						return false;
					}
				else
					return false;
			}
		}

		public bool IsDestroyNotify {
			get {
				return CType == "GDestroyNotify";
			}
		}

		public bool IsLength {
			get {
				
				if (Name.EndsWith("len") || Name.EndsWith("length"))
					switch (CSType) {
					case "int":
					case "uint":
					case "long":
					case "ulong":
					case "short":
					case "ushort": 
						return true;
					default:
						return false;
					}
				else
					return false;
			}
		}

		public bool IsParams {
			get {
				return elem.HasAttribute("params");
			}
		}

		public bool IsString {
			get {
				return (CSType == "string");
			}
		}

		public bool IsUserData {
			get {
				return CSType == "IntPtr" && (Name.EndsWith ("data") || Name.EndsWith ("data_or_owner"));
			}
		}

		public virtual string MarshalType {
			get {
				string type = SymbolTable.Table.GetMarshalType( elem.GetAttribute("type"));
				if (type == "void" || Generatable is IManualMarshaler)
					type = "IntPtr";
				if (IsArray) {
					type += "[]";
					type = type.Replace ("ref ", "");
				}
				return type;
			}
		}

		public string Name {
			get {
				return SymbolTable.Table.MangleName (elem.GetAttribute("name"));
			}
		}

		public bool Owned {
			get {
				return elem.GetAttribute ("owned") == "true";
			}
		}

		public virtual string NativeSignature {
			get {
				string sig = MarshalType + " " + Name;
				if (PassAs != String.Empty)
					sig = PassAs + " " + sig;
				return sig;
			}
		}

		public string PropertyName {
			get {
				return elem.GetAttribute("property_name");
			}
		}

		string pass_as;

		public string PassAs {
			get {
				if (pass_as != null)
					return pass_as;

				if (elem.HasAttribute ("pass_as"))
					return elem.GetAttribute ("pass_as");

				if (IsArray || CSType.EndsWith ("IntPtr"))
					return "";

				if (CType.EndsWith ("*") && (Generatable is SimpleGen || Generatable is EnumGen))
					return "out";

				return "";
			}
			set {
				pass_as = value;
			}
		}

		string scope;
		public string Scope {
			get {
				if (scope == null)
					scope = elem.GetAttribute ("scope");
				return scope;
			}
			set {
				scope = value;
			}
		}

		public virtual string[] Prepare {
			get {
				IGeneratable gen = Generatable;
				if (gen is IManualMarshaler) {
					string result = "IntPtr native_" + CallName;
					if (PassAs != "out")
						result += " = " + (gen as IManualMarshaler).AllocNative (CallName);
					return new string [] { result + ";" }; 
				} else if (PassAs == "out" && CSType != MarshalType) {
					return new string [] { gen.MarshalType + " native_" + CallName + ";" };
				} else if (PassAs == "ref" && CSType != MarshalType) {
					return new string [] { gen.MarshalType + " native_" + CallName + " = (" + gen.MarshalType + ") " + CallName + ";" };
				}

				return new string [0];
			}
		}

		public virtual string CallString {
			get {
				string call_parm;

				IGeneratable gen = Generatable;
				if (gen is CallbackGen)
					return SymbolTable.Table.CallByName (CType, CallName + "_wrapper");
				else if (PassAs != String.Empty) {
					call_parm = PassAs + " ";
					if (CSType != MarshalType)
						call_parm += "native_";
					call_parm += CallName;
				} else if (gen is IManualMarshaler)
					call_parm = "native_" + CallName;
				else
					call_parm = SymbolTable.Table.CallByName(CType, CallName);
			
				return call_parm;
			}
		}

		public virtual string[] Finish {
			get {
				IGeneratable gen = Generatable;
				if (gen is IManualMarshaler) {
					string[] result = new string [PassAs == "ref" ? 2 : 1];
					int i = 0;
					if (PassAs != String.Empty)
						result [i++] = CallName + " = " + Generatable.FromNative ("native_" + CallName) + ";";
					if (PassAs != "out")
						result [i] = (gen as IManualMarshaler).ReleaseNative ("native_" + CallName) + ";";
					return result;
				} else if (PassAs != String.Empty && MarshalType != CSType)
					return new string [] { CallName + " = " + gen.FromNative ("native_" + CallName) + ";" };
				return new string [0];
			}
		}

		public string FromNative (string var)
		{
			if (Generatable == null)
				return String.Empty;
			else if (Generatable is HandleBase)
				return ((HandleBase)Generatable).FromNative (var, Owned);
			else
				return Generatable.FromNative (var);
		}

		public string StudlyName {
			get {
				string name = elem.GetAttribute("name");
				string[] segs = name.Split('_');
				string studly = "";
				foreach (string s in segs) {
					if (s.Trim () == "")
						continue;
					studly += (s.Substring(0,1).ToUpper() + s.Substring(1));
				}
				return studly;
				
			}
		}
	}

	public class ArrayParameter : Parameter {

		bool null_terminated;

		public ArrayParameter (XmlElement elem) : base (elem) 
		{
			null_terminated = elem.HasAttribute ("null_term_array");
		}

		public override string MarshalType {
			get {
				if (Generatable is StructBase)
					return CSType;
				else
					return base.MarshalType;
			}
		}

		bool NullTerminated {
			get {
				return null_terminated;
			}
		}

		public override string[] Prepare {
			get {
				if (CSType == MarshalType)
					return new string [0];

				ArrayList result = new ArrayList ();
				result.Add (String.Format ("int cnt_{0} = {0} == null ? 0 : {0}.Length;", CallName));
				result.Add (String.Format ("{0}[] native_{1} = new {0} [cnt_{1}" + (NullTerminated ? " + 1" : "") + "];", MarshalType.TrimEnd('[', ']'), CallName));
				result.Add (String.Format ("for (int i = 0; i < cnt_{0}; i++)", CallName));
				IGeneratable gen = Generatable;
				if (gen is IManualMarshaler)
					result.Add (String.Format ("\tnative_{0} [i] = {1};", CallName, (gen as IManualMarshaler).AllocNative (CallName + "[i]")));
				else
					result.Add (String.Format ("\tnative_{0} [i] = {1};", CallName, gen.CallByName (CallName + "[i]")));

				if (NullTerminated)
					result.Add (String.Format ("native_{0} [cnt_{0}] = IntPtr.Zero;", CallName));
				return (string[]) result.ToArray (typeof (string));
			}
		}

		public override string CallString {
			get {
				if (CSType != MarshalType)
					return "native_" + CallName;
				else
					return CallName;
			}
		}

		public override string[] Finish {
			get {
				if (CSType == MarshalType)
					return new string [0];

				IGeneratable gen = Generatable;
				if (gen is IManualMarshaler) {
					string [] result = new string [4];
					result [0] = "for (int i = 0; i < native_" + CallName + ".Length" + (NullTerminated ? " - 1" : "") + "; i++) {";
					result [1] = "\t" + CallName + " [i] = " + Generatable.FromNative ("native_" + CallName + "[i]") + ";";
					result [2] = "\t" + (gen as IManualMarshaler).ReleaseNative ("native_" + CallName + "[i]") + ";";
					result [3] = "}";
					return result;
				}

				return new string [0];
			}
		}
	}

	public class ArrayCountPair : ArrayParameter {

		XmlElement count_elem;
		bool invert;

		public ArrayCountPair (XmlElement array_elem, XmlElement count_elem, bool invert) : base (array_elem)
		{
			this.count_elem = count_elem;
			this.invert = invert;
		}

		string CountNativeType {
			get {
				return SymbolTable.Table.GetMarshalType(count_elem.GetAttribute("type"));
			}
		}

		string CountType {
			get {
				return SymbolTable.Table.GetCSType(count_elem.GetAttribute("type"));
			}
		}

		string CountCast {
			get {
				if (CountType == "int")
					return String.Empty;
				else
					return "(" + CountType + ") ";
			}
		}

		string CountName {
			get {
				return SymbolTable.Table.MangleName (count_elem.GetAttribute("name"));
			}
		}

		string CallCount (string name)
		{
			string result = CountCast + "(" + name + " == null ? 0 : " + name + ".Length)";
			IGeneratable gen = SymbolTable.Table[count_elem.GetAttribute("type")];
			return gen.CallByName (result);
		}

		public override string CallString {
			get {
				if (invert)
					return CallCount (CallName) + ", " + base.CallString;
				else
					return base.CallString + ", " + CallCount (CallName);
			}
		}

		public override string NativeSignature {
			get {
				if (invert)
					return CountNativeType + " " + CountName + ", " + MarshalType + " " + Name;
				else
					return MarshalType + " " + Name + ", " + CountNativeType + " " + CountName;
			}
		}
	}

	public class ErrorParameter : Parameter {

		public ErrorParameter (XmlElement elem) : base (elem) 
		{
			PassAs = "out";
		}

		public override string CallString {
			get {
				return "out error";
			}
		}
	}

	public class StructParameter : Parameter {

		public StructParameter (XmlElement elem) : base (elem) {}

		public override string MarshalType {
			get {
				return "IntPtr";
			}
		}

		public override string[] Prepare {
			get {
				if (PassAs == "out")
					return new string [] { "IntPtr native_" + CallName + " = Marshal.AllocHGlobal (Marshal.SizeOf (typeof (" + Generatable.QualifiedName + ")));"};
				else
					return new string [] { "IntPtr native_" + CallName + " = " + (Generatable as IManualMarshaler).AllocNative (CallName) + ";"};
			}
		}

		public override string CallString {
			get {
				return "native_" + CallName;
			}
		}

		public override string[] Finish {
			get {
				string[] result = new string [2];
				result [0] = CallName + " = " + FromNative ("native_" + CallName) + ";";
				result [1] = (Generatable as IManualMarshaler).ReleaseNative ("native_" + CallName) + ";";
				return result;
			}
		}

		public override string NativeSignature {
			get {
				return "IntPtr " + CallName;
			}
		}
	}

	public class Parameters : IEnumerable {
		
		ArrayList param_list = new ArrayList ();
		XmlElement elem;
		bool first_is_instance;

		public Parameters (XmlElement elem) : this (elem, false) { }

		public Parameters (XmlElement elem, bool first_is_instance) 
		{
			if (elem == null)
				valid = true;
			this.elem = elem;
			this.first_is_instance = first_is_instance;
			if (first_is_instance)
				is_static = false;
		}

		public int Count {
			get {
				return param_list.Count;
			}
		}

		public int VisibleCount {
			get {
				int visible = 0;
				foreach (Parameter p in this) {
					if (!IsHidden (p))
						visible++;
				}
				return visible;
			}
		}

		public Parameter this [int idx] {
			get {
				return param_list [idx] as Parameter;
			}
		}

		public bool IsHidden (Parameter p)
		{
			int idx = param_list.IndexOf (p);

			if (idx > 0 && p.IsLength && p.PassAs == String.Empty && this [idx - 1].IsString)
				return true;

			if (p.IsCount && ((idx > 0 && this [idx - 1].IsArray) ||
					  (idx < Count - 1 && this [idx + 1].IsArray)))
				return true;

			if (p.CType == "GError**")
				return true;

			if (HasCB || HideData) {
				if (p.IsUserData && (idx == Count - 1))
                                        return true;
				if (p.IsUserData && (idx == Count - 2) && this [Count - 1] is ErrorParameter)
                                        return true;
				if (p.IsUserData && idx > 0 &&
				    this [idx - 1].Generatable is CallbackGen)
					return true;
				if (p.IsDestroyNotify && (idx == Count - 1) &&
				    this [idx - 1].IsUserData)
					return true;
			}

			return false;
		}

		bool has_cb;
		public bool HasCB {
			get { return has_cb; }
			set { has_cb = value; }
		}

		public bool HasOutParam {
			get {
				foreach (Parameter p in this)
					if (p.PassAs == "out")
						return true;
				return false;
			}
		}

		bool hide_data;
		public bool HideData {
			get { return hide_data; }
			set { hide_data = value; }
		}

		bool is_static;
		public bool Static {
			get { return is_static; }
			set { is_static = value; }
		}

		void Clear ()
		{
			elem = null;
			param_list.Clear ();
			param_list = null;
		}

		public IEnumerator GetEnumerator ()
		{
			return param_list.GetEnumerator ();
		}

		bool valid = false;

		public bool Validate ()
		{
			if (valid)
				return true;

			if (elem == null)
				return false;

			for (int i = first_is_instance ? 1 : 0; i < elem.ChildNodes.Count; i++) {
				XmlElement parm = elem.ChildNodes [i] as XmlElement;
				if (parm == null || parm.Name != "parameter")
					continue;
				Parameter p = new Parameter (parm);
				
				if (p.IsEllipsis) {
					Console.Write("Ellipsis parameter ");
					Clear ();
					return false;
				}

				if ((p.CSType == "") || (p.Name == "") || 
				    (p.MarshalType == "") || (SymbolTable.Table.CallByName(p.CType, p.Name) == "")) {
					Console.Write ("Invalid parameter {0} of type {1}", p.Name, p.CType);
					Clear ();
					return false;
				}

				IGeneratable gen = p.Generatable;

				if (p.IsArray) {
					p = new ArrayParameter (parm);
					if (i < elem.ChildNodes.Count - 1) {
						XmlElement next = elem.ChildNodes [i + 1] as XmlElement;
						if (next != null || next.Name == "parameter") {
							Parameter c = new Parameter (next);
							if (c.IsCount) {
								p = new ArrayCountPair (parm, next, false);
								i++;
							} 
						}
					}
				} else if (p.IsCount && i < elem.ChildNodes.Count - 1) {
					XmlElement next = elem.ChildNodes [i + 1] as XmlElement;
					if (next != null || next.Name == "parameter") {
						Parameter a = new Parameter (next);
						if (a.IsArray) {
							p = new ArrayCountPair (next, parm, true);
							i++;
						}
					}
				} else if (p.CType == "GError**")
					p = new ErrorParameter (parm);
				else if (gen is StructBase || gen is ByRefGen) {
					p = new StructParameter (parm);
				} else if (gen is CallbackGen) {
					has_cb = true;
				}
				param_list.Add (p);
			}

			if (has_cb && Count > 2 && this [Count - 3].Generatable is CallbackGen && this [Count - 2].IsUserData && this [Count - 1].IsDestroyNotify)
				this [Count - 3].Scope = "notified";

			valid = true;
			return true;
		}

		public bool IsAccessor {
			get {
				return VisibleCount == 1 && AccessorParam.PassAs == "out";
			}
		}

		public Parameter AccessorParam {
			get {
				foreach (Parameter p in this) {
					if (!IsHidden (p))
						return p;
				}
				return null;
			}
		}

		public string AccessorReturnType {
			get {
				Parameter p = AccessorParam;
				if (p != null)
					return p.CSType;
				else
					return null;
			}
		}

		public string AccessorName {
			get {
				Parameter p = AccessorParam;
				if (p != null)
					return p.Name;
				else
					return null;
			}
		}

		public string ImportSignature {
			get {
				if (Count == 0)
					return String.Empty;

				string[] result = new string [Count];
				for (int i = 0; i < Count; i++)
					result [i] = this [i].NativeSignature;

				return String.Join (", ", result);
			}
		}
	}
}

