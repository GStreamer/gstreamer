/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *
 * gstscheduler.c: Default scheduling code for most cases
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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <gst/gst.h>

GST_DEBUG_CATEGORY_STATIC (debug_scheduler);
#define GST_CAT_DEFAULT debug_scheduler

#ifdef USE_COTHREADS
# include "cothreads_compat.h"
#else
# define COTHREADS_NAME_CAPITAL ""
# define COTHREADS_NAME 	""
#endif

#define GST_ELEMENT_SCHED_CONTEXT(elem)		((GstOptSchedulerCtx*) (GST_ELEMENT (elem)->sched_private))
#define GST_ELEMENT_SCHED_GROUP(elem)		(GST_ELEMENT_SCHED_CONTEXT (elem)->group)
/* need this first macro to not run into lvalue casts */
#define GST_PAD_BUFPEN(pad)			(GST_REAL_PAD(pad)->sched_private)
#define GST_PAD_BUFLIST(pad)            	((GList*) GST_PAD_BUFPEN(pad))

#define GST_ELEMENT_COTHREAD_STOPPING			GST_ELEMENT_SCHEDULER_PRIVATE1
#define GST_ELEMENT_IS_COTHREAD_STOPPING(element)	GST_FLAG_IS_SET((element), GST_ELEMENT_COTHREAD_STOPPING)
#define GST_ELEMENT_INTERRUPTED				GST_ELEMENT_SCHEDULER_PRIVATE2
#define GST_ELEMENT_IS_INTERRUPTED(element)		GST_FLAG_IS_SET((element), GST_ELEMENT_INTERRUPTED)

typedef struct _GstOptScheduler GstOptScheduler;
typedef struct _GstOptSchedulerClass GstOptSchedulerClass;

#define GST_TYPE_OPT_SCHEDULER \
  (gst_opt_scheduler_get_type())
#define GST_OPT_SCHEDULER(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_OPT_SCHEDULER,GstOptScheduler))
#define GST_OPT_SCHEDULER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_OPT_SCHEDULER,GstOptSchedulerClass))
#define GST_IS_OPT_SCHEDULER(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_OPT_SCHEDULER))
#define GST_IS_OPT_SCHEDULER_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_OPT_SCHEDULER))

typedef enum
{
  GST_OPT_SCHEDULER_STATE_NONE,
  GST_OPT_SCHEDULER_STATE_STOPPED,
  GST_OPT_SCHEDULER_STATE_ERROR,
  GST_OPT_SCHEDULER_STATE_RUNNING,
  GST_OPT_SCHEDULER_STATE_INTERRUPTED
}
GstOptSchedulerState;

struct _GstOptScheduler
{
  GstScheduler parent;

  GstOptSchedulerState state;

#ifdef USE_COTHREADS
  cothread_context *context;
#endif
  gint iterations;

  GSList *elements;
  GSList *chains;

  GList *runqueue;
  gint recursion;

  gint max_recursion;
};

struct _GstOptSchedulerClass
{
  GstSchedulerClass parent_class;
};

static GType _gst_opt_scheduler_type = 0;

typedef enum
{
  GST_OPT_SCHEDULER_CHAIN_DIRTY = (1 << 1),
  GST_OPT_SCHEDULER_CHAIN_DISABLED = (1 << 2),
  GST_OPT_SCHEDULER_CHAIN_RUNNING = (1 << 3),
}
GstOptSchedulerChainFlags;

#define GST_OPT_SCHEDULER_CHAIN_SET_DIRTY(chain)	((chain)->flags |= GST_OPT_SCHEDULER_CHAIN_DIRTY)
#define GST_OPT_SCHEDULER_CHAIN_SET_CLEAN(chain)	((chain)->flags &= ~GST_OPT_SCHEDULER_CHAIN_DIRTY)
#define GST_OPT_SCHEDULER_CHAIN_IS_DIRTY(chain) 	((chain)->flags & GST_OPT_SCHEDULER_CHAIN_DIRTY)

#define GST_OPT_SCHEDULER_CHAIN_DISABLE(chain) 		((chain)->flags |= GST_OPT_SCHEDULER_CHAIN_DISABLED)
#define GST_OPT_SCHEDULER_CHAIN_ENABLE(chain) 		((chain)->flags &= ~GST_OPT_SCHEDULER_CHAIN_DISABLED)
#define GST_OPT_SCHEDULER_CHAIN_IS_DISABLED(chain) 	((chain)->flags & GST_OPT_SCHEDULER_CHAIN_DISABLED)

typedef struct _GstOptSchedulerChain GstOptSchedulerChain;

struct _GstOptSchedulerChain
{
  gint refcount;

  GstOptScheduler *sched;

  GstOptSchedulerChainFlags flags;

  GSList *groups;               /* the groups in this chain */
  gint num_groups;
  gint num_enabled;
};

/* 
 * elements that are scheduled in one cothread 
 */
typedef enum
{
  GST_OPT_SCHEDULER_GROUP_DIRTY = (1 << 1),     /* this group has been modified */
  GST_OPT_SCHEDULER_GROUP_COTHREAD_STOPPING = (1 << 2), /* the group's cothread stops after one iteration */
  GST_OPT_SCHEDULER_GROUP_DISABLED = (1 << 3),  /* this group is disabled */
  GST_OPT_SCHEDULER_GROUP_RUNNING = (1 << 4),   /* this group is running */
  GST_OPT_SCHEDULER_GROUP_SCHEDULABLE = (1 << 5),       /* this group is schedulable */
  GST_OPT_SCHEDULER_GROUP_VISITED = (1 << 6),   /* this group is visited when finding links */
}
GstOptSchedulerGroupFlags;

typedef enum
{
  GST_OPT_SCHEDULER_GROUP_UNKNOWN = 3,
  GST_OPT_SCHEDULER_GROUP_GET = 1,
  GST_OPT_SCHEDULER_GROUP_LOOP = 2,
}
GstOptSchedulerGroupType;

#define GST_OPT_SCHEDULER_GROUP_SET_FLAG(group,flag) 	((group)->flags |= (flag))
#define GST_OPT_SCHEDULER_GROUP_UNSET_FLAG(group,flag) 	((group)->flags &= ~(flag))
#define GST_OPT_SCHEDULER_GROUP_IS_FLAG_SET(group,flag) ((group)->flags & (flag))

#define GST_OPT_SCHEDULER_GROUP_DISABLE(group) 		((group)->flags |= GST_OPT_SCHEDULER_GROUP_DISABLED)
#define GST_OPT_SCHEDULER_GROUP_ENABLE(group) 		((group)->flags &= ~GST_OPT_SCHEDULER_GROUP_DISABLED)
#define GST_OPT_SCHEDULER_GROUP_IS_ENABLED(group) 	(!((group)->flags & GST_OPT_SCHEDULER_GROUP_DISABLED))
#define GST_OPT_SCHEDULER_GROUP_IS_DISABLED(group) 	((group)->flags & GST_OPT_SCHEDULER_GROUP_DISABLED)


typedef struct _GstOptSchedulerGroup GstOptSchedulerGroup;
typedef struct _GstOptSchedulerGroupLink GstOptSchedulerGroupLink;

/* used to keep track of links with other groups */
struct _GstOptSchedulerGroupLink
{
  GstOptSchedulerGroup *src;    /* the group we are linked with */
  GstOptSchedulerGroup *sink;   /* the group we are linked with */
  gint count;                   /* the number of links with the group */
};

#define IS_GROUP_LINK(link, srcg, sinkg)	((link->src == srcg && link->sink == sinkg) || \
		                                 (link->sink == srcg && link->src == sinkg))
#define OTHER_GROUP_LINK(link, group)		(link->src == group ? link->sink : link->src)

typedef int (*GroupScheduleFunction) (int argc, char *argv[]);

struct _GstOptSchedulerGroup
{
  GstOptSchedulerChain *chain;  /* the chain this group belongs to */
  GstOptSchedulerGroupFlags flags;      /* flags for this group */
  GstOptSchedulerGroupType type;        /* flags for this group */

  gint refcount;

  GSList *elements;             /* elements of this group */
  gint num_elements;
  gint num_enabled;
  GstElement *entry;            /* the group's entry point */

  GSList *group_links;          /* other groups that are linked with this group */

#ifdef USE_COTHREADS
  cothread *cothread;           /* the cothread of this group */
#else
  GroupScheduleFunction schedulefunc;
#endif
  int argc;
  char **argv;
};


/* 
 * A group is a set of elements through which data can flow without switching
 * cothreads or without invoking the scheduler's run queue.
 */
static GstOptSchedulerGroup *ref_group (GstOptSchedulerGroup * group);
static GstOptSchedulerGroup *unref_group (GstOptSchedulerGroup * group);
static GstOptSchedulerGroup *create_group (GstOptSchedulerChain * chain,
    GstElement * element, GstOptSchedulerGroupType type);
static void destroy_group (GstOptSchedulerGroup * group);
static GstOptSchedulerGroup *add_to_group (GstOptSchedulerGroup * group,
    GstElement * element, gboolean with_links);
static GstOptSchedulerGroup *remove_from_group (GstOptSchedulerGroup * group,
    GstElement * element);
static void group_dec_links_for_element (GstOptSchedulerGroup * group,
    GstElement * element);
static void group_inc_links_for_element (GstOptSchedulerGroup * group,
    GstElement * element);
static GstOptSchedulerGroup *merge_groups (GstOptSchedulerGroup * group1,
    GstOptSchedulerGroup * group2);
static void setup_group_scheduler (GstOptScheduler * osched,
    GstOptSchedulerGroup * group);
static void destroy_group_scheduler (GstOptSchedulerGroup * group);
static void group_error_handler (GstOptSchedulerGroup * group);
static void group_element_set_enabled (GstOptSchedulerGroup * group,
    GstElement * element, gboolean enabled);
static gboolean schedule_group (GstOptSchedulerGroup * group);


/* 
 * A chain is a set of groups that are linked to each other.
 */
static void destroy_chain (GstOptSchedulerChain * chain);
static GstOptSchedulerChain *create_chain (GstOptScheduler * osched);
static GstOptSchedulerChain *ref_chain (GstOptSchedulerChain * chain);
static GstOptSchedulerChain *unref_chain (GstOptSchedulerChain * chain);
static GstOptSchedulerChain *add_to_chain (GstOptSchedulerChain * chain,
    GstOptSchedulerGroup * group);
static GstOptSchedulerChain *remove_from_chain (GstOptSchedulerChain * chain,
    GstOptSchedulerGroup * group);
