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
gst_bin_loopfunc_wrapper (int argc,char *argv[])
{
  GstElement *element = GST_ELEMENT (argv);
  G_GNUC_UNUSED const gchar *name = GST_ELEMENT_NAME (element);

  GST_DEBUG_ENTER("(%d,'%s')",argc,name);

  do {
    GST_DEBUG (0,"calling loopfunc %s for element %s\n",
          GST_DEBUG_FUNCPTR_NAME (element->loopfunc),name);
    (element->loopfunc) (element);
    GST_DEBUG (0,"element %s ended loop function\n", name);
  } while (!GST_ELEMENT_IS_COTHREAD_STOPPING(element));
  GST_FLAG_UNSET(element,GST_ELEMENT_COTHREAD_STOPPING);

  GST_DEBUG_LEAVE("(%d,'%s')",argc,name);
  return 0;
}

static int
gst_bin_chain_wrapper (int argc,char *argv[])
{
  GstElement *element = GST_ELEMENT (argv);
  G_GNUC_UNUSED const gchar *name = GST_ELEMENT_NAME (element);
  GList *pads;
  GstPad *pad;
  GstRealPad *realpad;
  GstBuffer *buf;

  GST_DEBUG_ENTER("(\"%s\")",name);
  GST_DEBUG (0,"stepping through pads\n");
  do {
    pads = element->pads;
    while (pads) {
      pad = GST_PAD (pads->data);
      pads = g_list_next (pads);
      if (!GST_IS_REAL_PAD(pad)) continue;
      realpad = GST_REAL_PAD(pad);
      if (GST_RPAD_DIRECTION(realpad) == GST_PAD_SINK) {
        GST_DEBUG (0,"pulling a buffer from %s:%s\n", name, GST_PAD_NAME (pad));
        buf = gst_pad_pull (pad);
        GST_DEBUG (0,"calling chain function of %s:%s\n", name, GST_PAD_NAME (pad));
        if (buf) GST_RPAD_CHAINFUNC(realpad) (pad,buf);
        GST_DEBUG (0,"calling chain function of %s:%s done\n", name, GST_PAD_NAME (pad));
      }
    }
  } while (!GST_ELEMENT_IS_COTHREAD_STOPPING(element));
  GST_FLAG_UNSET(element,GST_ELEMENT_COTHREAD_STOPPING);

  GST_DEBUG_LEAVE("(%d,'%s')",argc,name);
  return 0;
}

static int
gst_bin_src_wrapper (int argc,char *argv[])
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
        GST_DEBUG (0,"calling _getfunc for %s:%s\n",GST_DEBUG_PAD_NAME(realpad));
        if (realpad->regiontype != GST_REGION_NONE) {
          g_return_val_if_fail (GST_RPAD_GETREGIONFUNC(realpad) != NULL, 0);
//          if (GST_RPAD_GETREGIONFUNC(realpad) == NULL)
//            fprintf(stderr,"error, no getregionfunc in \"%s\"\n", name);
//          else
          buf = (GST_RPAD_GETREGIONFUNC(realpad))((GstPad*)realpad,realpad->regiontype,realpad->offset,realpad->len);
        } else {
          g_return_val_if_fail (GST_RPAD_GETFUNC(realpad) != NULL, 0);
//          if (GST_RPAD_GETFUNC(realpad) == NULL)
//            fprintf(stderr,"error, no getfunc in \"%s\"\n", name);
//          else
          buf = GST_RPAD_GETFUNC(realpad) ((GstPad*)realpad);
        }

        GST_DEBUG (0,"calling gst_pad_push on pad %s:%s\n",GST_DEBUG_PAD_NAME(realpad));
        if (buf) gst_pad_push ((GstPad*)realpad, buf);
      }
    }
  } while (!GST_ELEMENT_IS_COTHREAD_STOPPING(element));
  GST_FLAG_UNSET(element,GST_ELEMENT_COTHREAD_STOPPING);

  GST_DEBUG_LEAVE("");
  return 0;
}

