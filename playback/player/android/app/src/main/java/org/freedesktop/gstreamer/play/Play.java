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

package org.freedesktop.gstreamer.play;

import java.text.SimpleDateFormat;
import java.util.Date;
import java.util.TimeZone;

import android.content.Context;
import android.content.Intent;
import android.os.Bundle;
import android.os.PowerManager;
import android.util.Log;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.View;
import android.view.View.OnClickListener;
import android.widget.ImageButton;
import android.widget.SeekBar;
import android.widget.SeekBar.OnSeekBarChangeListener;
import android.widget.TextView;
import android.widget.Toast;
import android.support.v7.app.AppCompatActivity;

import org.freedesktop.gstreamer.Player;

public class Play extends AppCompatActivity implements SurfaceHolder.Callback, OnSeekBarChangeListener {
    private PowerManager.WakeLock wake_lock;
    private Player player;

    @Override
    public void onCreate(Bundle savedInstanceState)
    {
        super.onCreate(savedInstanceState);

        try {
            Player.init(this);
        } catch (Exception e) {
            Toast.makeText(this, e.getMessage(), Toast.LENGTH_LONG).show();
            finish();
            return;
        }

        setContentView(R.layout.main);

        player = new Player();

        PowerManager pm = (PowerManager) getSystemService(Context.POWER_SERVICE);
        wake_lock = pm.newWakeLock(PowerManager.FULL_WAKE_LOCK, "GStreamer Play");
        wake_lock.setReferenceCounted(false);

        ImageButton play = (ImageButton) this.findViewById(R.id.button_play);
        play.setOnClickListener(new OnClickListener() {
            public void onClick(View v) {
                player.play();
                wake_lock.acquire();
            }
        });

        ImageButton pause = (ImageButton) this.findViewById(R.id.button_pause);
        pause.setOnClickListener(new OnClickListener() {
            public void onClick(View v) {
                player.pause();
                wake_lock.release();
            }
        });

        final SeekBar sb = (SeekBar) this.findViewById(R.id.seek_bar);
        sb.setOnSeekBarChangeListener(this);

        player.setPositionUpdatedListener(new Player.PositionUpdatedListener() {
            public void positionUpdated(Player player, final long position) {
                runOnUiThread (new Runnable() {
                    public void run() {
                        sb.setProgress((int) (position / 1000000));
                        updateTimeWidget();
                    }
                });
            }
        });

        player.setDurationChangedListener(new Player.DurationChangedListener() {
            public void durationChanged(Player player, final long duration) {
                runOnUiThread (new Runnable() {
                    public void run() {
                        sb.setMax((int) (duration / 1000000));
                        updateTimeWidget();
                    }
                });
            }
        });

        final GStreamerSurfaceView gsv = (GStreamerSurfaceView) this.findViewById(R.id.surface_video);
        player.setVideoDimensionsChangedListener(new Player.VideoDimensionsChangedListener() {
            public void videoDimensionsChanged(Player player, final int width, final int height) {
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
        });

        SurfaceHolder sh = gsv.getHolder();
        sh.addCallback(this);

        String mediaUri = null;
        Intent intent = getIntent();
        android.net.Uri uri = intent.getData();
        Log.i ("GStreamer", "Received URI: " + uri);
        if (uri != null) {
            if (uri.getScheme().equals("content")) {
                android.database.Cursor cursor = getContentResolver().query(uri, null, null, null, null);
                cursor.moveToFirst();
                mediaUri = "file://" + cursor.getString(cursor.getColumnIndex(android.provider.MediaStore.Video.Media.DATA));
                cursor.close();
            } else {
                mediaUri = uri.toString();
            }
            player.setUri(mediaUri);
        }

        updateTimeWidget();
    }

    protected void onDestroy() {
        player.close();
        super.onDestroy();
    }

    private void updateTimeWidget () {
        final TextView tv = (TextView) this.findViewById(R.id.textview_time);
        final SeekBar sb = (SeekBar) this.findViewById(R.id.seek_bar);
        final int pos = sb.getProgress();
        final int max = sb.getMax();

        SimpleDateFormat df = new SimpleDateFormat("HH:mm:ss");
        df.setTimeZone(TimeZone.getTimeZone("UTC"));
        final String message = df.format(new Date (pos)) + " / " + df.format(new Date (max));
        tv.setText(message);
    }

    public void surfaceChanged(SurfaceHolder holder, int format, int width,
            int height) {
        Log.d("GStreamer", "Surface changed to format " + format + " width "
                + width + " height " + height);
        player.setSurface(holder.getSurface());
    }

    public void surfaceCreated(SurfaceHolder holder) {
        Log.d("GStreamer", "Surface created: " + holder.getSurface());
    }

    public void surfaceDestroyed(SurfaceHolder holder) {
        Log.d("GStreamer", "Surface destroyed");
        player.setSurface(null);
    }

    public void onProgressChanged(SeekBar sb, int progress, boolean fromUser) {
        if (!fromUser) return;

        updateTimeWidget();
    }

    public void onStartTrackingTouch(SeekBar sb) {
    }

    public void onStopTrackingTouch(SeekBar sb) {
        Log.d("GStreamer", "Seek to " + sb.getProgress());
        player.seek(((long) sb.getProgress()) * 1000000);
    }
}
