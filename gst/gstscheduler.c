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

//#define GST_DEBUG_ENABLED
#include "gst_private.h"

#include "gstscheduler.h"


static int
gst_schedule_loopfunc_wrapper (int argc,char *argv[])
{
  GstElement *element = GST_ELEMENT (argv);
  G_GNUC_UNUSED const gchar *name = GST_ELEMENT_NAME (element);

  GST_DEBUG_ENTER("(%d,'%s')",argc,name);

  do {
    GST_DEBUG (GST_CAT_DATAFLOW,"calling loopfunc %s for element %s\n",
               GST_DEBUG_FUNCPTR_NAME (element->loopfunc),name);
    (element->loopfunc) (element);
    GST_DEBUG (GST_CAT_DATAFLOW,"element %s ended loop function\n", name);
  } while (!GST_ELEMENT_IS_COTHREAD_STOPPING(element));
  GST_FLAG_UNSET(element,GST_ELEMENT_COTHREAD_STOPPING);

  GST_DEBUG_LEAVE("(%d,'%s')",argc,name);
  return 0;
}

static int
gst_schedule_chain_wrapper (int argc,char *argv[])
{
  GstElement *element = GST_ELEMENT (argv);
  G_GNUC_UNUSED const gchar *name = GST_ELEMENT_NAME (element);
  GList *pads;
  GstPad *pad;
  GstRealPad *realpad;
  GstBuffer *buf;

  GST_DEBUG_ENTER("(\"%s\")",name);

  GST_DEBUG (GST_CAT_DATAFLOW,"stepping through pads\n");
  do {
    pads = element->pads;
    while (pads) {
      pad = GST_PAD (pads->data);
      pads = g_list_next (pads);
      if (!GST_IS_REAL_PAD(pad)) continue;
      realpad = GST_REAL_PAD(pad);
      if (GST_RPAD_DIRECTION(realpad) == GST_PAD_SINK) {
        GST_DEBUG (GST_CAT_DATAFLOW,"pulling a buffer from %s:%s\n", name, GST_PAD_NAME (pad));
        buf = gst_pad_pull (pad);
        GST_DEBUG (GST_CAT_DATAFLOW,"calling chain function of %s:%s\n", name, GST_PAD_NAME (pad));
        if (buf) GST_RPAD_CHAINFUNC(realpad) (pad,buf);
        GST_DEBUG (GST_CAT_DATAFLOW,"calling chain function of %s:%s done\n", name, GST_PAD_NAME (pad));
      }
    }
  } while (!GST_ELEMENT_IS_COTHREAD_STOPPING(element));
  GST_FLAG_UNSET(element,GST_ELEMENT_COTHREAD_STOPPING);

  GST_DEBUG_LEAVE("(%d,'%s')",argc,name);
  return 0;
}

static int
gst_schedule_src_wrapper (int argc,char *argv[])
{
  GstElement *element = GST_ELEMENT (argv);
  GList *pads;
  GstRealPad *realpad;
  GstBuffer *buf = NULL;
  G_GNUC_UNUSED const gchar *name = GST_ELEMENT_NAME (element);

  GST_DEBUG_ENTER("(%d,\"%s\")",argc,name);

  do {
    pads = element->pads;
    while (pads) {
      if (!GST_IS_REAL_PAD(pads->data)) continue;
      realpad = (GstRealPad*)(pads->data);
      pads = g_list_next(pads);
      if (GST_RPAD_DIRECTION(realpad) == GST_PAD_SRC) {
        GST_DEBUG (GST_CAT_DATAFLOW,"calling _getfunc for %s:%s\n",GST_DEBUG_PAD_NAME(realpad));
        if (realpad->regiontype != GST_REGION_NONE) {
          g_return_val_if_fail (GST_RPAD_GETREGIONFUNC(realpad) != NULL, 0);
//          if (GST_RPAD_GETREGIONFUNC(realpad) == NULL)
//            fprintf(stderr,"error, no getregionfunc in \"%s\"\n", name);
//          else
          buf = (GST_RPAD_GETREGIONFUNC(realpad))((GstPad*)realpad,realpad->regiontype,realpad->offset,realpad->len);
	  realpad->regiontype = GST_REGION_NONE;
        } else {
          g_return_val_if_fail (GST_RPAD_GETFUNC(realpad) != NULL, 0);
//          if (GST_RPAD_GETFUNC(realpad) == NULL)
//            fprintf(stderr,"error, no getfunc in \"%s\"\n", name);
//          else
          buf = GST_RPAD_GETFUNC(realpad) ((GstPad*)realpad);
        }

        GST_DEBUG (GST_CAT_DATAFLOW,"calling gst_pad_push on pad %s:%s\n",GST_DEBUG_PAD_NAME(realpad));
        if (buf) gst_pad_push ((GstPad*)realpad, buf);
      }
    }
  } while (!GST_ELEMENT_IS_COTHREAD_STOPPING(element));
  GST_FLAG_UNSET(element,GST_ELEMENT_COTHREAD_STOPPING);

  GST_DEBUG_LEAVE("");
  return 0;
}

static void
gst_schedule_pushfunc_proxy (GstPad *pad, GstBuffer *buf)
{
  GstRealPad *peer = GST_RPAD_PEER(pad);

  GST_DEBUG_ENTER("(%s:%s)",GST_DEBUG_PAD_NAME(pad));
  GST_DEBUG (GST_CAT_DATAFLOW,"putting buffer %p in peer's pen\n",buf);

  // FIXME this should be bounded
  // loop until the bufferpen is empty so we can fill it up again
  while (GST_RPAD_BUFPEN(pad) != NULL) {
    GST_DEBUG (GST_CAT_DATAFLOW, "switching to %p to empty bufpen\n",
               GST_ELEMENT (GST_PAD_PARENT (pad))->threadstate);
    cothread_switch (GST_ELEMENT (GST_PAD_PARENT (pad))->threadstate);

    // we may no longer be the same pad, check.
    if (GST_RPAD_PEER(peer) != (GstRealPad *)pad) {
      GST_DEBUG (GST_CAT_DATAFLOW, "new pad in mid-switch!\n");
      pad = (GstPad *)GST_RPAD_PEER(peer);
    }
  }

  // now fill the bufferpen and switch so it can be consumed
  GST_RPAD_BUFPEN(GST_RPAD_PEER(pad)) = buf;
  GST_DEBUG (GST_CAT_DATAFLOW,"switching to %p\n",GST_ELEMENT (GST_PAD_PARENT (pad))->threadstate);
  cothread_switch (GST_ELEMENT (GST_PAD_PARENT (pad))->threadstate);

  GST_DEBUG (GST_CAT_DATAFLOW,"done switching\n");
}

static GstBuffer*
gst_schedule_pullfunc_proxy (GstPad *pad)
{
  GstBuffer *buf;
  GstRealPad *peer = GST_RPAD_PEER(pad);

  GST_DEBUG_ENTER("(%s:%s)",GST_DEBUG_PAD_NAME(pad));

  // FIXME this should be bounded
  // we will loop switching to the peer until it's filled up the bufferpen
  while (GST_RPAD_BUFPEN(pad) == NULL) {
    GST_DEBUG (GST_CAT_DATAFLOW, "switching to \"%s\": %p to fill bufpen\n",
               GST_ELEMENT_NAME(GST_ELEMENT(GST_PAD_PARENT(pad))),
               GST_ELEMENT(GST_PAD_PARENT(pad))->threadstate);
    cothread_switch (GST_ELEMENT(GST_PAD_PARENT(pad))->threadstate);

    // we may no longer be the same pad, check.
    if (GST_RPAD_PEER(peer) != (GstRealPad *)pad) {
      GST_DEBUG (GST_CAT_DATAFLOW, "new pad in mid-switch!\n");
      pad = (GstPad *)GST_RPAD_PEER(peer);
    }
  }
  GST_DEBUG (GST_CAT_DATAFLOW,"done switching\n");

  // now grab the buffer from the pen, clear the pen, and return the buffer
  buf = GST_RPAD_BUFPEN(pad);
  GST_RPAD_BUFPEN(pad) = NULL;
  return buf;
}

