
#include <glib.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <gst/gst.h>

#define INFINITY 1e10

typedef struct _Element Element;
typedef struct _Feature Feature;
typedef struct _ElementInfo ElementInfo;

enum
{
  PAD_SRC,
  PAD_SINK,
  FD,
  TIME
};

struct _Element
{
  char *name;
  GList *features;
  int type;
  gboolean init;
  gboolean idle;

  void (*iterate) (Element * element);
  void (*caps_iterate) (Element * element);

  int state;
};

struct _Feature
{
  char *name;
  int type;

  Element *parent;

  gboolean waiting;
  gboolean ready;

  /* for pads */
  gboolean bufpen;
  Feature *peer;
  GstCaps *caps;

  /* for fds */
  double next_time;
  double interval;
};

struct _ElementInfo
{
  char *type_name;
  void (*iterate) (Element * element);
  void (*caps_iterate) (Element * element);
};

GList *elements;

void run (void);
void dump (void);
void dump_element (Element * e);

Element *element_factory_make (const char *type);
Element *element_factory_make_full (const char *type, const char *name);
void element_link (const char *name1, const char *name2);
void element_link_full (const char *name1, const char *padname1,
    const char *name2, const char *padname2);
Element *element_get (const char *name);
gboolean element_ready (Element * e);
gboolean feature_is_ready (Feature * f);
double element_next_time (Element * e);

Feature *feature_create (Element * e, int type, const char *name);
Feature *feature_get (Element * e, const char *name);
gboolean feature_ready (Element * e, const char *name);

void fakesrc_iterate (Element * element);
void identity_iterate (Element * element);
void fakesink_iterate (Element * element);
void audiosink_iterate (Element * element);
void mad_iterate (Element * element);
void queue_iterate (Element * element);
void videosink_iterate (Element * element);
void tee_iterate (Element * element);

void fakesrc_caps (Element * element);
void identity_caps (Element * element);
void fakesink_caps (Element * element);
void audiosink_caps (Element * element);
void mad_caps (Element * element);
void queue_caps (Element * element);
void videosink_caps (Element * element);
void tee_caps (Element * element);

double time;

int
main (int argc, char *argv[])
{
  int x = 8;

  switch (x) {
    case 0:
      /* fakesrc ! fakesink */
      element_factory_make ("fakesrc");
      element_factory_make ("fakesink");
      element_link ("fakesrc", "fakesink");
      break;
    case 1:
      /* fakesrc ! identity ! fakesink */
      element_factory_make ("fakesrc");
      element_factory_make ("identity");
      element_factory_make ("fakesink");

      element_link ("fakesrc", "identity");
      element_link ("identity", "fakesink");
      break;
    case 2:
      /* fakesrc ! identity ! identity ! fakesink */
      element_factory_make ("fakesrc");
      element_factory_make_full ("identity", "identity0");
      element_factory_make_full ("identity", "identity1");
      element_factory_make ("fakesink");

      element_link ("fakesrc", "identity0");
      element_link ("identity0", "identity1");
      element_link ("identity1", "fakesink");
      break;
    case 3:
      /* fakesrc ! audiosink */
      element_factory_make ("fakesrc");
      element_factory_make ("audiosink");

      element_link ("fakesrc", "audiosink");
      break;
    case 4:
      /* fakesrc ! mad ! fakesink */
      element_factory_make ("fakesrc");
      element_factory_make ("mad");
      element_factory_make ("fakesink");

      element_link ("fakesrc", "mad");
      element_link ("mad", "fakesink");
      break;
    case 5:
      /* fakesrc ! queue ! fakesink */
      element_factory_make ("fakesrc");
      element_factory_make ("queue");
      element_factory_make ("fakesink");

      element_link ("fakesrc", "queue");
      element_link ("queue", "fakesink");
      break;
    case 6:
      /* fakesrc ! queue ! audiosink */
      element_factory_make ("fakesrc");
      element_factory_make ("queue");
      element_factory_make ("audiosink");

      element_link ("fakesrc", "queue");
      element_link ("queue", "audiosink");
      break;
    case 7:
      /* fakesrc ! videosink */
      element_factory_make ("fakesrc");
      element_factory_make ("videosink");

      element_link ("fakesrc", "videosink");
      break;
    case 8:
      /* fakesrc ! tee ! videosink tee0.src2 ! videosink */
      element_factory_make ("fakesrc");
      element_factory_make ("tee");
      element_factory_make_full ("videosink", "vs0");
      element_factory_make_full ("videosink", "vs1");

      element_link ("fakesrc", "tee");
      element_link_full ("tee", "src1", "vs0", "sink");
      element_link_full ("tee", "src2", "vs1", "sink");
      break;
  }

  run ();

  return 0;
}

