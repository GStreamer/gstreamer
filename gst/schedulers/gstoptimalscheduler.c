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

/*#define GST_DEBUG_ENABLED */
#include <gst/gst.h>

#ifdef USE_COTHREADS
# include "cothreads_compat.h"
#else
# define COTHREADS_NAME_CAPITAL ""
# define COTHREADS_NAME 	""
#endif

#define GST_ELEMENT_SCHED_CONTEXT(elem)		((GstOptSchedulerCtx*) (GST_ELEMENT_CAST (elem)->sched_private))
#define GST_ELEMENT_SCHED_GROUP(elem)		(GST_ELEMENT_SCHED_CONTEXT (elem)->group)
#define GST_PAD_BUFLIST(pad)            	((GList*) (GST_REAL_PAD_CAST(pad)->sched_private))

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

#define GST_OPT_SCHEDULER_CAST(sched)	((GstOptScheduler *)(sched))

typedef enum {
  GST_OPT_SCHEDULER_STATE_NONE,
  GST_OPT_SCHEDULER_STATE_STOPPED,
  GST_OPT_SCHEDULER_STATE_ERROR,
  GST_OPT_SCHEDULER_STATE_RUNNING,
  GST_OPT_SCHEDULER_STATE_INTERRUPTED
} GstOptSchedulerState;

struct _GstOptScheduler {
  GstScheduler 		 parent;

  GstOptSchedulerState	 state;

#ifdef USE_COTHREADS
  cothread_context 	*context;
#endif
  gint 			 iterations;

  GSList 		*elements;
  GSList 		*chains;

  GList			*runqueue;
  gint			 recursion;

  gint			 max_recursion;
};

struct _GstOptSchedulerClass {
  GstSchedulerClass parent_class;
};

static GType _gst_opt_scheduler_type = 0;

typedef enum {
  GST_OPT_SCHEDULER_CHAIN_DIRTY			= (1 << 1),	
  GST_OPT_SCHEDULER_CHAIN_DISABLED		= (1 << 2),
  GST_OPT_SCHEDULER_CHAIN_RUNNING		= (1 << 3),
} GstOptSchedulerChainFlags;

#define GST_OPT_SCHEDULER_CHAIN_DISABLE(chain) 		((chain)->flags |= GST_OPT_SCHEDULER_CHAIN_DISABLED)
#define GST_OPT_SCHEDULER_CHAIN_ENABLE(chain) 		((chain)->flags &= ~GST_OPT_SCHEDULER_CHAIN_DISABLED)
#define GST_OPT_SCHEDULER_CHAIN_IS_DISABLED(chain) 	((chain)->flags & GST_OPT_SCHEDULER_CHAIN_DISABLED)

typedef struct _GstOptSchedulerChain GstOptSchedulerChain;

struct _GstOptSchedulerChain {
  gint				 refcount;
  
  GstOptScheduler 		*sched;

  GstOptSchedulerChainFlags	 flags;
  
  GSList 			*groups;			/* the groups in this chain */
  gint				 num_groups;
  gint				 num_enabled;
};

/* 
 * elements that are scheduled in one cothread 
 */
typedef enum {
  GST_OPT_SCHEDULER_GROUP_DIRTY			= (1 << 1),	/* this group has been modified */
  GST_OPT_SCHEDULER_GROUP_COTHREAD_STOPPING	= (1 << 2),	/* the group's cothread stops after one iteration */
  GST_OPT_SCHEDULER_GROUP_DISABLED		= (1 << 3),	/* this group is disabled */
  GST_OPT_SCHEDULER_GROUP_RUNNING		= (1 << 4),	/* this group is running */
  GST_OPT_SCHEDULER_GROUP_SCHEDULABLE		= (1 << 5),	/* this group is schedulable */
} GstOptSchedulerGroupFlags;

typedef enum {
  GST_OPT_SCHEDULER_GROUP_GET			= 1,
  GST_OPT_SCHEDULER_GROUP_LOOP			= 2,
} GstOptSchedulerGroupType;

#define GST_OPT_SCHEDULER_GROUP_SET_FLAG(group,flag) 	((group)->flags |= (flag))
#define GST_OPT_SCHEDULER_GROUP_UNSET_FLAG(group,flag) 	((group)->flags &= (flag))
#define GST_OPT_SCHEDULER_GROUP_IS_FLAG_SET(group,flag) ((group)->flags & (flag))

#define GST_OPT_SCHEDULER_GROUP_DISABLE(group) 		((group)->flags |= GST_OPT_SCHEDULER_GROUP_DISABLED)
#define GST_OPT_SCHEDULER_GROUP_ENABLE(group) 		((group)->flags &= ~GST_OPT_SCHEDULER_GROUP_DISABLED)
#define GST_OPT_SCHEDULER_GROUP_IS_ENABLED(group) 	(!((group)->flags & GST_OPT_SCHEDULER_GROUP_DISABLED))
#define GST_OPT_SCHEDULER_GROUP_IS_DISABLED(group) 	((group)->flags & GST_OPT_SCHEDULER_GROUP_DISABLED)

typedef struct _GstOptSchedulerGroup GstOptSchedulerGroup;

typedef int (*GroupScheduleFunction)	(int argc, char *argv[]);

struct _GstOptSchedulerGroup {
  GstOptSchedulerChain 		*chain;  		/* the chain this group belongs to */
  GstOptSchedulerGroupFlags	 flags;			/* flags for this group */
  GstOptSchedulerGroupType	 type;			/* flags for this group */

  gint				 refcount;

  GSList 			*elements;		/* elements of this group */
  gint				 num_elements;
  gint				 num_enabled;
  GstElement 			*entry;			/* the group's entry point */

  GSList			*providers;		/* other groups that provide data
							   for this group */

#ifdef USE_COTHREADS
  cothread 			*cothread;		/* the cothread of this group */
#endif
  GroupScheduleFunction 	 schedulefunc;
  int				 argc;
  char			       **argv;
};

/* some group operations */
static GstOptSchedulerGroup* 	ref_group 		(GstOptSchedulerGroup *group);
#ifndef USE_COTHREADS
static GstOptSchedulerGroup* 	ref_group_by_count 	(GstOptSchedulerGroup *group, gint count);
#endif
static GstOptSchedulerGroup* 	unref_group 		(GstOptSchedulerGroup *group);
static void 			destroy_group 		(GstOptSchedulerGroup *group);

/* 
 * Scheduler private data for an element 
 */
typedef struct _GstOptSchedulerCtx GstOptSchedulerCtx;

typedef enum {
  GST_OPT_SCHEDULER_CTX_DISABLED		= (1 << 1),	/* the element is disabled */
} GstOptSchedulerCtxFlags;

struct _GstOptSchedulerCtx {
  GstOptSchedulerGroup *group;  			/* the group this element belongs to */

  GstOptSchedulerCtxFlags flags;			/* flags for this element */
};

enum
{
  ARG_0,
  ARG_ITERATIONS,
  ARG_MAX_RECURSION,
};
 

static void 		gst_opt_scheduler_class_init 		(GstOptSchedulerClass *klass);
static void 		gst_opt_scheduler_init 			(GstOptScheduler *scheduler);

static void 		gst_opt_scheduler_set_property 		(GObject *object, guint prop_id,
		               					 const GValue *value, GParamSpec *pspec);
static void 		gst_opt_scheduler_get_property 		(GObject *object, guint prop_id,
		               					 GValue *value, GParamSpec *pspec);

static void 		gst_opt_scheduler_dispose 		(GObject *object);