static GstOptSchedulerChain *merge_chains (GstOptSchedulerChain * chain1,
    GstOptSchedulerChain * chain2);
static void chain_recursively_migrate_group (GstOptSchedulerChain * chain,
    GstOptSchedulerGroup * group);
static void chain_group_set_enabled (GstOptSchedulerChain * chain,
    GstOptSchedulerGroup * group, gboolean enabled);
static void schedule_chain (GstOptSchedulerChain * chain);


/*
 * The schedule functions are the entry points for cothreads, or called directly
 * by gst_opt_scheduler_schedule_run_queue
 */
static int get_group_schedule_function (int argc, char *argv[]);
static int loop_group_schedule_function (int argc, char *argv[]);
static int unknown_group_schedule_function (int argc, char *argv[]);


/*
 * These wrappers are set on the pads as the chain handler (what happens when
 * gst_pad_push is called) or get handler (for gst_pad_pull).
 */
static void gst_opt_scheduler_loop_wrapper (GstPad * sinkpad, GstData * data);
static GstData *gst_opt_scheduler_get_wrapper (GstPad * srcpad);


/*
 * Without cothreads, gst_pad_push or gst_pad_pull on a loop-based group will
 * just queue the peer element on a list. We need to actually run the queue
 * instead of relying on cothreads to do the switch for us.
 */
#ifndef USE_COTHREADS
static void gst_opt_scheduler_schedule_run_queue (GstOptScheduler * osched);
#endif


/* 
 * Scheduler private data for an element 
 */
typedef struct _GstOptSchedulerCtx GstOptSchedulerCtx;

typedef enum
{
  GST_OPT_SCHEDULER_CTX_DISABLED = (1 << 1),    /* the element is disabled */
}
GstOptSchedulerCtxFlags;

struct _GstOptSchedulerCtx
{
  GstOptSchedulerGroup *group;  /* the group this element belongs to */

  GstOptSchedulerCtxFlags flags;        /* flags for this element */
};


/*
 * Implementation of GstScheduler
 */
enum
{
  ARG_0,
  ARG_ITERATIONS,
  ARG_MAX_RECURSION,
};

static void gst_opt_scheduler_class_init (GstOptSchedulerClass * klass);
static void gst_opt_scheduler_init (GstOptScheduler * scheduler);

static void gst_opt_scheduler_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_opt_scheduler_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static void gst_opt_scheduler_dispose (GObject * object);

static void gst_opt_scheduler_setup (GstScheduler * sched);
static void gst_opt_scheduler_reset (GstScheduler * sched);
static void gst_opt_scheduler_add_element (GstScheduler * sched,
    GstElement * element);
static void gst_opt_scheduler_remove_element (GstScheduler * sched,
    GstElement * element);
static GstElementStateReturn gst_opt_scheduler_state_transition (GstScheduler *
    sched, GstElement * element, gint transition);
static void gst_opt_scheduler_scheduling_change (GstScheduler * sched,
    GstElement * element);
static gboolean gst_opt_scheduler_yield (GstScheduler * sched,
    GstElement * element);
static gboolean gst_opt_scheduler_interrupt (GstScheduler * sched,
    GstElement * element);
static void gst_opt_scheduler_error (GstScheduler * sched,
    GstElement * element);
static void gst_opt_scheduler_pad_link (GstScheduler * sched, GstPad * srcpad,
    GstPad * sinkpad);
static void gst_opt_scheduler_pad_unlink (GstScheduler * sched, GstPad * srcpad,
    GstPad * sinkpad);
static GstSchedulerState gst_opt_scheduler_iterate (GstScheduler * sched);

static void gst_opt_scheduler_show (GstScheduler * sched);

static GstSchedulerClass *parent_class = NULL;

static GType
gst_opt_scheduler_get_type (void)
{
  if (!_gst_opt_scheduler_type) {
    static const GTypeInfo scheduler_info = {
      sizeof (GstOptSchedulerClass),
      NULL,
      NULL,
      (GClassInitFunc) gst_opt_scheduler_class_init,
      NULL,
      NULL,
      sizeof (GstOptScheduler),
      0,
      (GInstanceInitFunc) gst_opt_scheduler_init,
      NULL
    };

    _gst_opt_scheduler_type = g_type_register_static (GST_TYPE_SCHEDULER,
        "GstOpt" COTHREADS_NAME_CAPITAL "Scheduler", &scheduler_info, 0);
  }
  return _gst_opt_scheduler_type;
}

static void
gst_opt_scheduler_class_init (GstOptSchedulerClass * klass)
{
  GObjectClass *gobject_class;
  GstObjectClass *gstobject_class;
  GstSchedulerClass *gstscheduler_class;

  gobject_class = (GObjectClass *) klass;
  gstobject_class = (GstObjectClass *) klass;
  gstscheduler_class = (GstSchedulerClass *) klass;

  parent_class = g_type_class_ref (GST_TYPE_SCHEDULER);

  gobject_class->set_property =
      GST_DEBUG_FUNCPTR (gst_opt_scheduler_set_property);
  gobject_class->get_property =
      GST_DEBUG_FUNCPTR (gst_opt_scheduler_get_property);
  gobject_class->dispose = GST_DEBUG_FUNCPTR (gst_opt_scheduler_dispose);

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_ITERATIONS,
      g_param_spec_int ("iterations", "Iterations",
          "Number of groups to schedule in one iteration (-1 == until EOS/error)",
          -1, G_MAXINT, 1, G_PARAM_READWRITE));
#ifndef USE_COTHREADS
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_MAX_RECURSION,
      g_param_spec_int ("max_recursion", "Max recursion",
          "Maximum number of recursions", 1, G_MAXINT, 100, G_PARAM_READWRITE));
#endif

  gstscheduler_class->setup = GST_DEBUG_FUNCPTR (gst_opt_scheduler_setup);
  gstscheduler_class->reset = GST_DEBUG_FUNCPTR (gst_opt_scheduler_reset);
  gstscheduler_class->add_element =
      GST_DEBUG_FUNCPTR (gst_opt_scheduler_add_element);
  gstscheduler_class->remove_element =
      GST_DEBUG_FUNCPTR (gst_opt_scheduler_remove_element);
  gstscheduler_class->state_transition =
      GST_DEBUG_FUNCPTR (gst_opt_scheduler_state_transition);
  gstscheduler_class->scheduling_change =
      GST_DEBUG_FUNCPTR (gst_opt_scheduler_scheduling_change);
  gstscheduler_class->yield = GST_DEBUG_FUNCPTR (gst_opt_scheduler_yield);
  gstscheduler_class->interrupt =
      GST_DEBUG_FUNCPTR (gst_opt_scheduler_interrupt);
  gstscheduler_class->error = GST_DEBUG_FUNCPTR (gst_opt_scheduler_error);
  gstscheduler_class->pad_link = GST_DEBUG_FUNCPTR (gst_opt_scheduler_pad_link);
  gstscheduler_class->pad_unlink =
      GST_DEBUG_FUNCPTR (gst_opt_scheduler_pad_unlink);
  gstscheduler_class->clock_wait = NULL;
  gstscheduler_class->iterate = GST_DEBUG_FUNCPTR (gst_opt_scheduler_iterate);
  gstscheduler_class->show = GST_DEBUG_FUNCPTR (gst_opt_scheduler_show);

#ifdef USE_COTHREADS
  do_cothreads_init (NULL);
#endif
}

static void
gst_opt_scheduler_init (GstOptScheduler * scheduler)
{
  scheduler->elements = NULL;
  scheduler->iterations = 1;
  scheduler->max_recursion = 100;
}

static void
gst_opt_scheduler_dispose (GObject * object)
{
  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  GstSchedulerFactory *factory;

  GST_DEBUG_CATEGORY_INIT (debug_scheduler, "scheduler", 0,
      "optimal scheduler");

#ifdef USE_COTHREADS
  factory = gst_scheduler_factory_new ("opt" COTHREADS_NAME,
      "An optimal scheduler using " COTHREADS_NAME " cothreads",
      gst_opt_scheduler_get_type ());
#else
  factory = gst_scheduler_factory_new ("opt",
      "An optimal scheduler using no cothreads", gst_opt_scheduler_get_type ());
#endif

  if (factory != NULL) {
    gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (factory));
  } else {
    g_warning ("could not register scheduler: optimal");
  }
  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "gstopt" COTHREADS_NAME "scheduler",
    "An optimal scheduler using " COTHREADS_NAME " cothreads",
    plugin_init, VERSION, GST_LICENSE, GST_PACKAGE, GST_ORIGIN);


static GstOptSchedulerChain *
ref_chain (GstOptSchedulerChain * chain)
{
  GST_LOG ("ref chain %p %d->%d", chain, chain->refcount, chain->refcount + 1);
  chain->refcount++;

  return chain;
}

static GstOptSchedulerChain *
unref_chain (GstOptSchedulerChain * chain)
{
  GST_LOG ("unref chain %p %d->%d", chain,
      chain->refcount, chain->refcount - 1);

  if (--chain->refcount == 0) {
    destroy_chain (chain);
    chain = NULL;
  }

  return chain;
}

static GstOptSchedulerChain *
create_chain (GstOptScheduler * osched)
{
  GstOptSchedulerChain *chain;

  chain = g_new0 (GstOptSchedulerChain, 1);
  chain->sched = osched;
  chain->refcount = 1;
  chain->flags = GST_OPT_SCHEDULER_CHAIN_DISABLED;

  gst_object_ref (GST_OBJECT (osched));
  osched->chains = g_slist_prepend (osched->chains, chain);

  GST_LOG ("new chain %p", chain);

  return chain;
}

static void
destroy_chain (GstOptSchedulerChain * chain)
{
  GstOptScheduler *osched;

  GST_LOG ("destroy chain %p", chain);

  g_assert (chain->num_groups == 0);
  g_assert (chain->groups == NULL);

  osched = chain->sched;
  osched->chains = g_slist_remove (osched->chains, chain);

  gst_object_unref (GST_OBJECT (osched));

  g_free (chain);
}

static GstOptSchedulerChain *
add_to_chain (GstOptSchedulerChain * chain, GstOptSchedulerGroup * group)
{
  gboolean enabled;

  GST_LOG ("adding group %p to chain %p", group, chain);

  g_assert (group->chain == NULL);

  group = ref_group (group);

  group->chain = ref_chain (chain);
  chain->groups = g_slist_prepend (chain->groups, group);
  chain->num_groups++;

  enabled = GST_OPT_SCHEDULER_GROUP_IS_ENABLED (group);

  if (enabled) {
    /* we can now setup the scheduling of the group */
    setup_group_scheduler (chain->sched, group);

    chain->num_enabled++;
    if (chain->num_enabled == chain->num_groups) {
      GST_LOG ("enabling chain %p after adding of enabled group", chain);
      GST_OPT_SCHEDULER_CHAIN_ENABLE (chain);
    }
  }

  /* queue a resort of the group list, which determines which group will be run
   * first. */
  GST_OPT_SCHEDULER_CHAIN_SET_DIRTY (chain);

  return chain;
}

