
#ifndef __GST_DVBSRC_H__
#define __GST_DVBSRC_H__

#include <gst/gst.h>
#include <gst/base/gstpushsrc.h>

G_BEGIN_DECLS

 typedef enum
{
  DVB_POL_H,
  DVB_POL_V,
  DVB_POL_ZERO
} GstDvbSrcPol;


#define IPACKS 2048
#define TS_SIZE 188
#define IN_SIZE TS_SIZE*10

#define MAX_FILTERS 32

#define GST_TYPE_DVBSRC \
  (gst_dvbsrc_get_type())
#define GST_DVBSRC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_DVBSRC,GstDvbSrc))
#define GST_DVBSRC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_DVBSRC,GstDvbSrcClass))
#define GST_IS_DVBSRC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_DVBSRC))
#define GST_IS_DVBSRC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_DVBSRC))

typedef struct _GstDvbSrc GstDvbSrc;
typedef struct _GstDvbSrcClass GstDvbSrcClass;
typedef struct _GstDvbSrcParam GstDvbSrcParam;

struct _GstDvbSrc
{
  GstPushSrc element;
  GstPad *srcpad;

  GMutex tune_mutex;
  gboolean need_tune;

  int adapter_type;

  int adapter_number;           /* the device directory with the sub-devices */
  int frontend_number;

  int fd_frontend;
  int fd_dvr;
  int fd_filters[MAX_FILTERS];
  GstPoll *poll;
  GstPollFD poll_fd_dvr;

  guint16 pids[MAX_FILTERS];
  unsigned int freq;
  unsigned int sym_rate;
  int tone;
  int diseqc_src;
  gboolean send_diseqc;

  int bandwidth;
  int code_rate_hp;
  int code_rate_lp;
  int modulation;
  int guard_interval;
  int transmission_mode;
  int hierarchy_information;
  int inversion;
  guint64 timeout;

  GstDvbSrcPol pol;
  guint stats_interval;
  guint stats_counter;
  gboolean need_unlock;

  guint dvb_buffer_size;
};

struct _GstDvbSrcClass
{
  GstPushSrcClass parent_class;

  void (*adapter_type) (GstElement * element, gint type);
  void (*signal_quality) (GstElement * element, gint strength, gint snr);
};


GType gst_dvbsrc_get_type (void);
gboolean gst_dvbsrc_plugin_init (GstPlugin * plugin);

G_END_DECLS
#endif /* __GST_DVBSRC_H__ */