void
run (void)
{
  int iter = 0;

  while (iter < 20) {
    Element *e;
    GList *l;
    gboolean did_something = FALSE;
    double ent = INFINITY;

    g_print ("iteration %d time %g\n", iter, time);
    for (l = g_list_first (elements); l; l = g_list_next (l)) {
      double nt;

      e = l->data;
      if (element_ready (e)) {
        g_print ("%s: is ready, iterating\n", e->name);
        e->iterate (e);
        did_something = TRUE;
      } else {
        g_print ("%s: is not ready\n", e->name);
      }
      nt = element_next_time (e);
      if (nt < ent) {
        ent = nt;
      }
    }
    if (did_something == FALSE) {
      if (ent < INFINITY) {
        g_print ("nothing to do, waiting for %g\n", ent);
        time = ent;
      } else {
        g_print ("ERROR: deadlock\n");
        exit (1);
      }
    }
    iter++;
  }

}

void
dump (void)
{
  Element *e;
  GList *l;

  for (l = g_list_first (elements); l; l = g_list_next (l)) {
    e = l->data;
    dump_element (e);
  }
}

void
dump_element (Element * e)
{
  Feature *f;
  GList *m;

  g_print ("%s:\n", e->name);
  for (m = g_list_first (e->features); m; m = g_list_next (m)) {
    f = m->data;
    g_print ("  %s:\n", f->name);
    g_print ("    type %d\n", f->type);
    g_print ("    ready %d\n", f->ready);
    g_print ("    waiting %d\n", f->waiting);

  }
}

/* Element */

const ElementInfo element_types[] = {
  {"fakesrc", fakesrc_iterate},
  {"identity", identity_iterate},
  {"fakesink", fakesink_iterate},
  {"audiosink", audiosink_iterate},
  {"mad", mad_iterate},
  {"queue", queue_iterate},
  {"videosink", videosink_iterate},
  {"tee", tee_iterate},
  {NULL, NULL}
};

Element *
element_factory_make (const char *type)
{
  return element_factory_make_full (type, type);
}

Element *
element_factory_make_full (const char *type, const char *name)
{
  int i;
  Element *e = g_new0 (Element, 1);

  for (i = 0; element_types[i].type_name; i++) {
    if (strcmp (type, element_types[i].type_name) == 0) {
      e->type = i;
      e->iterate = element_types[i].iterate;
      e->caps_iterate = element_types[i].iterate;
      e->iterate (e);
      e->name = g_strdup (name);

      elements = g_list_append (elements, e);

      return e;
    }
  }

  g_print ("ERROR: element type %s not found\n", type);
  return NULL;
}

void
element_link (const char *name1, const char *name2)
{
  element_link_full (name1, "src", name2, "sink");
}

void
element_link_full (const char *name1, const char *padname1, const char *name2,
    const char *padname2)
{
  Element *e1, *e2;
  Feature *pad1, *pad2;

  e1 = element_get (name1);
  e2 = element_get (name2);

  pad1 = feature_get (e1, padname1);
  pad2 = feature_get (e2, padname2);

  pad1->peer = pad2;
  pad2->peer = pad1;

}

Element *
element_get (const char *name)
{
  GList *l;

  for (l = g_list_first (elements); l; l = g_list_next (l)) {
    Element *e = l->data;

    if (strcmp (name, e->name) == 0)
      return e;
  }

  g_print ("ERROR: element_get(%s) element not found\n", name);
  return NULL;
}