static GstBuffer*
gst_schedule_pullregionfunc_proxy (GstPad *pad,GstRegionType type,guint64 offset,guint64 len)
{
  GstBuffer *buf;
  GstRealPad *peer = GST_RPAD_PEER(pad);

  GST_DEBUG_ENTER("%s:%s,%d,%lld,%lld",GST_DEBUG_PAD_NAME(pad),type,offset,len);

  // put the region info into the pad
  GST_RPAD_REGIONTYPE(pad) = type;
  GST_RPAD_OFFSET(pad) = offset;
  GST_RPAD_LEN(pad) = len;

  // FIXME this should be bounded
  // we will loop switching to the peer until it's filled up the bufferpen
  while (GST_RPAD_BUFPEN(pad) == NULL) {
    GST_DEBUG (GST_CAT_DATAFLOW, "switching to %p to fill bufpen\n",
               GST_ELEMENT(GST_PAD_PARENT(pad))->threadstate);
    cothread_switch (GST_ELEMENT(GST_PAD_PARENT(pad))->threadstate);

    // we may no longer be the same pad, check.
    if (GST_RPAD_PEER(peer) != (GstRealPad *)pad) {
      GST_DEBUG (GST_CAT_DATAFLOW, "new pad in mid-switch!\n");
      pad = (GstPad *)GST_RPAD_PEER(peer);
    }
  }
  GST_DEBUG (GST_CAT_DATAFLOW,"done switching\n");

  // now grab the buffer from the pen, clear the pen, and return the buffer
  buf = GST_RPAD_BUFPEN(pad);
  GST_RPAD_BUFPEN(pad) = NULL;
  return buf;
}


static void
gst_schedule_cothreaded_chain (GstBin *bin, GstScheduleChain *chain) {
  GList *elements;
  GstElement *element;
  cothread_func wrapper_function;
  GList *pads;
  GstPad *pad;

  GST_DEBUG (GST_CAT_SCHEDULING,"chain is using COTHREADS\n");

  // first create thread context
  if (bin->threadcontext == NULL) {
    GST_DEBUG (GST_CAT_SCHEDULING,"initializing cothread context\n");
    bin->threadcontext = cothread_init ();
  }

  // walk through all the chain's elements
  elements = chain->elements;
  while (elements) {
    element = GST_ELEMENT (elements->data);
    elements = g_list_next (elements);

    // start out without a wrapper function, we select it later
    wrapper_function = NULL;

    // if the element has a loopfunc...
    if (element->loopfunc != NULL) {
      wrapper_function = GST_DEBUG_FUNCPTR(gst_schedule_loopfunc_wrapper);
      GST_DEBUG (GST_CAT_SCHEDULING,"element '%s' is a loop-based\n",GST_ELEMENT_NAME(element));
    } else {
      // otherwise we need to decide what kind of cothread
      // if it's not DECOUPLED, we decide based on whether it's a source or not
      if (!GST_FLAG_IS_SET (element, GST_ELEMENT_DECOUPLED)) {
        // if it doesn't have any sinks, it must be a source (duh)
        if (element->numsinkpads == 0) {
          wrapper_function = GST_DEBUG_FUNCPTR(gst_schedule_src_wrapper);
          GST_DEBUG (GST_CAT_SCHEDULING,"element '%s' is a source, using _src_wrapper\n",GST_ELEMENT_NAME(element));
        } else {
          wrapper_function = GST_DEBUG_FUNCPTR(gst_schedule_chain_wrapper);
          GST_DEBUG (GST_CAT_SCHEDULING,"element '%s' is a filter, using _chain_wrapper\n",GST_ELEMENT_NAME(element));
        }
      }
    }

    // now we have to walk through the pads to set up their state
    pads = gst_element_get_pad_list (element);
    while (pads) {
      pad = GST_PAD (pads->data);
      pads = g_list_next (pads);
      if (!GST_IS_REAL_PAD(pad)) continue;

      // if the element is DECOUPLED or outside the manager, we have to chain
      if ((wrapper_function == NULL) ||
          (GST_RPAD_PEER(pad) &&
           (GST_ELEMENT (GST_PAD_PARENT (GST_PAD (GST_RPAD_PEER (pad))))->sched != chain->sched))
         ) {
        // set the chain proxies
        if (GST_RPAD_DIRECTION(pad) == GST_PAD_SINK) {
          GST_DEBUG (GST_CAT_SCHEDULING,"copying chain function into push proxy for %s:%s\n",GST_DEBUG_PAD_NAME(pad));
          GST_RPAD_PUSHFUNC(pad) = GST_RPAD_CHAINFUNC(pad);
        } else {
          GST_DEBUG (GST_CAT_SCHEDULING,"copying get function into pull proxy for %s:%s\n",GST_DEBUG_PAD_NAME(pad));
          GST_RPAD_PULLFUNC(pad) = GST_RPAD_GETFUNC(pad);
          GST_RPAD_PULLREGIONFUNC(pad) = GST_RPAD_GETREGIONFUNC(pad);
        }

      // otherwise we really are a cothread
      } else {
        if (gst_pad_get_direction (pad) == GST_PAD_SINK) {
          GST_DEBUG (GST_CAT_SCHEDULING,"setting cothreaded push proxy for sinkpad %s:%s\n",GST_DEBUG_PAD_NAME(pad));
          GST_RPAD_PUSHFUNC(pad) = GST_DEBUG_FUNCPTR(gst_schedule_pushfunc_proxy);
        } else {
          GST_DEBUG (GST_CAT_SCHEDULING,"setting cothreaded pull proxy for srcpad %s:%s\n",GST_DEBUG_PAD_NAME(pad));
          GST_RPAD_PULLFUNC(pad) = GST_DEBUG_FUNCPTR(gst_schedule_pullfunc_proxy);
          GST_RPAD_PULLREGIONFUNC(pad) = GST_DEBUG_FUNCPTR(gst_schedule_pullregionfunc_proxy);
        }
      }
    }

    // need to set up the cothread now
    if (wrapper_function != NULL) {
      if (element->threadstate == NULL) {
        element->threadstate = cothread_create (bin->threadcontext);
        GST_DEBUG (GST_CAT_SCHEDULING,"created cothread %p for '%s'\n",element->threadstate,GST_ELEMENT_NAME(element));
      }
      cothread_setfunc (element->threadstate, wrapper_function, 0, (char **)element);
      GST_DEBUG (GST_CAT_SCHEDULING,"set wrapper function for '%s' to &%s\n",GST_ELEMENT_NAME(element),
            GST_DEBUG_FUNCPTR_NAME(wrapper_function));
    }
  }
}

static void
gst_schedule_chained_chain (GstBin *bin, _GstBinChain *chain) {
  GList *elements;
  GstElement *element;
  GList *pads;
  GstPad *pad;

  GST_DEBUG (GST_CAT_SCHEDULING,"chain entered\n");
  // walk through all the elements
  elements = chain->elements;
  while (elements) {
    element = GST_ELEMENT (elements->data);
    elements = g_list_next (elements);

    // walk through all the pads
    pads = gst_element_get_pad_list (element);
    while (pads) {
      pad = GST_PAD (pads->data);
      pads = g_list_next (pads);
      if (!GST_IS_REAL_PAD(pad)) continue;

      if (GST_RPAD_DIRECTION(pad) == GST_PAD_SINK) {
        GST_DEBUG (GST_CAT_SCHEDULING,"copying chain function into push proxy for %s:%s\n",GST_DEBUG_PAD_NAME(pad));
        GST_RPAD_PUSHFUNC(pad) = GST_RPAD_CHAINFUNC(pad);
      } else {
        GST_DEBUG (GST_CAT_SCHEDULING,"copying get function into pull proxy for %s:%s\n",GST_DEBUG_PAD_NAME(pad));
        GST_RPAD_PULLFUNC(pad) = GST_RPAD_GETFUNC(pad);
        GST_RPAD_PULLREGIONFUNC(pad) = GST_RPAD_GETREGIONFUNC(pad);
      }
    }
  }
}