static GstOptSchedulerChain *
remove_from_chain (GstOptSchedulerChain * chain, GstOptSchedulerGroup * group)
{
  gboolean enabled;

  GST_LOG ("removing group %p from chain %p", group, chain);

  if (!chain)
    return NULL;

  g_assert (group);
  g_assert (group->chain == chain);

  enabled = GST_OPT_SCHEDULER_GROUP_IS_ENABLED (group);

  group->chain = NULL;
  chain->groups = g_slist_remove (chain->groups, group);
  chain->num_groups--;
  unref_group (group);

  if (chain->num_groups == 0)
    chain = unref_chain (chain);
  else {
    /* removing an enabled group from the chain decrements the 
     * enabled counter */
    if (enabled) {
      chain->num_enabled--;
      if (chain->num_enabled == 0) {
        GST_LOG ("disabling chain %p after removal of the only enabled group",
            chain);
        GST_OPT_SCHEDULER_CHAIN_DISABLE (chain);
      }
    } else {
      if (chain->num_enabled == chain->num_groups) {
        GST_LOG ("enabling chain %p after removal of the only disabled group",
            chain);
        GST_OPT_SCHEDULER_CHAIN_ENABLE (chain);
      }
    }
  }

  GST_OPT_SCHEDULER_CHAIN_SET_DIRTY (chain);

  chain = unref_chain (chain);
  return chain;
}

static GstOptSchedulerChain *
merge_chains (GstOptSchedulerChain * chain1, GstOptSchedulerChain * chain2)
{
  GSList *walk;

  g_assert (chain1 != NULL);

  GST_LOG ("merging chain %p and %p", chain1, chain2);

  /* FIXME: document how chain2 can be NULL */
  if (chain1 == chain2 || chain2 == NULL)
    return chain1;

  /* switch if it's more efficient */
  if (chain1->num_groups < chain2->num_groups) {
    GstOptSchedulerChain *tmp = chain2;

    chain2 = chain1;
    chain1 = tmp;
  }

  walk = chain2->groups;
  while (walk) {
    GstOptSchedulerGroup *group = (GstOptSchedulerGroup *) walk->data;

    walk = g_slist_next (walk);

    GST_LOG ("reparenting group %p from chain %p to %p", group, chain2, chain1);

    ref_group (group);

    remove_from_chain (chain2, group);
    add_to_chain (chain1, group);

    unref_group (group);
  }

  /* chain2 is now freed, if nothing else was referencing it before */

  return chain1;
}

/* sorts the group list so that terminal sinks come first -- prevents pileup of
 * buffers in bufpens */
static void
sort_chain (GstOptSchedulerChain * chain)
{
  GSList *original = chain->groups;
  GSList *new = NULL;
  GSList *walk, *links, *this;

  /* if there's only one group, just return */
  if (!original->next)
    return;
  /* otherwise, we know that all groups are somehow linked together */

  GST_LOG ("sorting chain %p (%d groups)", chain, g_slist_length (original));

  /* first find the terminal sinks */
  for (walk = original; walk;) {
    GstOptSchedulerGroup *group = (GstOptSchedulerGroup *) walk->data;

    this = walk;
    walk = walk->next;
    if (group->group_links) {
      gboolean is_sink = TRUE;

      for (links = group->group_links; links; links = links->next)
        if (((GstOptSchedulerGroupLink *) links->data)->src == group)
          is_sink = FALSE;
      if (is_sink) {
        /* found one */
        original = g_slist_remove_link (original, this);
        new = g_slist_concat (new, this);
      }
    }
  }
  g_assert (new != NULL);

  /* now look for the elements that are linked to the terminal sinks */
  for (walk = new; walk; walk = walk->next) {
    GstOptSchedulerGroup *group = (GstOptSchedulerGroup *) walk->data;

    for (links = group->group_links; links; links = links->next) {
      this =
          g_slist_find (original,
          ((GstOptSchedulerGroupLink *) links->data)->src);
      if (this) {
        original = g_slist_remove_link (original, this);
        new = g_slist_concat (new, this);
      }
    }
  }
  g_assert (original == NULL);

  chain->groups = new;
}

static void
chain_group_set_enabled (GstOptSchedulerChain * chain,
    GstOptSchedulerGroup * group, gboolean enabled)
{
  gboolean oldstate;

  g_assert (group != NULL);
  g_assert (chain != NULL);

  GST_LOG
      ("request to %d group %p in chain %p, have %d groups enabled out of %d",
      enabled, group, chain, chain->num_enabled, chain->num_groups);

  oldstate = (GST_OPT_SCHEDULER_GROUP_IS_ENABLED (group) ? TRUE : FALSE);
  if (oldstate == enabled) {
    GST_LOG ("group %p in chain %p was in correct state", group, chain);
    return;
  }

  if (enabled)
    GST_OPT_SCHEDULER_GROUP_ENABLE (group);
  else
    GST_OPT_SCHEDULER_GROUP_DISABLE (group);

  if (enabled) {
    g_assert (chain->num_enabled < chain->num_groups);

    chain->num_enabled++;

    GST_DEBUG ("enable group %p in chain %p, now %d groups enabled out of %d",
        group, chain, chain->num_enabled, chain->num_groups);

    /* OK to call even if the scheduler (cothread context / schedulerfunc) was
       setup already -- will get destroyed when the group is destroyed */
    setup_group_scheduler (chain->sched, group);

    if (chain->num_enabled == chain->num_groups) {
      GST_DEBUG ("enable chain %p", chain);
      GST_OPT_SCHEDULER_CHAIN_ENABLE (chain);
    }
  } else {
    g_assert (chain->num_enabled > 0);

    chain->num_enabled--;
    GST_DEBUG ("disable group %p in chain %p, now %d groups enabled out of %d",
        group, chain, chain->num_enabled, chain->num_groups);

    if (chain->num_enabled == 0) {
      GST_DEBUG ("disable chain %p", chain);
      GST_OPT_SCHEDULER_CHAIN_DISABLE (chain);
    }
  }
}

/* recursively migrate the group and all connected groups into the new chain */
static void
chain_recursively_migrate_group (GstOptSchedulerChain * chain,
    GstOptSchedulerGroup * group)
{
  GSList *links;

  /* group already in chain */
  if (group->chain == chain)
    return;

  /* first remove the group from its old chain */
  remove_from_chain (group->chain, group);
  /* add to new chain */
  add_to_chain (chain, group);

  /* then follow all links */
  links = group->group_links;
  while (links) {
    GstOptSchedulerGroupLink *link = (GstOptSchedulerGroupLink *) links->data;

    links = g_slist_next (links);

    chain_recursively_migrate_group (chain, OTHER_GROUP_LINK (link, group));
  }
}

static GstOptSchedulerGroup *
ref_group (GstOptSchedulerGroup * group)
{
  GST_LOG ("ref group %p %d->%d", group, group->refcount, group->refcount + 1);

  group->refcount++;

  return group;
}

static GstOptSchedulerGroup *
unref_group (GstOptSchedulerGroup * group)
{
  GST_LOG ("unref group %p %d->%d", group,
      group->refcount, group->refcount - 1);

  if (--group->refcount == 0) {
    destroy_group (group);
    group = NULL;
  }

  return group;
}

static GstOptSchedulerGroup *
create_group (GstOptSchedulerChain * chain, GstElement * element,
    GstOptSchedulerGroupType type)
{
  GstOptSchedulerGroup *group;

  group = g_new0 (GstOptSchedulerGroup, 1);
  GST_LOG ("new group %p, type %d", group, type);
  group->refcount = 1;          /* float... */
  group->flags = GST_OPT_SCHEDULER_GROUP_DISABLED;
  group->type = type;

  add_to_group (group, element, FALSE);
  add_to_chain (chain, group);
  group = unref_group (group);  /* ...and sink. */

  /* group's refcount is now 2 (one for the element, one for the chain) */

  return group;
}

static void
destroy_group (GstOptSchedulerGroup * group)
{
  GST_LOG ("destroy group %p", group);

  g_assert (group != NULL);
  g_assert (group->elements == NULL);
  g_assert (group->chain == NULL);
  g_assert (group->group_links == NULL);

  if (group->flags & GST_OPT_SCHEDULER_GROUP_SCHEDULABLE)
    destroy_group_scheduler (group);

  g_free (group);
}

static GstOptSchedulerGroup *
add_to_group (GstOptSchedulerGroup * group, GstElement * element,
    gboolean with_links)
{
  g_assert (group != NULL);
  g_assert (element != NULL);

  GST_DEBUG ("adding element \"%s\" to group %p", GST_ELEMENT_NAME (element),
      group);

  if (GST_ELEMENT_IS_DECOUPLED (element)) {
    GST_DEBUG ("element \"%s\" is decoupled, not adding to group %p",
        GST_ELEMENT_NAME (element), group);
    return group;
  }

  g_assert (GST_ELEMENT_SCHED_GROUP (element) == NULL);

  /* first increment the links that this group has with other groups through
   * this element */
  if (with_links)
    group_inc_links_for_element (group, element);

  /* Ref the group... */
  GST_ELEMENT_SCHED_GROUP (element) = ref_group (group);

  gst_object_ref (GST_OBJECT (element));
  group->elements = g_slist_prepend (group->elements, element);
  group->num_elements++;

  if (gst_element_get_state (element) == GST_STATE_PLAYING) {
    group_element_set_enabled (group, element, TRUE);
  }

  return group;
}

static GstOptSchedulerGroup *
remove_from_group (GstOptSchedulerGroup * group, GstElement * element)
{
  GST_DEBUG ("removing element \"%s\" from group %p",
      GST_ELEMENT_NAME (element), group);

  g_assert (group != NULL);
  g_assert (element != NULL);
  g_assert (GST_ELEMENT_SCHED_GROUP (element) == group);

  /* first decrement the links that this group has with other groups through
   * this element */
  group_dec_links_for_element (group, element);

  group->elements = g_slist_remove (group->elements, element);
  group->num_elements--;

  /* if the element was an entry point in the group, clear the group's
   * entry point, and mark it as unknown */
  if (group->entry == element) {
    group->entry = NULL;
    group->type = GST_OPT_SCHEDULER_GROUP_UNKNOWN;
  }

  GST_ELEMENT_SCHED_GROUP (element) = NULL;
  gst_object_unref (GST_OBJECT (element));

  if (group->num_elements == 0) {
    GST_LOG ("group %p is now empty", group);
    /* don't know in what case group->chain would be NULL, but putting this here
       in deference to 0.8 -- remove me in 0.9 */
    if (group->chain) {
      GST_LOG ("removing group %p from its chain", group);
      chain_group_set_enabled (group->chain, group, FALSE);
      remove_from_chain (group->chain, group);
    }
  }
  group = unref_group (group);

  return group;
}

