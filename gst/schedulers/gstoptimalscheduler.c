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

#include "cothreads_compat.h"


#define GST_ELEMENT_SCHED_CONTEXT(elem)		((GstOptSchedulerCtx*) (GST_ELEMENT_CAST (elem)->sched_private))
#define GST_ELEMENT_SCHED_GROUP(elem)		(GST_ELEMENT_SCHED_CONTEXT (elem)->group)

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
} GstOptSchedulerState;

struct _GstOptScheduler {
  GstScheduler parent;

  GstOptSchedulerState state;

  cothread_context *context;
  gboolean use_cothreads;

  GList *elements;
  GList *chains;
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
  GstOptScheduler 		*sched;

  GstOptSchedulerChainFlags	 flags;
  
  GList 			*groups;			/* the groups in this chain */
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

#define GST_OPT_SCHEDULER_GROUP_DISABLE(group) 		((group)->flags |= GST_OPT_SCHEDULER_GROUP_DISABLED)
#define GST_OPT_SCHEDULER_GROUP_ENABLE(group) 		((group)->flags &= ~GST_OPT_SCHEDULER_GROUP_DISABLED)
#define GST_OPT_SCHEDULER_GROUP_IS_DISABLED(group) 	((group)->flags & GST_OPT_SCHEDULER_GROUP_DISABLED)

typedef struct _GstOptSchedulerGroup GstOptSchedulerGroup;

typedef int (*GroupScheduleFunction)	(int argc, char *argv[]);

struct _GstOptSchedulerGroup {
  GstOptSchedulerChain 		*chain;  		/* the chain this group belongs to */
  GstOptSchedulerGroupFlags	 flags;			/* flags for this group */
  GstOptSchedulerGroupType	 type;			/* flags for this group */

  GList 			*elements;		/* elements of this group */
  gint				 num_elements;
  gint				 num_enabled;
  GstElement 			*entry;			/* the group's entry point */

  cothread 			*cothread;		/* the cothread of this group */

  GroupScheduleFunction 	 schedulefunc;
  int				 argc;
  char			       **argv;
};

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
  gint element_type;
};


static void 		gst_opt_scheduler_class_init 		(GstOptSchedulerClass *klass);
static void 		gst_opt_scheduler_init 			(GstOptScheduler *scheduler);

static void 		gst_opt_scheduler_dispose 		(GObject *object);

static void 		gst_opt_scheduler_setup 		(GstScheduler *sched);
static void 		gst_opt_scheduler_reset 		(GstScheduler *sched);
static void		gst_opt_scheduler_add_element		(GstScheduler *sched, GstElement *element);
static void     	gst_opt_scheduler_remove_element	(GstScheduler *sched, GstElement *element);
static GstElementStateReturn  
			gst_opt_scheduler_state_transition	(GstScheduler *sched, GstElement *element, gint transition);