/* depracated!! */
static void
gst_bin_schedule_cleanup (GstBin *bin)
{
  GList *chains;
  _GstBinChain *chain;

  chains = bin->chains;
  while (chains) {
    chain = (_GstBinChain *)(chains->data);
    chains = g_list_next(chains);

//    g_list_free(chain->disabled);
    g_list_free(chain->elements);
    g_list_free(chain->entries);

    g_free(chain);
  }
  g_list_free(bin->chains);

  bin->chains = NULL;
}

static void
gst_scheduler_handle_eos (GstElement *element, _GstBinChain *chain)
{
  GST_DEBUG (GST_CAT_SCHEDULING,"chain removed from scheduler, EOS from element \"%s\"\n", GST_ELEMENT_NAME (element));
  chain->need_scheduling = FALSE;
}

/*
void gst_bin_schedule_func(GstBin *bin) {
  GList *elements;
  GstElement *element;
  GSList *pending = NULL;
  GList *pads;
  GstPad *pad;
  GstElement *peerparent;
  GList *chains;
  GstScheduleChain *chain;

  GST_DEBUG_ENTER("(\"%s\")",GST_ELEMENT_NAME (GST_ELEMENT (bin)));

  gst_bin_schedule_cleanup(bin);

  // next we have to find all the separate scheduling chains
  GST_DEBUG (GST_CAT_SCHEDULING,"attempting to find scheduling chains...\n");
  // first make a copy of the managed_elements we can mess with
  elements = g_list_copy (bin->managed_elements);
  // we have to repeat until the list is empty to get all chains
  while (elements) {
    element = GST_ELEMENT (elements->data);

    // if this is a DECOUPLED element
    if (GST_FLAG_IS_SET (element, GST_ELEMENT_DECOUPLED)) {
      // skip this element entirely
      GST_DEBUG (GST_CAT_SCHEDULING,"skipping '%s' because it's decoupled\n",GST_ELEMENT_NAME(element));
      elements = g_list_next (elements);
      continue;
    }

    GST_DEBUG (GST_CAT_SCHEDULING,"starting with element '%s'\n",GST_ELEMENT_NAME(element));

    // prime the pending list with the first element off the top
    pending = g_slist_prepend (NULL, element);
    // and remove that one from the main list
    elements = g_list_remove (elements, element);

    // create a chain structure
    chain = g_new0 (_GstBinChain, 1);
    chain->need_scheduling = TRUE;

    // for each pending element, walk the pipeline
    do {
      // retrieve the top of the stack and pop it
      element = GST_ELEMENT (pending->data);
      pending = g_slist_remove (pending, element);

      // add ourselves to the chain's list of elements
      GST_DEBUG (GST_CAT_SCHEDULING,"adding '%s' to chain\n",GST_ELEMENT_NAME(element));
      chain->elements = g_list_prepend (chain->elements, element);
      chain->num_elements++;
      gtk_signal_connect (GTK_OBJECT (element), "eos", gst_scheduler_handle_eos, chain);
      // set the cothreads flag as appropriate
      if (GST_FLAG_IS_SET (element, GST_ELEMENT_USE_COTHREAD))
        chain->need_cothreads = TRUE;
      if (bin->use_cothreads == TRUE)
        chain->need_cothreads = TRUE;

      // if we're managed by the current bin, and we're not decoupled,
      // go find all the peers and add them to the list of elements to check
      if ((element->manager == GST_ELEMENT(bin)) &&
          !GST_FLAG_IS_SET (element, GST_ELEMENT_DECOUPLED)) {
        // remove ourselves from the outer list of all managed elements
//        GST_DEBUG (GST_CAT_SCHEDULING,"removing '%s' from list of possible elements\n",GST_ELEMENT_NAME(element));
        elements = g_list_remove (elements, element);

        // if this element is a source, add it as an entry
        if (element->numsinkpads == 0) {
          chain->entries = g_list_prepend (chain->entries, element);
          GST_DEBUG (GST_CAT_SCHEDULING,"added '%s' as SRC entry into the chain\n",GST_ELEMENT_NAME(element));
        }

        // now we have to walk the pads to find peers
        pads = gst_element_get_pad_list (element);
        while (pads) {
          pad = GST_PAD (pads->data);
          pads = g_list_next (pads);
          if (!GST_IS_REAL_PAD(pad)) continue;
          GST_DEBUG (GST_CAT_SCHEDULING,"have pad %s:%s\n",GST_DEBUG_PAD_NAME(pad));

          if (GST_RPAD_PEER(pad) == NULL) continue;
	  if (GST_RPAD_PEER(pad) == NULL) GST_ERROR(pad,"peer is null!");
          g_assert(GST_RPAD_PEER(pad) != NULL);
          g_assert(GST_PAD_PARENT (GST_PAD(GST_RPAD_PEER(pad))) != NULL);

          peerparent = GST_ELEMENT(GST_PAD_PARENT (GST_PAD(GST_RPAD_PEER(pad))));

	  GST_DEBUG (GST_CAT_SCHEDULING,"peer pad %p\n", GST_RPAD_PEER(pad));
          // only bother with if the pad's peer's parent is this bin or it's DECOUPLED
          // only add it if it's in the list of un-visited elements still
          if ((g_list_find (elements, peerparent) != NULL) ||
              GST_FLAG_IS_SET (peerparent, GST_ELEMENT_DECOUPLED)) {
            // add the peer element to the pending list
            GST_DEBUG (GST_CAT_SCHEDULING,"adding '%s' to list of pending elements\n",
                       GST_ELEMENT_NAME(peerparent));
            pending = g_slist_prepend (pending, peerparent);

            // if this is a sink pad, then the element on the other side is an entry
            if ((GST_RPAD_DIRECTION(pad) == GST_PAD_SINK) &&
                (GST_FLAG_IS_SET (peerparent, GST_ELEMENT_DECOUPLED))) {
              chain->entries = g_list_prepend (chain->entries, peerparent);
              gtk_signal_connect (GTK_OBJECT (peerparent), "eos", gst_scheduler_handle_eos, chain);
              GST_DEBUG (GST_CAT_SCHEDULING,"added '%s' as DECOUPLED entry into the chain\n",GST_ELEMENT_NAME(peerparent));
            }
          } else
            GST_DEBUG (GST_CAT_SCHEDULING,"element '%s' has already been dealt with\n",GST_ELEMENT_NAME(peerparent));
        }
      }
    } while (pending);

    // add the chain to the bin
    GST_DEBUG (GST_CAT_SCHEDULING,"have chain with %d elements: ",chain->num_elements);
    { GList *elements = chain->elements;
      while (elements) {
        element = GST_ELEMENT (elements->data);
        elements = g_list_next(elements);
        GST_DEBUG_NOPREFIX(GST_CAT_SCHEDULING,"%s, ",GST_ELEMENT_NAME(element));
      }
    }
    GST_DEBUG_NOPREFIX(GST_CAT_DATAFLOW,"\n");
    bin->chains = g_list_prepend (bin->chains, chain);
    bin->num_chains++;
  }
  // free up the list in case it's full of DECOUPLED elements
  g_list_free (elements);

  GST_DEBUG (GST_CAT_SCHEDULING,"\nwe have %d chains to schedule\n",bin->num_chains);

  // now we have to go through all the chains and schedule them
  chains = bin->chains;
  while (chains) {
    chain = (GstScheduleChain *)(chains->data);
    chains = g_list_next (chains);

    // schedule as appropriate
    if (chain->need_cothreads) {
      gst_schedule_cothreaded_chain (bin,chain);
    } else {
      gst_schedule_chained_chain (bin,chain);
    }
  }

  GST_DEBUG_LEAVE("(\"%s\")",GST_ELEMENT_NAME(GST_ELEMENT(bin)));
}
*/