/* FIXME need to check if the groups are of the same type -- otherwise need to
   setup the scheduler again, if it is setup */
static GstOptSchedulerGroup *
merge_groups (GstOptSchedulerGroup * group1, GstOptSchedulerGroup * group2)
{
  g_assert (group1 != NULL);

  GST_DEBUG ("merging groups %p and %p", group1, group2);

  if (group1 == group2 || group2 == NULL)
    return group1;

  /* make sure they end up in the same chain */
  merge_chains (group1->chain, group2->chain);

  while (group2 && group2->elements) {
    GstElement *element = (GstElement *) group2->elements->data;

    group2 = remove_from_group (group2, element);
    add_to_group (group1, element, TRUE);
  }

  return group1;
}

/* setup the scheduler context for a group. The right schedule function
 * is selected based on the group type and cothreads are created if 
 * needed */
static void
setup_group_scheduler (GstOptScheduler * osched, GstOptSchedulerGroup * group)
{
  GroupScheduleFunction wrapper;

  GST_DEBUG ("setup group %p scheduler, type %d", group, group->type);

  wrapper = unknown_group_schedule_function;

  /* figure out the wrapper function for this group */
  if (group->type == GST_OPT_SCHEDULER_GROUP_GET)
    wrapper = get_group_schedule_function;
  else if (group->type == GST_OPT_SCHEDULER_GROUP_LOOP)
    wrapper = loop_group_schedule_function;

#ifdef USE_COTHREADS
  if (!(group->flags & GST_OPT_SCHEDULER_GROUP_SCHEDULABLE)) {
    do_cothread_create (group->cothread, osched->context,
        (cothread_func) wrapper, 0, (char **) group);
  } else {
    do_cothread_setfunc (group->cothread, osched->context,
        (cothread_func) wrapper, 0, (char **) group);
  }
#else
  group->schedulefunc = wrapper;
  group->argc = 0;
  group->argv = (char **) group;
#endif
  group->flags |= GST_OPT_SCHEDULER_GROUP_SCHEDULABLE;
}

static void
destroy_group_scheduler (GstOptSchedulerGroup * group)
{
  g_assert (group);

  if (group->flags & GST_OPT_SCHEDULER_GROUP_RUNNING)
    g_warning ("destroying running group scheduler");

#ifdef USE_COTHREADS
  if (group->cothread) {
    do_cothread_destroy (group->cothread);
    group->cothread = NULL;
  }
#else
  group->schedulefunc = NULL;
  group->argc = 0;
  group->argv = NULL;
#endif

  group->flags &= ~GST_OPT_SCHEDULER_GROUP_SCHEDULABLE;
}

static void
group_error_handler (GstOptSchedulerGroup * group)
{
  GST_DEBUG ("group %p has errored", group);

  chain_group_set_enabled (group->chain, group, FALSE);
  group->chain->sched->state = GST_OPT_SCHEDULER_STATE_ERROR;
}

/* this function enables/disables an element, it will set/clear a flag on the element 
 * and tells the chain that the group is enabled if all elements inside the group are
 * enabled */
static void
group_element_set_enabled (GstOptSchedulerGroup * group, GstElement * element,
    gboolean enabled)
{
  g_assert (group != NULL);
  g_assert (element != NULL);

  GST_LOG
      ("request to %d element %s in group %p, have %d elements enabled out of %d",
      enabled, GST_ELEMENT_NAME (element), group, group->num_enabled,
      group->num_elements);

  /* Note that if an unlinked PLAYING element is added to a bin, we have to
     create a new group to hold the element, and this function will be called
     before the group is added to the chain. Thus we have a valid case for
     group->chain==NULL. */

  if (enabled) {
    g_assert (group->num_enabled < group->num_elements);

    group->num_enabled++;

    GST_DEBUG
        ("enable element %s in group %p, now %d elements enabled out of %d",
        GST_ELEMENT_NAME (element), group, group->num_enabled,
        group->num_elements);

    if (group->num_enabled == group->num_elements) {
      if (!group->chain) {
        GST_DEBUG ("enable chainless group %p", group);
        GST_OPT_SCHEDULER_GROUP_ENABLE (group);
      } else {
        GST_LOG ("enable group %p", group);
        chain_group_set_enabled (group->chain, group, TRUE);
      }
    }
  } else {
    g_assert (group->num_enabled > 0);

    group->num_enabled--;

    GST_DEBUG
        ("disable element %s in group %p, now %d elements enabled out of %d",
        GST_ELEMENT_NAME (element), group, group->num_enabled,
        group->num_elements);

    if (group->num_enabled == 0) {
      if (!group->chain) {
        GST_DEBUG ("disable chainless group %p", group);
        GST_OPT_SCHEDULER_GROUP_DISABLE (group);
      } else {
        GST_LOG ("disable group %p", group);
        chain_group_set_enabled (group->chain, group, FALSE);
      }
    }
  }
}

/* a group is scheduled by doing a cothread switch to it or
 * by calling the schedule function. In the non-cothread case
 * we cannot run already running groups so we return FALSE here
 * to indicate this to the caller */
static gboolean
schedule_group (GstOptSchedulerGroup * group)
{
  if (!group->entry) {
    GST_INFO ("not scheduling group %p without entry", group);
    return FALSE;
  }
#ifdef USE_COTHREADS
  if (group->cothread)
    do_cothread_switch (group->cothread);
  else
    g_warning ("(internal error): trying to schedule group without cothread");
  return TRUE;
#else
  /* cothreads automatically call the pre- and post-run functions for us;
   * without cothreads we need to call them manually */
  if (group->schedulefunc == NULL) {
    GST_INFO ("not scheduling group %p without schedulefunc", group);
    return FALSE;
  } else {
    GSList *l;

    for (l = group->elements; l; l = l->next) {
      GstElement *e = (GstElement *) l->data;

      if (e->pre_run_func)
        e->pre_run_func (e);
    }

    group->schedulefunc (group->argc, group->argv);

    for (l = group->elements; l; l = l->next) {
      GstElement *e = (GstElement *) l->data;

      if (e->post_run_func)
        e->post_run_func (e);
    }

  }
  return TRUE;
#endif
}

#ifndef USE_COTHREADS
static void
gst_opt_scheduler_schedule_run_queue (GstOptScheduler * osched)
{
  GST_LOG_OBJECT (osched, "running queue: %d groups, recursed %d times",
      g_list_length (osched->runqueue),
      osched->recursion, g_list_length (osched->runqueue));

  /* note that we have a ref on each group on the queue (unref after running) */

  /* make sure we don't exceed max_recursion */
  if (osched->recursion > osched->max_recursion) {
    osched->state = GST_OPT_SCHEDULER_STATE_ERROR;
    return;
  }

  osched->recursion++;

  while (osched->runqueue) {
    GstOptSchedulerGroup *group;
    gboolean res;

    group = (GstOptSchedulerGroup *) osched->runqueue->data;

    /* runqueue holds refcount to group */
    osched->runqueue = g_list_remove (osched->runqueue, group);

    GST_LOG_OBJECT (osched, "scheduling group %p", group);

    res = schedule_group (group);
    if (!res) {
      g_warning ("error scheduling group %p", group);
      group_error_handler (group);
    } else {
      GST_LOG_OBJECT (osched, "done scheduling group %p", group);
    }
    unref_group (group);
  }

  GST_LOG_OBJECT (osched, "run queue length after scheduling %d",
      g_list_length (osched->runqueue));

  osched->recursion--;
}
#endif

/* a chain is scheduled by picking the first active group and scheduling it */
static void
schedule_chain (GstOptSchedulerChain * chain)
{
  GSList *groups;
  GstOptScheduler *osched;

  osched = chain->sched;

  /* if the chain has changed, we need to resort the groups so we enter in the
     proper place */
  if (GST_OPT_SCHEDULER_CHAIN_IS_DIRTY (chain))
    sort_chain (chain);
  GST_OPT_SCHEDULER_CHAIN_SET_CLEAN (chain);

  groups = chain->groups;
  while (groups) {
    GstOptSchedulerGroup *group = (GstOptSchedulerGroup *) groups->data;

    if (!GST_OPT_SCHEDULER_GROUP_IS_DISABLED (group)) {
      ref_group (group);
      GST_LOG ("scheduling group %p in chain %p", group, chain);

#ifdef USE_COTHREADS
      schedule_group (group);
#else
      osched->recursion = 0;
      if (!g_list_find (osched->runqueue, group)) {
        ref_group (group);
        osched->runqueue = g_list_append (osched->runqueue, group);
      }
      gst_opt_scheduler_schedule_run_queue (osched);
#endif

      GST_LOG ("done scheduling group %p in chain %p", group, chain);
      unref_group (group);
      break;
    }

    groups = g_slist_next (groups);
  }
}

/* a get-based group is scheduled by getting a buffer from the get based
 * entry point and by pushing the buffer to the peer.
 * We also set the running flag on this group for as long as this
 * function is running. */
static int
get_group_schedule_function (int argc, char *argv[])
{
  GstOptSchedulerGroup *group = (GstOptSchedulerGroup *) argv;
  GstElement *entry = group->entry;
  const GList *pads = gst_element_get_pad_list (entry);

  GST_LOG ("executing get-based group %p", group);

  group->flags |= GST_OPT_SCHEDULER_GROUP_RUNNING;

  while (pads) {
    GstData *data;
    GstPad *pad = GST_PAD (pads->data);

    pads = g_list_next (pads);

    /* skip sinks and ghostpads */
    if (!GST_PAD_IS_SRC (pad) || !GST_IS_REAL_PAD (pad))
      continue;

    GST_DEBUG ("doing get and push on pad \"%s:%s\" in group %p",
        GST_DEBUG_PAD_NAME (pad), group);

    data = gst_pad_call_get_function (pad);
    if (data) {
      if (GST_EVENT_IS_INTERRUPT (data)) {
        gst_event_unref (GST_EVENT (data));
        break;
      }
      gst_pad_push (pad, data);
    }
  }

  group->flags &= ~GST_OPT_SCHEDULER_GROUP_RUNNING;

  return 0;
}