static void
gst_bin_pushfunc_proxy (GstPad *pad, GstBuffer *buf)
{
  cothread_state *threadstate = GST_ELEMENT (GST_PAD_PARENT (pad))->threadstate;
  GST_DEBUG_ENTER("(%s:%s)",GST_DEBUG_PAD_NAME(pad));
  GST_DEBUG (GST_CAT_DATAFLOW,"putting buffer %p in peer's pen\n",buf);

  // FIXME this should be bounded
  // loop until the bufferpen is empty so we can fill it up again
  while (GST_RPAD_BUFPEN(pad) != NULL) {
    GST_DEBUG (GST_CAT_DATAFLOW, "switching to %p to empty bufpen\n",threadstate);
    cothread_switch (threadstate);
  }

  // now fill the bufferpen and switch so it can be consumed
  GST_RPAD_BUFPEN(GST_RPAD_PEER(pad)) = buf;
  GST_DEBUG (GST_CAT_DATAFLOW,"switching to %p (@%p)\n",threadstate,&(GST_ELEMENT(GST_PAD_PARENT(pad))->threadstate));
  cothread_switch (threadstate);

  GST_DEBUG (GST_CAT_DATAFLOW,"done switching\n");
}

static GstBuffer*
gst_bin_pullfunc_proxy (GstPad *pad)
{
  GstBuffer *buf;

  cothread_state *threadstate = GST_ELEMENT(GST_PAD_PARENT(pad))->threadstate;
  GST_DEBUG_ENTER("(%s:%s)",GST_DEBUG_PAD_NAME(pad));

  // FIXME this should be bounded
  // we will loop switching to the peer until it's filled up the bufferpen
  while (GST_RPAD_BUFPEN(pad) == NULL) {
    GST_DEBUG (GST_CAT_DATAFLOW, "switching to %p to fill bufpen\n",threadstate);
    cothread_switch (threadstate);
  }
  GST_DEBUG (GST_CAT_DATAFLOW,"done switching\n");

  // now grab the buffer from the pen, clear the pen, and return the buffer
  buf = GST_RPAD_BUFPEN(pad);
  GST_RPAD_BUFPEN(pad) = NULL;
  return buf;
}

static GstBuffer*
gst_bin_pullregionfunc_proxy (GstPad *pad,GstRegionType type,guint64 offset,guint64 len)
{
  GstBuffer *buf;

  cothread_state *threadstate = GST_ELEMENT(GST_PAD_PARENT(pad))->threadstate;
  GST_DEBUG_ENTER("%s:%s,%d,%lld,%lld",GST_DEBUG_PAD_NAME(pad),type,offset,len);

  // put the region info into the pad
  GST_RPAD_REGIONTYPE(pad) = type;
  GST_RPAD_OFFSET(pad) = offset;
  GST_RPAD_LEN(pad) = len;

  // FIXME this should be bounded
  // we will loop switching to the peer until it's filled up the bufferpen
  while (GST_RPAD_BUFPEN(pad) == NULL) {
    GST_DEBUG (GST_CAT_DATAFLOW, "switching to %p to fill bufpen\n",threadstate);
    cothread_switch (threadstate);
  }
  GST_DEBUG (GST_CAT_DATAFLOW,"done switching\n");

  // now grab the buffer from the pen, clear the pen, and return the buffer
  buf = GST_RPAD_BUFPEN(pad);
  GST_RPAD_BUFPEN(pad) = NULL;
  return buf;
}