/*
        // ***** check for possible connections outside
        // get the pad's peer
        peer = gst_pad_get_peer (pad);
        // FIXME this should be an error condition, if not disabled
        if (!peer) break;
        // get the parent of the peer of the pad
        outside = GST_ELEMENT (gst_pad_get_parent (peer));
        // FIXME this should *really* be an error condition
        if (!outside) break;
        // if it's a source or connection and it's not ours...
        if ((GST_IS_SRC (outside) || GST_IS_CONNECTION (outside)) &&
            (gst_object_get_parent (GST_OBJECT (outside)) != GST_OBJECT (bin))) {
          if (gst_pad_get_direction (pad) == GST_PAD_SINK) {
            GST_DEBUG (0,"dealing with outside source element %s\n",GST_ELEMENT_NAME(outside));
//            GST_DEBUG (0,"PUNT: copying pullfunc ptr from %s:%s to %s:%s (@ %p)\n",
//GST_DEBUG_PAD_NAME(pad->peer),GST_DEBUG_PAD_NAME(pad),&pad->pullfunc);
//            pad->pullfunc = pad->peer->pullfunc;
//            GST_DEBUG (0,"PUNT: setting pushfunc proxy to fake proxy on %s:%s\n",GST_DEBUG_PAD_NAME(pad->peer));
//            pad->peer->pushfunc = GST_DEBUG_FUNCPTR(gst_bin_pushfunc_fake_proxy);
            GST_RPAD_PULLFUNC(pad) = GST_DEBUG_FUNCPTR(gst_bin_pullfunc_proxy);
            GST_RPAD_PULLREGIONFUNC(pad) = GST_DEBUG_FUNCPTR(gst_bin_pullregionfunc_proxy);
          }
        } else {
*/





/*
      } else if (GST_IS_SRC (element)) {
        GST_DEBUG (0,"adding '%s' as entry point, because it's a source\n",GST_ELEMENT_NAME (element));
        bin->entries = g_list_prepend (bin->entries,element);
        bin->num_entries++;
        cothread_setfunc(element->threadstate,gst_bin_src_wrapper,0,(char **)element);
      }

      pads = gst_element_get_pad_list (element);
      while (pads) {
        pad = GST_PAD(pads->data);

        if (gst_pad_get_direction (pad) == GST_PAD_SINK) {
          GST_DEBUG (0,"setting push proxy for sinkpad %s:%s\n",GST_DEBUG_PAD_NAME(pad));
          // set the proxy functions
          pad->pushfunc = GST_DEBUG_FUNCPTR(gst_bin_pushfunc_proxy);
          GST_DEBUG (0,"pushfunc %p = gst_bin_pushfunc_proxy %p\n",&pad->pushfunc,gst_bin_pushfunc_proxy);
        } else if (gst_pad_get_direction (pad) == GST_PAD_SRC) {
          GST_DEBUG (0,"setting pull proxies for srcpad %s:%s\n",GST_DEBUG_PAD_NAME(pad));
          // set the proxy functions
          GST_RPAD_PULLFUNC(pad) = GST_DEBUG_FUNCPTR(gst_bin_pullfunc_proxy);
          GST_RPAD_PULLREGIONFUNC(pad) = GST_DEBUG_FUNCPTR(gst_bin_pullregionfunc_proxy);
          GST_DEBUG (0,"pad->pullfunc(@%p) = gst_bin_pullfunc_proxy(@%p)\n",
                &pad->pullfunc,gst_bin_pullfunc_proxy);
          pad->pullregionfunc = GST_DEBUG_FUNCPTR(gst_bin_pullregionfunc_proxy);
        }
        pads = g_list_next (pads);
      }
      elements = g_list_next (elements);

      // if there are no entries, we have to pick one at random
      if (bin->num_entries == 0)
        bin->entries = g_list_prepend (bin->entries, GST_ELEMENT(bin->children->data));
    }
  } else {
    GST_DEBUG (0,"don't need cothreads, looking for entry points\n");
    // we have to find which elements will drive an iteration
    elements = bin->children;
    while (elements) {
      element = GST_ELEMENT (elements->data);
      GST_DEBUG (0,"found element \"%s\"\n", GST_ELEMENT_NAME (element));
      if (GST_IS_BIN (element)) {
        gst_bin_create_plan (GST_BIN (element));
      }
      if (GST_IS_SRC (element)) {
        GST_DEBUG (0,"adding '%s' as entry point, because it's a source\n",GST_ELEMENT_NAME (element));
        bin->entries = g_list_prepend (bin->entries, element);
        bin->num_entries++;
      }

      // go through all the pads, set pointers, and check for connections
      pads = gst_element_get_pad_list (element);
      while (pads) {
        pad = GST_PAD (pads->data);

        if (gst_pad_get_direction (pad) == GST_PAD_SINK) {
	  GST_DEBUG (0,"found SINK pad %s:%s\n", GST_DEBUG_PAD_NAME(pad));

          // copy the peer's chain function, easy enough
          GST_DEBUG (0,"copying peer's chainfunc to %s:%s's pushfunc\n",GST_DEBUG_PAD_NAME(pad));
          GST_RPAD_PUSHFUNC(pad) = GST_DEBUG_FUNCPTR(GST_RPAD_CHAINFUNC(GST_RPAD_PEER(pad)));

          // need to walk through and check for outside connections
//FIXME need to do this for all pads
          // get the pad's peer
          peer = GST_RPAD_PEER(pad);
          if (!peer) {
	    GST_DEBUG (0,"found SINK pad %s has no peer\n", GST_ELEMENT_NAME (pad));
	    break;
	  }
          // get the parent of the peer of the pad
          outside = GST_ELEMENT (GST_RPAD_PARENT(peer));
          if (!outside) break;
          // if it's a connection and it's not ours...
          if (GST_IS_CONNECTION (outside) &&
               (gst_object_get_parent (GST_OBJECT (outside)) != GST_OBJECT (bin))) {
            gst_info("gstbin: element \"%s\" is the external source Connection "
				    "for internal element \"%s\"\n",
	                  GST_ELEMENT_NAME (GST_ELEMENT (outside)),
	                  GST_ELEMENT_NAME (GST_ELEMENT (element)));
	    bin->entries = g_list_prepend (bin->entries, outside);
	    bin->num_entries++;
	  }
	}
	else {
	  GST_DEBUG (0,"found pad %s\n", GST_ELEMENT_NAME (pad));
	}
	pads = g_list_next (pads);

      }
      elements = g_list_next (elements);
    }
*/