/* a loop-based group is scheduled by calling the loop function
 * on the entry point. 
 * We also set the running flag on this group for as long as this
 * function is running. */
static int
loop_group_schedule_function (int argc, char *argv[])
{
  GstOptSchedulerGroup *group = (GstOptSchedulerGroup *) argv;
  GstElement *entry = group->entry;

  GST_LOG ("executing loop-based group %p", group);

  group->flags |= GST_OPT_SCHEDULER_GROUP_RUNNING;

  GST_DEBUG ("calling loopfunc of element %s in group %p",
      GST_ELEMENT_NAME (entry), group);

  if (entry->loopfunc)
    entry->loopfunc (entry);
  else
    group_error_handler (group);

  GST_LOG ("loopfunc ended of element %s in group %p",
      GST_ELEMENT_NAME (entry), group);

  group->flags &= ~GST_OPT_SCHEDULER_GROUP_RUNNING;

  return 0;

}

/* the function to schedule an unknown group, which just gives an error */
static int
unknown_group_schedule_function (int argc, char *argv[])
{
  GstOptSchedulerGroup *group = (GstOptSchedulerGroup *) argv;

  g_warning ("(internal error) unknown group type %d, disabling\n",
      group->type);
  group_error_handler (group);

  return 0;
}

/* this function is called when the first element of a chain-loop or a loop-loop
 * link performs a push to the loop element. We then schedule the
 * group with the loop-based element until the bufpen is empty */
static void
gst_opt_scheduler_loop_wrapper (GstPad * sinkpad, GstData * data)
{
  GstOptSchedulerGroup *group;
  GstOptScheduler *osched;
  GstRealPad *peer;

  group = GST_ELEMENT_SCHED_GROUP (GST_PAD_PARENT (sinkpad));
  osched = group->chain->sched;
  peer = GST_RPAD_PEER (sinkpad);

  GST_LOG ("chain handler for loop-based pad %" GST_PTR_FORMAT, sinkpad);

#ifdef USE_COTHREADS
  if (GST_PAD_BUFLIST (peer)) {
    g_warning ("deadlock detected, disabling group %p", group);
    group_error_handler (group);
  } else {
    GST_LOG ("queueing data %p on %s:%s's bufpen", data,
        GST_DEBUG_PAD_NAME (peer));
    GST_PAD_BUFPEN (peer) = g_list_append (GST_PAD_BUFLIST (peer), data);
    schedule_group (group);
  }
#else
  GST_LOG ("queueing data %p on %s:%s's bufpen", data,
      GST_DEBUG_PAD_NAME (peer));
  GST_PAD_BUFPEN (peer) = g_list_append (GST_PAD_BUFLIST (peer), data);
  if (!(group->flags & GST_OPT_SCHEDULER_GROUP_RUNNING)) {
    GST_LOG ("adding group %p to runqueue", group);
    if (!g_list_find (osched->runqueue, group)) {
      ref_group (group);
      osched->runqueue = g_list_append (osched->runqueue, group);
    }
  }
#endif

  GST_LOG ("%d buffers left on %s:%s's bufpen after chain handler",
      g_list_length (GST_PAD_BUFLIST (peer)));
}

/* this function is called by a loop based element that performs a
 * pull on a sinkpad. We schedule the peer group until the bufpen
 * is filled with the buffer so that this function  can return */
static GstData *
gst_opt_scheduler_get_wrapper (GstPad * srcpad)
{
  GstData *data;
  GstOptSchedulerGroup *group;
  GstOptScheduler *osched;
  gboolean disabled;

  GST_LOG ("get handler for %" GST_PTR_FORMAT, srcpad);

  /* first try to grab a queued buffer */
  if (GST_PAD_BUFLIST (srcpad)) {
    data = GST_PAD_BUFLIST (srcpad)->data;
    GST_PAD_BUFPEN (srcpad) = g_list_remove (GST_PAD_BUFLIST (srcpad), data);

    GST_LOG ("returning popped queued data %p", data);

    return data;
  }

  /* else we need to schedule the peer element */
  group = GST_ELEMENT_SCHED_GROUP (GST_PAD_PARENT (srcpad));
  osched = group->chain->sched;
  data = NULL;
  disabled = FALSE;

  do {
    GST_LOG ("scheduling upstream group %p to fill bufpen", group);
#ifdef USE_COTHREADS
    schedule_group (group);
#else
    if (!(group->flags & GST_OPT_SCHEDULER_GROUP_RUNNING)) {
      ref_group (group);

      if (!g_list_find (osched->runqueue, group)) {
        ref_group (group);
        osched->runqueue = g_list_append (osched->runqueue, group);
      }

      GST_LOG ("recursing into scheduler group %p", group);
      gst_opt_scheduler_schedule_run_queue (osched);
      GST_LOG ("return from recurse group %p", group);

      /* if the other group was disabled we might have to break out of the loop */
      disabled = GST_OPT_SCHEDULER_GROUP_IS_DISABLED (group);
      group = unref_group (group);
      /* group is gone */
      if (group == NULL) {
        /* if the group was gone we also might have to break out of the loop */
        disabled = TRUE;
      }
    } else {
      /* in this case, the group was running and we wanted to swtich to it,
       * this is not allowed in the optimal scheduler (yet) */
      g_warning ("deadlock detected, disabling group %p", group);
      group_error_handler (group);
      return GST_DATA (gst_event_new (GST_EVENT_INTERRUPT));
    }
#endif
    /* if the scheduler interrupted, make sure we send an INTERRUPTED event to the
     * loop based element */
    if (osched->state == GST_OPT_SCHEDULER_STATE_INTERRUPTED) {
      GST_INFO ("scheduler interrupted, return interrupt event");
      data = GST_DATA (gst_event_new (GST_EVENT_INTERRUPT));
    } else {
      if (GST_PAD_BUFLIST (srcpad)) {
        data = GST_PAD_BUFLIST (srcpad)->data;
        GST_PAD_BUFPEN (srcpad) =
            g_list_remove (GST_PAD_BUFLIST (srcpad), data);
      } else if (disabled) {
        /* no buffer in queue and peer group was disabled */
        data = GST_DATA (gst_event_new (GST_EVENT_INTERRUPT));
      }
    }
  }
  while (data == NULL);

  GST_LOG ("get handler, returning data %p, queue length %d",
      data, g_list_length (GST_PAD_BUFLIST (srcpad)));

  return data;
}

static void
pad_clear_queued (GstPad * srcpad, gpointer user_data)
{
  GList *buflist = GST_PAD_BUFLIST (srcpad);

  if (buflist) {
    GST_LOG ("need to clear some buffers");
    g_list_foreach (buflist, (GFunc) gst_data_unref, NULL);
    g_list_free (buflist);
    GST_PAD_BUFPEN (srcpad) = NULL;
  }
}

static gboolean
gst_opt_scheduler_event_wrapper (GstPad * srcpad, GstEvent * event)
{
  gboolean flush;

  GST_DEBUG ("intercepting event %d on pad %s:%s",
      GST_EVENT_TYPE (event), GST_DEBUG_PAD_NAME (srcpad));

  /* figure out if this is a flush event */
  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH:
      flush = TRUE;
      break;
    case GST_EVENT_SEEK:
    case GST_EVENT_SEEK_SEGMENT:
      flush = GST_EVENT_SEEK_FLAGS (event) & GST_SEEK_FLAG_FLUSH;
      break;
    default:
      flush = FALSE;
      break;
  }

  if (flush) {
    GST_LOG ("event is flush");

    pad_clear_queued (srcpad, NULL);
  }
  return GST_RPAD_EVENTFUNC (srcpad) (srcpad, event);
}

static GstElementStateReturn
gst_opt_scheduler_state_transition (GstScheduler * sched, GstElement * element,
    gint transition)
{
  GstOptSchedulerGroup *group;
  GstElementStateReturn res = GST_STATE_SUCCESS;

  GST_DEBUG ("element \"%s\" state change %d", GST_ELEMENT_NAME (element),
      transition);

  /* we check the state of the managing pipeline here */
  if (GST_IS_BIN (element)) {
    if (GST_SCHEDULER_PARENT (sched) == element) {
      GST_LOG ("parent \"%s\" changed state", GST_ELEMENT_NAME (element));

      switch (transition) {
        case GST_STATE_PLAYING_TO_PAUSED:
          GST_INFO ("setting scheduler state to stopped");
          GST_SCHEDULER_STATE (sched) = GST_SCHEDULER_STATE_STOPPED;
          break;
        case GST_STATE_PAUSED_TO_PLAYING:
          GST_INFO ("setting scheduler state to running");
          GST_SCHEDULER_STATE (sched) = GST_SCHEDULER_STATE_RUNNING;
          break;
        default:
          GST_LOG ("no interesting state change, doing nothing");
      }
    }
    return res;
  }

  /* we don't care about decoupled elements after this */
  if (GST_ELEMENT_IS_DECOUPLED (element))
    return GST_STATE_SUCCESS;

  /* get the group of the element */
  group = GST_ELEMENT_SCHED_GROUP (element);

  switch (transition) {
    case GST_STATE_PAUSED_TO_PLAYING:
      /* an element without a group has to be an unlinked src, sink
       * filter element */
      if (!group) {
        GST_INFO ("element \"%s\" has no group", GST_ELEMENT_NAME (element));
      }
      /* else construct the scheduling context of this group and enable it */
      else {
        group_element_set_enabled (group, element, TRUE);
      }
      break;
    case GST_STATE_PLAYING_TO_PAUSED:
      /* if the element still has a group, we disable it */
      if (group)
        group_element_set_enabled (group, element, FALSE);
      break;
    case GST_STATE_PAUSED_TO_READY:
    {
      GList *pads = (GList *) gst_element_get_pad_list (element);

      g_list_foreach (pads, (GFunc) pad_clear_queued, NULL);
      break;
    }
    default:
      break;
  }

  return res;
}

static void
gst_opt_scheduler_scheduling_change (GstScheduler * sched, GstElement * element)
{
  g_warning ("scheduling change, implement me");
}

static void
get_group (GstElement * element, GstOptSchedulerGroup ** group)
{
  GstOptSchedulerCtx *ctx;

  /*GList *pads; */

  ctx = GST_ELEMENT_SCHED_CONTEXT (element);
  if (ctx)
    *group = ctx->group;
  else
    *group = NULL;
}

