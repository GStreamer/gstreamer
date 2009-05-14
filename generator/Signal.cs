// GtkSharp.Generation.Signal.cs - The Signal Generatable.
//
// Author: Mike Kestner <mkestner@speakeasy.net>
//
// Copyright (c) 2001-2003 Mike Kestner 
// Copyright (c) 2003-2005 Novell, Inc.
// Copyright (c) 2007 Novell, Inc.
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

	public class Signal {

		bool marshaled;
		string name;
		XmlElement elem;
		ReturnValue retval;
		Parameters parms;
		ObjectBase container_type;

		public Signal (XmlElement elem, ObjectBase container_type)
		{
			this.elem = elem;
			name = elem.GetAttribute ("name");
			marshaled = elem.GetAttribute ("manual") == "true";
			retval = new ReturnValue (elem ["return-type"]);
			parms = new Parameters (elem["parameters"], container_type.ParserVersion == 1 ? true : false);
			this.container_type = container_type;
		}

		bool Marshaled {
			get { return marshaled; }
		}

		public string Name {
			get {
				return name; 
			}
			set {
				name = value;
			}
		}

		public bool Validate ()
		{
			if (Name == "") {
				Console.Write ("Nameless signal ");
				Statistics.ThrottledCount++;
				return false;
			}
			
			if (!parms.Validate () || !retval.Validate ()) {
				Console.Write (" in signal " + Name + " ");
				Statistics.ThrottledCount++;
				return false;
			}

			return true;
		}

 		public void GenerateDecl (StreamWriter sw)
 		{
			if (elem.HasAttribute("new_flag") || (container_type != null && container_type.GetSignalRecursively (Name) != null))
				sw.Write("new ");

 			sw.WriteLine ("\t\tevent " + EventHandlerQualifiedName + " " + Name + ";");
		}

		public string CName {
			get {
				return "\"" + elem.GetAttribute("cname") + "\"";
			}
		}

		string CallbackSig {
			get {
				string result = "";
				for (int i = 0; i < parms.Count; i++) {
					if (i > 0)
						result += ", ";

					Parameter p = parms [i];
					if (p.PassAs != "" && !(p.Generatable is StructBase))
						result += p.PassAs + " ";
					result += (p.MarshalType + " arg" + i);
				}

				return result;
			}
		}

		string CallbackName {
			get { return Name + "SignalCallback"; }
		}

		string DelegateName {
			get { return Name + "SignalDelegate"; }
		}

                private string EventArgsName {
                        get {
                                if (IsEventHandler)
                                        return "EventArgs";
                                else
                                        return Name + "Args";
                        }
                }
                                                                                                                        
                private string EventArgsQualifiedName {
                        get {
                                if (IsEventHandler)
                                        return "System.EventArgs";
                                else
                                        return container_type.NS + "." + Name + "Args";
                        }
                }
                                                                                                                        
                private string EventHandlerName {
                        get {
                                if (IsEventHandler)
                                        return "EventHandler";
                                else if (SymbolTable.Table [container_type.NS + Name + "Handler"] != null)
                                        return Name + "EventHandler";
				else
                                        return Name + "Handler";
                        }
                }
                                                                                                                        
                private string EventHandlerQualifiedName {
                        get {
                                if (IsEventHandler)
                                        return "System.EventHandler";
                                else
                                        return container_type.NS + "." + EventHandlerName;
                        }
                }

		private bool IsEventHandler {
			get {
				return retval.CSType == "void" && parms.Count == 0;
			}
		}

		public string GenArgsInitialization (StreamWriter sw)
		{
			if (parms.Count > 0)
				sw.WriteLine("\t\t\t\targs.Args = new object[" + parms.Count + "];");
			string finish = "";
			for (int idx = 0; idx < parms.Count; idx++) {
				Parameter p = parms [idx];
				IGeneratable igen = p.Generatable;
				if (p.PassAs != "out") {
					if (igen is ManualGen) {
						sw.WriteLine("\t\t\t\tif (arg{0} == IntPtr.Zero)", idx);
						sw.WriteLine("\t\t\t\t\targs.Args[{0}] = null;", idx);
						sw.WriteLine("\t\t\t\telse {");
						sw.WriteLine("\t\t\t\t\targs.Args[" + idx + "] = " + p.FromNative ("arg" + idx)  + ";");
						sw.WriteLine("\t\t\t\t}");
					} else
						sw.WriteLine("\t\t\t\targs.Args[" + idx + "] = " + p.FromNative ("arg" + idx)  + ";");
				}
				if (igen is StructBase && p.PassAs == "ref")
					finish += "\t\t\t\tif (arg" + idx + " != IntPtr.Zero) System.Runtime.InteropServices.Marshal.StructureToPtr (args.Args[" + (idx-1) + "], arg" + idx + ", false);\n";
				else if (p.PassAs != "")
					finish += "\t\t\t\targ" + idx + " = " + igen.ToNativeReturn ("((" + p.CSType + ")args.Args[" + (idx - 1) + "])") + ";\n";
			}
			return finish;
		}

		public void GenArgsCleanup (StreamWriter sw, string finish)
		{
			if (retval.IsVoid && finish.Length == 0)
				return;

			sw.WriteLine("\n\t\t\ttry {");
			sw.Write (finish);
			if (!retval.IsVoid) {
				if (retval.CSType == "bool") {
					sw.WriteLine ("\t\t\t\tif (args.RetVal == null)");
					sw.WriteLine ("\t\t\t\t\treturn false;");
				}
				sw.WriteLine("\t\t\t\treturn " + SymbolTable.Table.ToNativeReturn (retval.CType, "((" + retval.CSType + ")args.RetVal)") + ";");
			}
			sw.WriteLine("\t\t\t} catch (Exception) {");
			sw.WriteLine ("\t\t\t\tException ex = new Exception (\"args.RetVal or 'out' property unset or set to incorrect type in " + EventHandlerQualifiedName + " callback\");");
			sw.WriteLine("\t\t\t\tGLib.ExceptionManager.RaiseUnhandledException (ex, true);");
			
			sw.WriteLine ("\t\t\t\t// NOTREACHED: above call doesn't return.");
			sw.WriteLine ("\t\t\t\tthrow ex;");
			sw.WriteLine("\t\t\t}");
		}

		public void GenCallback (StreamWriter sw)
		{
			if (IsEventHandler)
				return;

			string native_signature = "IntPtr inst";
			if (parms.Count > 0)
				native_signature += ", " + CallbackSig;
			native_signature += ", IntPtr gch";

			sw.WriteLine ("\t\t[GLib.CDeclCallback]");
			sw.WriteLine ("\t\tdelegate {0} {1} ({2});", retval.ToNativeType, DelegateName, native_signature);
			sw.WriteLine ();
			sw.WriteLine ("\t\tstatic {0} {1} ({2})", retval.ToNativeType, CallbackName, native_signature);
			sw.WriteLine("\t\t{");
			sw.WriteLine("\t\t\t{0} args = new {0} ();", EventArgsQualifiedName);
			sw.WriteLine("\t\t\ttry {");
			sw.WriteLine("\t\t\t\tGLib.Signal sig = ((GCHandle) gch).Target as GLib.Signal;");
			sw.WriteLine("\t\t\t\tif (sig == null)");
			sw.WriteLine("\t\t\t\t\tthrow new Exception(\"Unknown signal GC handle received \" + gch);");
			sw.WriteLine();
			string finish = GenArgsInitialization (sw);
			sw.WriteLine("\t\t\t\t{0} handler = ({0}) sig.Handler;", EventHandlerQualifiedName);
			sw.WriteLine("\t\t\t\thandler (GLib.Object.GetObject (inst), args);");
			sw.WriteLine("\t\t\t} catch (Exception e) {");
			sw.WriteLine("\t\t\t\tGLib.ExceptionManager.RaiseUnhandledException (e, false);");
			sw.WriteLine("\t\t\t}");
			GenArgsCleanup (sw, finish);
			sw.WriteLine("\t\t}");
			sw.WriteLine();
		}

		private bool NeedNew (ObjectBase implementor)
		{
			return elem.HasAttribute ("new_flag") ||
				(container_type != null && container_type.GetSignalRecursively (Name) != null) ||
				(implementor != null && implementor.GetSignalRecursively (Name) != null);
		}

		public void GenEventHandler (GenerationInfo gen_info)
		{
			if (IsEventHandler)
				return;

			string ns = container_type.NS;

			StreamWriter sw = gen_info.OpenStream (EventHandlerName);
			
			sw.WriteLine ("namespace " + ns + " {");
			sw.WriteLine ();
			sw.WriteLine ("\tusing System;");

			sw.WriteLine ();
			sw.WriteLine ("\tpublic delegate void " + EventHandlerName + "(object o, " + EventArgsName + " args);");
			sw.WriteLine ();
			sw.WriteLine ("\tpublic class " + EventArgsName + " : GLib.SignalArgs {");
			for (int i = 0; i < parms.Count; i++) {
				sw.WriteLine ("\t\tpublic " + parms[i].CSType + " " + parms[i].StudlyName + "{");
				if (parms[i].PassAs != "out") {
					sw.WriteLine ("\t\t\tget {");
					sw.WriteLine ("\t\t\t\treturn (" + parms[i].CSType + ") Args[" + i + "];");
					sw.WriteLine ("\t\t\t}");
				}
				if (parms[i].PassAs != "") {
					sw.WriteLine ("\t\t\tset {");
					sw.WriteLine ("\t\t\t\tArgs[" + i + "] = (" + parms[i].CSType + ")value;");
					sw.WriteLine ("\t\t\t}");
				}
				sw.WriteLine ("\t\t}");
				sw.WriteLine ();
			}
			sw.WriteLine ("\t}");
			sw.WriteLine ("}");
			sw.Close ();
		}

		public void GenEvent (StreamWriter sw, ObjectBase implementor, string target)
		{
			string args_type = IsEventHandler ? "" : ", typeof (" + EventArgsQualifiedName + ")";
			
			if (Marshaled) {
				GenCallback (sw);
				args_type = ", new " + DelegateName + "(" + CallbackName + ")";
			}

			sw.WriteLine("\t\t[GLib.Signal("+ CName + ")]");
			sw.Write("\t\tpublic ");
			if (NeedNew (implementor))
				sw.Write("new ");
			sw.WriteLine("event " + EventHandlerQualifiedName + " " + Name + " {");
			sw.WriteLine("\t\t\tadd {");
			sw.WriteLine("\t\t\t\tGLib.Signal sig = GLib.Signal.Lookup (" + target + ", " + CName + args_type + ");");
			sw.WriteLine("\t\t\t\tsig.AddDelegate (value);");
			sw.WriteLine("\t\t\t}");
			sw.WriteLine("\t\t\tremove {");
			sw.WriteLine("\t\t\t\tGLib.Signal sig = GLib.Signal.Lookup (" + target + ", " + CName + args_type + ");");
			sw.WriteLine("\t\t\t\tsig.RemoveDelegate (value);");
			sw.WriteLine("\t\t\t}");
			sw.WriteLine("\t\t}");
			sw.WriteLine();
		}

		public void Generate (GenerationInfo gen_info, ObjectBase implementor)
		{
			StreamWriter sw = gen_info.Writer;

			if (implementor == null)
				GenEventHandler (gen_info);

			GenEvent (sw, implementor, "this");
			
			Statistics.SignalCount++;
		}
	}
}