/*
  // If cothreads are needed, we need to not only find elements but
  // set up cothread states and various proxy functions.
  if (bin->need_cothreads) {
    GST_DEBUG (0,"bin is using cothreads\n");

    // first create thread context
    if (bin->threadcontext == NULL) {
      GST_DEBUG (0,"initializing cothread context\n");
      bin->threadcontext = cothread_init ();
    }

    // walk through all the children
    elements = bin->managed_elements;
    while (elements) {
      element = GST_ELEMENT (elements->data);
      elements = g_list_next (elements);

      // start out with a NULL warpper function, we'll set it if we want a cothread
      wrapper_function = NULL;

      // have to decide if we need to or can use a cothreads, and if so which wrapper
      // first of all, if there's a loopfunc, the decision's already made
      if (element->loopfunc != NULL) {
        wrapper_function = GST_DEBUG_FUNCPTR(gst_bin_loopfunc_wrapper);
        GST_DEBUG (0,"element %s is a loopfunc, must use a cothread\n",GST_ELEMENT_NAME (element));
      } else {
        // otherwise we need to decide if it needs a cothread
        // if it's complex, or cothreads are preferred and it's *not* decoupled, cothread it
        if (GST_FLAG_IS_SET (element,GST_ELEMENT_COMPLEX) ||
            (GST_FLAG_IS_SET (bin,GST_BIN_FLAG_PREFER_COTHREADS) &&
             !GST_FLAG_IS_SET (element,GST_ELEMENT_DECOUPLED))) {
          // base it on whether we're going to loop through source or sink pads
          if (element->numsinkpads == 0)
            wrapper_function = GST_DEBUG_FUNCPTR(gst_bin_src_wrapper);
          else
            wrapper_function = GST_DEBUG_FUNCPTR(gst_bin_chain_wrapper);
        }
      }

      // walk through the all the pads for this element, setting proxy functions
      // the selection of proxy functions depends on whether we're in a cothread or not
      pads = gst_element_get_pad_list (element);
      while (pads) {
        pad = GST_PAD (pads->data);
        pads = g_list_next (pads);

        // check to see if someone else gets to set up the element
        peer_manager = GST_ELEMENT((pad)->peer->parent)->manager;
        if (peer_manager != GST_ELEMENT(bin)) {
          GST_DEBUG (0,"WARNING: pad %s:%s is connected outside of bin\n",GST_DEBUG_PAD_NAME(pad));
	}

        // if the wrapper_function is set, we need to use the proxy functions
        if (wrapper_function != NULL) {
          // set up proxy functions
          if (gst_pad_get_direction (pad) == GST_PAD_SINK) {
            GST_DEBUG (0,"setting push proxy for sinkpad %s:%s\n",GST_DEBUG_PAD_NAME(pad));
            pad->pushfunc = GST_DEBUG_FUNCPTR(gst_bin_pushfunc_proxy);
          } else if (gst_pad_get_direction (pad) == GST_PAD_SRC) {
            GST_DEBUG (0,"setting pull proxy for srcpad %s:%s\n",GST_DEBUG_PAD_NAME(pad));
            GST_RPAD_PULLFUNC(pad) = GST_DEBUG_FUNCPTR(gst_bin_pullfunc_proxy);
            GST_RPAD_PULLREGIONFUNC(pad) = GST_DEBUG_FUNCPTR(gst_bin_pullregionfunc_proxy);
          }
        } else {
          // otherwise we need to set up for 'traditional' chaining
          if (gst_pad_get_direction (pad) == GST_PAD_SINK) {
            // we can just copy the chain function, since it shares the prototype
            GST_DEBUG (0,"copying chain function into push proxy for %s:%s\n",
                  GST_DEBUG_PAD_NAME(pad));
            pad->pushfunc = pad->chainfunc;
          } else if (gst_pad_get_direction (pad) == GST_PAD_SRC) {
            // we can just copy the get function, since it shares the prototype
            GST_DEBUG (0,"copying get function into pull proxy for %s:%s\n",
                  GST_DEBUG_PAD_NAME(pad));
            pad->pullfunc = pad->getfunc;
          }
        }
      }

      // if a loopfunc has been specified, create and set up a cothread
      if (wrapper_function != NULL) {
        if (element->threadstate == NULL) {
          element->threadstate = cothread_create (bin->threadcontext);
          GST_DEBUG (0,"created cothread %p (@%p) for \"%s\"\n",element->threadstate,
                &element->threadstate,GST_ELEMENT_NAME (element));
        }
        cothread_setfunc (element->threadstate, wrapper_function, 0, (char **)element);
        GST_DEBUG (0,"set wrapper function for \"%s\" to &%s\n",GST_ELEMENT_NAME (element),
              GST_DEBUG_FUNCPTR_NAME(wrapper_function));
      }

//      // HACK: if the element isn't decoupled, it's an entry
//      if (!GST_FLAG_IS_SET(element,GST_ELEMENT_DECOUPLED))
//        bin->entries = g_list_append(bin->entries, element);
    }

  // otherwise, cothreads are not needed
  } else {
    GST_DEBUG (0,"bin is chained, no cothreads needed\n");

    elements = bin->managed_elements;
    while (elements) {
      element = GST_ELEMENT (elements->data);
      elements = g_list_next (elements);

      pads = gst_element_get_pad_list (element);
      while (pads) {
        pad = GST_PAD (pads->data);
        pads = g_list_next (pads);

        if (gst_pad_get_direction (pad) == GST_PAD_SINK) {
          GST_DEBUG (0,"copying chain function into push proxy for %s:%s\n",GST_DEBUG_PAD_NAME(pad));
          pad->pushfunc = pad->chainfunc;
        } else {
          GST_DEBUG (0,"copying get function into pull proxy for %s:%s\n",GST_DEBUG_PAD_NAME(pad));
          pad->pullfunc = pad->getfunc;
        }
      }
    }
  }
*/

static void 
gst_schedule_lock_element (GstSchedule *sched,GstElement *element)
{
  cothread_lock(element->threadstate);
}

static void
gst_schedule_unlock_element (GstSchedule *sched,GstElement *element)
{
  cothread_unlock(element->threadstate);
}


/*************** INCREMENTAL SCHEDULING CODE STARTS HERE ***************/


static void	gst_schedule_class_init	(GstScheduleClass *klass);
static void	gst_schedule_init	(GstSchedule *schedule);

static GstObjectClass *parent_class = NULL;

GtkType gst_schedule_get_type(void) {
  static GtkType schedule_type = 0;

  if (!schedule_type) {
    static const GtkTypeInfo schedule_info = {
      "GstSchedule",
      sizeof(GstSchedule),
      sizeof(GstScheduleClass),
      (GtkClassInitFunc)gst_schedule_class_init,
      (GtkObjectInitFunc)gst_schedule_init,
      (GtkArgSetFunc)NULL,
      (GtkArgGetFunc)NULL,
      (GtkClassInitFunc)NULL,
    };
    schedule_type = gtk_type_unique(GST_TYPE_OBJECT,&schedule_info);
  }
  return schedule_type;
}

static void
gst_schedule_class_init (GstScheduleClass *klass)
{
  parent_class = gtk_type_class(GST_TYPE_OBJECT);
}

static void
gst_schedule_init (GstSchedule *schedule)
{
  schedule->add_element = GST_DEBUG_FUNCPTR(gst_schedule_add_element);
  schedule->remove_element = GST_DEBUG_FUNCPTR(gst_schedule_remove_element);
  schedule->enable_element = GST_DEBUG_FUNCPTR(gst_schedule_enable_element);
  schedule->disable_element = GST_DEBUG_FUNCPTR(gst_schedule_disable_element);
  schedule->lock_element = GST_DEBUG_FUNCPTR(gst_schedule_lock_element);
  schedule->unlock_element = GST_DEBUG_FUNCPTR(gst_schedule_unlock_element);
  schedule->pad_connect = GST_DEBUG_FUNCPTR(gst_schedule_pad_connect);
  schedule->pad_disconnect = GST_DEBUG_FUNCPTR(gst_schedule_pad_disconnect);
  schedule->iterate = GST_DEBUG_FUNCPTR(gst_schedule_iterate);
}