static void 		gst_opt_scheduler_lock_element 		(GstScheduler *sched, GstElement *element);
static void 		gst_opt_scheduler_unlock_element 	(GstScheduler *sched, GstElement *element);
static void 		gst_opt_scheduler_yield 		(GstScheduler *sched, GstElement *element);
static gboolean		gst_opt_scheduler_interrupt 		(GstScheduler *sched, GstElement *element);
static void 		gst_opt_scheduler_error	 		(GstScheduler *sched, GstElement *element);
static void     	gst_opt_scheduler_pad_connect		(GstScheduler *sched, GstPad *srcpad, GstPad *sinkpad);
static void     	gst_opt_scheduler_pad_disconnect 	(GstScheduler *sched, GstPad *srcpad, GstPad *sinkpad);
static GstPad*  	gst_opt_scheduler_pad_select 		(GstScheduler *sched, GList *padlist);
static GstClockReturn   gst_opt_scheduler_clock_wait        	(GstScheduler *sched, GstElement *element,
	                                                         GstClock *clock, GstClockTime time, GstClockTimeDiff *jitter);
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

    _gst_opt_scheduler_type = g_type_register_static (GST_TYPE_SCHEDULER, "GstOptScheduler", &scheduler_info, 0);
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

  gobject_class->dispose	= GST_DEBUG_FUNCPTR (gst_opt_scheduler_dispose);

  gstscheduler_class->setup             = GST_DEBUG_FUNCPTR (gst_opt_scheduler_setup);
  gstscheduler_class->reset             = GST_DEBUG_FUNCPTR (gst_opt_scheduler_reset);
  gstscheduler_class->add_element 	= GST_DEBUG_FUNCPTR (gst_opt_scheduler_add_element);
  gstscheduler_class->remove_element 	= GST_DEBUG_FUNCPTR (gst_opt_scheduler_remove_element);
  gstscheduler_class->state_transition 	= GST_DEBUG_FUNCPTR (gst_opt_scheduler_state_transition);
  gstscheduler_class->lock_element 	= GST_DEBUG_FUNCPTR (gst_opt_scheduler_lock_element);
  gstscheduler_class->unlock_element 	= GST_DEBUG_FUNCPTR (gst_opt_scheduler_unlock_element);
  gstscheduler_class->yield	 	= GST_DEBUG_FUNCPTR (gst_opt_scheduler_yield);
  gstscheduler_class->interrupt 	= GST_DEBUG_FUNCPTR (gst_opt_scheduler_interrupt);
  gstscheduler_class->error	 	= GST_DEBUG_FUNCPTR (gst_opt_scheduler_error);
  gstscheduler_class->pad_connect 	= GST_DEBUG_FUNCPTR (gst_opt_scheduler_pad_connect);
  gstscheduler_class->pad_disconnect 	= GST_DEBUG_FUNCPTR (gst_opt_scheduler_pad_disconnect);
  gstscheduler_class->pad_select	= GST_DEBUG_FUNCPTR (gst_opt_scheduler_pad_select);
  gstscheduler_class->clock_wait	= GST_DEBUG_FUNCPTR (gst_opt_scheduler_clock_wait);
  gstscheduler_class->iterate 		= GST_DEBUG_FUNCPTR (gst_opt_scheduler_iterate);
  gstscheduler_class->show 		= GST_DEBUG_FUNCPTR (gst_opt_scheduler_show);
}

static void
gst_opt_scheduler_init (GstOptScheduler *scheduler)
{
  scheduler->elements = NULL;
  scheduler->use_cothreads = FALSE;
  scheduler->use_cothreads = TRUE;
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

  factory = gst_scheduler_factory_new ("optimal",
	                              "An optimal scheduler",
		                      gst_opt_scheduler_get_type());

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
  "gstoptimalscheduler",
  plugin_init
};

/*
 * Entry points for this scheduler.
 */
static void
gst_opt_scheduler_setup (GstScheduler *sched)
{   
  GstOptScheduler *osched = GST_OPT_SCHEDULER_CAST (sched);
	      
  /* first create thread context */
  if (osched->context == NULL && osched->use_cothreads) {
    GST_DEBUG (GST_CAT_SCHEDULING, "initializing cothread context");
    osched->context = do_cothread_context_init ();
  }
} 
  
static void 
gst_opt_scheduler_reset (GstScheduler *sched)
{ 
  GstOptScheduler *osched = GST_OPT_SCHEDULER_CAST (sched);
	      
  if (osched->context && osched->use_cothreads) {
    do_cothread_context_destroy (osched->context);
    osched->context = NULL; 
  }
}     

static void
delete_chain (GstOptScheduler *osched, GstOptSchedulerChain *chain)
{
  GST_INFO (GST_CAT_SCHEDULING, "delete chain %p", chain);

  g_assert (chain->sched == osched);

  osched->chains = g_list_remove (osched->chains, chain);

  g_list_free (chain->groups);
  g_free (chain);
}

static void
add_to_chain (GstOptSchedulerChain *chain, GstOptSchedulerGroup *group)
{
  GST_INFO (GST_CAT_SCHEDULING, "adding group %p to chain %p", group, chain);

  g_assert (group->chain == NULL);

  chain->groups = g_list_prepend (chain->groups, group);
  group->chain = chain;
  chain->num_groups++;
}

static GstOptSchedulerChain*
create_chain (GstOptScheduler *osched)
{
  GstOptSchedulerChain *chain;

  chain = g_new0 (GstOptSchedulerChain, 1);
  chain->sched = osched;

  osched->chains = g_list_prepend (osched->chains, chain);

  GST_INFO (GST_CAT_SCHEDULING, "new chain %p", chain);

  return chain;
}

