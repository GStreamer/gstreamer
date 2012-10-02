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

import com.gst_sdk.GStreamer;
import com.lamerman.FileDialog;
import com.lamerman.SelectionMode;

import android.app.Activity;
import android.content.Intent;
import android.util.Log;
import android.os.Bundle;
import android.os.Environment;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.View;
import android.view.View.OnClickListener;
import android.widget.ImageButton;
import android.widget.SeekBar;
import android.widget.SeekBar.OnSeekBarChangeListener;
import android.widget.TextView;
import android.widget.Toast;

public class Tutorial1 extends Activity implements SurfaceHolder.Callback, OnSeekBarChangeListener {
    private native void nativeInit();
    private native void nativeFinalize();
    private native void nativeSetUri(String uri);
    private native void nativePlay();
    private native void nativePause();
    private native void nativeSetPosition(int milliseconds);
    private static native boolean classInit();
    private native void nativeSurfaceInit(Object surface);
    private native void nativeSurfaceFinalize();
    private long native_custom_data;

    private boolean is_playing_desired;
    private int position;
    private int duration;
    private boolean is_local_media;
    private int desired_position;

    private Bundle initialization_data;
    
    private String mediaUri = "http://docs.gstreamer.com/media/sintel_trailer-480p.ogv";
    static private final int PICK_FILE_CODE = 1;
    
    /* Called when the activity is first created. */
    @Override
    public void onCreate(Bundle savedInstanceState)
    {
        super.onCreate(savedInstanceState);

        try {
        GStreamer.init(this);
        } catch (Exception e) {
            Toast.makeText(this, e.getMessage(), Toast.LENGTH_LONG).show();
            finish(); 
            return;
        }

        setContentView(R.layout.main);

        ImageButton play = (ImageButton) this.findViewById(R.id.button_play);
        play.setOnClickListener(new OnClickListener() {
            public void onClick(View v) {
                is_playing_desired = true;
                nativePlay();
            }
        });

        ImageButton pause = (ImageButton) this.findViewById(R.id.button_stop);
        pause.setOnClickListener(new OnClickListener() {
            public void onClick(View v) {
                is_playing_desired = false;
                nativePause();
            }
        });
        
        ImageButton select = (ImageButton) this.findViewById(R.id.button_select);
        select.setOnClickListener(new OnClickListener() {
            public void onClick(View v) {
            	Intent i = new Intent(getBaseContext(), FileDialog.class);
            	i.putExtra(FileDialog.START_PATH, Environment.getExternalStoragePublicDirectory(Environment.DIRECTORY_MOVIES).getAbsolutePath());
            	i.putExtra(FileDialog.SELECTION_MODE, SelectionMode.MODE_OPEN);
            	startActivityForResult(i, PICK_FILE_CODE);
            }
        });

        SurfaceView sv = (SurfaceView) this.findViewById(R.id.surface_video);
        SurfaceHolder sh = sv.getHolder();
        sh.addCallback(this);

        SeekBar sb = (SeekBar) this.findViewById(R.id.seek_bar);
        sb.setOnSeekBarChangeListener(this);

        initialization_data = savedInstanceState;
        
        is_local_media = false;
        is_playing_desired = false;

        nativeInit();
    }
    
    protected void onSaveInstanceState (Bundle outState) {
        Log.d ("GStreamer", "Saving state, playing:" + is_playing_desired + " position:" + position + " uri: " + mediaUri);
        outState.putBoolean("playing", is_playing_desired);
        outState.putInt("position", position);
        outState.putInt("duration", duration);
        outState.putString("mediaUri", mediaUri);
    }

    protected void onDestroy() {
        nativeFinalize();
        super.onDestroy();
    }

    /* Called from native code */
    private void setMessage(final String message) {
        final TextView tv = (TextView) this.findViewById(R.id.textview_message);
        runOnUiThread (new Runnable() {
          public void run() {
            tv.setText(message);
          }
        });
    }
    
    private void setMediaUri() {
        nativeSetUri (mediaUri);
        if (mediaUri.startsWith("file://")) is_local_media = true;
    }

    /* Called from native code */
    private void onGStreamerInitialized () {    	
        if (initialization_data != null) {
            is_playing_desired = initialization_data.getBoolean("playing");
            int milliseconds = initialization_data.getInt("position");
            Log.i ("GStreamer", "Restoring state, playing:" + is_playing_desired + " position:" + milliseconds + " ms.");
            mediaUri = initialization_data.getString ("mediaUri");
            /* Actually, move to one millisecond in the future. Otherwise, due to rounding errors between the
             * milliseconds used here and the nanoseconds used by GStreamer, we would be jumping a bit behind
             * where we were before. This, combined with seeking to keyframe positions, would skip one keyframe
             * backwards on each iteration.
             */
            nativeSetPosition(milliseconds + 1);
        }
        
        setMediaUri ();
        if (is_playing_desired) {
            nativePlay();
        } else {
            nativePause();
        }
    }

    /* The text widget acts as an slave for the seek bar, so it reflects what the seek bar shows, whether
     * it is an actual pipeline position or the position the user is currently dragging to.
     */
    private void updateTimeWidget () {
        final TextView tv = (TextView) this.findViewById(R.id.textview_time);
        final SeekBar sb = (SeekBar) this.findViewById(R.id.seek_bar);
        final int pos = sb.getProgress();

        SimpleDateFormat df = new SimpleDateFormat("HH:mm:ss");
        df.setTimeZone(TimeZone.getTimeZone("UTC"));
        final String message = df.format(new Date (pos)) + " / " + df.format(new Date (duration));
        tv.setText(message);        
    }

    /* Called from native code */
    private void setCurrentPosition(final int position, final int duration) {
        final SeekBar sb = (SeekBar) this.findViewById(R.id.seek_bar);
        
        /* Ignore position messages from the pipeline if the seek bar is being dragged */
        if (sb.isPressed()) return;

        runOnUiThread (new Runnable() {
          public void run() {
            sb.setMax(duration);
            sb.setProgress(position);
            updateTimeWidget();
          }
        });
        this.position = position;
        this.duration = duration;
    }

    /* Called from native code */
    private void setCurrentState (int state) {
        Log.d ("GStreamer", "State has changed to " + state);
        switch (state) {
        case 1:
            setMessage ("NULL");
            break;
        case 2:
            setMessage ("READY");
            break;
        case 3:
            setMessage ("PAUSED");
            break;
        case 4:
            setMessage ("PLAYING");
            break;
        }
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
        desired_position = progress;
        /* If this is a local file, allow scrub seeking, this is, seek soon as the slider
         * is moved.
         */
        if (is_local_media) nativeSetPosition(desired_position);
        updateTimeWidget();
    }

    public void onStartTrackingTouch(SeekBar sb) {
        nativePause();
    }

    public void onStopTrackingTouch(SeekBar sb) {
        /* If this is a remote file, scrub seeking is probably not going to work smoothly enough.
         * Therefore, perform only the seek when the slider is released.
         */
        if (!is_local_media) nativeSetPosition(desired_position);
        if (is_playing_desired) nativePlay();
    }
    
    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data)
    {
    	if (resultCode == RESULT_OK && requestCode == PICK_FILE_CODE) {
    		mediaUri = "file://" + data.getStringExtra(FileDialog.RESULT_PATH);
    		setMediaUri();
    	}
    } 
}