static void
gst_schedule_cothreaded_chain (GstBin *bin, _GstBinChain *chain) {
  GList *elements;
  GstElement *element;
  cothread_func wrapper_function;
  GList *pads;
  GstPad *pad;

  GST_DEBUG (0,"chain is using cothreads\n");

  // first create thread context
  if (bin->threadcontext == NULL) {
    GST_DEBUG (0,"initializing cothread context\n");
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
      wrapper_function = GST_DEBUG_FUNCPTR(gst_bin_loopfunc_wrapper);
      GST_DEBUG (0,"\nelement '%s' is a loop-based\n",GST_ELEMENT_NAME(element));
    } else {
      // otherwise we need to decide what kind of cothread
      // if it's not DECOUPLED, we decide based on whether it's a source or not
      if (!GST_FLAG_IS_SET (element, GST_ELEMENT_DECOUPLED)) {
        // if it doesn't have any sinks, it must be a source (duh)
        if (element->numsinkpads == 0) {
          wrapper_function = GST_DEBUG_FUNCPTR(gst_bin_src_wrapper);
          GST_DEBUG (0,"\nelement '%s' is a source, using _src_wrapper\n",GST_ELEMENT_NAME(element));
        } else {
          wrapper_function = GST_DEBUG_FUNCPTR(gst_bin_chain_wrapper);
          GST_DEBUG (0,"\nelement '%s' is a filter, using _chain_wrapper\n",GST_ELEMENT_NAME(element));
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
          (GST_ELEMENT (GST_PAD_PARENT (GST_PAD (GST_RPAD_PEER (pad))))->manager != GST_ELEMENT(bin))) {
        // set the chain proxies
        if (GST_RPAD_DIRECTION(pad) == GST_PAD_SINK) {
          GST_DEBUG (0,"copying chain function into push proxy for %s:%s\n",GST_DEBUG_PAD_NAME(pad));
          GST_RPAD_PUSHFUNC(pad) = GST_RPAD_CHAINFUNC(pad);
        } else {
          GST_DEBUG (0,"copying get function into pull proxy for %s:%s\n",GST_DEBUG_PAD_NAME(pad));
          GST_RPAD_PULLFUNC(pad) = GST_RPAD_GETFUNC(pad);
          GST_RPAD_PULLREGIONFUNC(pad) = GST_RPAD_GETREGIONFUNC(pad);
        }

      // otherwise we really are a cothread
      } else {
        if (gst_pad_get_direction (pad) == GST_PAD_SINK) {
          GST_DEBUG (0,"setting cothreaded push proxy for sinkpad %s:%s\n",GST_DEBUG_PAD_NAME(pad));
          GST_RPAD_PUSHFUNC(pad) = GST_DEBUG_FUNCPTR(gst_bin_pushfunc_proxy);
        } else {
          GST_DEBUG (0,"setting cothreaded pull proxy for srcpad %s:%s\n",GST_DEBUG_PAD_NAME(pad));
          GST_RPAD_PULLFUNC(pad) = GST_DEBUG_FUNCPTR(gst_bin_pullfunc_proxy);
          GST_RPAD_PULLREGIONFUNC(pad) = GST_DEBUG_FUNCPTR(gst_bin_pullregionfunc_proxy);
        }
      }
    }

    // need to set up the cothread now
    if (wrapper_function != NULL) {
      if (element->threadstate == NULL) {
        element->threadstate = cothread_create (bin->threadcontext);
        GST_DEBUG (0,"created cothread %p for '%s'\n",element->threadstate,GST_ELEMENT_NAME(element));
      }
      cothread_setfunc (element->threadstate, wrapper_function, 0, (char **)element);
      GST_DEBUG (0,"set wrapper function for '%s' to &%s\n",GST_ELEMENT_NAME(element),
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

  GST_DEBUG (0,"chain entered\n");
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
        GST_DEBUG (0,"copying chain function into push proxy for %s:%s\n",GST_DEBUG_PAD_NAME(pad));
        GST_RPAD_PUSHFUNC(pad) = GST_RPAD_CHAINFUNC(pad);
      } else {
        GST_DEBUG (0,"copying get function into pull proxy for %s:%s\n",GST_DEBUG_PAD_NAME(pad));
        GST_RPAD_PULLFUNC(pad) = GST_RPAD_GETFUNC(pad);
        GST_RPAD_PULLREGIONFUNC(pad) = GST_RPAD_GETREGIONFUNC(pad);
      }
    }
  }
}

