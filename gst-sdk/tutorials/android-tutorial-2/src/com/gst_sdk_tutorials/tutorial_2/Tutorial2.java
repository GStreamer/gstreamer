package com.gst_sdk_tutorials.tutorial_2;

import android.app.Activity;
import android.os.Bundle;
import android.util.Log;
import android.view.View;
import android.view.View.OnClickListener;
import android.widget.ImageButton;
import android.widget.TextView;
import android.widget.Toast;

import com.gst_sdk.GStreamer;

public class Tutorial2 extends Activity {
    private native void nativeInit();
    private native void nativeFinalize();
    private native void nativePlay();
    private native void nativePause();
    private static native boolean classInit();
    private long native_custom_data;

    private boolean is_playing_desired;

    private Bundle initialization_data;

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

        initialization_data = savedInstanceState;

        /* Start with disabled buttons, until GStreamer is initialized */
        this.findViewById(R.id.button_play).setEnabled(false);
        this.findViewById(R.id.button_stop).setEnabled(false);
        is_playing_desired = false;

        nativeInit();
    }
    
    protected void onSaveInstanceState (Bundle outState) {
        Log.d ("GStreamer", "Saving state, playing:" + is_playing_desired);
        outState.putBoolean("playing", is_playing_desired);
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

    /* Called from native code */
    private void onGStreamerInitialized () {    	
        if (initialization_data != null) {
            is_playing_desired = initialization_data.getBoolean("playing");
            Log.i ("GStreamer", "Restoring state, playing:" + is_playing_desired);
        }

        /* Restore previous playing state */
        if (is_playing_desired) {
            nativePlay();
        } else {
            nativePause();
        }

        /* Re-enable buttons, now that GStreamer is initialized */
        this.findViewById(R.id.button_play).setEnabled(true);
        this.findViewById(R.id.button_stop).setEnabled(true);
    }

    static {
        System.loadLibrary("gstreamer_android");
        System.loadLibrary("tutorial-2");
        classInit();
    }

}