static void
remove_from_chain (GstOptSchedulerChain *chain, GstOptSchedulerGroup *group)
{
  GST_INFO (GST_CAT_SCHEDULING, "removing group %p from chain %p", group, chain);

  g_assert (group->chain == chain);

  chain->groups = g_list_remove (chain->groups, group);
  chain->num_groups--;

  group->chain = NULL;
}

static void
merge_chains (GstOptSchedulerChain *chain1, GstOptSchedulerChain *chain2)
{
  GList *walk;
  
  GST_INFO (GST_CAT_SCHEDULING, "mergin chain %p and %p", chain1, chain2);
  
  if (chain1 == chain2)
    return;

  walk = chain2->groups;
  while (walk) {
    GstOptSchedulerGroup *group = (GstOptSchedulerGroup *) walk->data;

    group->chain = NULL;
    add_to_chain (chain1, group);
    walk = g_list_next (walk);
  }
  delete_chain (chain2->sched, chain2);
}

static void
chain_group_set_enabled (GstOptSchedulerChain *chain, GstOptSchedulerGroup *group, gboolean enabled)
{
  if (enabled) {
    chain->num_enabled++;
    GST_INFO (GST_CAT_SCHEDULING, "enable group %p in chain %p, now %d groups enabled out of %d", group, chain,
		    chain->num_enabled, chain->num_groups);
    if (chain->num_enabled == chain->num_groups) {
      GST_INFO (GST_CAT_SCHEDULING, "enable chain %p", chain);
      GST_OPT_SCHEDULER_CHAIN_ENABLE (chain);
    }
  }
  else {
    chain->num_enabled--;
    GST_INFO (GST_CAT_SCHEDULING, "disable group %p in chain %p, now %d groups enabled out of %d", group, chain,
		    chain->num_enabled, chain->num_groups);
    if (chain->num_enabled == 0) {
      GST_INFO (GST_CAT_SCHEDULING, "disable chain %p", chain);
      GST_OPT_SCHEDULER_CHAIN_DISABLE (chain);
    }
  }
}

static void
add_to_group (GstOptSchedulerGroup *group, GstElement *element)
{
  GST_INFO (GST_CAT_SCHEDULING, "adding element \"%s\" to group %p", GST_ELEMENT_NAME (element), group);

  if (GST_ELEMENT_IS_DECOUPLED (element)) {
    GST_INFO (GST_CAT_SCHEDULING, "element \"%s\" is decoupled, not adding to group %p", GST_ELEMENT_NAME (element), group);
    return;
  }

  g_assert (GST_ELEMENT_SCHED_GROUP (element) == NULL);

  group->elements = g_list_prepend (group->elements, element);
  group->num_elements++;

  GST_ELEMENT_SCHED_GROUP (element) = group;
}

static GstOptSchedulerGroup*
create_group (GstOptSchedulerChain *chain, GstElement *element)
{
  GstOptSchedulerGroup *group;

  group = g_new0 (GstOptSchedulerGroup, 1);
  GST_INFO (GST_CAT_SCHEDULING, "new group %p", group);

  add_to_group (group, element);
  add_to_chain (chain, group);
  
  return group;
}

static void 
destroy_group_scheduler (GstOptSchedulerGroup *group) 
{
  if (group->flags & GST_OPT_SCHEDULER_GROUP_RUNNING)
    g_warning ("removing running element");

  if (group->cothread) {
    do_cothread_destroy (group->cothread);
  }
  else {
    group->schedulefunc = NULL;
    group->argc = 0;
    group->argv = NULL;
  }

  group->flags &= ~GST_OPT_SCHEDULER_GROUP_SCHEDULABLE;
}

static void
delete_group (GstOptSchedulerGroup *group)
{
  GST_INFO (GST_CAT_SCHEDULING, "delete group %p", group);

  g_assert (group->chain == NULL);

  if (group->flags & GST_OPT_SCHEDULER_GROUP_SCHEDULABLE)
    destroy_group_scheduler (group);

  g_list_free (group->elements);
  g_free (group);
}

static void
merge_groups (GstOptSchedulerGroup *group1, GstOptSchedulerGroup *group2)
{
  GList *walk;
  GstOptSchedulerChain *chain1;
  GstOptSchedulerChain *chain2;
  
  GST_INFO (GST_CAT_SCHEDULING, "merging groups %p and %p", group1, group2);
  
  if (group1 == group2)
    return;

  walk = group2->elements;
  while (walk) {
    add_to_group (group1, (GstElement *)walk->data);
    walk = g_list_next (walk);
  }

  chain1 = group1->chain;
  chain2 = group2->chain;

  remove_from_chain (chain2, group2);
  delete_group (group2);

  merge_chains (chain1, chain2);
}