static void
gst_bin_schedule_cleanup (GstBin *bin)
{
  GList *chains;
  _GstBinChain *chain;

  chains = bin->chains;
  while (chains) {
    chain = (_GstBinChain *)(chains->data);
    chains = g_list_next(chains);

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
  GST_DEBUG (0,"chain removed from scheduler, EOS from element \"%s\"\n", GST_ELEMENT_NAME (element));
  chain->need_scheduling = FALSE;
}

void gst_bin_schedule_func(GstBin *bin) {
  GList *elements;
  GstElement *element;
  GSList *pending = NULL;
  GList *pads;
  GstPad *pad;
  GstElement *peerparent;
  GList *chains;
  _GstBinChain *chain;

  GST_DEBUG_ENTER("(\"%s\")",GST_ELEMENT_NAME (GST_ELEMENT (bin)));

  gst_bin_schedule_cleanup(bin);

  // next we have to find all the separate scheduling chains
  GST_DEBUG (0,"attempting to find scheduling chains...\n");
  // first make a copy of the managed_elements we can mess with
  elements = g_list_copy (bin->managed_elements);
  // we have to repeat until the list is empty to get all chains
  while (elements) {
    element = GST_ELEMENT (elements->data);

    // if this is a DECOUPLED element
    if (GST_FLAG_IS_SET (element, GST_ELEMENT_DECOUPLED)) {
      // skip this element entirely
      GST_DEBUG (0,"skipping '%s' because it's decoupled\n",GST_ELEMENT_NAME(element));
      elements = g_list_next (elements);
      continue;
    }

    GST_DEBUG (0,"starting with element '%s'\n",GST_ELEMENT_NAME(element));

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
      GST_DEBUG (0,"adding '%s' to chain\n",GST_ELEMENT_NAME(element));
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
//        GST_DEBUG (0,"removing '%s' from list of possible elements\n",GST_ELEMENT_NAME(element));
        elements = g_list_remove (elements, element);

        // if this element is a source, add it as an entry
        if (element->numsinkpads == 0) {
          chain->entries = g_list_prepend (chain->entries, element);
          GST_DEBUG (0,"added '%s' as SRC entry into the chain\n",GST_ELEMENT_NAME(element));
        }

        // now we have to walk the pads to find peers
        pads = gst_element_get_pad_list (element);
        while (pads) {
          pad = GST_PAD (pads->data);
          pads = g_list_next (pads);
          if (!GST_IS_REAL_PAD(pad)) continue;
          GST_DEBUG (0,"have pad %s:%s\n",GST_DEBUG_PAD_NAME(pad));

          if (GST_RPAD_PEER(pad) == NULL) continue;
	  if (GST_RPAD_PEER(pad) == NULL) GST_ERROR(pad,"peer is null!");
          g_assert(GST_RPAD_PEER(pad) != NULL);
          g_assert(GST_PAD_PARENT (GST_PAD(GST_RPAD_PEER(pad))) != NULL);

          peerparent = GST_ELEMENT(GST_PAD_PARENT (GST_PAD(GST_RPAD_PEER(pad))));

	  GST_DEBUG (0,"peer pad %p\n", GST_RPAD_PEER(pad));
          // only bother with if the pad's peer's parent is this bin or it's DECOUPLED
          // only add it if it's in the list of un-visited elements still
          if ((g_list_find (elements, peerparent) != NULL) ||
              GST_FLAG_IS_SET (peerparent, GST_ELEMENT_DECOUPLED)) {
            // add the peer element to the pending list
            GST_DEBUG (0,"adding '%s' to list of pending elements\n",
                       GST_ELEMENT_NAME(peerparent));
            pending = g_slist_prepend (pending, peerparent);

            // if this is a sink pad, then the element on the other side is an entry
            if ((GST_RPAD_DIRECTION(pad) == GST_PAD_SINK) &&
                (GST_FLAG_IS_SET (peerparent, GST_ELEMENT_DECOUPLED))) {
              chain->entries = g_list_prepend (chain->entries, peerparent);
              gtk_signal_connect (GTK_OBJECT (peerparent), "eos", gst_scheduler_handle_eos, chain);
              GST_DEBUG (0,"added '%s' as DECOUPLED entry into the chain\n",GST_ELEMENT_NAME(peerparent));
            }
          } else
            GST_DEBUG (0,"element '%s' has already been dealt with\n",GST_ELEMENT_NAME(peerparent));
        }
      }
    } while (pending);

    // add the chain to the bin
    GST_DEBUG (0,"have chain with %d elements: ",chain->num_elements);
    { GList *elements = chain->elements;
      while (elements) {
        element = GST_ELEMENT (elements->data);
        elements = g_list_next(elements);
        GST_DEBUG_NOPREFIX(0,"%s, ",GST_ELEMENT_NAME(element));
      }
    }
    GST_DEBUG_NOPREFIX(0,"\n");
    bin->chains = g_list_prepend (bin->chains, chain);
    bin->num_chains++;
  }
  // free up the list in case it's full of DECOUPLED elements
  g_list_free (elements);

  GST_DEBUG (0,"\nwe have %d chains to schedule\n",bin->num_chains);

  // now we have to go through all the chains and schedule them
  chains = bin->chains;
  while (chains) {
    chain = (_GstBinChain *)(chains->data);
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



/*************** INCREMENTAL SCHEDULING CODE STARTS HERE ***************/

static GstSchedule realsched;
static GstSchedule *sched = &realsched;


/* this function will look at a pad and determine if the peer parent is
 * a possible candidate for connecting up in the same chain. */
GstElement *gst_schedule_check_pad (GstSchedule *schedule, GstPad *pad) {
  GstRealPad *peer;
  GstElement *peerelement;

  GST_INFO (GST_CAT_SCHEDULING, "checking pad %s:%s for peer in scheduler",
            GST_DEBUG_PAD_NAME(pad));

  peer = GST_PAD_PEER (pad);
  if (peer == NULL) return NULL;
  peerelement = GST_ELEMENT(GST_PAD_PARENT (peer));

  // first of all, if the peer element is decoupled, it will be in the same chain
  if (GST_FLAG_IS_SET(peerelement,GST_ELEMENT_DECOUPLED))
    return peerelement;

  // now check to see if it's in the same schedule
  if (g_list_find(sched->elements,peerelement))
    return peerelement;

  // otherwise it's not a candidate
  return NULL;
}

GstScheduleChain *
gst_schedule_chain_new (GstSchedule *sched)
{
  GstScheduleChain *chain = g_new (GstScheduleChain, 1);

  chain->sched = sched;
  chain->elements = NULL;
  chain->num_elements = 0;
  chain->entry = NULL;
  chain->need_cothreads = TRUE;
  chain->schedule = FALSE;

  sched->chains = g_list_prepend (sched->chains, chain);
  sched->num_chains++;

  GST_INFO (GST_CAT_SCHEDULING, "created new chain, now are %d chains",sched->num_chains);

  return chain;
}

void
gst_schedule_chain_destroy (GstScheduleChain *chain)
{
  chain->sched->chains = g_list_remove (chain->sched->chains, chain);
  sched->num_chains--;

  g_list_free (chain->elements);
  g_free (chain);
}

void
gst_schedule_chain_add_element (GstScheduleChain *chain, GstElement *element)
{
  GST_INFO (GST_CAT_SCHEDULING, "adding element \"%s\" to chain", GST_ELEMENT_NAME (element));
  chain->elements = g_list_prepend (chain->elements, element);
  chain->num_elements++;
  // FIXME need to update chain schedule here, or not
}

void
gst_schedule_chain_elements (GstSchedule *schedule, GstElement *element1, GstElement *element2)
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

    if (g_list_find (chain->elements,element1))
      chain1 = chain;
    if (g_list_find (chain->elements,element2))
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

  // otherwise if both have chains already, join them
  } else if ((chain1 != NULL) && (chain2 != NULL)) {
    GST_INFO (GST_CAT_SCHEDULING, "joining two existing chains together");
    // take the contents of chain2 and merge them into chain1
    chain1->elements = g_list_concat (chain1->elements, chain2->elements);
    chain1->num_elements += chain2->num_elements;
    // FIXME chain changed here

    g_free(chain2);

  // otherwise one has a chain already, the other doesn't
  } else {
    // pick out which one has the chain, and which doesn't
    if (chain1 != NULL) chain = chain1, element = element2;
    else chain = chain2, element = element1;

    GST_INFO (GST_CAT_SCHEDULING, "adding element to existing chain");
    gst_schedule_chain_add_element (chain, element);
    // FIXME chain changed here
  }
}