GstSchedule*
gst_schedule_new(GstElement *parent)
{
  GstSchedule *sched = GST_SCHEDULE (gtk_type_new (GST_TYPE_SCHEDULE));

  sched->parent = parent;

  return sched;
}


/* this function will look at a pad and determine if the peer parent is
 * a possible candidate for connecting up in the same chain. */
/* DEPRACATED !!!!
GstElement *gst_schedule_check_pad (GstSchedule *sched, GstPad *pad) {
  GstRealPad *peer;
  GstElement *element, *peerelement;

  GST_INFO (GST_CAT_SCHEDULING, "checking pad %s:%s for peer in scheduler",
            GST_DEBUG_PAD_NAME(pad));

  element = GST_ELEMENT(GST_PAD_PARENT(peer));
  GST_DEBUG(GST_CAT_SCHEDULING, "element is \"%s\"\n",GST_ELEMENT_NAME(element));

  peer = GST_PAD_PEER (pad);
  if (peer == NULL) return NULL;
  peerelement = GST_ELEMENT(GST_PAD_PARENT (peer));
  if (peerelement == NULL) return NULL;
  GST_DEBUG(GST_CAT_SCHEDULING, "peer element is \"%s\"\n",GST_ELEMENT_NAME(peerelement));

  // now check to see if it's in the same schedule
  if (GST_ELEMENT_SCHED(element) == GST_ELEMENT_SCHED(peerelement)) {
    GST_DEBUG(GST_CAT_SCHEDULING, "peer is in same schedule\n");
    return peerelement;
  }

  // otherwise it's not a candidate
  return NULL;
}
*/

GstScheduleChain *
gst_schedule_chain_new (GstSchedule *sched)
{
  GstScheduleChain *chain = g_new (GstScheduleChain, 1);

  // initialize the chain with sane values
  chain->sched = sched;
  chain->disabled = NULL;
  chain->elements = NULL;
  chain->num_elements = 0;
  chain->entry = NULL;
  chain->cothreaded_elements = 0;
  chain->schedule = FALSE;

  // add the chain to the schedules' list of chains
  sched->chains = g_list_prepend (sched->chains, chain);
  sched->num_chains++;

  GST_INFO (GST_CAT_SCHEDULING, "created new chain %p, now are %d chains in sched %p",
            chain,sched->num_chains,sched);

  return chain;
}

void
gst_schedule_chain_destroy (GstScheduleChain *chain)
{
  GstSchedule *sched = chain->sched;

  // remove the chain from the schedules' list of chains
  chain->sched->chains = g_list_remove (chain->sched->chains, chain);
  chain->sched->num_chains--;

  // destroy the chain
  g_list_free (chain->disabled);	// should be empty...
  g_list_free (chain->elements);	// ditto
  g_free (chain);

  GST_INFO (GST_CAT_SCHEDULING, "destroyed chain %p, now are %d chains in sched %p",chain,sched->num_chains,sched);
}

void
gst_schedule_chain_add_element (GstScheduleChain *chain, GstElement *element)
{
  GST_INFO (GST_CAT_SCHEDULING, "adding element \"%s\" to chain %p", GST_ELEMENT_NAME (element),chain);

  // set the sched pointer for the element
  element->sched = chain->sched;

  // add the element to the list of 'disabled' elements
  chain->disabled = g_list_prepend (chain->disabled, element);
  chain->num_elements++;
}

void
gst_schedule_chain_enable_element (GstScheduleChain *chain, GstElement *element)
{
  GST_INFO (GST_CAT_SCHEDULING, "enabling element \"%s\" in chain %p", GST_ELEMENT_NAME (element),chain);

  // remove from disabled list
  chain->disabled = g_list_remove (chain->disabled, element);

  // add to elements list
  chain->elements = g_list_prepend (chain->elements, element);

  // reschedule the chain
  gst_schedule_cothreaded_chain(GST_BIN(chain->sched->parent),chain);
}

void
gst_schedule_chain_disable_element (GstScheduleChain *chain, GstElement *element)
{
  GST_INFO (GST_CAT_SCHEDULING, "disabling element \"%s\" in chain %p", GST_ELEMENT_NAME (element),chain);

  // remove from elements list
  chain->elements = g_list_remove (chain->elements, element);

  // add to disabled list
  chain->disabled = g_list_prepend (chain->disabled, element);

  // reschedule the chain
// FIXME this should be done only if manager state != NULL
//  gst_schedule_cothreaded_chain(GST_BIN(chain->sched->parent),chain);
}

void
gst_schedule_chain_remove_element (GstScheduleChain *chain, GstElement *element)
{
  GST_INFO (GST_CAT_SCHEDULING, "removing element \"%s\" from chain %p", GST_ELEMENT_NAME (element),chain);

  // if it's active, deactivate it
  if (g_list_find (chain->elements, element)) {
    gst_schedule_chain_disable_element (chain, element);
  }

  // remove the element from the list of elements
  chain->disabled = g_list_remove (chain->disabled, element);
  chain->num_elements--;

  // if there are no more elements in the chain, destroy the chain
  if (chain->num_elements == 0)
    gst_schedule_chain_destroy(chain);

  // unset the sched pointer for the element
  element->sched = NULL;
}

void
gst_schedule_chain_elements (GstSchedule *sched, GstElement *element1, GstElement *element2)
{
  GList *chains;
  GstScheduleChain *chain;
  GstScheduleChain *chain1 = NULL, *chain2 = NULL;
  GstElement *element;

  // first find the chains that hold the two 
  chains = sched->chains;
  while (chains) {
    chain = (GstScheduleChain *)(chains->data);
    chains = g_list_next(chains);

    if (g_list_find (chain->disabled,element1))
      chain1 = chain;
    else if (g_list_find (chain->elements,element1))
      chain1 = chain;

    if (g_list_find (chain->disabled,element2))
      chain2 = chain;
    else if (g_list_find (chain->elements,element2))
      chain2 = chain;
  }

  // first check to see if they're in the same chain, we're done if that's the case
  if ((chain1 != NULL) && (chain1 == chain2)) {
    GST_INFO (GST_CAT_SCHEDULING, "elements are already in the same chain");
    return;
  }

  // now, if neither element has a chain, create one
  if ((chain1 == NULL) && (chain2 == NULL)) {
    GST_INFO (GST_CAT_SCHEDULING, "creating new chain to hold two new elements");
    chain = gst_schedule_chain_new (sched);
    gst_schedule_chain_add_element (chain, element1);
    gst_schedule_chain_add_element (chain, element2);
    // FIXME chain changed here
//    gst_schedule_cothreaded_chain(chain->sched->parent,chain);

  // otherwise if both have chains already, join them
  } else if ((chain1 != NULL) && (chain2 != NULL)) {
    GST_INFO (GST_CAT_SCHEDULING, "merging chain %p into chain %p",chain2,chain1);
    // take the contents of chain2 and merge them into chain1
    chain1->disabled = g_list_concat (chain1->disabled, g_list_copy(chain2->disabled));
    chain1->elements = g_list_concat (chain1->elements, g_list_copy(chain2->elements));
    chain1->num_elements += chain2->num_elements;
    // FIXME chain changed here
//    gst_schedule_cothreaded_chain(chain->sched->parent,chain);

    gst_schedule_chain_destroy(chain2);

  // otherwise one has a chain already, the other doesn't
  } else {
    // pick out which one has the chain, and which doesn't
    if (chain1 != NULL) chain = chain1, element = element2;
    else chain = chain2, element = element1;

    GST_INFO (GST_CAT_SCHEDULING, "adding element to existing chain");
    gst_schedule_chain_add_element (chain, element);
    // FIXME chain changed here
//    gst_schedule_cothreaded_chain(chain->sched->parent,chain);
  }
}