gboolean
element_ready (Element * e)
{
  GList *l;

  //dump_element(e);
  if (e->idle)
    return TRUE;
  for (l = g_list_first (e->features); l; l = g_list_next (l)) {
    Feature *f = l->data;

    if (f->waiting && feature_is_ready (f)) {
      g_print ("element %s is ready because feature %s is ready\n",
          e->name, f->name);
      return TRUE;
    }
  }
  return FALSE;
}

double
element_next_time (Element * e)
{
  GList *l;
  double ent = INFINITY;

  for (l = g_list_first (e->features); l; l = g_list_next (l)) {
    Feature *f = l->data;

    if (f->type == FD || f->type == TIME) {
      if (f->next_time < ent) {
        ent = f->next_time;
      }
    }
  }
  return ent;
}

/* Feature */

Feature *
feature_get (Element * e, const char *name)
{
  GList *l;

  for (l = g_list_first (e->features); l; l = g_list_next (l)) {
    Feature *f = l->data;

    if (strcmp (name, f->name) == 0)
      return f;
  }

  g_print ("ERROR: feature_get(%s): feature not found\n", name);
  return NULL;
}

Feature *
feature_create (Element * e, int type, const char *name)
{
  Feature *f = g_new0 (Feature, 1);

  f->name = g_strdup (name);
  f->type = type;

  e->features = g_list_append (e->features, f);

  return f;
}

void
feature_wait (Element * e, const char *name, gboolean wait)
{
  Feature *f = feature_get (e, name);

  f->waiting = wait;
  switch (f->type) {
    case PAD_SRC:
      if (f->peer) {
        f->peer->ready = wait && f->peer->bufpen;
      }
      break;
    case PAD_SINK:
      if (f->peer) {
        f->peer->ready = wait && f->bufpen;
      }
      break;
  }
}

gboolean
feature_ready (Element * e, const char *name)
{
  Feature *f = feature_get (e, name);

  return feature_is_ready (f);
}

gboolean
feature_is_ready (Feature * f)
{
  switch (f->type) {
    case PAD_SRC:
      if (f->peer) {
        return f->peer->waiting && !f->peer->bufpen;
      }
      break;
    case PAD_SINK:
      if (f->peer) {
        return f->peer->waiting && f->bufpen;
      }
      break;
    case FD:
      g_print ("testing %g <= %g\n", f->next_time, time);
      if (f->next_time <= time) {
        return TRUE;
      } else {
        return FALSE;
      }
      break;
    case TIME:
      g_print ("testing %g <= %g\n", f->next_time, time);
      if (f->next_time <= time) {
        return TRUE;
      } else {
        return FALSE;
      }
      break;
  }

  return FALSE;
}

void
pad_push (Element * e, const char *name)
{
  Feature *f = feature_get (e, name);

  g_assert (f->type == PAD_SRC);

  g_print ("pushing pad on %s:%s\n", e->name, name);
  if (f->peer->bufpen) {
    g_print ("ERROR: push when bufpen full link\n");
    exit (0);
  }
  f->peer->bufpen = TRUE;
  f->peer->ready = f->waiting;
  f->ready = FALSE;
}

void
pad_pull (Element * e, const char *name)
{
  Feature *f = feature_get (e, name);

  g_assert (f->type == PAD_SINK);

  g_print ("pulling pad on %s:%s\n", e->name, name);
  if (!f->bufpen) {
    g_print ("ERROR: pull when bufpen empty\n");
    exit (0);
  }
  f->bufpen = FALSE;
  f->ready = FALSE;
  f->peer->ready = f->waiting;
}

void
fd_push (Element * e, const char *name)
{
  Feature *f = feature_get (e, name);

  g_assert (f->type == FD);

  g_print ("pushing to fd %s:%s\n", e->name, name);
  if (time < f->next_time) {
    g_print ("ERROR: push too early\n");
    exit (0);
  }
  f->next_time += f->interval;
}

