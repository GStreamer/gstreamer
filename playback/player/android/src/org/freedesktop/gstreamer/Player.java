/* GStreamer
 *
 * Copyright (C) 2014-2015 Sebastian Dröge <sebastian@centricular.com>
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

public class Player implements Closeable {
    private static native void nativeClassInit();
    public static void init(Context context) throws Exception {
        System.loadLibrary("gstreamer_android");
        GStreamer.init(context);

        System.loadLibrary("gstplayer");
        nativeClassInit();
    }

    private long native_player;
    private native void nativeNew();
    public Player() {
        nativeNew();
    }

    private native void nativeFree();
    @Override
    public void close() {
        nativeFree();
    }

    private native void nativePlay();
    public void play() {
        nativePlay();
    }

    private native void nativePause();
    public void pause() {
        nativePause();
    }

    private native void nativeStop();
    public void stop() {
        nativeStop();
    }

    private native void nativeSeek(long position);
    public void seek(long position) {
        nativeSeek(position);
    }

    private native String nativeGetUri();
    public String getUri() {
        return nativeGetUri();
    }

    private native void nativeSetUri(String uri);
    public void setUri(String uri) {
        nativeSetUri(uri);
    }

    private native long nativeGetPosition();
    public long getPosition() {
        return nativeGetPosition();
    }

    private native long nativeGetDuration();
    public long getDuration() {
        return nativeGetDuration();
    }

    private native double nativeGetVolume();
    public double getVolume() {
        return nativeGetVolume();
    }

    private native void nativeSetVolume(double volume);
    public void setVolume(double volume) {
        nativeSetVolume(volume);
    }

    private native boolean nativeGetMute();
    public boolean getMute() {
        return nativeGetMute();
    }

    private native void nativeSetMute(boolean mute);
    public void setMute(boolean mute) {
        nativeSetMute(mute);
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

    public static interface PositionUpdatedListener {
        abstract void positionUpdated(Player player, long position);
    }

    private PositionUpdatedListener positionUpdatedListener;
    public void setPositionUpdatedListener(PositionUpdatedListener listener) {
        positionUpdatedListener = listener;
    }

    private void onPositionUpdated(long position) {
        if (positionUpdatedListener != null) {
            positionUpdatedListener.positionUpdated(this, position);
        }
    }

    public static interface DurationChangedListener {
        abstract void durationChanged(Player player, long duration);
    }

    private DurationChangedListener durationChangedListener;
    public void setDurationChangedListener(DurationChangedListener listener) {
        durationChangedListener = listener;
    }

    private void onDurationChanged(long duration) {
        if (durationChangedListener != null) {
            durationChangedListener.durationChanged(this, duration);
        }
    }

    private static final State[] stateMap = {State.STOPPED, State.BUFFERING, State.PAUSED, State.PLAYING};
    public enum State {
        STOPPED,
        BUFFERING,
        PAUSED,
        PLAYING
    }

    public static interface StateChangedListener {
        abstract void stateChanged(Player player, State state);
    }

    private StateChangedListener stateChangedListener;
    public void setStateChangedListener(StateChangedListener listener) {
        stateChangedListener = listener;
    }

    private void onStateChanged(int stateIdx) {
        if (stateChangedListener != null) {
            State state = stateMap[stateIdx];
            stateChangedListener.stateChanged(this, state);
        }
    }

    public static interface BufferingListener {
        abstract void buffering(Player player, int percent);
    }

    private BufferingListener bufferingListener;
    public void setBufferingListener(BufferingListener listener) {
        bufferingListener = listener;
    }

    private void onBuffering(int percent) {
        if (bufferingListener != null) {
            bufferingListener.buffering(this, percent);
        }
    }

    public static interface EndOfStreamListener {
        abstract void endOfStream(Player player);
    }

    private EndOfStreamListener endOfStreamListener;
    public void setEndOfStreamListener(EndOfStreamListener listener) {
        endOfStreamListener = listener;
    }

    private void onEndOfStream() {
        if (endOfStreamListener != null) {
            endOfStreamListener.endOfStream(this);
        }
    }

    // Keep these in sync with gstplayer.h
    private static final Error[] errorMap = {Error.FAILED};
    public enum Error {
        FAILED
    }

    public static interface ErrorListener {
        abstract void error(Player player, Error error, String errorMessage);
    }

    private ErrorListener errorListener;
    public void setErrorListener(ErrorListener listener) {
        errorListener = listener;
    }

    private void onError(int errorCode, String errorMessage) {
        if (errorListener != null) {
            Error error = errorMap[errorCode];
            errorListener.error(this, error, errorMessage);
        }
    }

    public static interface VideoDimensionsChangedListener {
        abstract void videoDimensionsChanged(Player player, int width, int height);
    }

    private VideoDimensionsChangedListener videoDimensionsChangedListener;
    public void setVideoDimensionsChangedListener(VideoDimensionsChangedListener listener) {
        videoDimensionsChangedListener = listener;
    }

    private void onVideoDimensionsChanged(int width, int height) {
        if (videoDimensionsChangedListener != null) {
            videoDimensionsChangedListener.videoDimensionsChanged(this, width, height);
        }
    }
}