/*
static void
remove_from_group (GstOptSchedulerGroup *group, GstElement *element)
{
  GST_INFO (GST_CAT_SCHEDULING, "removing element \"%s\" to group %p", GST_ELEMENT_NAME (element), group);

  group->elements = g_list_remove (group->elements, element);
  group->num_elements--;

  GST_ELEMENT_SCHED_GROUP (element) = NULL;
}
*/

static void
group_element_set_enabled (GstOptSchedulerGroup *group, GstElement *element, gboolean enabled)
{
  if (enabled) {
    group->num_enabled++;
    GST_INFO (GST_CAT_SCHEDULING, "enable element %s in group %p, now %d elements enabled out of %d", 
		    GST_ELEMENT_NAME (element), group, group->num_enabled, group->num_elements);
    if (group->num_enabled == group->num_elements) {
      GST_INFO (GST_CAT_SCHEDULING, "enable group %p", group);
      GST_OPT_SCHEDULER_GROUP_ENABLE (group);
      chain_group_set_enabled (group->chain, group, TRUE);
    }
  }
  else {
    group->num_enabled--;
    GST_INFO (GST_CAT_SCHEDULING, "disable element %s in group %p, now %d elements enabled out of %d", 
		    GST_ELEMENT_NAME (element), group, group->num_enabled, group->num_elements);
    if (group->num_enabled == 0) {
      GST_INFO (GST_CAT_SCHEDULING, "disable group %p", group);
      GST_OPT_SCHEDULER_GROUP_DISABLE (group);
      chain_group_set_enabled (group->chain, group, FALSE);
    }
  }
}

static int 
schedule_group (GstOptSchedulerGroup *group) 
{
  if (group->chain->sched->use_cothreads) {
    if (group->cothread)
      do_cothread_switch (group->cothread);
    return 1;
  }
  else {
    if (!(group->flags & GST_OPT_SCHEDULER_GROUP_RUNNING))
      return group->schedulefunc (group->argc, group->argv);
    else
      return 0;
  }
}

static void 
schedule_chain (GstOptSchedulerChain *chain) 
{
  GList *groups = chain->groups;

  while (groups) {
    GstOptSchedulerGroup *group = (GstOptSchedulerGroup *) groups->data;
    groups = g_list_next (groups);

    if (!GST_OPT_SCHEDULER_GROUP_IS_DISABLED (group)) {
      GST_INFO (GST_CAT_SCHEDULING, "scheduling group %p in chain %p", 
 	        group, chain);

      schedule_group (group);

      break;
    }
  }
}

static void
gst_opt_scheduler_add_element (GstScheduler *sched, GstElement *element)
{
  GstOptScheduler *osched = GST_OPT_SCHEDULER_CAST (sched);
  GstOptSchedulerCtx *ctx;

  GST_INFO (GST_CAT_SCHEDULING, "adding element \"%s\" to scheduler", GST_ELEMENT_NAME (element));

  if (GST_ELEMENT_IS_DECOUPLED (element))
    return;

  ctx = g_new0 (GstOptSchedulerCtx, 1);
  GST_ELEMENT_SCHED_CONTEXT (element) = ctx;

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
  //GstOptScheduler *osched = GST_OPT_SCHEDULER_CAST (sched);

  GST_INFO (GST_CAT_SCHEDULING, "removing element \"%s\" from scheduler", GST_ELEMENT_NAME (element));

  g_free (GST_ELEMENT_SCHED_CONTEXT (element));
  GST_ELEMENT_SCHED_CONTEXT (element) = NULL;

  g_warning ("remove implement me");
}