void
fd_start (Element * e, const char *name, double interval)
{
  Feature *f = feature_get (e, name);

  g_assert (f->type == FD);

  f->interval = interval;
  f->next_time = f->interval;
}

void
time_start (Element * e, const char *name, double interval)
{
  Feature *f = feature_get (e, name);

  g_assert (f->type == TIME);

  f->interval = interval;
  f->next_time = f->interval;
}

void
time_increment (Element * e, const char *name, double interval)
{
  Feature *f = feature_get (e, name);

  g_assert (f->type == TIME);

  f->interval = interval;
  f->next_time += f->interval;
}

/* elements */

void
fakesrc_iterate (Element * element)
{
  //Event *event;

  if (!element->init) {
    feature_create (element, PAD_SRC, "src");

    feature_wait (element, "src", TRUE);

    element->init = TRUE;
    return;
  }

  pad_push (element, "src");
}

void
identity_iterate (Element * element)
{
  if (!element->init) {
    feature_create (element, PAD_SINK, "sink");
    feature_create (element, PAD_SRC, "src");

    feature_wait (element, "sink", FALSE);
    feature_wait (element, "src", TRUE);

    element->init = TRUE;
    return;
  }

  if (feature_ready (element, "sink") && feature_ready (element, "src")) {
    pad_pull (element, "sink");
    pad_push (element, "src");
    feature_wait (element, "sink", FALSE);
    feature_wait (element, "src", TRUE);
  } else {
    if (feature_ready (element, "sink")) {
      g_print ("ERROR: assert not reached\n");
      feature_wait (element, "src", TRUE);
      feature_wait (element, "sink", FALSE);
    }
    if (feature_ready (element, "src")) {
      feature_wait (element, "src", FALSE);
      feature_wait (element, "sink", TRUE);
    }
  }
}

void
fakesink_iterate (Element * element)
{
  if (!element->init) {
    feature_create (element, PAD_SINK, "sink");

    element->idle = TRUE;
    element->init = TRUE;
    return;
  }

  if (feature_ready (element, "sink")) {
    pad_pull (element, "sink");
    g_print ("FAKESINK\n");
  } else {
    feature_wait (element, "sink", TRUE);
    element->idle = FALSE;
  }

}

void
audiosink_iterate (Element * element)
{
  if (!element->init) {
    Feature *f;

    feature_create (element, PAD_SINK, "sink");
    f = feature_create (element, FD, "fd");
    fd_start (element, "fd", 1024 / 44100.0);

    feature_wait (element, "fd", TRUE);

    element->init = TRUE;
    return;
  }

  if (feature_ready (element, "fd")) {
    if (feature_ready (element, "sink")) {
      pad_pull (element, "sink");
      fd_push (element, "fd");
      g_print ("AUDIOSINK\n");
      feature_wait (element, "fd", TRUE);
      feature_wait (element, "sink", FALSE);
    } else {
      feature_wait (element, "fd", FALSE);
      feature_wait (element, "sink", TRUE);
    }
  } else {
    g_print ("ERROR: assert not reached\n");

    feature_wait (element, "sink", FALSE);
    feature_wait (element, "fd", TRUE);
  }

}

void
mad_iterate (Element * element)
{
  if (!element->init) {
    feature_create (element, PAD_SINK, "sink");
    feature_create (element, PAD_SRC, "src");

    element->state = 0;
    feature_wait (element, "sink", FALSE);
    feature_wait (element, "src", TRUE);

    element->init = TRUE;
    return;
  }

  if (element->state > 0) {
    if (feature_ready (element, "src")) {
      pad_push (element, "src");
      element->state--;
      if (element->state > 0) {
        feature_wait (element, "sink", FALSE);
        feature_wait (element, "src", TRUE);
      } else {
        feature_wait (element, "sink", FALSE);
        feature_wait (element, "src", TRUE);
      }
    } else {
      g_print ("ERROR: assert not reached\n");
    }
  } else {
    if (feature_ready (element, "sink")) {
      pad_pull (element, "sink");
      element->state += 5;
      pad_push (element, "src");
      element->state--;
      feature_wait (element, "sink", FALSE);
      feature_wait (element, "src", TRUE);
    } else {
      feature_wait (element, "sink", TRUE);
      feature_wait (element, "src", FALSE);
    }
  }
}