static void 		gst_opt_scheduler_setup 		(GstScheduler *sched);
static void 		gst_opt_scheduler_reset 		(GstScheduler *sched);
static void		gst_opt_scheduler_add_element		(GstScheduler *sched, GstElement *element);
static void     	gst_opt_scheduler_remove_element	(GstScheduler *sched, GstElement *element);
static GstElementStateReturn  
			gst_opt_scheduler_state_transition	(GstScheduler *sched, GstElement *element, gint transition);
static void             gst_opt_scheduler_scheduling_change     (GstScheduler *sched, GstElement *element);
static void 		gst_opt_scheduler_lock_element 		(GstScheduler *sched, GstElement *element);
static void 		gst_opt_scheduler_unlock_element 	(GstScheduler *sched, GstElement *element);
static gboolean		gst_opt_scheduler_yield 		(GstScheduler *sched, GstElement *element);
static gboolean		gst_opt_scheduler_interrupt 		(GstScheduler *sched, GstElement *element);
static void 		gst_opt_scheduler_error	 		(GstScheduler *sched, GstElement *element);
static void     	gst_opt_scheduler_pad_link		(GstScheduler *sched, GstPad *srcpad, GstPad *sinkpad);
static void     	gst_opt_scheduler_pad_unlink 	(GstScheduler *sched, GstPad *srcpad, GstPad *sinkpad);
static GstPad*  	gst_opt_scheduler_pad_select 		(GstScheduler *sched, GList *padlist);
static GstClockReturn   gst_opt_scheduler_clock_wait        	(GstScheduler *sched, GstElement *element,
	                                                         GstClockID id, GstClockTimeDiff *jitter);
static GstSchedulerState
			gst_opt_scheduler_iterate    		(GstScheduler *sched);

static void     	gst_opt_scheduler_show  		(GstScheduler *sched);

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
		    "GstOpt"COTHREADS_NAME_CAPITAL"Scheduler", &scheduler_info, 0);
  }
  return _gst_opt_scheduler_type;
}

static void
gst_opt_scheduler_class_init (GstOptSchedulerClass *klass)
{
  GObjectClass *gobject_class;
  GstObjectClass *gstobject_class;
  GstSchedulerClass *gstscheduler_class;

  gobject_class = (GObjectClass*)klass;
  gstobject_class = (GstObjectClass*)klass;
  gstscheduler_class = (GstSchedulerClass*)klass;

  parent_class = g_type_class_ref (GST_TYPE_SCHEDULER);

  gobject_class->set_property   = GST_DEBUG_FUNCPTR (gst_opt_scheduler_set_property);
  gobject_class->get_property   = GST_DEBUG_FUNCPTR (gst_opt_scheduler_get_property);
  gobject_class->dispose	= GST_DEBUG_FUNCPTR (gst_opt_scheduler_dispose);

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_ITERATIONS,
    g_param_spec_int ("iterations", "Iterations", 
	    	      "Number of groups to schedule in one iteration (-1 == until EOS/error)",
                      -1, G_MAXINT, 1, G_PARAM_READWRITE));
#ifndef USE_COTHREADS
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_MAX_RECURSION,
    g_param_spec_int ("max_recursion", "Max recursion", 
	    	      "Maximum number of recursions",
                      1, G_MAXINT, 100, G_PARAM_READWRITE));
#endif

  gstscheduler_class->setup             = GST_DEBUG_FUNCPTR (gst_opt_scheduler_setup);
  gstscheduler_class->reset             = GST_DEBUG_FUNCPTR (gst_opt_scheduler_reset);
  gstscheduler_class->add_element 	= GST_DEBUG_FUNCPTR (gst_opt_scheduler_add_element);
  gstscheduler_class->remove_element 	= GST_DEBUG_FUNCPTR (gst_opt_scheduler_remove_element);
  gstscheduler_class->state_transition 	= GST_DEBUG_FUNCPTR (gst_opt_scheduler_state_transition);
  gstscheduler_class->scheduling_change = GST_DEBUG_FUNCPTR (gst_opt_scheduler_scheduling_change);
  gstscheduler_class->lock_element 	= GST_DEBUG_FUNCPTR (gst_opt_scheduler_lock_element);
  gstscheduler_class->unlock_element 	= GST_DEBUG_FUNCPTR (gst_opt_scheduler_unlock_element);
  gstscheduler_class->yield	 	= GST_DEBUG_FUNCPTR (gst_opt_scheduler_yield);
  gstscheduler_class->interrupt 	= GST_DEBUG_FUNCPTR (gst_opt_scheduler_interrupt);
  gstscheduler_class->error	 	= GST_DEBUG_FUNCPTR (gst_opt_scheduler_error);
  gstscheduler_class->pad_link 	= GST_DEBUG_FUNCPTR (gst_opt_scheduler_pad_link);
  gstscheduler_class->pad_unlink 	= GST_DEBUG_FUNCPTR (gst_opt_scheduler_pad_unlink);
  gstscheduler_class->pad_select	= GST_DEBUG_FUNCPTR (gst_opt_scheduler_pad_select);
  gstscheduler_class->clock_wait	= GST_DEBUG_FUNCPTR (gst_opt_scheduler_clock_wait);
  gstscheduler_class->iterate 		= GST_DEBUG_FUNCPTR (gst_opt_scheduler_iterate);
  gstscheduler_class->show 		= GST_DEBUG_FUNCPTR (gst_opt_scheduler_show);
}

static void
gst_opt_scheduler_init (GstOptScheduler *scheduler)
{
  scheduler->elements = NULL;
  scheduler->iterations = 1;
  scheduler->max_recursion = 100;
}

static void
gst_opt_scheduler_dispose (GObject *object)
{
  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static gboolean
plugin_init (GModule *module, GstPlugin *plugin)
{
  GstSchedulerFactory *factory;

  gst_plugin_set_longname (plugin, "An optimal scheduler");

#ifdef USE_COTHREADS
  factory = gst_scheduler_factory_new ("opt"COTHREADS_NAME,
                                       "An optimal scheduler using "COTHREADS_NAME" cothreads",
		                      gst_opt_scheduler_get_type());
#else
  factory = gst_scheduler_factory_new ("opt",
                                       "An optimal scheduler using no cothreads",
		                      gst_opt_scheduler_get_type());
#endif

  if (factory != NULL) {
    gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (factory));
  }
  else {
    g_warning ("could not register scheduler: optimal");
  }
  return TRUE;
}

GstPluginDesc plugin_desc = {
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "gstopt"COTHREADS_NAME"scheduler",
  plugin_init
};


static void
destroy_chain (GstOptSchedulerChain *chain)
{
  GstOptScheduler *osched;
  
  GST_INFO (GST_CAT_SCHEDULING, "destroy chain %p", chain);

  g_assert (chain->num_groups == 0);
  g_assert (chain->groups == NULL);

  osched = chain->sched;
  osched->chains = g_slist_remove (osched->chains, chain);

  gst_object_unref (GST_OBJECT (osched));

  g_free (chain);
}

static GstOptSchedulerChain*
create_chain (GstOptScheduler *osched)
{
  GstOptSchedulerChain *chain;

  chain = g_new0 (GstOptSchedulerChain, 1);
  chain->sched = osched;
  chain->refcount = 1;

  gst_object_ref (GST_OBJECT (osched));
  osched->chains = g_slist_prepend (osched->chains, chain);

  GST_INFO (GST_CAT_SCHEDULING, "new chain %p", chain);

  return chain;
}