static int
wrapper_function (int argc, char *argv[])
{
  GstOptSchedulerGroup *group = (GstOptSchedulerGroup *) argv;

  GST_INFO (GST_CAT_SCHEDULING, "wrapper function of group %p", group);

  group->flags |= GST_OPT_SCHEDULER_GROUP_RUNNING;

  switch (group->type) {
    case GST_OPT_SCHEDULER_GROUP_GET:
    {
      const GList *pads = gst_element_get_pad_list (group->entry);
      GstBuffer *buffer;

      while (pads) {
	GstPad *pad = GST_PAD_CAST (pads->data);
	pads = g_list_next (pads);

	/* skip sinks and ghostpads */
	if (!GST_PAD_IS_SRC (pad) || !GST_IS_REAL_PAD (pad))
          continue;

        GST_INFO (GST_CAT_SCHEDULING, "doing get and push on pad \"%s:%s\" in group %p", 
		      GST_DEBUG_PAD_NAME (pad), group);

	buffer = GST_RPAD_GETFUNC (pad) (pad);
	if (buffer)
	  gst_pad_push (pad, buffer);
      }
      break;
    }
    case GST_OPT_SCHEDULER_GROUP_LOOP:
    {
      GstElement *entry = group->entry;

      GST_INFO (GST_CAT_SCHEDULING, "calling loopfunc of element %s in group %p", 
		      GST_ELEMENT_NAME (entry), group);

      entry->loopfunc (entry);
      break;
    }
    default:
      g_warning ("(internal error) unkown group type %d, disabling\n", group->type);
      chain_group_set_enabled (group->chain, group, FALSE);
      group->chain->sched->state = GST_OPT_SCHEDULER_STATE_ERROR;
      break;
  }

  group->flags &= ~GST_OPT_SCHEDULER_GROUP_RUNNING;

  return 0;
}

static void
gst_opt_scheduler_loop_wrapper (GstPad *sinkpad, GstBuffer *buffer)
{
  GstOptSchedulerGroup *group;

  GST_INFO (GST_CAT_SCHEDULING, "loop wrapper, putting buffer in bufpen");

  group = GST_ELEMENT_SCHED_GROUP (GST_PAD_PARENT (sinkpad));

  GST_RPAD_BUFPEN (GST_RPAD_PEER (sinkpad)) = buffer;

  while (GST_RPAD_BUFPEN (GST_RPAD_PEER (sinkpad))) {
    if (!schedule_group (group))
      break;
  }
}

static GstBuffer*
gst_opt_scheduler_get_wrapper (GstPad *srcpad)
{
  GstBuffer *buffer;

  GST_INFO (GST_CAT_SCHEDULING, "get wrapper, removing buffer from bufpen");

  buffer = GST_RPAD_BUFPEN (srcpad);

  while (!buffer) {
    GstOptSchedulerGroup *group;
    
    group = GST_ELEMENT_SCHED_GROUP (GST_PAD_PARENT (srcpad));

    schedule_group (group);
    
    buffer = GST_RPAD_BUFPEN (srcpad);
  }

  GST_RPAD_BUFPEN (srcpad) = NULL;

  return buffer;
}

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
setup_group_scheduler (GstOptScheduler *osched, GstOptSchedulerGroup *group) 
{
  if (osched->use_cothreads) {
    do_cothread_create (group->cothread, osched->context,
		      wrapper_function, 0, (char **) group);
  }
  else {
    group->schedulefunc = wrapper_function;
    group->argc = 0;
    group->argv = (char **) group;
  }

  group->flags |= GST_OPT_SCHEDULER_GROUP_SCHEDULABLE;
}

static GstElementStateReturn
gst_opt_scheduler_state_transition (GstScheduler *sched, GstElement *element, gint transition)
{
  GstOptScheduler *osched = GST_OPT_SCHEDULER_CAST (sched);
  GstOptSchedulerGroup *group;
  
  GST_INFO (GST_CAT_SCHEDULING, "element \"%s\" state change %d", GST_ELEMENT_NAME (element), transition);

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
    return GST_STATE_SUCCESS;
  }

  if (GST_ELEMENT_IS_DECOUPLED (element))
    return GST_STATE_SUCCESS;

  group = GST_ELEMENT_SCHED_GROUP (element);

  switch (transition) {
    case GST_STATE_PAUSED_TO_PLAYING:
      if (!(group->flags & GST_OPT_SCHEDULER_GROUP_SCHEDULABLE)) {
	setup_group_scheduler (osched, group);
      }
      group_element_set_enabled (group, element, TRUE);
      break;
    case GST_STATE_PLAYING_TO_PAUSED:
      group_element_set_enabled (group, element, FALSE);
      break;
    default:
      break;
  }

  return GST_STATE_SUCCESS;
}