/*
 * the idea is to put the two elements into the same group. 
 * - When no element is inside a group, we create a new group and add 
 *   the elements to it. 
 * - When one of the elements has a group, add the other element to 
 *   that group
 * - if both of the elements have a group, we merge the groups, which
 *   will also merge the chains.
 * Group links must be managed by the caller.
 */
static GstOptSchedulerGroup *
group_elements (GstOptScheduler * osched, GstElement * element1,
    GstElement * element2, GstOptSchedulerGroupType type)
{
  GstOptSchedulerGroup *group1, *group2, *group = NULL;

  get_group (element1, &group1);
  get_group (element2, &group2);

  /* none of the elements is added to a group, create a new group
   * and chain to add the elements to */
  if (!group1 && !group2) {
    GstOptSchedulerChain *chain;

    GST_DEBUG ("creating new group to hold \"%s\" and \"%s\"",
        GST_ELEMENT_NAME (element1), GST_ELEMENT_NAME (element2));

    chain = create_chain (osched);
    group = create_group (chain, element1, type);
    add_to_group (group, element2, TRUE);
  }
  /* the first element has a group */
  else if (group1) {
    GST_DEBUG ("adding \"%s\" to \"%s\"'s group",
        GST_ELEMENT_NAME (element2), GST_ELEMENT_NAME (element1));

    /* the second element also has a group, merge */
    if (group2)
      merge_groups (group1, group2);
    /* the second element has no group, add it to the group
     * of the first element */
    else
      add_to_group (group1, element2, TRUE);

    group = group1;
  }
  /* element1 has no group, element2 does. Add element1 to the
   * group of element2 */
  else {
    GST_DEBUG ("adding \"%s\" to \"%s\"'s group",
        GST_ELEMENT_NAME (element1), GST_ELEMENT_NAME (element2));
    add_to_group (group2, element1, TRUE);
    group = group2;
  }
  return group;
}

/*
 * increment link counts between groups -- it's important that src is actually
 * the src group, so we can introspect the topology later
 */
static void
group_inc_link (GstOptSchedulerGroup * src, GstOptSchedulerGroup * sink)
{
  GSList *links = src->group_links;
  gboolean done = FALSE;
  GstOptSchedulerGroupLink *link;

  /* first try to find a previous link */
  while (links && !done) {
    link = (GstOptSchedulerGroupLink *) links->data;
    links = g_slist_next (links);

    if (IS_GROUP_LINK (link, src, sink)) {
      /* we found a link to this group, increment the link count */
      link->count++;
      GST_LOG ("incremented group link count between %p and %p to %d",
          src, sink, link->count);
      done = TRUE;
    }
  }
  if (!done) {
    /* no link was found, create a new one */
    link = g_new0 (GstOptSchedulerGroupLink, 1);

    link->src = src;
    link->sink = sink;
    link->count = 1;

    src->group_links = g_slist_prepend (src->group_links, link);
    sink->group_links = g_slist_prepend (sink->group_links, link);

    GST_DEBUG ("added group link between %p and %p", src, sink);
  }
}

/*
 * decrement link counts between groups, returns TRUE if the link count reaches
 * 0 -- note that the groups are not necessarily ordered as (src, sink) like
 * inc_link requires
 */
static gboolean
group_dec_link (GstOptSchedulerGroup * group1, GstOptSchedulerGroup * group2)
{
  GSList *links = group1->group_links;
  gboolean res = FALSE;
  GstOptSchedulerGroupLink *link;

  while (links) {
    link = (GstOptSchedulerGroupLink *) links->data;
    links = g_slist_next (links);

    if (IS_GROUP_LINK (link, group1, group2)) {
      g_assert (link->count > 0);
      link->count--;
      GST_LOG ("link count between %p and %p is now %d",
          group1, group2, link->count);
      if (link->count == 0) {
        group1->group_links = g_slist_remove (group1->group_links, link);
        group2->group_links = g_slist_remove (group2->group_links, link);
        g_free (link);
        GST_DEBUG ("removed group link between %p and %p", group1, group2);
        res = TRUE;
      }
      break;
    }
  }
  return res;
}


typedef enum
{
  GST_OPT_INVALID,
  GST_OPT_GET_TO_CHAIN,
  GST_OPT_LOOP_TO_CHAIN,
  GST_OPT_GET_TO_LOOP,
  GST_OPT_CHAIN_TO_CHAIN,
  GST_OPT_CHAIN_TO_LOOP,
  GST_OPT_LOOP_TO_LOOP,
}
LinkType;

/*
 * Entry points for this scheduler.
 */
static void
gst_opt_scheduler_setup (GstScheduler * sched)
{
#ifdef USE_COTHREADS
  GstOptScheduler *osched = GST_OPT_SCHEDULER (sched);

  /* first create thread context */
  if (osched->context == NULL) {
    GST_DEBUG ("initializing cothread context");
    osched->context = do_cothread_context_init ();
  }
#endif
}

static void
gst_opt_scheduler_reset (GstScheduler * sched)
{
#ifdef USE_COTHREADS
  GstOptScheduler *osched = GST_OPT_SCHEDULER (sched);
  GSList *chains = osched->chains;

  while (chains) {
    GstOptSchedulerChain *chain = (GstOptSchedulerChain *) chains->data;
    GSList *groups = chain->groups;

    while (groups) {
      GstOptSchedulerGroup *group = (GstOptSchedulerGroup *) groups->data;

      destroy_group_scheduler (group);
      groups = groups->next;
    }
    chains = chains->next;
  }

  if (osched->context) {
    do_cothread_context_destroy (osched->context);
    osched->context = NULL;
  }
#endif
}

static void
gst_opt_scheduler_add_element (GstScheduler * sched, GstElement * element)
{
  GstOptScheduler *osched = GST_OPT_SCHEDULER (sched);
  GstOptSchedulerCtx *ctx;
  const GList *pads;

  GST_DEBUG_OBJECT (sched, "adding element \"%s\"", GST_OBJECT_NAME (element));

  /* decoupled elements are not added to the scheduler lists */
  if (GST_ELEMENT_IS_DECOUPLED (element))
    return;

  ctx = g_new0 (GstOptSchedulerCtx, 1);
  GST_ELEMENT (element)->sched_private = ctx;
  ctx->flags = GST_OPT_SCHEDULER_CTX_DISABLED;

  /* set event handler on all pads here so events work unconnected too;
   * in _link, it can be overruled if need be */
  /* FIXME: we should also do this when new pads on the element are created;
     but there are no hooks, so we do it again in _link */
  pads = gst_element_get_pad_list (element);
  while (pads) {
    GstPad *pad = GST_PAD (pads->data);

    pads = g_list_next (pads);

    if (!GST_IS_REAL_PAD (pad))
      continue;
    GST_RPAD_EVENTHANDLER (pad) = GST_RPAD_EVENTFUNC (pad);
  }

  /* loop based elements *always* end up in their own group. It can eventually
   * be merged with another group when a link is made */
  if (element->loopfunc) {
    GstOptSchedulerGroup *group;
    GstOptSchedulerChain *chain;

    chain = create_chain (osched);

    group = create_group (chain, element, GST_OPT_SCHEDULER_GROUP_LOOP);
    group->entry = element;

    GST_LOG ("added element \"%s\" as loop based entry",
        GST_ELEMENT_NAME (element));
  }
}

static void
gst_opt_scheduler_remove_element (GstScheduler * sched, GstElement * element)
{
  GstOptSchedulerGroup *group;

  GST_DEBUG_OBJECT (sched, "removing element \"%s\"",
      GST_OBJECT_NAME (element));

  /* decoupled elements are not added to the scheduler lists and should therefore
   * not be removed */
  if (GST_ELEMENT_IS_DECOUPLED (element))
    return;

  /* the element is guaranteed to live in it's own group/chain now */
  get_group (element, &group);
  if (group) {
    remove_from_group (group, element);
  }

  g_free (GST_ELEMENT (element)->sched_private);
  GST_ELEMENT (element)->sched_private = NULL;
}

static gboolean
gst_opt_scheduler_yield (GstScheduler * sched, GstElement * element)
{
#ifdef USE_COTHREADS
  /* yield hands control to the main cothread context if the requesting 
   * element is the entry point of the group */
  GstOptSchedulerGroup *group;

  get_group (element, &group);
  if (group && group->entry == element)
    do_cothread_switch (do_cothread_get_main (((GstOptScheduler *) sched)->
            context));

  return FALSE;
#else
  g_warning ("element %s performs a yield, please fix the element",
      GST_ELEMENT_NAME (element));
  return TRUE;
#endif
}

static gboolean
gst_opt_scheduler_interrupt (GstScheduler * sched, GstElement * element)
{
  GST_INFO ("interrupt from \"%s\"", GST_OBJECT_NAME (element));

#ifdef USE_COTHREADS
  do_cothread_switch (do_cothread_get_main (((GstOptScheduler *) sched)->
          context));
  return FALSE;
#else
  {
    GstOptScheduler *osched = GST_OPT_SCHEDULER (sched);

    GST_INFO ("scheduler set interrupted state");
    osched->state = GST_OPT_SCHEDULER_STATE_INTERRUPTED;
  }
  return TRUE;
#endif
}

static void
gst_opt_scheduler_error (GstScheduler * sched, GstElement * element)
{
  GstOptScheduler *osched = GST_OPT_SCHEDULER (sched);
  GstOptSchedulerGroup *group;

  get_group (element, &group);
  if (group)
    group_error_handler (group);

  osched->state = GST_OPT_SCHEDULER_STATE_ERROR;
}