void
queue_iterate (Element * element)
{
  if (!element->init) {
    feature_create (element, PAD_SINK, "sink");
    feature_create (element, PAD_SRC, "src");

    element->state = 0;
    feature_wait (element, "sink", FALSE);
    feature_wait (element, "src", TRUE);

    element->init = TRUE;
    return;
  }

  if (feature_ready (element, "sink") && element->state < 5) {
    pad_pull (element, "sink");
    element->state++;
  }
  if (feature_ready (element, "src") && element->state > 0) {
    pad_push (element, "src");
    element->state--;
  }

  if (element->state < 5) {
    feature_wait (element, "sink", TRUE);
  } else {
    feature_wait (element, "sink", FALSE);
  }
  if (element->state > 0) {
    feature_wait (element, "src", TRUE);
  } else {
    feature_wait (element, "src", FALSE);
  }
}

void
demux_iterate (Element * element)
{
  if (!element->init) {
    feature_create (element, PAD_SINK, "sink");
    feature_create (element, PAD_SRC, "video_src");
    feature_create (element, PAD_SRC, "audio_src");

    feature_wait (element, "sink", TRUE);
    feature_wait (element, "video_src", FALSE);
    feature_wait (element, "audio_src", FALSE);

    element->init = TRUE;
    return;
  }
#if 0
  /* demux waits for a buffer on the sinkpad, then queues buffers and
   * eventually pushes them to sinkpads */
  if (feautre_ready (element, "sink") &&) {

  }
#endif

}

void
videosink_iterate (Element * element)
{
  if (!element->init) {
    Feature *f;

    feature_create (element, PAD_SINK, "sink");
    f = feature_create (element, TIME, "time");
    time_start (element, "time", 1 / 25.0);

    feature_wait (element, "sink", TRUE);
    feature_wait (element, "time", FALSE);

    element->init = TRUE;
    return;
  }

  /* this version hold the buffer in the bufpen */
  if (feature_ready (element, "sink")) {
    if (feature_ready (element, "time")) {
      pad_pull (element, "sink");
      g_print ("VIDEOSINK\n");
      time_increment (element, "time", 1 / 25.0);
      feature_wait (element, "time", FALSE);
      feature_wait (element, "sink", TRUE);
    } else {
      feature_wait (element, "time", TRUE);
      feature_wait (element, "sink", FALSE);
    }
  } else {
    g_print ("ERROR: assert not reached\n");
  }
}

void
tee_iterate (Element * element)
{
  if (!element->init) {
    feature_create (element, PAD_SINK, "sink");
    feature_create (element, PAD_SRC, "src1");
    feature_create (element, PAD_SRC, "src2");

    feature_wait (element, "sink", FALSE);
    feature_wait (element, "src1", TRUE);
    feature_wait (element, "src2", TRUE);

    element->init = TRUE;
    return;
  }

  /* this version hold the buffer in the bufpen */
  if (feature_ready (element, "sink")) {
    pad_pull (element, "sink");
    pad_push (element, "src1");
    pad_push (element, "src2");

    feature_wait (element, "sink", FALSE);
    feature_wait (element, "src1", TRUE);
    feature_wait (element, "src2", TRUE);
  } else {
    if (feature_ready (element, "src1")) {
      if (feature_ready (element, "src2")) {
        feature_wait (element, "sink", TRUE);
        feature_wait (element, "src1", FALSE);
        feature_wait (element, "src2", FALSE);
      } else {
        feature_wait (element, "sink", FALSE);
        feature_wait (element, "src1", FALSE);
        feature_wait (element, "src2", TRUE);
      }
    } else {
      if (feature_ready (element, "src2")) {
        feature_wait (element, "sink", FALSE);
        feature_wait (element, "src1", TRUE);
        feature_wait (element, "src2", FALSE);
      } else {
        g_print ("ERROR: assert not reached\n");
      }
    }
  }
}