static void
gst_opt_scheduler_lock_element (GstScheduler *sched, GstElement *element)
{
  //GstOptScheduler *osched = GST_OPT_SCHEDULER_CAST (sched);
}

static void
gst_opt_scheduler_unlock_element (GstScheduler *sched, GstElement *element)
{
  //GstOptScheduler *osched = GST_OPT_SCHEDULER_CAST (sched);
}

static void
gst_opt_scheduler_yield (GstScheduler *sched, GstElement *element)
{
  //GstOptScheduler *osched = GST_OPT_SCHEDULER_CAST (sched);
}

static gboolean
gst_opt_scheduler_interrupt (GstScheduler *sched, GstElement *element)
{
  //GstOptScheduler *osched = GST_OPT_SCHEDULER_CAST (sched);
  return TRUE;
}

static void
gst_opt_scheduler_error (GstScheduler *sched, GstElement *element)
{
  //GstOptScheduler *osched = GST_OPT_SCHEDULER_CAST (sched);
}

static GstOptSchedulerGroup*
group_elements (GstOptScheduler *osched, GstElement *element1, GstElement *element2)
{
  GstOptSchedulerCtx *ctx1, *ctx2;
  GstOptSchedulerGroup *group1 = NULL, *group2 = NULL, *group = NULL;
  
  ctx1 = GST_ELEMENT_SCHED_CONTEXT (element1);
  if (ctx1)
    group1 = ctx1->group;
  ctx2 = GST_ELEMENT_SCHED_CONTEXT (element2);
  if (ctx2)
    group2 = ctx2->group;
  
  if (!group1 && !group2) {
    GstOptSchedulerChain *chain;

    GST_INFO (GST_CAT_SCHEDULING, "creating new group to hold \"%s\" and \"%s\"", 
		  GST_ELEMENT_NAME (element1), GST_ELEMENT_NAME (element2));

    chain = create_chain (osched);
    group = create_group (chain, element1);
    add_to_group (group, element2);
  }
  else if (group1) {
    GST_INFO (GST_CAT_SCHEDULING, "adding \"%s\" to \"%s\"'s group", 
		  GST_ELEMENT_NAME (element2), GST_ELEMENT_NAME (element1));
    if (group2)
      merge_groups (group1, group2);
    else
      add_to_group (group1, element2);

    group = group1;
  }
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
} ConnectionType;