/* link pads, merge groups and chains */
static void
gst_opt_scheduler_pad_link (GstScheduler * sched, GstPad * srcpad,
    GstPad * sinkpad)
{
  GstOptScheduler *osched = GST_OPT_SCHEDULER (sched);
  LinkType type = GST_OPT_INVALID;
  GstElement *src_element, *sink_element;

  GST_INFO ("scheduling link between %s:%s and %s:%s",
      GST_DEBUG_PAD_NAME (srcpad), GST_DEBUG_PAD_NAME (sinkpad));

  src_element = GST_PAD_PARENT (srcpad);
  sink_element = GST_PAD_PARENT (sinkpad);

  /* first we need to figure out what type of link we're dealing
   * with */
  if (src_element->loopfunc && sink_element->loopfunc)
    type = GST_OPT_LOOP_TO_LOOP;
  else {
    if (src_element->loopfunc) {
      if (GST_RPAD_CHAINFUNC (sinkpad))
        type = GST_OPT_LOOP_TO_CHAIN;
    } else if (sink_element->loopfunc) {
      if (GST_RPAD_GETFUNC (srcpad)) {
        type = GST_OPT_GET_TO_LOOP;
        /* this could be tricky, the get based source could 
         * already be part of a loop based group in another pad,
         * we assert on that for now */
        if (GST_ELEMENT_SCHED_CONTEXT (src_element) != NULL &&
            GST_ELEMENT_SCHED_GROUP (src_element) != NULL) {
          GstOptSchedulerGroup *group = GST_ELEMENT_SCHED_GROUP (src_element);

          /* if the loop based element is the entry point we're ok, if it
           * isn't then we have multiple loop based elements in this group */
          if (group->entry != sink_element) {
            g_error
                ("internal error: cannot schedule get to loop in multi-loop based group");
            return;
          }
        }
      } else
        type = GST_OPT_CHAIN_TO_LOOP;
    } else {
      if (GST_RPAD_GETFUNC (srcpad) && GST_RPAD_CHAINFUNC (sinkpad)) {
        type = GST_OPT_GET_TO_CHAIN;
        /* the get based source could already be part of a loop 
         * based group in another pad, we assert on that for now */
        if (GST_ELEMENT_SCHED_CONTEXT (src_element) != NULL &&
            GST_ELEMENT_SCHED_GROUP (src_element) != NULL) {
          GstOptSchedulerGroup *group = GST_ELEMENT_SCHED_GROUP (src_element);

          /* if the get based element is the entry point we're ok, if it
           * isn't then we have a mixed loop/chain based group */
          if (group->entry != src_element) {
            g_error ("internal error: cannot schedule get to chain "
                "with mixed loop/chain based group");
            return;
          }
        }
      } else
        type = GST_OPT_CHAIN_TO_CHAIN;
    }
  }

  /* since we can't set event handlers on pad creation after addition, it is
   * best we set all of them again to the default before linking */
  GST_RPAD_EVENTHANDLER (srcpad) = GST_RPAD_EVENTFUNC (srcpad);
  GST_RPAD_EVENTHANDLER (sinkpad) = GST_RPAD_EVENTFUNC (sinkpad);

  /* for each link type, perform specific actions */
  switch (type) {
    case GST_OPT_GET_TO_CHAIN:
    {
      GstOptSchedulerGroup *group = NULL;

      GST_LOG ("get to chain based link");

      /* setup get/chain handlers */
      GST_RPAD_GETHANDLER (srcpad) = gst_pad_call_get_function;
      GST_RPAD_CHAINHANDLER (sinkpad) = gst_pad_call_chain_function;

      /* the two elements should be put into the same group, 
       * this also means that they are in the same chain automatically */
      group = group_elements (osched, src_element, sink_element,
          GST_OPT_SCHEDULER_GROUP_GET);

      /* if there is not yet an entry in the group, select the source
       * element as the entry point and mark the group as a get based
       * group */
      if (!group->entry) {
        group->entry = src_element;
        group->type = GST_OPT_SCHEDULER_GROUP_GET;

        GST_DEBUG ("setting \"%s\" as entry point of _get-based group %p",
            GST_ELEMENT_NAME (src_element), group);
      }
      break;
    }
    case GST_OPT_LOOP_TO_CHAIN:
    case GST_OPT_CHAIN_TO_CHAIN:
      GST_LOG ("loop/chain to chain based link");

      GST_RPAD_CHAINHANDLER (sinkpad) = gst_pad_call_chain_function;

      /* the two elements should be put into the same group, this also means
       * that they are in the same chain automatically, in case of a loop-based
       * src_element, there will be a group for src_element and sink_element
       * will be added to it. In the case a new group is created, we can't know
       * the type so we pass UNKNOWN as an arg */
      group_elements (osched, src_element, sink_element,
          GST_OPT_SCHEDULER_GROUP_UNKNOWN);
      break;
    case GST_OPT_GET_TO_LOOP:
      GST_LOG ("get to loop based link");

      GST_RPAD_GETHANDLER (srcpad) = gst_pad_call_get_function;

      /* the two elements should be put into the same group, this also means
       * that they are in the same chain automatically, sink_element is
       * loop-based so it already has a group where src_element will be added
       * to */
      group_elements (osched, src_element, sink_element,
          GST_OPT_SCHEDULER_GROUP_LOOP);
      break;
    case GST_OPT_CHAIN_TO_LOOP:
    case GST_OPT_LOOP_TO_LOOP:
    {
      GstOptSchedulerGroup *group1, *group2;

      GST_LOG ("chain/loop to loop based link");

      GST_RPAD_CHAINHANDLER (sinkpad) = gst_opt_scheduler_loop_wrapper;
      GST_RPAD_GETHANDLER (srcpad) = gst_opt_scheduler_get_wrapper;
      /* events on the srcpad have to be intercepted as we might need to
       * flush the buffer lists, so override the given eventfunc */
      GST_RPAD_EVENTHANDLER (srcpad) = gst_opt_scheduler_event_wrapper;

      group1 = GST_ELEMENT_SCHED_GROUP (src_element);
      group2 = GST_ELEMENT_SCHED_GROUP (sink_element);

      g_assert (group2 != NULL);

      /* group2 is guaranteed to exist as it contains a loop-based element.
       * group1 only exists if src_element is linked to some other element */
      if (!group1) {
        /* create a new group for src_element as it cannot be merged into another group
         * here. we create the group in the same chain as the loop-based element. */
        GST_DEBUG ("creating new group for element %s",
            GST_ELEMENT_NAME (src_element));
        group1 =
            create_group (group2->chain, src_element,
            GST_OPT_SCHEDULER_GROUP_LOOP);
      } else {
        /* both elements are already in a group, make sure they are added to
         * the same chain */
        merge_chains (group1->chain, group2->chain);
      }
      group_inc_link (group1, group2);
      break;
    }
    case GST_OPT_INVALID:
      g_error ("(internal error) invalid element link, what are you doing?");
      break;
  }
}

/* 
 * checks if an element is still linked to some other element in the group. 
 * no checking is done on the brokenpad arg 
 */
static gboolean
element_has_link_with_group (GstElement * element, GstOptSchedulerGroup * group,
    GstPad * brokenpad)
{
  gboolean linked = FALSE;
  const GList *pads;

  /* see if the element has no more links to the peer group */
  pads = gst_element_get_pad_list (element);
  while (pads && !linked) {
    GstPad *pad = GST_PAD (pads->data);

    pads = g_list_next (pads);

    /* we only operate on real pads and on the pad that is not broken */
    if (!GST_IS_REAL_PAD (pad) || pad == brokenpad)
      continue;

    if (GST_PAD_PEER (pad)) {
      GstElement *parent;
      GstOptSchedulerGroup *parentgroup;

      /* see in what group this element is */
      parent = GST_PAD_PARENT (GST_PAD_PEER (pad));

      /* links with decoupled elements are valid */
      if (GST_ELEMENT_IS_DECOUPLED (parent)) {
        linked = TRUE;
      } else {
        /* for non-decoupled elements we need to check the group */
        get_group (parent, &parentgroup);

        /* if it's in the same group, we're still linked */
        if (parentgroup == group)
          linked = TRUE;
      }
    }
  }
  return linked;
}

/* 
 * checks if a target group is still reachable from the group without taking the broken
 * group link into account.
 */
static gboolean
group_can_reach_group (GstOptSchedulerGroup * group,
    GstOptSchedulerGroup * target)
{
  gboolean reachable = FALSE;
  const GSList *links = group->group_links;

  GST_LOG ("checking if group %p can reach %p", group, target);

  /* seems like we found the target element */
  if (group == target) {
    GST_LOG ("found way to reach %p", target);
    return TRUE;
  }

  /* if the group is marked as visited, we don't need to check here */
  if (GST_OPT_SCHEDULER_GROUP_IS_FLAG_SET (group,
          GST_OPT_SCHEDULER_GROUP_VISITED)) {
    GST_LOG ("already visited %p", group);
    return FALSE;
  }

  /* mark group as visited */
  GST_OPT_SCHEDULER_GROUP_SET_FLAG (group, GST_OPT_SCHEDULER_GROUP_VISITED);

  while (links && !reachable) {
    GstOptSchedulerGroupLink *link = (GstOptSchedulerGroupLink *) links->data;
    GstOptSchedulerGroup *other;

    links = g_slist_next (links);

    /* find other group in this link */
    other = OTHER_GROUP_LINK (link, group);

    GST_LOG ("found link from %p to %p, count %d", group, other, link->count);

    /* check if we can reach the target recursiveley */
    reachable = group_can_reach_group (other, target);
  }
  /* unset the visited flag, note that this is not optimal as we might be checking
   * groups several times when they are reachable with a loop. An alternative would be
   * to not clear the group flag at this stage but clear all flags in the chain when
   * all groups are checked. */
  GST_OPT_SCHEDULER_GROUP_UNSET_FLAG (group, GST_OPT_SCHEDULER_GROUP_VISITED);

  GST_LOG ("leaving group %p with %s", group, (reachable ? "TRUE" : "FALSE"));

  return reachable;
}

/*
 * Go through all the pads of the given element and decrement the links that
 * this group has with the group of the peer element.  This function is mainly used
 * to update the group connections before we remove the element from the group.
 */
static void
group_dec_links_for_element (GstOptSchedulerGroup * group, GstElement * element)
{
  GList *l;
  GstPad *pad;
  GstOptSchedulerGroup *peer_group;

  for (l = GST_ELEMENT_PADS (element); l; l = l->next) {
    pad = (GstPad *) l->data;
    if (GST_IS_REAL_PAD (pad) && GST_PAD_PEER (pad)) {
      get_group (GST_PAD_PARENT (GST_PAD_PEER (pad)), &peer_group);
      if (peer_group && peer_group != group)
        group_dec_link (group, peer_group);
    }
  }
}

/*
 * Go through all the pads of the given element and increment the links that
 * this group has with the group of the peer element.  This function is mainly used
 * to update the group connections before we add the element to the group.
 */
static void
group_inc_links_for_element (GstOptSchedulerGroup * group, GstElement * element)
{
  GList *l;
  GstPad *pad;
  GstOptSchedulerGroup *peer_group;

  for (l = GST_ELEMENT_PADS (element); l; l = l->next) {
    pad = (GstPad *) l->data;
    if (GST_IS_REAL_PAD (pad) && GST_PAD_PEER (pad)) {
      get_group (GST_PAD_PARENT (GST_PAD_PEER (pad)), &peer_group);
      if (peer_group && peer_group != group)
        group_inc_link (group, peer_group);
    }
  }
}

