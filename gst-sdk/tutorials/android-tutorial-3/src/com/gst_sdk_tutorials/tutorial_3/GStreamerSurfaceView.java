package com.gst_sdk_tutorials.tutorial_3;

import android.content.Context;
import android.util.AttributeSet;
import android.view.SurfaceView;
import android.view.View;

// A simple SurfaceView whose width and height is set from the outside
public class GStreamerSurfaceView extends SurfaceView {
    public int media_width = 320;  // Default values, only really meaningful for the layout editor in Eclipse
    public int media_height = 200;

    // Mandatory constructors, they do not do much
    public GStreamerSurfaceView(Context context, AttributeSet attrs,
            int defStyle) {
        super(context, attrs, defStyle);
    }

    public GStreamerSurfaceView(Context context, AttributeSet attrs) {
        super(context, attrs);
    }

    public GStreamerSurfaceView (Context context) {
        super(context);
    }

    // Called by the layout manager to find out our size and give us some rules
    @Override
    protected void onMeasure(int widthMeasureSpec, int heightMeasureSpec) {
        int width = 0, height = 0;

        // Obey width rules
        switch (View.MeasureSpec.getMode(widthMeasureSpec)) {
        case View.MeasureSpec.AT_MOST:
            width = Math.min (View.MeasureSpec.getSize(widthMeasureSpec), media_width);
            break;
        case View.MeasureSpec.EXACTLY:
            width = View.MeasureSpec.getSize(widthMeasureSpec);
            break;
        case View.MeasureSpec.UNSPECIFIED:
            width = media_width;
        }

        // Obey height rules
        switch (View.MeasureSpec.getMode(heightMeasureSpec)) {
        case View.MeasureSpec.AT_MOST:
            height = Math.min (View.MeasureSpec.getSize(heightMeasureSpec), media_height);
            break;
        case View.MeasureSpec.EXACTLY:
            height = View.MeasureSpec.getSize(heightMeasureSpec);
            break;
        case View.MeasureSpec.UNSPECIFIED:
            height = media_height;
        }

        // Obey minimum size
        width = Math.max (getSuggestedMinimumWidth(), width);
        height = Math.max (getSuggestedMinimumHeight(), height);
        setMeasuredDimension(width, height);
    }

}
