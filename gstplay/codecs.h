
/* mpeg1.c */
void mpeg1_new_pad_created(GstElement *parse,GstPad *pad,GstElement *pipeline); 
void mpeg1_setup_video_thread(GstPad *pad, GstElement *show, GstElement *pipeline);

/* mpeg2.c */
void mpeg2_new_pad_created(GstElement *parse,GstPad *pad,GstElement *pipeline); 
void mpeg2_setup_video_thread(GstPad *pad, GstElement *show, GstElement *pipeline);

/* avi.c */
void avi_new_pad_created(GstElement *parse,GstPad *pad,GstElement *pipeline); 