void
gst_schedule_pad_connect (GstSchedule *sched, GstPad *srcpad, GstPad *sinkpad)
{
  GstElement *srcelement,*sinkelement;

  srcelement = GST_PAD_PARENT(srcpad);
  g_return_if_fail(srcelement != NULL);
  sinkelement = GST_PAD_PARENT(sinkpad);
  g_return_if_fail(sinkelement != NULL);

  GST_INFO (GST_CAT_SCHEDULING, "have pad connected callback on %s:%s to %s:%s",GST_DEBUG_PAD_NAME(srcpad),GST_DEBUG_PAD_NAME(sinkpad));
  GST_DEBUG(GST_CAT_SCHEDULING, "srcpad sched is %p, sinkpad sched is %p\n",
GST_ELEMENT_SCHED(srcelement),GST_ELEMENT_SCHED(sinkelement));

  if (GST_ELEMENT_SCHED(srcelement) == GST_ELEMENT_SCHED(sinkelement)) {
    GST_INFO (GST_CAT_SCHEDULING, "peer %s:%s is in same schedule, chaining together",GST_DEBUG_PAD_NAME(sinkpad));
    gst_schedule_chain_elements (sched, srcelement, sinkelement);
  }
}

// find the chain within the schedule that holds the element, if any
GstScheduleChain *
gst_schedule_find_chain (GstSchedule *sched, GstElement *element)
{
  GList *chains;
  GstScheduleChain *chain;

  GST_INFO (GST_CAT_SCHEDULING, "searching for element \"%s\" in chains",GST_ELEMENT_NAME(element));

  chains = sched->chains;
  while (chains) {
    chain = (GstScheduleChain *)(chains->data);
    chains = g_list_next (chains);

    if (g_list_find (chain->elements, element))
      return chain;
    if (g_list_find (chain->disabled, element))
      return chain;
  }

  return NULL;
}

void
gst_schedule_chain_recursive_add (GstScheduleChain *chain, GstElement *element)
{
  GList *pads;
  GstPad *pad;
  GstElement *peerelement;

  // add the element to the chain
  gst_schedule_chain_add_element (chain, element);

  GST_DEBUG(GST_CAT_SCHEDULING, "recursing on element \"%s\"\n",GST_ELEMENT_NAME(element));
  // now go through all the pads and see which peers can be added
  pads = element->pads;
  while (pads) {
    pad = GST_PAD(pads->data);
    pads = g_list_next (pads);

    GST_DEBUG(GST_CAT_SCHEDULING, "have pad %s:%s, checking for valid peer\n",GST_DEBUG_PAD_NAME(pad));
    // if the peer exists and could be in the same chain
    if (GST_PAD_PEER(pad)) {
      GST_DEBUG(GST_CAT_SCHEDULING, "has peer %s:%s\n",GST_DEBUG_PAD_NAME(GST_PAD_PEER(pad)));
      peerelement = GST_PAD_PARENT(GST_PAD_PEER(pad));
      if (GST_ELEMENT_SCHED(GST_PAD_PARENT(pad)) == GST_ELEMENT_SCHED(peerelement)) {
        GST_DEBUG(GST_CAT_SCHEDULING, "peer \"%s\" is valid for same chain\n",GST_ELEMENT_NAME(peerelement));
        // if it's not already in a chain, add it to this one
        if (gst_schedule_find_chain (chain->sched, peerelement) == NULL) {
          gst_schedule_chain_recursive_add (chain, peerelement);
        }
      }
    }
  }
}

void
gst_schedule_pad_disconnect (GstSchedule *sched, GstPad *srcpad, GstPad *sinkpad)
{
  GstScheduleChain *chain;
  GstElement *element1, *element2;
  GstScheduleChain *chain1, *chain2;

  GST_INFO (GST_CAT_SCHEDULING, "have pad disconnected callback on %s:%s",GST_DEBUG_PAD_NAME(srcpad));

  // we need to have the parent elements of each pad
  element1 = GST_ELEMENT(GST_PAD_PARENT(srcpad));
  element2 = GST_ELEMENT(GST_PAD_PARENT(sinkpad));
  GST_INFO (GST_CAT_SCHEDULING, "disconnecting elements \"%s\" and \"%s\"",
            GST_ELEMENT_NAME(element1), GST_ELEMENT_NAME(element2));

  // first task is to remove the old chain they belonged to.
  // this can be accomplished by taking either of the elements,
  // since they are guaranteed to be in the same chain
  // FIXME is it potentially better to make an attempt at splitting cleaner??
  chain = gst_schedule_find_chain (sched, element1);
  if (chain) {
    GST_INFO (GST_CAT_SCHEDULING, "destroying chain");
    gst_schedule_chain_destroy (chain);
  }

  // now create a new chain to hold element1 and build it from scratch
  chain1 = gst_schedule_chain_new (sched);
  gst_schedule_chain_recursive_add (chain1, element1);

  // check the other element to see if it landed in the newly created chain
  if (gst_schedule_find_chain (sched, element2) == NULL) {
    // if not in chain, create chain and build from scratch
    chain2 = gst_schedule_chain_new (sched);
    gst_schedule_chain_recursive_add (chain2, element2);
  }
}


void
gst_schedule_add_element (GstSchedule *sched, GstElement *element)
{
  GList *pads;
  GstPad *pad;
  GstElement *peerelement;
  GstScheduleChain *chain;

  g_return_if_fail (element != NULL);
  g_return_if_fail (GST_IS_ELEMENT(element));

  // if it's already in this schedule, don't bother doing anything
  if (GST_ELEMENT_SCHED(element) == sched) return;

  GST_INFO (GST_CAT_SCHEDULING, "adding element \"%s\" to schedule",
    GST_ELEMENT_NAME(element));

  // if the element already has a different scheduler, remove the element from it
  if (GST_ELEMENT_SCHED(element)) {
    gst_schedule_remove_element(GST_ELEMENT_SCHED(element),element);
  }

  // set the sched pointer in the element itself
  GST_ELEMENT_SCHED(element) = sched;

  // only deal with elements after this point, not bins
  if (GST_IS_BIN (element)) return;

  // first add it to the list of elements that are to be scheduled
  sched->elements = g_list_prepend (sched->elements, element);
  sched->num_elements++;

  // create a chain to hold it, and add
  chain = gst_schedule_chain_new (sched);
  gst_schedule_chain_add_element (chain, element);

  // set the sched pointer in all the pads
  pads = element->pads;
  while (pads) {
    pad = GST_PAD(pads->data);
    pads = g_list_next(pads);

    // we only operate on real pads
    if (!GST_IS_REAL_PAD(pad)) continue;

    // set the pad's sched pointer
    gst_pad_set_sched (pad, sched);

    // if the peer element exists and is a candidate
    if (GST_PAD_PEER(pad)) {
      peerelement = GST_PAD_PARENT( GST_PAD_PEER (pad) );
      if (GST_ELEMENT_SCHED(element) == GST_ELEMENT_SCHED(peerelement)) {
        GST_INFO (GST_CAT_SCHEDULING, "peer is in same schedule, chaining together");
        // make sure that the two elements are in the same chain
        gst_schedule_chain_elements (sched,element,peerelement);
      }
    }
  }
}

void
gst_schedule_enable_element (GstSchedule *sched, GstElement *element)
{
  GstScheduleChain *chain;

  // find the chain the element's in
  chain = gst_schedule_find_chain (sched, element);

  if (chain)
    gst_schedule_chain_enable_element (chain, element);
  else
    GST_INFO (GST_CAT_SCHEDULING, "element not found in any chain, not enabling");
}

void
gst_schedule_disable_element (GstSchedule *sched, GstElement *element)
{
  GstScheduleChain *chain;

  // find the chain the element is in
  chain = gst_schedule_find_chain (sched, element);

  // remove it from the chain
  if (chain) {
    gst_schedule_chain_disable_element(chain,element);
  }
}

