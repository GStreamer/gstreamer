/*
 * Copyright (C) 2009 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
package com.gst_sdk_tutorials.tutorial_1;

import java.text.SimpleDateFormat;
import java.util.Date;
import java.util.TimeZone;

import android.app.Activity;
import android.util.Log;
import android.os.Bundle;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.View;
import android.view.View.OnClickListener;
import android.widget.ImageButton;
import android.widget.SeekBar;
import android.widget.SeekBar.OnSeekBarChangeListener;
import android.widget.TextView;

public class Tutorial1 extends Activity implements SurfaceHolder.Callback, OnSeekBarChangeListener {
    private native void nativeInit();
    private native void nativeFinalize();
    private native void nativePlay();
    private native void nativePause();
    private native void nativeSetPosition(int milliseconds);
    private static native void classInit();
    private native void nativeSurfaceInit(Object surface);
    private native void nativeSurfaceFinalize();
    private long native_custom_data;
    
    private boolean playing;
    private int position;
    private int duration;

    /* Called when the activity is first created. 
    @Override */
    public void onCreate(Bundle savedInstanceState)
    {
        super.onCreate(savedInstanceState);

        setContentView(R.layout.main);

        ImageButton play = (ImageButton) this.findViewById(R.id.button_play);
        play.setOnClickListener(new OnClickListener() {
            public void onClick(View v) {
                nativePlay();
                playing = true;
            }
        });

        ImageButton pause = (ImageButton) this.findViewById(R.id.button_stop);
        pause.setOnClickListener(new OnClickListener() {
            public void onClick(View v) {
                nativePause();
                playing = false;
            }
        });

        SurfaceView sv = (SurfaceView) this.findViewById(R.id.surface_video);
        SurfaceHolder sh = sv.getHolder();
        sh.addCallback(this);

        SeekBar sb = (SeekBar) this.findViewById(R.id.seek_bar);
        sb.setOnSeekBarChangeListener(this);

        nativeInit();

        playing = savedInstanceState==null ? false : savedInstanceState.getBoolean("playing");
        if (playing) {
            int milliseconds = savedInstanceState.getInt("position");
            Log.i ("GStreamer", "Restoring to playing state at " + milliseconds + " ms.");
            nativePlay();
            nativeSetPosition(milliseconds);
        }
    }
    
    protected void onSaveInstanceState (Bundle outState) {
        Log.d ("GStreamer", "Saving state, playing:" + playing + " position:" + position);
        outState.putBoolean("playing", playing);
        outState.putInt("position", position);
        outState.putInt("duration", duration);
    }

    protected void onDestroy() {
        nativeFinalize();
        super.onDestroy();
    }

    private void setMessage(final String message) {
        final TextView tv = (TextView) this.findViewById(R.id.textview_message);
        runOnUiThread (new Runnable() {
          public void run() {
            tv.setText(message);
          }
        });
    }

    private void setCurrentPosition(final int position, final int duration) {
        final TextView tv = (TextView) this.findViewById(R.id.textview_time);
        final SeekBar sb = (SeekBar) this.findViewById(R.id.seek_bar);
        SimpleDateFormat df = new SimpleDateFormat("HH:mm:ss");
        df.setTimeZone(TimeZone.getTimeZone("UTC"));
        final String message = df.format(new Date (position)) + " / " + df.format(new Date (duration));
        runOnUiThread (new Runnable() {
          public void run() {
            tv.setText(message);
            sb.setMax(duration);
            sb.setProgress(position);
          }
        });
        this.position = position;
        this.duration = duration;
    }

    static {
        System.loadLibrary("gstreamer_android");
        System.loadLibrary("tutorial-1");
        classInit();
    }

    public void surfaceChanged(SurfaceHolder holder, int format, int width,
            int height) {
        Log.d("GStreamer", "Surface changed to format " + format + " width "
                + width + " height " + height);
        nativeSurfaceInit (holder.getSurface());
    }

    public void surfaceCreated(SurfaceHolder holder) {
        Log.d("GStreamer", "Surface created: " + holder.getSurface());
    }

    public void surfaceDestroyed(SurfaceHolder holder) {
        Log.d("GStreamer", "Surface destroyed");
        nativeSurfaceFinalize ();
    }

    public void onProgressChanged(SeekBar sb, int progress, boolean fromUser) {
        if (fromUser == false) return;
        nativeSetPosition(progress);
    }

    public void onStartTrackingTouch(SeekBar sb) {
        nativePause();
    }

    public void onStopTrackingTouch(SeekBar sb) {
        if (playing) nativePlay();
    }
}