static GstOptSchedulerChain*
ref_chain (GstOptSchedulerChain *chain)
{
  GST_INFO (GST_CAT_SCHEDULING, "ref chain %p %d->%d", chain, 
		  chain->refcount, chain->refcount+1);
  chain->refcount++;

  return chain;
}

static GstOptSchedulerChain*
unref_chain (GstOptSchedulerChain *chain)
{
  GST_INFO (GST_CAT_SCHEDULING, "unref chain %p %d->%d", chain, 
		  chain->refcount, chain->refcount-1);

  if (--chain->refcount == 0) {
    destroy_chain (chain);
    chain = NULL;
  }

  return chain;
}

static GstOptSchedulerChain*
add_to_chain (GstOptSchedulerChain *chain, GstOptSchedulerGroup *group)
{
  GST_INFO (GST_CAT_SCHEDULING, "adding group %p to chain %p", group, chain);

  g_assert (group->chain == NULL);

  group = ref_group (group);

  group->chain = ref_chain (chain);
  chain->groups = g_slist_prepend (chain->groups, group);
  chain->num_groups++;

  return chain;
}

static GstOptSchedulerChain*
remove_from_chain (GstOptSchedulerChain *chain, GstOptSchedulerGroup *group)
{
  GST_INFO (GST_CAT_SCHEDULING, "removing group %p from chain %p", group, chain);

  if (!chain)
    return NULL;

  g_assert (group);
  g_assert (group->chain == chain);

  group->chain = NULL;
  chain->groups = g_slist_remove (chain->groups, group);
  chain->num_groups--;
  unref_group (group);

  if (chain->num_groups == 0) 
    chain = unref_chain (chain);

  chain = unref_chain (chain);
  return chain;
}

static GstOptSchedulerChain*
merge_chains (GstOptSchedulerChain *chain1, GstOptSchedulerChain *chain2)
{
  GSList *walk;

  g_assert (chain1 != NULL);
  
  GST_INFO (GST_CAT_SCHEDULING, "merging chain %p and %p", chain1, chain2);
  
  if (chain1 == chain2 || chain2 == NULL)
    return chain1;

  ref_chain (chain2);
  walk = chain2->groups;
  while (walk) {
    GstOptSchedulerGroup *group = (GstOptSchedulerGroup *) walk->data;
    walk = g_slist_next (walk);

    GST_INFO (GST_CAT_SCHEDULING, "reparenting group %p from chain %p to %p", 
		    group, chain2, chain1);

    group->chain = NULL;
    chain2->num_groups--;
    chain2 = unref_chain (chain2);

    group->chain = ref_chain (chain1);
    chain1->groups = g_slist_prepend (chain1->groups, group);
    chain1->num_groups++;
  }
  g_slist_free (chain2->groups);
  chain2->groups = NULL;
  unref_chain (chain2);

  return chain1;
}

static void
chain_group_set_enabled (GstOptSchedulerChain *chain, GstOptSchedulerGroup *group, gboolean enabled)
{
  g_assert (chain != NULL);
  g_assert (group != NULL);

  GST_INFO (GST_CAT_SCHEDULING, "request to %d group %p in chain %p, have %d groups enabled out of %d", 
		  enabled, group, chain, chain->num_enabled, chain->num_groups);

  if (enabled)
    GST_OPT_SCHEDULER_GROUP_ENABLE (group);
  else 
    GST_OPT_SCHEDULER_GROUP_DISABLE (group);

  if (enabled) {
    if (chain->num_enabled < chain->num_groups)
      chain->num_enabled++;

    GST_INFO (GST_CAT_SCHEDULING, "enable group %p in chain %p, now %d groups enabled out of %d", group, chain,
		    chain->num_enabled, chain->num_groups);

    if (chain->num_enabled == chain->num_groups) {
      GST_INFO (GST_CAT_SCHEDULING, "enable chain %p", chain);
      GST_OPT_SCHEDULER_CHAIN_ENABLE (chain);
    }
  }
  else {
    if (chain->num_enabled > 0)
      chain->num_enabled--;

    GST_INFO (GST_CAT_SCHEDULING, "disable group %p in chain %p, now %d groups enabled out of %d", group, chain,
		    chain->num_enabled, chain->num_groups);

    if (chain->num_enabled == 0) {
      GST_INFO (GST_CAT_SCHEDULING, "disable chain %p", chain);
      GST_OPT_SCHEDULER_CHAIN_DISABLE (chain);
    }
  }
}

static GstOptSchedulerGroup*
ref_group (GstOptSchedulerGroup *group)
{
  GST_INFO (GST_CAT_SCHEDULING, "ref group %p %d->%d", group, 
		  group->refcount, group->refcount+1);

  group->refcount++;

  return group;
}

#ifndef USE_COTHREADS
static GstOptSchedulerGroup*
ref_group_by_count (GstOptSchedulerGroup *group, gint count)
{
  GST_INFO (GST_CAT_SCHEDULING, "ref group %p %d->%d", group, 
		  group->refcount, group->refcount+count);

  group->refcount += count;

  return group;
}
#endif

static GstOptSchedulerGroup*
unref_group (GstOptSchedulerGroup *group)
{
  GST_INFO (GST_CAT_SCHEDULING, "unref group %p %d->%d", group, 
		  group->refcount, group->refcount-1);

  if (--group->refcount == 1) {
    destroy_group (group);
    group = NULL;
  }

  return group;
}

static GstOptSchedulerGroup*
add_to_group (GstOptSchedulerGroup *group, GstElement *element)
{
  g_assert (group != NULL);
  g_assert (element != NULL);

  GST_INFO (GST_CAT_SCHEDULING, "adding element \"%s\" to group %p", GST_ELEMENT_NAME (element), group);

  if (GST_ELEMENT_IS_DECOUPLED (element)) {
    GST_INFO (GST_CAT_SCHEDULING, "element \"%s\" is decoupled, not adding to group %p", 
	      GST_ELEMENT_NAME (element), group);
    return group;
  }

  g_assert (GST_ELEMENT_SCHED_GROUP (element) == NULL);

  GST_ELEMENT_SCHED_GROUP (element) = ref_group (group);

  gst_object_ref (GST_OBJECT (element));
  group->elements = g_slist_prepend (group->elements, element);
  group->num_elements++;

  return group;
}

static GstOptSchedulerGroup*
create_group (GstOptSchedulerChain *chain, GstElement *element)
{
  GstOptSchedulerGroup *group;

  group = g_new0 (GstOptSchedulerGroup, 1);
  GST_INFO (GST_CAT_SCHEDULING, "new group %p", group);
  group->refcount = 1;

  add_to_group (group, element);
  add_to_chain (chain, group);
  
  return group;
}

static void 
destroy_group_scheduler (GstOptSchedulerGroup *group) 
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
destroy_group (GstOptSchedulerGroup *group)
{
  GST_INFO (GST_CAT_SCHEDULING, "destroy group %p", group);

  g_assert (group != NULL);
  g_assert (group->elements == NULL);

  remove_from_chain (group->chain, group);

  if (group->flags & GST_OPT_SCHEDULER_GROUP_SCHEDULABLE)
    destroy_group_scheduler (group);

  g_free (group);
}

static GstOptSchedulerGroup*
remove_from_group (GstOptSchedulerGroup *group, GstElement *element)
{
  GST_INFO (GST_CAT_SCHEDULING, "removing element \"%s\" from group %p", GST_ELEMENT_NAME (element), group);

  g_assert (group != NULL);
  g_assert (element != NULL);

  group->elements = g_slist_remove (group->elements, element);
  group->num_elements--;

  GST_ELEMENT_SCHED_GROUP (element) = NULL;
  gst_object_unref (GST_OBJECT (element));

  if (group->num_elements == 0) {
    group = unref_group (group);
  }

  group = unref_group (group);

  return group;
}

