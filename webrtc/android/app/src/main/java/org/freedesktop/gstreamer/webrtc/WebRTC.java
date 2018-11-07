/* GStreamer
 *
 * Copyright (C) 2014 Sebastian Dröge <sebastian@centricular.com>
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

package org.freedesktop.gstreamer.webrtc;

import android.app.Activity;
import android.content.Context;
import android.content.Intent;
import android.os.Bundle;
import android.os.PowerManager;
import android.util.Log;
import android.view.SurfaceHolder;
import android.view.View;
import android.view.View.OnClickListener;
import android.widget.ImageButton;
import android.widget.TextView;
import android.widget.Toast;

public class WebRTC extends Activity implements SurfaceHolder.Callback {
    private PowerManager.WakeLock wake_lock;
    private org.freedesktop.gstreamer.WebRTC webRTC;

    @Override
    public void onCreate(Bundle savedInstanceState)
    {
        super.onCreate(savedInstanceState);

        try {
            org.freedesktop.gstreamer.WebRTC.init(this);
        } catch (Exception e) {
            Toast.makeText(this, e.getMessage(), Toast.LENGTH_LONG).show();
            finish();
            return;
        }

        setContentView(R.layout.main);

        webRTC = new org.freedesktop.gstreamer.WebRTC();

        PowerManager pm = (PowerManager) getSystemService(Context.POWER_SERVICE);
        wake_lock = pm.newWakeLock(PowerManager.FULL_WAKE_LOCK, "GStreamer WebRTC");
        wake_lock.setReferenceCounted(false);

        final TextView URLText = (TextView) this.findViewById(R.id.URLText);
        final TextView IDText = (TextView) this.findViewById(R.id.IDText);
        final GStreamerSurfaceView gsv = (GStreamerSurfaceView) this.findViewById(R.id.surface_video);

        ImageButton play = (ImageButton) this.findViewById(R.id.button_play);
        play.setOnClickListener(new OnClickListener() {
            public void onClick(View v) {
                webRTC.setSignallingServer(URLText.getText().toString());
                webRTC.setCallID(IDText.getText().toString());
                webRTC.setSurface(gsv.getHolder().getSurface());
                webRTC.callOtherParty();
                wake_lock.acquire();
            }
        });

        ImageButton pause= (ImageButton) this.findViewById(R.id.button_pause);
        pause.setOnClickListener(new OnClickListener() {
            public void onClick(View v) {
                webRTC.endCall();
                wake_lock.release();
            }
        });

/*        webRTC.setVideoDimensionsChangedListener(new org.freedesktop.gstreamer.WebRTC.VideoDimensionsChangedListener() {
            public void videoDimensionsChanged(org.freedesktop.gstreamer.WebRTC webRTC, final int width, final int height) {
                runOnUiThread (new Runnable() {
                    public void run() {
                        Log.i ("GStreamer", "Media size changed to " + width + "x" + height);
                        gsv.media_width = width;
                        gsv.media_height = height;
                        runOnUiThread(new Runnable() {
                            public void run() {
                                gsv.requestLayout();
                            }
                        });
                    }
                });
            }
        });*/

        SurfaceHolder sh = gsv.getHolder();
        sh.addCallback(this);
    }

    protected void onDestroy() {
        webRTC.close();
        super.onDestroy();
    }

    public void surfaceChanged(SurfaceHolder holder, int format, int width,
            int height) {
        Log.d("GStreamer", "Surface changed to format " + format + " width "
                + width + " height " + height);
        webRTC.setSurface(holder.getSurface());
    }

    public void surfaceCreated(SurfaceHolder holder) {
        Log.d("GStreamer", "Surface created: " + holder.getSurface());
    }

    public void surfaceDestroyed(SurfaceHolder holder) {
        Log.d("GStreamer", "Surface destroyed");
        webRTC.setSurface(null);
    }
}