void
gst_schedule_remove_element (GstSchedule *sched, GstElement *element)
{
  GstScheduleChain *chain;

  g_return_if_fail (element != NULL);
  g_return_if_fail (GST_IS_ELEMENT(element));

  if (g_list_find (sched->elements, element)) {
    GST_INFO (GST_CAT_SCHEDULING, "removing element \"%s\" from schedule",
      GST_ELEMENT_NAME(element));

    // find what chain the element is in
    chain = gst_schedule_find_chain(sched, element);

    // remove it from its chain
    gst_schedule_chain_remove_element (chain, element);

    // remove it from the list of elements
    sched->elements = g_list_remove (sched->elements, element);
    sched->num_elements--;

    // unset the scheduler pointer in the element
    GST_ELEMENT_SCHED(element) = NULL;
  }
}

gboolean
gst_schedule_iterate (GstSchedule *sched)
{
  GstBin *bin = GST_BIN(sched->parent);
  GList *chains;
  GstScheduleChain *chain;
  GstElement *entry;
  gint num_scheduled = 0;
  gboolean eos = FALSE;
  GList *elements;

  GST_DEBUG_ENTER("(\"%s\")", GST_ELEMENT_NAME (bin));

  g_return_val_if_fail (bin != NULL, TRUE);
  g_return_val_if_fail (GST_IS_BIN (bin), TRUE);
//  g_return_val_if_fail (GST_STATE (bin) == GST_STATE_PLAYING, TRUE);

  // step through all the chains
  chains = sched->chains;
//  if (chains == NULL) return FALSE;
g_return_val_if_fail (chains != NULL, FALSE);
  while (chains) {
    chain = (GstScheduleChain *)(chains->data);
    chains = g_list_next (chains);

//    if (!chain->need_scheduling) continue;

//    if (chain->need_cothreads) {
      // all we really have to do is switch to the first child
      // FIXME this should be lots more intelligent about where to start
      GST_DEBUG (GST_CAT_DATAFLOW,"starting iteration via cothreads\n");

      if (chain->elements) {
        entry = NULL; //MattH ADDED?
GST_DEBUG(GST_CAT_SCHEDULING,"there are %d elements in this chain\n",chain->num_elements);
        elements = chain->elements;
        while (elements) {
          entry = GST_ELEMENT(elements->data);
          elements = g_list_next(elements);
          if (GST_FLAG_IS_SET(entry,GST_ELEMENT_DECOUPLED))
            GST_DEBUG(GST_CAT_SCHEDULING,"entry \"%s\" is DECOUPLED, skipping\n",GST_ELEMENT_NAME(entry));
          else if (GST_FLAG_IS_SET(entry,GST_ELEMENT_NO_ENTRY))
            GST_DEBUG(GST_CAT_SCHEDULING,"entry \"%s\" is not valid, skipping\n",GST_ELEMENT_NAME(entry));
          else
            break;
        }
        if (entry) {
          GST_FLAG_SET (entry, GST_ELEMENT_COTHREAD_STOPPING);
          GST_DEBUG (GST_CAT_DATAFLOW,"set COTHREAD_STOPPING flag on \"%s\"(@%p)\n",
               GST_ELEMENT_NAME (entry),entry);
          cothread_switch (entry->threadstate);

          // following is a check to see if the chain was interrupted due to a
          // top-half state_change().  (i.e., if there's a pending state.)
          //
          // if it was, return to gstthread.c::gst_thread_main_loop() to
          // execute the state change.
          GST_DEBUG (GST_CAT_DATAFLOW,"cothread switch ended or interrupted\n");
          if (GST_STATE_PENDING(GST_SCHEDULE(sched)->parent) != GST_STATE_NONE_PENDING)
          {
            GST_DEBUG (GST_CAT_DATAFLOW,"handle pending state %d\n",
                       GST_STATE_PENDING(GST_SCHEDULE(sched)->parent));
            return 0;
          }

        } else {
          GST_INFO (GST_CAT_DATAFLOW,"no entry into chain!");
        }
      } else {
        GST_INFO (GST_CAT_DATAFLOW,"no entry into chain!");
      }

/*                
    } else {
      GST_DEBUG (GST_CAT_DATAFLOW,"starting iteration via chain-functions\n");

      entries = chain->entries;
         
      g_assert (entries != NULL);
     
      while (entries) {
        entry = GST_ELEMENT (entries->data);
        entries = g_list_next (entries);
 
        GST_DEBUG (GST_CAT_DATAFLOW,"have entry \"%s\"\n",GST_ELEMENT_NAME (entry));
  
        if (GST_IS_BIN (entry)) {
          gst_bin_iterate (GST_BIN (entry));
        } else {
          pads = entry->pads;
          while (pads) {
            pad = GST_PAD (pads->data);
            if (GST_RPAD_DIRECTION(pad) == GST_PAD_SRC) {
              GST_DEBUG (GST_CAT_DATAFLOW,"calling getfunc of %s:%s\n",GST_DEBUG_PAD_NAME(pad));
              if (GST_REAL_PAD(pad)->getfunc == NULL) 
                fprintf(stderr, "error, no getfunc in \"%s\"\n", GST_ELEMENT_NAME  (entry));
              else
                buf = (GST_REAL_PAD(pad)->getfunc)(pad);
              if (buf) gst_pad_push(pad,buf);
            }
            pads = g_list_next (pads);
          }
        }
      }
    }*/
    num_scheduled++;
  }

/*
  // check if nothing was scheduled that was ours..
  if (!num_scheduled) {
    // are there any other elements that are still busy?
    if (bin->num_eos_providers) {
      GST_LOCK (bin);
      GST_DEBUG (GST_CATA_DATAFLOW,"waiting for eos providers\n");
      g_cond_wait (bin->eoscond, GST_OBJECT(bin)->lock);  
      GST_DEBUG (GST_CAT_DATAFLOW,"num eos providers %d\n", bin->num_eos_providers);
      GST_UNLOCK (bin);
    }
    else {      
      gst_element_signal_eos (GST_ELEMENT (bin));
      eos = TRUE;
    }       
  }
*/

  GST_DEBUG (GST_CAT_DATAFLOW, "leaving (%s)\n", GST_ELEMENT_NAME (bin));
  return !eos;
}



void
gst_schedule_show (GstSchedule *sched)
{
  GList *chains, *elements;
  GstElement *element;
  GstScheduleChain *chain;

  if (sched == NULL) {
    g_print("schedule doesn't exist for this element\n");
    return;
  }

  g_return_if_fail(GST_IS_SCHEDULE(sched));

  g_print("SCHEDULE DUMP FOR MANAGING BIN \"%s\"\n",GST_ELEMENT_NAME(sched->parent));

  g_print("schedule has %d elements in it: ",sched->num_elements);
  elements = sched->elements;
  while (elements) {
    element = GST_ELEMENT(elements->data);
    elements = g_list_next(elements);

    g_print("%s, ",GST_ELEMENT_NAME(element));
  }
  g_print("\n");

  g_print("schedule has %d chains in it\n",sched->num_chains);
  chains = sched->chains;
  while (chains) {
    chain = (GstScheduleChain *)(chains->data);
    chains = g_list_next(chains);

    g_print("%p: ",chain);

    elements = chain->disabled;
    while (elements) {
      element = GST_ELEMENT(elements->data);
      elements = g_list_next(elements);

      g_print("!%s, ",GST_ELEMENT_NAME(element));
    }

    elements = chain->elements;
    while (elements) {
      element = GST_ELEMENT(elements->data);
      elements = g_list_next(elements);

      g_print("%s, ",GST_ELEMENT_NAME(element));
    }
    g_print("\n");
  }
}
