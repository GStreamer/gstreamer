#ifndef __GST_TYPES_H__
#define __GST_TYPES_H__

typedef struct _GstObject GstObject;
typedef struct _GstObjectClass GstObjectClass;
typedef struct _GstPad GstPad;
typedef struct _GstPadClass GstPadClass;
typedef struct _GstPadTemplate GstPadTemplate;
typedef struct _GstPadTemplateClass GstPadTemplateClass;
typedef struct _GstElement GstElement;
typedef struct _GstElementClass GstElementClass;
typedef struct _GstBin GstBin;
typedef struct _GstBinClass GstBinClass;
typedef struct _GstScheduler GstScheduler;
typedef struct _GstSchedulerClass GstSchedulerClass;

typedef enum {
  GST_STATE_VOID_PENDING        = 0,
  GST_STATE_NULL                = (1 << 0),
  GST_STATE_READY               = (1 << 1),
  GST_STATE_PAUSED              = (1 << 2),
  GST_STATE_PLAYING             = (1 << 3),
} GstElementState;

typedef enum {
  GST_STATE_FAILURE             = 0,
  GST_STATE_SUCCESS             = 1,
  GST_STATE_ASYNC               = 2,
} GstElementStateReturn;


#endif
