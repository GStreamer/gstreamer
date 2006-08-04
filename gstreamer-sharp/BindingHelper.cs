//
// BindingHelper.cs: Utility methods to make creating
//   element bindings by hand an easier task 
//
// Authors:
//   Aaron Bockover (abockover@novell.com)
//
// (C) 2006 Novell, Inc.
//

using System;
using GLib;

namespace Gst
{
    public static class BindingHelper
    {
        public static Delegate AddProxySignalDelegate(Element element, string signal, 
            GLib.DynamicSignalHandler baseHandler, Delegate existingHandler, Delegate addHandler)
        {
            if(existingHandler == null) {
                element.Connect(signal, baseHandler);
            }
                
            return Delegate.Combine(existingHandler, addHandler); 
        }
        
        public static Delegate RemoveProxySignalDelegate(Element element, string signal, 
            GLib.DynamicSignalHandler baseHandler, Delegate existingHandler, Delegate removeHandler)
        {
            Delegate temp_delegate = Delegate.Remove(existingHandler, removeHandler);
            if(temp_delegate == null) {
                element.Disconnect(signal, baseHandler);
            }
            
            return temp_delegate;
        }
        
        public static void InvokeProxySignalDelegate(Delegate raiseDelegate, Type type, 
            object o, GLib.SignalArgs args)
        {
            if(!type.IsSubclassOf(typeof(GLib.SignalArgs))) {
                throw new ArgumentException("Args type must derive SignalArgs");
            }
            
            if(raiseDelegate != null) {
                raiseDelegate.DynamicInvoke(new object [] { o, 
                    Activator.CreateInstance(type, new object [] { args }) });
            }
        }
    }
}