void
gst_schedule_pad_connect_callback (GstPad *pad, GstPad *peer, GstSchedule *sched)
{
  GstElement *peerelement;

  GST_INFO (GST_CAT_SCHEDULING, "have pad connected callback on %s:%s",GST_DEBUG_PAD_NAME(pad));

  if ((peerelement = gst_schedule_check_pad(sched,pad))) {
    GST_INFO (GST_CAT_SCHEDULING, "peer is in same schedule, chaining together");
    gst_schedule_chain_elements (sched, GST_ELEMENT(GST_PAD_PARENT(pad)), peerelement);
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

  // now go through all the pads and see which peers can be added
  pads = element->pads;
  while (pads) {
    pad = GST_PAD(pads->data);
    pads = g_list_next (pads);

    // if it's a potential peer
    if ((peerelement = gst_schedule_check_pad (sched, pad))) {
      // if it's not already in a chain, add it to this one
      if (gst_schedule_find_chain (sched, peerelement) == NULL) {
        gst_schedule_chain_recursive_add (chain, peerelement);
      }
    }
  }
}

void
gst_schedule_pad_disconnect_callback (GstPad *pad, GstPad *peer, GstSchedule *sched)
{
  GstScheduleChain *chain;
  GstElement *element1, *element2;
  GstScheduleChain *chain1, *chain2;

  GST_INFO (GST_CAT_SCHEDULING, "have pad disconnected callback on %s:%s",GST_DEBUG_PAD_NAME(pad));

  // we need to have the parent elements of each pad
  element1 = GST_PAD_PARENT(pad);
  element2 = GST_PAD_PARENT(peer);
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

return;

  // check the other element to see if it landed in the newly created chain
  if (gst_schedule_find_chain (sched, element2) == NULL) {
    // if not in chain, create chain and build from scratch
    chain2 = gst_schedule_chain_new (sched);
    gst_schedule_chain_recursive_add (chain2, element2);
  }
}

void
gst_schedule_add_element (GstSchedule *schedule, GstElement *element)
{
  GList *pads;
  GstPad *pad;
  GstElement *peerelement;

  g_return_if_fail (element != NULL);
  g_return_if_fail (GST_IS_ELEMENT(element));

  GST_INFO (GST_CAT_SCHEDULING, "adding element \"%s\" to schedule",
    GST_ELEMENT_NAME(element));

  // first add it to the list of elements that are to be scheduled
  sched->elements = g_list_prepend (sched->elements, element);
  sched->num_elements++;

  // now look through the pads and see what we need to do
  pads = element->pads;
  while (pads) {
    pad = GST_PAD(pads->data);
    pads = g_list_next(pads);

    // if the peer element is a candidate
    if ((peerelement = gst_schedule_check_pad(sched,pad))) {
      GST_INFO (GST_CAT_SCHEDULING, "peer is in same schedule, chaining together");
      // make sure that the two elements are in the same chain
      gst_schedule_chain_elements (sched,element,peerelement);
    }

    // now we have to attach a signal to each pad
    // FIXME this is stupid
    gtk_signal_connect(pad,"connected",GTK_SIGNAL_FUNC(gst_schedule_pad_connect_callback),sched);
    gtk_signal_connect(pad,"disconnected",GTK_SIGNAL_FUNC(gst_schedule_pad_disconnect_callback),sched);
  }
}

void
gst_schedule_remove_element (GstSchedule *schedule, GstElement *element)
{
  GList *chains;
  GstScheduleChain *chain;

  g_return_if_fail (element != NULL);
  g_return_if_fail (GST_IS_ELEMENT(element));

  if (g_list_find (sched->elements, element)) {
    GST_INFO (GST_CAT_SCHEDULING, "removing element \"%s\" from schedule",
      GST_ELEMENT_NAME(element));

    // find which chain it's in and remove it from that chain
    chains = sched->chains;
    while (chains) {
      chain = (GstScheduleChain *)(chains->data);
      chains = g_list_next(chains);

      if (g_list_find (chain->elements, element)) {
        GST_INFO (GST_CAT_SCHEDULING, "removing element from chain");
        chain->elements = g_list_remove (chain->elements, element);
        // FIXME chain changed here
        break;
      }
    }

    sched->elements = g_list_remove (sched->elements, element);
    sched->num_elements--;
  }
}