static void
gst_opt_scheduler_pad_unlink (GstScheduler * sched,
    GstPad * srcpad, GstPad * sinkpad)
{
  GstOptScheduler *osched = GST_OPT_SCHEDULER (sched);
  GstElement *src_element, *sink_element;
  GstOptSchedulerGroup *group1, *group2;

  GST_INFO ("unscheduling link between %s:%s and %s:%s",
      GST_DEBUG_PAD_NAME (srcpad), GST_DEBUG_PAD_NAME (sinkpad));

  src_element = GST_PAD_PARENT (srcpad);
  sink_element = GST_PAD_PARENT (sinkpad);

  get_group (src_element, &group1);
  get_group (sink_element, &group2);

  /* for decoupled elements (that are never put into a group) we use the
   * group of the peer element for the remainder of the algorithm */
  if (GST_ELEMENT_IS_DECOUPLED (src_element)) {
    group1 = group2;
  }
  if (GST_ELEMENT_IS_DECOUPLED (sink_element)) {
    group2 = group1;
  }

  /* if one the elements has no group (anymore) we don't really care 
   * about the link */
  if (!group1 || !group2) {
    GST_LOG
        ("one (or both) of the elements is not in a group, not interesting");
    return;
  }

  /* easy part, groups are different */
  if (group1 != group2) {
    gboolean zero;

    GST_LOG ("elements are in different groups");

    /* we can remove the links between the groups now */
    zero = group_dec_link (group1, group2);

    /* if the groups are not directly connected anymore, we have to perform a
     * recursive check to see if they are really unlinked */
    if (zero) {
      gboolean still_link;
      GstOptSchedulerChain *chain;

      /* see if group1 and group2 are still connected in any indirect way */
      still_link = group_can_reach_group (group1, group2);

      GST_DEBUG ("group %p %s reach group %p", group1,
          (still_link ? "can" : "can't"), group2);
      if (!still_link) {
        /* groups are really disconnected, migrate one group to a new chain */
        chain = create_chain (osched);
        chain_recursively_migrate_group (chain, group1);

        GST_DEBUG ("migrated group %p to new chain %p", group1, chain);
      }
    } else {
      GST_DEBUG ("group %p still has direct link with group %p", group1,
          group2);
    }
  }
  /* hard part, groups are equal */
  else {
    gboolean still_link1, still_link2;
    GstOptSchedulerGroup *group;

    /* since group1 == group2, it doesn't matter which group we take */
    group = group1;

    GST_LOG ("elements are in the same group %p", group);

    /* check if the element is still linked to some other element in the group,
     * we pass the pad that is broken up as an arg because a link on that pad
     * is not valid anymore.
     * Note that this check is only to make sure that a single element can be removed 
     * completely from the group, we also have to check for migrating several 
     * elements to a new group. */
    still_link1 = element_has_link_with_group (src_element, group, srcpad);
    still_link2 = element_has_link_with_group (sink_element, group, sinkpad);
    /* if there is still a link, we don't need to break this group */
    if (still_link1 && still_link2) {
      GSList *l;
      GList *m;
      int linkcount;

      GST_LOG ("elements still have links with other elements in the group");

      while (group->elements)
        for (l = group->elements; l && l->data; l = l->next) {
          GstElement *element = (GstElement *) l->data;

          if (!element || !GST_IS_ELEMENT (element) ||
              GST_ELEMENT_IS_DECOUPLED (element))
            continue;

          linkcount = 0;
          GST_LOG ("Examining %s\n", GST_ELEMENT_NAME (element));
          for (m = GST_ELEMENT_PADS (element); m; m = m->next) {
            GstPad *peer, *pad;
            GstElement *parent;
            GstOptSchedulerGroup *peer_group;

            pad = (GstPad *) m->data;
            if (!pad || !GST_IS_REAL_PAD (pad))
              continue;

            peer = GST_PAD_PEER (pad);
            if (!peer || !GST_IS_REAL_PAD (peer))
              continue;

            parent = GST_PAD_PARENT (GST_PAD_PEER (pad));
            get_group (parent, &peer_group);
            if (peer_group && peer_group != group) {
              GST_LOG ("pad %s is linked with %s\n",
                  GST_PAD_NAME (pad), GST_ELEMENT_NAME (parent));
              linkcount++;
            }
          }

          if (linkcount < 2) {
            remove_from_group (group, element);
          }
          /* if linkcount == 2, it will be unlinked later on */
          else if (linkcount > 2) {
            g_warning
                ("opt: Can't handle element %s with 3 or more links, aborting",
                GST_ELEMENT_NAME (element));
            return;
          }
        }
      /* Peer element will be caught during next iteration */
      return;
    }

    /* now check which one of the elements we can remove from the group */
    if (!still_link1) {
      /* we only remove elements that are not the entry point of a loop based
       * group and are not decoupled */
      if (!(group->entry == src_element &&
              group->type == GST_OPT_SCHEDULER_GROUP_LOOP) &&
          !GST_ELEMENT_IS_DECOUPLED (src_element)) {
        GST_LOG ("el ement1 is separated from the group");

        remove_from_group (group, src_element);
      } else {
        GST_LOG ("src_element is decoupled or entry in loop based group");
      }
    }

    if (!still_link2) {
      /* we only remove elements that are not the entry point of a loop based
       * group and are not decoupled */
      if (!(group->entry == sink_element &&
              group->type == GST_OPT_SCHEDULER_GROUP_LOOP) &&
          !GST_ELEMENT_IS_DECOUPLED (sink_element)) {
        GST_LOG ("sink_element is separated from the group");

        remove_from_group (group, sink_element);
      } else {
        GST_LOG ("sink_element is decoupled or entry in loop based group");
      }
    }
  }
}

/* a scheduler iteration is done by looping and scheduling the active chains */
static GstSchedulerState
gst_opt_scheduler_iterate (GstScheduler * sched)
{
  GstSchedulerState state = GST_SCHEDULER_STATE_STOPPED;
  GstOptScheduler *osched = GST_OPT_SCHEDULER (sched);
  gint iterations = osched->iterations;

  osched->state = GST_OPT_SCHEDULER_STATE_RUNNING;

  //gst_opt_scheduler_show (sched);

  GST_DEBUG_OBJECT (sched, "iterating");

  while (iterations) {
    gboolean scheduled = FALSE;
    GSList *chains;

    /* we have to schedule each of the scheduler chains now */
    chains = osched->chains;
    while (chains) {
      GstOptSchedulerChain *chain = (GstOptSchedulerChain *) chains->data;

      ref_chain (chain);
      /* if the chain is not disabled, schedule it */
      if (!GST_OPT_SCHEDULER_CHAIN_IS_DISABLED (chain)) {
        GST_LOG ("scheduling chain %p", chain);
        schedule_chain (chain);
        scheduled = TRUE;
      } else {
        GST_LOG ("not scheduling disabled chain %p", chain);
      }

      /* don't schedule any more chains when in error */
      if (osched->state == GST_OPT_SCHEDULER_STATE_ERROR) {
        GST_ERROR_OBJECT (sched, "in error state");
        break;
      } else if (osched->state == GST_OPT_SCHEDULER_STATE_INTERRUPTED) {
        GST_DEBUG_OBJECT (osched, "got interrupted, continue with next chain");
        osched->state = GST_OPT_SCHEDULER_STATE_RUNNING;
      }

      chains = g_slist_next (chains);
      unref_chain (chain);
    }

    /* at this point it's possible that the scheduler state is
     * in error, we then return an error */
    if (osched->state == GST_OPT_SCHEDULER_STATE_ERROR) {
      state = GST_SCHEDULER_STATE_ERROR;
      break;
    } else {
      /* if chains were scheduled, return our current state */
      if (scheduled)
        state = GST_SCHEDULER_STATE (sched);
      /* if no chains were scheduled, we say we are stopped */
      else {
        state = GST_SCHEDULER_STATE_STOPPED;
        break;
      }
    }
    if (iterations > 0)
      iterations--;
  }

  return state;
}


static void
gst_opt_scheduler_show (GstScheduler * sched)
{
  GstOptScheduler *osched = GST_OPT_SCHEDULER (sched);
  GSList *chains;

  g_print ("iterations:    %d\n", osched->iterations);
  g_print ("max recursion: %d\n", osched->max_recursion);

  chains = osched->chains;
  while (chains) {
    GstOptSchedulerChain *chain = (GstOptSchedulerChain *) chains->data;
    GSList *groups = chain->groups;

    chains = g_slist_next (chains);

    g_print ("+- chain %p: refcount %d, %d groups, %d enabled, flags %d\n",
        chain, chain->refcount, chain->num_groups, chain->num_enabled,
        chain->flags);

    while (groups) {
      GstOptSchedulerGroup *group = (GstOptSchedulerGroup *) groups->data;
      GSList *elements = group->elements;
      GSList *group_links = group->group_links;

      groups = g_slist_next (groups);

      g_print
          (" +- group %p: refcount %d, %d elements, %d enabled, flags %d, entry %s, %s\n",
          group, group->refcount, group->num_elements, group->num_enabled,
          group->flags,
          (group->entry ? GST_ELEMENT_NAME (group->entry) : "(none)"),
          (group->type ==
              GST_OPT_SCHEDULER_GROUP_GET ? "get-based" : "loop-based"));

      while (elements) {
        GstElement *element = (GstElement *) elements->data;

        elements = g_slist_next (elements);

        g_print ("  +- element %s\n", GST_ELEMENT_NAME (element));
      }
      while (group_links) {
        GstOptSchedulerGroupLink *link =
            (GstOptSchedulerGroupLink *) group_links->data;

        group_links = g_slist_next (group_links);

        g_print ("group link %p between %p and %p, count %d\n",
            link, link->src, link->sink, link->count);
      }
    }
  }
}

static void
gst_opt_scheduler_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstOptScheduler *osched;

  g_return_if_fail (GST_IS_OPT_SCHEDULER (object));

  osched = GST_OPT_SCHEDULER (object);

  switch (prop_id) {
    case ARG_ITERATIONS:
      g_value_set_int (value, osched->iterations);
      break;
    case ARG_MAX_RECURSION:
      g_value_set_int (value, osched->max_recursion);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_opt_scheduler_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstOptScheduler *osched;

  g_return_if_fail (GST_IS_OPT_SCHEDULER (object));

  osched = GST_OPT_SCHEDULER (object);

  switch (prop_id) {
    case ARG_ITERATIONS:
      osched->iterations = g_value_get_int (value);
      break;
    case ARG_MAX_RECURSION:
      osched->max_recursion = g_value_get_int (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}