static void
gst_opt_scheduler_pad_connect (GstScheduler *sched, GstPad *srcpad, GstPad *sinkpad)
{
  GstOptScheduler *osched = GST_OPT_SCHEDULER_CAST (sched);
  ConnectionType type = GST_OPT_INVALID;
  GstElement *element1, *element2;

  GST_INFO (GST_CAT_SCHEDULING, "pad connect between \"%s:%s\" and \"%s:%s\"", 
		  GST_DEBUG_PAD_NAME (srcpad), GST_DEBUG_PAD_NAME (sinkpad));

  element1 = GST_PAD_PARENT (srcpad);
  element2 = GST_PAD_PARENT (sinkpad);

  if (element1->loopfunc && element2->loopfunc)
    type = GST_OPT_LOOP_TO_LOOP;
  else {
    if (element1->loopfunc) {
      if (GST_RPAD_CHAINFUNC (sinkpad))
        type = GST_OPT_LOOP_TO_CHAIN;
    }
    else if (element2->loopfunc) {
      if (GST_RPAD_GETFUNC (srcpad))
        type = GST_OPT_GET_TO_LOOP;
      else
        type = GST_OPT_CHAIN_TO_LOOP;
    }
    else {
      if (GST_RPAD_GETFUNC (srcpad) && GST_RPAD_CHAINFUNC (sinkpad))
        type = GST_OPT_GET_TO_CHAIN;
      else 
        type = GST_OPT_CHAIN_TO_CHAIN;
    }
  }
  
  switch (type) {
    case GST_OPT_GET_TO_CHAIN:
    {
      GstOptSchedulerGroup *group = NULL;

      GST_INFO (GST_CAT_SCHEDULING, "get to chain based connection");

      GST_RPAD_GETHANDLER (srcpad) = GST_RPAD_GETFUNC (srcpad);
      if (GST_ELEMENT_IS_EVENT_AWARE (element2))
        GST_RPAD_CHAINHANDLER (sinkpad) = GST_RPAD_CHAINFUNC (sinkpad);
      else
        GST_RPAD_CHAINHANDLER (sinkpad) = gst_opt_scheduler_chain_wrapper;

      group = group_elements (osched, element1, element2);

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
      GST_INFO (GST_CAT_SCHEDULING, "loop/chain to chain based connection");

      if (GST_ELEMENT_IS_EVENT_AWARE (element2))
        GST_RPAD_CHAINHANDLER (sinkpad) = GST_RPAD_CHAINFUNC (sinkpad);
      else
        GST_RPAD_CHAINHANDLER (sinkpad) = gst_opt_scheduler_chain_wrapper;

      group_elements (osched, element1, element2);
      break;
    case GST_OPT_GET_TO_LOOP:
      GST_INFO (GST_CAT_SCHEDULING, "get to loop based connection");

      GST_RPAD_GETHANDLER (srcpad) = GST_RPAD_GETFUNC (srcpad);
      group_elements (osched, element1, element2);
      break;
    case GST_OPT_CHAIN_TO_LOOP:
    case GST_OPT_LOOP_TO_LOOP:
    {
      GstOptSchedulerGroup *group1, *group2;

      GST_INFO (GST_CAT_SCHEDULING, "chain/loop to loop based connection");

      GST_RPAD_CHAINHANDLER (sinkpad) = gst_opt_scheduler_loop_wrapper;
      GST_RPAD_GETHANDLER (srcpad) = gst_opt_scheduler_get_wrapper;

      group1 = GST_ELEMENT_SCHED_GROUP (element1);
      group2 = GST_ELEMENT_SCHED_GROUP (element2);
      if (!group1) {
        GST_INFO (GST_CAT_SCHEDULING, "creating new group for element %s", GST_ELEMENT_NAME (element1));
        group1 = create_group (group2->chain, element1);
      }
      else {
        merge_chains (group1->chain, group2->chain);
      }
      break;
    }
    case GST_OPT_INVALID:
      g_warning ("(internal error) invalid element connection");
      break;
  }
}

static void
gst_opt_scheduler_pad_disconnect (GstScheduler *sched, GstPad *srcpad, GstPad *sinkpad)
{
  //GstOptScheduler *osched = GST_OPT_SCHEDULER_CAST (sched);
  GST_INFO (GST_CAT_SCHEDULING, "pad disconnect between \"%s:%s\" and \"%s:%s\"", 
		  GST_DEBUG_PAD_NAME (srcpad), GST_DEBUG_PAD_NAME (sinkpad));

  g_warning ("pad disconnect, implement me");
}

static GstPad*
gst_opt_scheduler_pad_select (GstScheduler *sched, GList *padlist)
{
  //GstOptScheduler *osched = GST_OPT_SCHEDULER_CAST (sched);
  return NULL;
}

static GstClockReturn
gst_opt_scheduler_clock_wait (GstScheduler *sched, GstElement *element,
	                      GstClock *clock, GstClockTime time, GstClockTimeDiff *jitter)
{
  return gst_clock_wait (clock, time, jitter);
}

static GstSchedulerState
gst_opt_scheduler_iterate (GstScheduler *sched)
{
  GstSchedulerState state;
  GstOptScheduler *osched = GST_OPT_SCHEDULER_CAST (sched);
  GList *chains;
  gboolean scheduled = FALSE;

  osched->state = GST_OPT_SCHEDULER_STATE_RUNNING;

  chains = osched->chains;
  while (chains) {
    GstOptSchedulerChain *chain = (GstOptSchedulerChain *) chains->data;
    chains = g_list_next (chains);

    if (!GST_OPT_SCHEDULER_CHAIN_IS_DISABLED (chain)) {
      schedule_chain (chain);
      scheduled = TRUE;
    }
  }

  if (osched->state == GST_OPT_SCHEDULER_STATE_ERROR) {
    state = GST_SCHEDULER_STATE_ERROR;
  }
  else {
    if (scheduled)
      state = GST_SCHEDULER_STATE (sched);
    else
      state = GST_SCHEDULER_STATE_STOPPED;
  }

  return state;
}


static void
gst_opt_scheduler_show (GstScheduler *sched)
{
  //GstOptScheduler *osched = GST_OPT_SCHEDULER_CAST (sched);
}
