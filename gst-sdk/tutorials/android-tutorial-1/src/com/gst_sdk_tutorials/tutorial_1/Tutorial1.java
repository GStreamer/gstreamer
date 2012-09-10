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

import android.app.Activity;
import android.os.Bundle;
import android.view.View;
import android.view.View.OnClickListener;
import android.widget.ImageButton;
import android.widget.TextView;

public class Tutorial1 extends Activity
{
    public native void nativeInit();
    public native void nativeFinalize();
    public native void nativePlay();
    public native void nativePause();
    private static native void classInit();
    private long native_custom_data;

    /* Called when the activity is first created. 
    @Override */
    public void onCreate(Bundle savedInstanceState)
    {
        super.onCreate(savedInstanceState);

        setContentView(R.layout.main);

        ImageButton play = (ImageButton)this.findViewById(R.id.button_play);
        play.setOnClickListener(new OnClickListener() {
          public void onClick(View v) {
        	  nativePlay();
          }
        });

        ImageButton pause = (ImageButton)this.findViewById(R.id.button_stop);
        pause.setOnClickListener(new OnClickListener() {
          public void onClick(View v) {
        	  nativePause();
          }
        });

        nativeInit();
    }

    protected void onDestroy () {
      nativeFinalize();
      super.onDestroy();
    }

    static {
        System.loadLibrary("gstreamer_android");
        System.loadLibrary("tutorial-1");
        classInit();
    }
}
