/* GStreamer
 *
 * Copyright (C) 2014-2015 Matthew Waters <matthew@centricular.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

package org.freedesktop.gstreamer;

import java.io.Closeable;
import android.view.Surface;
import android.content.Context;
import org.freedesktop.gstreamer.GStreamer;

public class WebRTC implements Closeable {
    private static native void nativeClassInit();
    public static void init(Context context) throws Exception {
        System.loadLibrary("gstreamer_android");
        GStreamer.init(context);

        System.loadLibrary("gstwebrtc");
        nativeClassInit();
    }

    private long native_webrtc;
    private native void nativeNew();
    public WebRTC() {
        nativeNew();
    }

    private native void nativeFree();
    @Override
    public void close() {
        nativeFree();
    }

    private Surface surface;
    private native void nativeSetSurface(Surface surface);
    public void setSurface(Surface surface) {
        this.surface = surface;
        nativeSetSurface(surface);
    }

    public Surface getSurface() {
        return surface;
    }

    private String signallingServer;
    private native void nativeSetSignallingServer(String server);
    public void setSignallingServer(String server) {
        this.signallingServer = server;
        nativeSetSignallingServer(server);
    }

    public String getSignallingServer() {
        return this.signallingServer;
    }

    private String callID;
    private native void nativeSetCallID(String ID);
    public void setCallID(String ID) {
        this.callID = ID;
        nativeSetCallID(ID);
    }

    public String getCallID() {
        return this.callID;
    }

    private native void nativeCallOtherParty();
    public void callOtherParty() {
        nativeCallOtherParty();
    }

    private native void nativeEndCall();
    public void endCall() {
        nativeEndCall();
    }
}