static GstOptSchedulerGroup*
merge_groups (GstOptSchedulerGroup *group1, GstOptSchedulerGroup *group2)
{
  g_assert (group1 != NULL);

  GST_INFO (GST_CAT_SCHEDULING, "merging groups %p and %p", group1, group2);
  
  if (group1 == group2 || group2 == NULL)
    return group1;

  while (group2) {
    GstElement *element = (GstElement *)group2->elements->data;
   
    group2 = remove_from_group (group2, element);
    add_to_group (group1, element);
  }
  
  return group1;
}

static void
group_error_handler (GstOptSchedulerGroup *group) 
{
  GST_INFO (GST_CAT_SCHEDULING, "group %p has errored", group);

  chain_group_set_enabled (group->chain, group, FALSE);
  group->chain->sched->state = GST_OPT_SCHEDULER_STATE_ERROR;
}

/* this function enables/disables an element, it will set/clear a flag on the element 
 * and tells the chain that the group is enabled if all elements inside the group are
 * enabled */
static void
group_element_set_enabled (GstOptSchedulerGroup *group, GstElement *element, gboolean enabled)
{
  g_assert (group != NULL);
  g_assert (element != NULL);

  GST_INFO (GST_CAT_SCHEDULING, "request to %d element %s in group %p, have %d elements enabled out of %d", 
		    enabled, GST_ELEMENT_NAME (element), group, group->num_enabled, group->num_elements);

  if (enabled) {
    if (group->num_enabled < group->num_elements)
      group->num_enabled++;

    GST_INFO (GST_CAT_SCHEDULING, "enable element %s in group %p, now %d elements enabled out of %d", 
		    GST_ELEMENT_NAME (element), group, group->num_enabled, group->num_elements);

    if (group->num_enabled == group->num_elements) {
      GST_INFO (GST_CAT_SCHEDULING, "enable group %p", group);
      chain_group_set_enabled (group->chain, group, TRUE);
    }
  }
  else {
    if (group->num_enabled > 0)
      group->num_enabled--;

    GST_INFO (GST_CAT_SCHEDULING, "disable element %s in group %p, now %d elements enabled out of %d", 
		    GST_ELEMENT_NAME (element), group, group->num_enabled, group->num_elements);

    if (group->num_enabled == 0) {
      GST_INFO (GST_CAT_SCHEDULING, "disable group %p", group);
      chain_group_set_enabled (group->chain, group, FALSE);
    }
  }
}

/* a group is scheduled by doing a cothread switch to it or
 * by calling the schedule function. In the non-cothread case
 * we cannot run already running groups so we return FALSE here
 * to indicate this to the caller */
static gboolean 
schedule_group (GstOptSchedulerGroup *group) 
{
  if (!group->entry)
    return FALSE;

#ifdef USE_COTHREADS
  if (group->cothread)
    do_cothread_switch (group->cothread);
  else
    g_warning ("(internal error): trying to schedule group without cothread");
  return TRUE;
#else
  group->schedulefunc (group->argc, group->argv);
  return TRUE;
#endif
}

#ifndef USE_COTHREADS
static void
gst_opt_scheduler_schedule_run_queue (GstOptScheduler *osched)
{
  GST_INFO (GST_CAT_SCHEDULING, "entering scheduler run queue recursion %d", osched->recursion);

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

    /* runqueue hols refcount to group */
    osched->runqueue = g_list_remove (osched->runqueue, group);

    GST_INFO (GST_CAT_SCHEDULING, "scheduling %p", group);

    res = schedule_group (group);
    if (!res) {
      g_warning  ("error scheduling group %p", group);
      group_error_handler (group);
    }
    else {
      GST_INFO (GST_CAT_SCHEDULING, "done scheduling %p", group);
    }
    unref_group (group);
  }

  GST_INFO (GST_CAT_SCHEDULING, "run queue length after scheduling %d", g_list_length (osched->runqueue));

  osched->recursion--;
}
#endif

/* a chain is scheduled by picking the first active group and scheduling it */
static void
schedule_chain (GstOptSchedulerChain *chain) 
{
  GSList *groups;
  GstOptScheduler *osched;

  osched = chain->sched;
  groups = chain->groups;

  while (groups) {
    GstOptSchedulerGroup *group = (GstOptSchedulerGroup *) groups->data;

    if (!GST_OPT_SCHEDULER_GROUP_IS_DISABLED (group)) {
      ref_group (group);
      GST_INFO (GST_CAT_SCHEDULING, "scheduling group %p in chain %p", 
 	        group, chain);

#ifdef USE_COTHREADS
      schedule_group (group);
#else
      osched->recursion = 0;
      if (!schedule_group (group)) {
        g_warning  ("error scheduling group %p", group);
        group_error_handler (group);
      }
      else {
        GST_INFO (GST_CAT_SCHEDULING, "done scheduling %p", group);
      }
#endif

      GST_INFO (GST_CAT_SCHEDULING, "done scheduling group %p in chain %p", 
 	        group, chain);
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
  const GList *pads = gst_element_get_pad_list (group->entry);

  GST_INFO (GST_CAT_SCHEDULING, "get wrapper of group %p", group);

  group->flags |= GST_OPT_SCHEDULER_GROUP_RUNNING;

  while (pads) {
    GstBuffer *buffer;
    GstPad *pad = GST_PAD_CAST (pads->data);
    pads = g_list_next (pads);

    /* skip sinks and ghostpads */
    if (!GST_PAD_IS_SRC (pad) || !GST_IS_REAL_PAD (pad))
      continue;

    GST_INFO (GST_CAT_SCHEDULING, "doing get and push on pad \"%s:%s\" in group %p", 
	      GST_DEBUG_PAD_NAME (pad), group);

    buffer = GST_RPAD_GETFUNC (pad) (pad);
    if (buffer) {
      if (GST_EVENT_IS_INTERRUPT (buffer)) {
	gst_event_unref (GST_EVENT (buffer));
	break;
      }
      gst_pad_push (pad, buffer);
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

  GST_INFO (GST_CAT_SCHEDULING, "loop wrapper of group %p", group);

  group->flags |= GST_OPT_SCHEDULER_GROUP_RUNNING;

  GST_INFO (GST_CAT_SCHEDULING, "calling loopfunc of element %s in group %p", 
	    GST_ELEMENT_NAME (entry), group);

  entry->loopfunc (entry);

  group->flags &= ~GST_OPT_SCHEDULER_GROUP_RUNNING;

  return 0;

}

/* the function to schedule an unkown group, which just gives an error */
static int
unkown_group_schedule_function (int argc, char *argv[])
{
  GstOptSchedulerGroup *group = (GstOptSchedulerGroup *) argv;

  g_warning ("(internal error) unkown group type %d, disabling\n", group->type);
  group_error_handler (group);

  return 0;
}

/* this function is called when the first element of a chain-loop or a loop-loop
 * link performs a push to the loop element. We then schedule the
 * group with the loop-based element until the bufpen is empty */
static void
gst_opt_scheduler_loop_wrapper (GstPad *sinkpad, GstBuffer *buffer)
{
  GstOptSchedulerGroup *group;
  GstOptScheduler *osched;

  GST_INFO (GST_CAT_SCHEDULING, "loop wrapper, putting buffer in bufpen");

  group = GST_ELEMENT_SCHED_GROUP (GST_PAD_PARENT (sinkpad));
  osched = group->chain->sched;


#ifdef USE_COTHREADS
  if (GST_PAD_BUFLIST (GST_RPAD_PEER (sinkpad))) {
    g_warning ("deadlock detected, disabling group %p", group);
    group_error_handler (group);
  }
  else {
    GST_PAD_BUFLIST (GST_RPAD_PEER (sinkpad)) = g_list_append (GST_PAD_BUFLIST (GST_RPAD_PEER (sinkpad)), buffer);
    schedule_group (group);
  }
#else
  GST_PAD_BUFLIST (GST_RPAD_PEER (sinkpad)) = g_list_append (GST_PAD_BUFLIST (GST_RPAD_PEER (sinkpad)), buffer);
  if (!(group->flags & GST_OPT_SCHEDULER_GROUP_RUNNING)) {
    ref_group (group);
    osched->runqueue = g_list_append (osched->runqueue, group);
  }
#endif
  
  GST_INFO (GST_CAT_SCHEDULING, "after loop wrapper buflist %d", 
	    g_list_length (GST_PAD_BUFLIST (GST_RPAD_PEER (sinkpad))));
}

/* this function is called by a loop based element that performs a
 * pull on a sinkpad. We schedule the peer group until the bufpen
 * is filled with the buffer so that this function  can return */
static GstBuffer*
gst_opt_scheduler_get_wrapper (GstPad *srcpad)
{
  GstBuffer *buffer = NULL;

  GST_INFO (GST_CAT_SCHEDULING, "get wrapper, removing buffer from bufpen");

  if (GST_PAD_BUFLIST (srcpad))
    buffer = GST_PAD_BUFLIST (srcpad)->data;

  while (!buffer) {
    GstOptSchedulerGroup *group;
    GstOptScheduler *osched;
    
    group = GST_ELEMENT_SCHED_GROUP (GST_PAD_PARENT (srcpad));
    osched = group->chain->sched;

#ifdef USE_COTHREADS
    schedule_group (group);
#else
    if (!(group->flags & GST_OPT_SCHEDULER_GROUP_RUNNING)) {
      ref_group_by_count (group, 2);
      osched->runqueue = g_list_append (osched->runqueue, group);
      GST_INFO (GST_CAT_SCHEDULING, "recursing into scheduler group %p", group);
      gst_opt_scheduler_schedule_run_queue (osched);
      GST_INFO (GST_CAT_SCHEDULING, "return from recurse group %p", group);
      group = unref_group (group);
      /* group is gone */
      if (group == NULL) {
        GST_INFO (GST_CAT_SCHEDULING, "group %p destroyed, sending interrupt", group);
        return GST_BUFFER (gst_event_new (GST_EVENT_INTERRUPT));
      }
    }
    else {
      g_warning ("deadlock detected, disabling group %p", group);
      group_error_handler (group);
      return GST_BUFFER (gst_event_new (GST_EVENT_INTERRUPT));
    }
#endif
    /* if the scheduler interrupted, make sure we send an INTERRUPTED event to the
     * loop based element */
    if (osched->state == GST_OPT_SCHEDULER_STATE_INTERRUPTED) {
      GST_INFO (GST_CAT_SCHEDULING, "scheduler interrupted, return interrupt event");
      return GST_BUFFER (gst_event_new (GST_EVENT_INTERRUPT));
    }
    
    if (GST_PAD_BUFLIST (srcpad)) {
      buffer = (GstBuffer *) GST_PAD_BUFLIST (srcpad)->data;
    }
  }
  GST_PAD_BUFLIST (srcpad) = g_list_remove (GST_PAD_BUFLIST (srcpad), buffer);

  GST_INFO (GST_CAT_SCHEDULING, "get wrapper, returning buffer %d",
	    g_list_length (GST_PAD_BUFLIST (srcpad)));

  return buffer;
}

/* this function is a chain wrapper for non-event-aware plugins,
 * it'll simply dispatch the events to the (default) event handler */
static void
gst_opt_scheduler_chain_wrapper (GstPad *sinkpad, GstBuffer *buffer)
{
  if (GST_IS_EVENT (buffer)) {
    gst_pad_send_event (sinkpad, GST_EVENT (buffer));
  }
  else {
    GST_RPAD_CHAINFUNC (sinkpad) (sinkpad, buffer);
  }
}

static void
clear_queued (GstData *data, gpointer user_data)
{
  gst_data_unref (data);
}

static void
pad_clear_queued (GstPad *srcpad, gpointer user_data)
{
  GList *buflist = GST_PAD_BUFLIST (srcpad);

  if (buflist) {
    GST_INFO (GST_CAT_SCHEDULING, "need to clear some buffers");
    g_list_foreach (buflist, (GFunc) clear_queued, NULL);
    g_list_free (buflist);
    GST_PAD_BUFLIST (srcpad) = NULL;
  }
}

static gboolean
gst_opt_scheduler_event_wrapper (GstPad *srcpad, GstEvent *event)
{
  gboolean flush;

  GST_INFO (GST_CAT_SCHEDULING, "intercepting event %d on pad %s:%s", 
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
    GST_INFO (GST_CAT_SCHEDULING, "event is flush");

    pad_clear_queued (srcpad, NULL);
  }
  return GST_RPAD_EVENTFUNC (srcpad) (srcpad, event);
}


/* setup the scheduler context for a group. The right schedule function
 * is selected based on the group type and cothreads are created if 
 * needed */
static void 
setup_group_scheduler (GstOptScheduler *osched, GstOptSchedulerGroup *group) 
{
  GroupScheduleFunction wrapper;

  wrapper = unkown_group_schedule_function;

  /* figure out the wrapper function for this group */
  if (group->type == GST_OPT_SCHEDULER_GROUP_GET)
    wrapper = get_group_schedule_function;
  else if (group->type == GST_OPT_SCHEDULER_GROUP_LOOP)
    wrapper = loop_group_schedule_function;
	
#ifdef USE_COTHREADS
  if (!(group->flags & GST_OPT_SCHEDULER_GROUP_SCHEDULABLE)) {
    do_cothread_create (group->cothread, osched->context,
 		      (cothread_func) wrapper, 0, (char **) group);
  }
  else {
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

static GstElementStateReturn
gst_opt_scheduler_state_transition (GstScheduler *sched, GstElement *element, gint transition)
{
  GstOptScheduler *osched = GST_OPT_SCHEDULER_CAST (sched);
  GstOptSchedulerGroup *group;
  GstElementStateReturn res = GST_STATE_SUCCESS;
  
  GST_INFO (GST_CAT_SCHEDULING, "element \"%s\" state change %d", GST_ELEMENT_NAME (element), transition);

  /* we check the state of the managing pipeline here */
  if (GST_IS_BIN (element)) {
    if (GST_SCHEDULER_PARENT (sched) == element) {
      GST_INFO (GST_CAT_SCHEDULING, "parent \"%s\" changed state", GST_ELEMENT_NAME (element));

      switch (transition) {
        case GST_STATE_PLAYING_TO_PAUSED:
          GST_INFO (GST_CAT_SCHEDULING, "setting scheduler state to stopped");
          GST_SCHEDULER_STATE (sched) = GST_SCHEDULER_STATE_STOPPED;
  	  break;
        case GST_STATE_PAUSED_TO_PLAYING:
          GST_INFO (GST_CAT_SCHEDULING, "setting scheduler state to running");
          GST_SCHEDULER_STATE (sched) = GST_SCHEDULER_STATE_RUNNING;
	  break;
        default:
          GST_INFO (GST_CAT_SCHEDULING, "no interesting state change, doing nothing");
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
      /* an element withut a group has to be an unlinked src, sink
       * filter element */
      if (!group) {
        GST_INFO (GST_CAT_SCHEDULING, "element \"%s\" has no group", GST_ELEMENT_NAME (element));
	res = GST_STATE_FAILURE;
      }
      /* else construct the scheduling context of this group and enable it */
      else {
        setup_group_scheduler (osched, group);
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
gst_opt_scheduler_scheduling_change (GstScheduler *sched, GstElement *element)
{
  g_warning ("scheduling change, implement me");
}

static void
get_group (GstElement *element, GstOptSchedulerGroup **group)
{
  GstOptSchedulerCtx *ctx;

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
 */
static GstOptSchedulerGroup*
group_elements (GstOptScheduler *osched, GstElement *element1, GstElement *element2)
{
  GstOptSchedulerGroup *group1, *group2, *group = NULL;
  
  get_group (element1, &group1);
  get_group (element2, &group2);
  
  /* none of the elements is added to a group, create a new group
   * and chain to add the elements to */
  if (!group1 && !group2) {
    GstOptSchedulerChain *chain;

    GST_INFO (GST_CAT_SCHEDULING, "creating new group to hold \"%s\" and \"%s\"", 
		  GST_ELEMENT_NAME (element1), GST_ELEMENT_NAME (element2));

    chain = create_chain (osched);
    group = create_group (chain, element1);
    add_to_group (group, element2);
  }
  /* the first element has a group */
  else if (group1) {
    GST_INFO (GST_CAT_SCHEDULING, "adding \"%s\" to \"%s\"'s group", 
		  GST_ELEMENT_NAME (element2), GST_ELEMENT_NAME (element1));

    /* the second element also has a group, merge */
    if (group2)
      merge_groups (group1, group2);
    /* the second element has no group, add it to the group
     * of the first element */
    else
      add_to_group (group1, element2);

    group = group1;
  }
  /* element1 has no group, element2 does. Add element1 to the
   * group of element2 */
  else {
    GST_INFO (GST_CAT_SCHEDULING, "adding \"%s\" to \"%s\"'s group", 
		  GST_ELEMENT_NAME (element1), GST_ELEMENT_NAME (element2));
    add_to_group (group2, element1);
    group = group2;
  }
  return group;
}

typedef enum {
  GST_OPT_INVALID,
  GST_OPT_GET_TO_CHAIN,
  GST_OPT_LOOP_TO_CHAIN,
  GST_OPT_GET_TO_LOOP,
  GST_OPT_CHAIN_TO_CHAIN,
  GST_OPT_CHAIN_TO_LOOP,
  GST_OPT_LOOP_TO_LOOP,
} LinkType;

/*
 * Entry points for this scheduler.
 */
static void
gst_opt_scheduler_setup (GstScheduler *sched)
{   
#ifdef USE_COTHREADS
  GstOptScheduler *osched = GST_OPT_SCHEDULER_CAST (sched);
	      
  /* first create thread context */
  if (osched->context == NULL) {
    GST_DEBUG (GST_CAT_SCHEDULING, "initializing cothread context");
    osched->context = do_cothread_context_init ();
  }
#endif
} 
  
static void 
gst_opt_scheduler_reset (GstScheduler *sched)
{ 
#ifdef USE_COTHREADS
  GstOptScheduler *osched = GST_OPT_SCHEDULER_CAST (sched);
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
gst_opt_scheduler_add_element (GstScheduler *sched, GstElement *element)
{
  GstOptScheduler *osched = GST_OPT_SCHEDULER_CAST (sched);
  GstOptSchedulerCtx *ctx;

  GST_INFO (GST_CAT_SCHEDULING, "adding element \"%s\" to scheduler", GST_ELEMENT_NAME (element));

  /* decoupled elements are not added to the scheduler lists */
  if (GST_ELEMENT_IS_DECOUPLED (element))
    return;

  ctx = g_new0 (GstOptSchedulerCtx, 1);
  GST_ELEMENT_SCHED_CONTEXT (element) = ctx;

  /* loop based elements *always* end up in their own group. It can eventually
   * be merged with another group when a link is made */
  if (element->loopfunc) {
    GstOptSchedulerGroup *group;
    GstOptSchedulerChain *chain;

    chain = create_chain (osched);

    group = create_group (chain, element);
    group->entry = element;
    group->type = GST_OPT_SCHEDULER_GROUP_LOOP;

    GST_INFO (GST_CAT_SCHEDULING, "added element \"%s\" as loop based entry", GST_ELEMENT_NAME (element));
  }
}

static void
gst_opt_scheduler_remove_element (GstScheduler *sched, GstElement *element)
{
  GstOptSchedulerGroup *group;

  GST_INFO (GST_CAT_SCHEDULING, "removing element \"%s\" from scheduler", GST_ELEMENT_NAME (element));

  /* decoupled elements are not added to the scheduler lists and should therefore
   * no be removed */
  if (GST_ELEMENT_IS_DECOUPLED (element))
    return;

  /* the element is guaranteed to live in it's own group/chain now */
  get_group (element, &group);
  if (group) {
    remove_from_group (group, element);
  }

  g_free (GST_ELEMENT_SCHED_CONTEXT (element));
  GST_ELEMENT_SCHED_CONTEXT (element) = NULL;
}

static void
gst_opt_scheduler_lock_element (GstScheduler *sched, GstElement *element)
{
  //GstOptScheduler *osched = GST_OPT_SCHEDULER_CAST (sched);
  g_warning ("lock element, implement me");
}

static void
gst_opt_scheduler_unlock_element (GstScheduler *sched, GstElement *element)
{
  //GstOptScheduler *osched = GST_OPT_SCHEDULER_CAST (sched);
  g_warning ("unlock element, implement me");
}

static gboolean
gst_opt_scheduler_yield (GstScheduler *sched, GstElement *element)
{
#ifdef USE_COTHREADS
  /* yield hands control to the main cothread context if the requesting 
   * element is the entry point of the group */
  GstOptSchedulerGroup *group;
  get_group (element, &group);
  if (group && group->entry == element)
    do_cothread_switch (do_cothread_get_main (((GstOptScheduler*)sched)->context)); 

  return FALSE;
#else
  g_warning ("element %s performs a yield, please fix the element", 
		  GST_ELEMENT_NAME (element));
  return TRUE;
#endif
}

static gboolean
gst_opt_scheduler_interrupt (GstScheduler *sched, GstElement *element)
{
  GST_INFO (GST_CAT_SCHEDULING, "interrupt from \"%s\"", 
            GST_ELEMENT_NAME (element));

#ifdef USE_COTHREADS
  do_cothread_switch (do_cothread_get_main (((GstOptScheduler*)sched)->context)); 
  return FALSE;
#else
  {
    GstOptScheduler *osched = GST_OPT_SCHEDULER_CAST (sched);
 
    GST_INFO (GST_CAT_SCHEDULING, "scheduler set interrupted state");
    osched->state = GST_OPT_SCHEDULER_STATE_INTERRUPTED;
  }
  return TRUE;
#endif
}

static void
gst_opt_scheduler_error (GstScheduler *sched, GstElement *element)
{
  GstOptScheduler *osched = GST_OPT_SCHEDULER_CAST (sched);
  GstOptSchedulerGroup *group;
  get_group (element, &group);
  if (group)
    group_error_handler (group);

  osched->state = GST_OPT_SCHEDULER_STATE_ERROR;
}

/* link pads, merge groups and chains */
static void
gst_opt_scheduler_pad_link (GstScheduler *sched, GstPad *srcpad, GstPad *sinkpad)
{
  GstOptScheduler *osched = GST_OPT_SCHEDULER_CAST (sched);
  LinkType type = GST_OPT_INVALID;
  GstElement *element1, *element2;

  GST_INFO (GST_CAT_SCHEDULING, "pad link between \"%s:%s\" and \"%s:%s\"", 
		  GST_DEBUG_PAD_NAME (srcpad), GST_DEBUG_PAD_NAME (sinkpad));

  element1 = GST_PAD_PARENT (srcpad);
  element2 = GST_PAD_PARENT (sinkpad);

  /* first we need to figure out what type of link we're dealing
   * with */
  if (element1->loopfunc && element2->loopfunc)
    type = GST_OPT_LOOP_TO_LOOP;
  else {
    if (element1->loopfunc) {
      if (GST_RPAD_CHAINFUNC (sinkpad))
        type = GST_OPT_LOOP_TO_CHAIN;
    }
    else if (element2->loopfunc) {
      if (GST_RPAD_GETFUNC (srcpad)) {
        type = GST_OPT_GET_TO_LOOP;
	/* this could be tricky, the get based source could 
	 * already be part of a loop based group in another pad,
	 * we assert on that for now */
	if (GST_ELEMENT_SCHED_CONTEXT (element1) != NULL &&
	    GST_ELEMENT_SCHED_GROUP (element1) != NULL) 
	{
          GstOptSchedulerGroup *group = GST_ELEMENT_SCHED_GROUP (element1);

	  /* if the loop based element is the entry point we're ok, if it
	   * isn't then we have multiple loop based elements in this group */
	  if (group->entry != element2) {
            g_error ("internal error: cannot schedule get to loop in multi-loop based group");
	    return;
	  }
	}
      }
      else
        type = GST_OPT_CHAIN_TO_LOOP;
    }
    else {
      if (GST_RPAD_GETFUNC (srcpad) && GST_RPAD_CHAINFUNC (sinkpad)) {
        type = GST_OPT_GET_TO_CHAIN;
	/* the get based source could already be part of a loop 
	 * based group in another pad, we assert on that for now */
	if (GST_ELEMENT_SCHED_CONTEXT (element1) != NULL &&
	    GST_ELEMENT_SCHED_GROUP (element1) != NULL) 
	{
          GstOptSchedulerGroup *group = GST_ELEMENT_SCHED_GROUP (element1);

	  /* if the get based element is the entry point we're ok, if it
	   * isn't then we have a mixed loop/chain based group */
	  if (group->entry != element1) {
            g_error ("internal error: cannot schedule get to chain with mixed loop/chain based group");
	    return;
	  }
	}
      }
      else 
        type = GST_OPT_CHAIN_TO_CHAIN;
    }
  }
  
  /* for each link type, perform specific actions */
  switch (type) {
    case GST_OPT_GET_TO_CHAIN:
    {
      GstOptSchedulerGroup *group = NULL;

      GST_INFO (GST_CAT_SCHEDULING, "get to chain based link");

      /* setup get/chain handlers */
      GST_RPAD_GETHANDLER (srcpad) = GST_RPAD_GETFUNC (srcpad);
      if (GST_ELEMENT_IS_EVENT_AWARE (element2))
        GST_RPAD_CHAINHANDLER (sinkpad) = GST_RPAD_CHAINFUNC (sinkpad);
      else
        GST_RPAD_CHAINHANDLER (sinkpad) = gst_opt_scheduler_chain_wrapper;
      GST_RPAD_EVENTHANDLER (srcpad) = GST_RPAD_EVENTFUNC (srcpad);
      GST_RPAD_EVENTHANDLER (sinkpad) = GST_RPAD_EVENTFUNC (sinkpad);

      /* the two elements should be put into the same group, 
       * this also means that they are in the same chain automatically */
      group = group_elements (osched, element1, element2);

      /* if there is not yet an entry in the group, select the source
       * element as the entry point */
      if (!group->entry) {
        group->entry = element1;
        group->type = GST_OPT_SCHEDULER_GROUP_GET;

        GST_INFO (GST_CAT_SCHEDULING, "setting \"%s\" as entry point of _get-based group %p", 
		  GST_ELEMENT_NAME (element1), group);
      }
      break;
    }
    case GST_OPT_LOOP_TO_CHAIN:
    case GST_OPT_CHAIN_TO_CHAIN:
      GST_INFO (GST_CAT_SCHEDULING, "loop/chain to chain based link");

      if (GST_ELEMENT_IS_EVENT_AWARE (element2))
        GST_RPAD_CHAINHANDLER (sinkpad) = GST_RPAD_CHAINFUNC (sinkpad);
      else
        GST_RPAD_CHAINHANDLER (sinkpad) = gst_opt_scheduler_chain_wrapper;
      GST_RPAD_EVENTHANDLER (srcpad) = GST_RPAD_EVENTFUNC (srcpad);
      GST_RPAD_EVENTHANDLER (sinkpad) = GST_RPAD_EVENTFUNC (sinkpad);

      /* the two elements should be put into the same group, 
       * this also means that they are in the same chain automatically, 
       * in case of a loop-based element1, there will be a group for element1 and
       * element2 will be added to it. */
      group_elements (osched, element1, element2);
      break;
    case GST_OPT_GET_TO_LOOP:
      GST_INFO (GST_CAT_SCHEDULING, "get to loop based link");

      GST_RPAD_GETHANDLER (srcpad) = GST_RPAD_GETFUNC (srcpad);
      GST_RPAD_EVENTHANDLER (srcpad) = GST_RPAD_EVENTFUNC (srcpad);
      GST_RPAD_EVENTHANDLER (sinkpad) = GST_RPAD_EVENTFUNC (sinkpad);

      /* the two elements should be put into the same group, 
       * this also means that they are in the same chain automatically, 
       * element2 is loop-based so it already has a group where element1
       * will be added to */
      group_elements (osched, element1, element2);
      break;
    case GST_OPT_CHAIN_TO_LOOP:
    case GST_OPT_LOOP_TO_LOOP:
    {
      GstOptSchedulerGroup *group1, *group2;

      GST_INFO (GST_CAT_SCHEDULING, "chain/loop to loop based link");

      GST_RPAD_CHAINHANDLER (sinkpad) = gst_opt_scheduler_loop_wrapper;
      GST_RPAD_GETHANDLER (srcpad) = gst_opt_scheduler_get_wrapper;
      GST_RPAD_EVENTHANDLER (sinkpad) = GST_RPAD_EVENTFUNC (sinkpad);
      /* events on the srcpad have to be intercepted as we might need to
       * flush the buffer lists */
      GST_RPAD_EVENTHANDLER (srcpad) = gst_opt_scheduler_event_wrapper;

      group1 = GST_ELEMENT_SCHED_GROUP (element1);
      group2 = GST_ELEMENT_SCHED_GROUP (element2);

      g_assert (group2 != NULL);

      /* group2 is guaranteed to exist as it contains a loop-based element.
       * group1 only exists if element1 is linked to some other element */
      if (!group1) {
	/* create a new group for element1 as it cannot be merged into another group
	 * here. we create the group in the same chain as the loop-based element. */
        GST_INFO (GST_CAT_SCHEDULING, "creating new group for element %s", GST_ELEMENT_NAME (element1));
        group1 = create_group (group2->chain, element1);
      }
      else {
	/* both elements are already in a group, make sure they are added to
	 * the same chain */
        merge_chains (group1->chain, group2->chain);
      }
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
 * */
static gboolean
element_has_link_with_group (GstElement *element, GstOptSchedulerGroup *group, GstPad *brokenpad)
{
  gboolean linked = FALSE;
  const GList *pads;

  /* see if the element has no more links to the peer group */
  pads = gst_element_get_pad_list (element);
  while (pads && !linked) {
    GstPad *pad = GST_PAD_CAST (pads->data);
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
      }
      else {
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

static void
gst_opt_scheduler_pad_unlink (GstScheduler *sched, GstPad *srcpad, GstPad *sinkpad)
{
  //GstOptScheduler *osched = GST_OPT_SCHEDULER_CAST (sched);
  GstElement *element1, *element2;
  GstOptSchedulerGroup *group1, *group2;

  GST_INFO (GST_CAT_SCHEDULING, "pad unlink between \"%s:%s\" and \"%s:%s\"", 
		  GST_DEBUG_PAD_NAME (srcpad), GST_DEBUG_PAD_NAME (sinkpad));

  element1 = GST_PAD_PARENT (srcpad);
  element2 = GST_PAD_PARENT (sinkpad);
  
  get_group (element1, &group1);
  get_group (element2, &group2);

  /* if one the elements has no group (anymore) we don't really care 
   * about the link */
  if (!group1 || !group2) {
    GST_INFO (GST_CAT_SCHEDULING, "one (or both) of the elements is not in a group, not interesting");
    return;
  }

  /* easy part, groups are different */
  if (group1 != group2) {
    GST_INFO (GST_CAT_SCHEDULING, "elements are in different groups");

    /* FIXME, need to eventually break the chain */
    g_warning ("pad unlink for different groups, implement me");
  }
  /* hard part, groups are equal */
  else {
    gboolean still_link1, still_link2;
    GstOptSchedulerGroup *group;
    
    /* since group1 == group2, it doesn't matter which group we take */
    group = group1;

    GST_INFO (GST_CAT_SCHEDULING, "elements are in the same group %p", group);

    /* check if the element is still linked to some other element in the group,
     * we pass the pad that is broken up as an arg because a link on that pad
     * is not valid anymore */
    still_link1 = element_has_link_with_group (element1, group, srcpad);
    still_link2 = element_has_link_with_group (element2, group, sinkpad);

    /* if there is still a link, we don't need to break this group */
    if (still_link1 && still_link2) {
      GST_INFO (GST_CAT_SCHEDULING, "elements still have links with other elements in the group");
      /* FIXME it's possible that we have to break the chain */
      return;
    }

    /* now check which one of the elements we can remove from the group */
    if (!still_link1) {
      GST_INFO (GST_CAT_SCHEDULING, "element1 is separated from the group");
      /* see if the element was an entry point for the group */
      if (group->entry == element1) {
	/* we're going to remove the element so we need to clear it as the
	 * entry point */
        group->entry = NULL;
      }
      remove_from_group (group, element1);
    }
    if (!still_link2) {
      GST_INFO (GST_CAT_SCHEDULING, "element2 is separated from the group");

      /* see if the element was an entry point for the group */
      if (group->entry == element2) {
	/* we're going to remove the element so we need to clear it as the
	 * entry point */
        group->entry = NULL;
      }
      remove_from_group (group, element2);
    }
  }
}

static GstPad*
gst_opt_scheduler_pad_select (GstScheduler *sched, GList *padlist)
{
  //GstOptScheduler *osched = GST_OPT_SCHEDULER_CAST (sched);
  
  g_warning ("pad select, implement me");

  return NULL;
}

static GstClockReturn
gst_opt_scheduler_clock_wait (GstScheduler *sched, GstElement *element,
	                      GstClockID id, GstClockTimeDiff *jitter)
{
  return gst_clock_id_wait (id, jitter);
}

/* a scheduler iteration is done by looping and scheduling the active chains */
static GstSchedulerState
gst_opt_scheduler_iterate (GstScheduler *sched)
{
  GstSchedulerState state = GST_SCHEDULER_STATE_STOPPED;
  GstOptScheduler *osched = GST_OPT_SCHEDULER_CAST (sched);
  gint iterations = osched->iterations;

  osched->state = GST_OPT_SCHEDULER_STATE_RUNNING;

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
        schedule_chain (chain);
        scheduled = TRUE;
      }

      /* don't schedule any more chains when in error */
      if (osched->state == GST_OPT_SCHEDULER_STATE_ERROR) {
        GST_INFO (GST_CAT_SCHEDULING, "scheduler %p is in error", sched);
        break;
      }	
      else if (osched->state == GST_OPT_SCHEDULER_STATE_INTERRUPTED) {
        GST_INFO (GST_CAT_SCHEDULING, "scheduler %p is interrupted, continue with next chain", sched);
        osched->state = GST_OPT_SCHEDULER_STATE_RUNNING;
      }

      GST_INFO (GST_CAT_SCHEDULING, "iterate scheduled %p", chain);

      chains = g_slist_next (chains);
      unref_chain (chain);
    }

    /* at this point it's possible that the scheduler state is
     * in error, we then return an error */
    if (osched->state == GST_OPT_SCHEDULER_STATE_ERROR) {
      state = GST_SCHEDULER_STATE_ERROR;
      break;
    }
    else {
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
gst_opt_scheduler_show (GstScheduler *sched)
{
  GstOptScheduler *osched = GST_OPT_SCHEDULER_CAST (sched);
  GSList *chains;

  g_print ("iterations:    %d\n", osched->iterations);
  g_print ("max recursion: %d\n", osched->max_recursion);

  chains = osched->chains;
  while (chains) {
    GstOptSchedulerChain *chain = (GstOptSchedulerChain *) chains->data;
    GSList *groups = chain->groups;
    chains = g_slist_next (chains);

    g_print ("+- chain %p: refcount %d, %d groups, %d enabled, flags %d\n", 
		    chain, chain->refcount, chain->num_groups, chain->num_enabled, chain->flags);

    while (groups) {
      GstOptSchedulerGroup *group = (GstOptSchedulerGroup *) groups->data;
      GSList *elements = group->elements;
      groups = g_slist_next (groups);

      g_print (" +- group %p: refcount %d, %d elements, %d enabled, flags %d, entry %s, %s\n", 
		      group, group->refcount, group->num_elements, group->num_enabled, group->flags,
		      (group->entry ? GST_ELEMENT_NAME (group->entry): "(none)"),
		      (group->type == GST_OPT_SCHEDULER_GROUP_GET ? "get-based" : "loop-based") );

      while (elements) {
        GstElement *element = (GstElement *) elements->data;
        elements = g_slist_next (elements);

        g_print ("  +- element %s\n", GST_ELEMENT_NAME (element));
      }
    }
  }
}

static void
gst_opt_scheduler_get_property (GObject *object, guint prop_id,
                                GValue *value, GParamSpec *pspec)
{
  GstOptScheduler *osched;
  
  g_return_if_fail (GST_IS_OPT_SCHEDULER (object));

  osched = GST_OPT_SCHEDULER_CAST (object);

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
gst_opt_scheduler_set_property (GObject *object, guint prop_id,
		                const GValue *value, GParamSpec *pspec)
{
  GstOptScheduler *osched;
  
  g_return_if_fail (GST_IS_OPT_SCHEDULER (object));

  osched = GST_OPT_SCHEDULER_CAST (object);

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

