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
  G_GNUC_UNUSED const gchar *name = gst_element_get_name (element);

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
  G_GNUC_UNUSED const gchar *name = gst_element_get_name (element);
  GList *pads;
  GstPad *pad;
  GstBuffer *buf;
        
  GST_DEBUG_ENTER("(\"%s\")",name);
  GST_DEBUG (0,"stepping through pads\n");
  do {
    pads = element->pads;
    while (pads) {
      pad = GST_PAD (pads->data);
      pads = g_list_next (pads);   
      if (pad->direction == GST_PAD_SINK) {
        GST_DEBUG (0,"pulling a buffer from %s:%s\n", name, gst_pad_get_name (pad));
        buf = gst_pad_pull (pad);
        GST_DEBUG (0,"calling chain function of %s:%s\n", name, gst_pad_get_name (pad));
        (pad->chainfunc) (pad,buf);
        GST_DEBUG (0,"calling chain function of %s:%s done\n", name, gst_pad_get_name (pad));
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
  GstPad *pad;
  GstBuffer *buf;
  G_GNUC_UNUSED const gchar *name = gst_element_get_name (element);
  
  GST_DEBUG_ENTER("(%d,\"%s\")",argc,name);

  do {
    pads = element->pads;
    while (pads) {
      pad = GST_PAD (pads->data);   
      if (pad->direction == GST_PAD_SRC) {
//        region_struct *region = cothread_get_data (element->threadstate, "region");
        GST_DEBUG (0,"calling _getfunc for %s:%s\n",GST_DEBUG_PAD_NAME(pad));
//        if (region) {
          //gst_src_push_region (GST_SRC (element), region->offset, region->size);
//          if (pad->getregionfunc == NULL)
//            fprintf(stderr,"error, no getregionfunc in \"%s\"\n", name);
//          buf = (pad->getregionfunc)(pad, region->offset, region->size);
//        } else {
          if (pad->getfunc == NULL)
            fprintf(stderr,"error, no getfunc in \"%s\"\n", name);
          buf = (pad->getfunc)(pad);
//        }
 
        GST_DEBUG (0,"calling gst_pad_push on pad %s:%s\n",GST_DEBUG_PAD_NAME(pad));
        gst_pad_push (pad, buf);
      }
      pads = g_list_next(pads);
    }
  } while (!GST_ELEMENT_IS_COTHREAD_STOPPING(element));
  GST_FLAG_UNSET(element,GST_ELEMENT_COTHREAD_STOPPING);

  GST_DEBUG_LEAVE("");
  return 0;
}
                                
static void
gst_bin_pushfunc_proxy (GstPad *pad, GstBuffer *buf)
{
  cothread_state *threadstate = GST_ELEMENT(pad->parent)->threadstate;
  GST_DEBUG_ENTER("(%s:%s)",GST_DEBUG_PAD_NAME(pad));
  GST_DEBUG (0,"putting buffer %p in peer's pen\n",buf);
  pad->peer->bufpen = buf;
  GST_DEBUG (0,"switching to %p (@%p)\n",threadstate,&(GST_ELEMENT(pad->parent)->threadstate));
  cothread_switch (threadstate);
  GST_DEBUG (0,"done switching\n");
}

static GstBuffer*
gst_bin_pullfunc_proxy (GstPad *pad)
{       
  GstBuffer *buf;

  cothread_state *threadstate = GST_ELEMENT(pad->parent)->threadstate;
  GST_DEBUG_ENTER("(%s:%s)",GST_DEBUG_PAD_NAME(pad));
  if (pad->bufpen == NULL) {
    GST_DEBUG (0,"switching to %p (@%p)\n",threadstate,&(GST_ELEMENT(pad->parent)->threadstate));
    cothread_switch (threadstate);
  }
  GST_DEBUG (0,"done switching\n");
  buf = pad->bufpen;  
  pad->bufpen = NULL;
  return buf; 
}
  
static GstBuffer *
gst_bin_chainfunc_proxy (GstPad *pad)
{
// FIXME!!
//  GstBuffer *buf;
  return NULL;
}
  
// FIXME!!!
static void
gst_bin_pullregionfunc_proxy (GstPad *pad,
                                gulong offset,
                                gulong size)
{
//  region_struct region;
  cothread_state *threadstate;
    
  GST_DEBUG_ENTER("%s:%s,%ld,%ld",GST_DEBUG_PAD_NAME(pad),offset,size);
      
//  region.offset = offset;
//  region.size = size;
  
//  threadstate = GST_ELEMENT(pad->parent)->threadstate;
//  cothread_set_data (threadstate, "region", &region);
  cothread_switch (threadstate);
//  cothread_set_data (threadstate, "region", NULL);
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
      GST_DEBUG (0,"\nelement '%s' is a loop-based\n",gst_element_get_name(element));
    } else {
      // otherwise we need to decide what kind of cothread
      // if it's not DECOUPLED, we decide based on whether it's a source or not
      if (!GST_FLAG_IS_SET (element, GST_ELEMENT_DECOUPLED)) {
        // if it doesn't have any sinks, it must be a source (duh)
        if (element->numsinkpads == 0) {
          wrapper_function = GST_DEBUG_FUNCPTR(gst_bin_src_wrapper);
          GST_DEBUG (0,"\nelement '%s' is a source, using _src_wrapper\n",gst_element_get_name(element));
        } else {
          wrapper_function = GST_DEBUG_FUNCPTR(gst_bin_chain_wrapper);
          GST_DEBUG (0,"\nelement '%s' is a filter, using _chain_wrapper\n",gst_element_get_name(element));
        }
      }
    }

    // now we have to walk through the pads to set up their state
    pads = gst_element_get_pad_list (element);
    while (pads) {
      pad = GST_PAD (pads->data);
      pads = g_list_next (pads);

      // if the element is DECOUPLED or outside the manager, we have to chain
      if ((wrapper_function == NULL) ||
          (GST_ELEMENT(pad->peer->parent)->manager != GST_ELEMENT(bin))) {
        // set the chain proxies
        if (gst_pad_get_direction (pad) == GST_PAD_SINK) {
          GST_DEBUG (0,"copying chain function into push proxy for %s:%s\n",GST_DEBUG_PAD_NAME(pad));
          pad->pushfunc = pad->chainfunc;
        } else {
          GST_DEBUG (0,"copying get function into pull proxy for %s:%s\n",GST_DEBUG_PAD_NAME(pad));
          pad->pullfunc = pad->getfunc;
          pad->pullregionfunc = pad->getregionfunc;
        }

      // otherwise we really are a cothread
      } else {
        if (gst_pad_get_direction (pad) == GST_PAD_SINK) {
          GST_DEBUG (0,"setting cothreaded push proxy for sinkpad %s:%s\n",GST_DEBUG_PAD_NAME(pad));
          pad->pushfunc = GST_DEBUG_FUNCPTR(gst_bin_pushfunc_proxy);
        } else {
          GST_DEBUG (0,"setting cothreaded pull proxy for srcpad %s:%s\n",GST_DEBUG_PAD_NAME(pad));
          pad->pullfunc = GST_DEBUG_FUNCPTR(gst_bin_pullfunc_proxy);
        }
      }
    }

    // need to set up the cothread now
    if (wrapper_function != NULL) {
      if (element->threadstate == NULL) {
        element->threadstate = cothread_create (bin->threadcontext);
        GST_DEBUG (0,"created cothread %p for '%s'\n",element->threadstate,gst_element_get_name(element));
      }
      cothread_setfunc (element->threadstate, wrapper_function, 0, (char **)element);
      GST_DEBUG (0,"set wrapper function for '%s' to &%s\n",gst_element_get_name(element),
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

      if (gst_pad_get_direction (pad) == GST_PAD_SINK) {
        GST_DEBUG (0,"copying chain function into push proxy for %s:%s\n",GST_DEBUG_PAD_NAME(pad));
        pad->pushfunc = pad->chainfunc; 
      } else {
        GST_DEBUG (0,"copying get function into pull proxy for %s:%s\n",GST_DEBUG_PAD_NAME(pad));
        pad->pullfunc = pad->getfunc;
        pad->pullregionfunc = pad->getregionfunc;
      }
    }
  }
}

static void gst_bin_schedule_cleanup(GstBin *bin) {
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

void gst_bin_schedule_func(GstBin *bin) {
//  GstElement *manager;
  GList *elements;
  GstElement *element;
//  const gchar *elementname;
  GSList *pending = NULL;
//  GstBin *pending_bin;
  GList *pads;
  GstPad *pad;
//  GstElement *peer_manager;
  GList *chains;
  _GstBinChain *chain;

  GST_DEBUG_ENTER("(\"%s\")",gst_element_get_name (GST_ELEMENT (bin)));

  gst_bin_schedule_cleanup(bin);

  // next we have to find all the separate scheduling chains
  GST_DEBUG (0,"\nattempting to find scheduling chains...\n");
  // first make a copy of the managed_elements we can mess with
  elements = g_list_copy (bin->managed_elements);
  // we have to repeat until the list is empty to get all chains
  while (elements) {
    element = GST_ELEMENT (elements->data);

    // if this is a DECOUPLED element
    if (GST_FLAG_IS_SET (element, GST_ELEMENT_DECOUPLED)) {
      // skip this element entirely
      GST_DEBUG (0,"skipping '%s' because it's decoupled\n",gst_element_get_name(element));
      elements = g_list_next (elements);
      continue;
    }

    GST_DEBUG (0,"starting with element '%s'\n",gst_element_get_name(element));

    // prime the pending list with the first element off the top
    pending = g_slist_prepend (NULL, element);
    // and remove that one from the main list
    elements = g_list_remove (elements, element);

    // create a chain structure
    chain = g_new0 (_GstBinChain, 1);

    // for each pending element, walk the pipeline
    do {
      // retrieve the top of the stack and pop it
      element = GST_ELEMENT (pending->data);
      pending = g_slist_remove (pending, element);

      // add ourselves to the chain's list of elements
      GST_DEBUG (0,"adding '%s' to chain\n",gst_element_get_name(element));
      chain->elements = g_list_prepend (chain->elements, element);
      chain->num_elements++;
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
//        GST_DEBUG (0,"removing '%s' from list of possible elements\n",gst_element_get_name(element));
        elements = g_list_remove (elements, element);

        // if this element is a source, add it as an entry
        if (element->numsinkpads == 0) {
          chain->entries = g_list_prepend (chain->entries, element);
          GST_DEBUG (0,"added '%s' as SRC entry into the chain\n",gst_element_get_name(element));
        }

        // now we have to walk the pads to find peers
        pads = gst_element_get_pad_list (element);
        while (pads) {
          pad = GST_PAD (pads->data);
          pads = g_list_next (pads);
          GST_DEBUG (0,"have pad %s:%s\n",GST_DEBUG_PAD_NAME(pad));

if (pad->peer == NULL) GST_ERROR(pad,"peer is null!");
          g_assert(pad->peer != NULL);
          g_assert(pad->peer->parent != NULL);
          //g_assert(GST_ELEMENT(pad->peer->parent)->manager != NULL);

	  GST_DEBUG (0,"peer pad %p\n", pad->peer);
          // only bother with if the pad's peer's parent is this bin or it's DECOUPLED
          // only add it if it's in the list of un-visited elements still
          if ((g_list_find (elements, pad->peer->parent) != NULL) ||
              GST_FLAG_IS_SET (pad->peer->parent, GST_ELEMENT_DECOUPLED)) {
            // add the peer element to the pending list
            GST_DEBUG (0,"adding '%s' to list of pending elements\n",gst_element_get_name(GST_ELEMENT(pad->peer->parent)));
            pending = g_slist_prepend (pending, GST_ELEMENT(pad->peer->parent));

            // if this is a sink pad, then the element on the other side is an entry
            if ((gst_pad_get_direction (pad) == GST_PAD_SINK) &&
                (GST_FLAG_IS_SET (pad->peer->parent, GST_ELEMENT_DECOUPLED))) {
              chain->entries = g_list_prepend (chain->entries, pad->peer->parent);
              GST_DEBUG (0,"added '%s' as DECOUPLED entry into the chain\n",gst_element_get_name(GST_ELEMENT(pad->peer->parent))); 
            }
          } else
            GST_DEBUG (0,"element '%s' has already been dealt with\n",gst_element_get_name(GST_ELEMENT(pad->peer->parent)));
        }
      }
    } while (pending);

    // add the chain to the bin
    GST_DEBUG (0,"have chain with %d elements: ",chain->num_elements);
    { GList *elements = chain->elements;
      while (elements) {
        element = GST_ELEMENT (elements->data);
        elements = g_list_next(elements);
        GST_DEBUG_NOPREFIX(0,"%s, ",gst_element_get_name(element));
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

  GST_DEBUG_LEAVE("(\"%s\")",gst_element_get_name(GST_ELEMENT(bin)));
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
            GST_DEBUG (0,"dealing with outside source element %s\n",gst_element_get_name(outside));
//            GST_DEBUG (0,"PUNT: copying pullfunc ptr from %s:%s to %s:%s (@ %p)\n",
//GST_DEBUG_PAD_NAME(pad->peer),GST_DEBUG_PAD_NAME(pad),&pad->pullfunc);
//            pad->pullfunc = pad->peer->pullfunc;
//            GST_DEBUG (0,"PUNT: setting pushfunc proxy to fake proxy on %s:%s\n",GST_DEBUG_PAD_NAME(pad->peer));
//            pad->peer->pushfunc = GST_DEBUG_FUNCPTR(gst_bin_pushfunc_fake_proxy);
            pad->pullfunc = GST_DEBUG_FUNCPTR(gst_bin_pullfunc_proxy);
          }
        } else {
*/





/*
      } else if (GST_IS_SRC (element)) {
        GST_DEBUG (0,"adding '%s' as entry point, because it's a source\n",gst_element_get_name (element));
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
          pad->pullfunc = GST_DEBUG_FUNCPTR(gst_bin_pullfunc_proxy);
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
      GST_DEBUG (0,"found element \"%s\"\n", gst_element_get_name (element));
      if (GST_IS_BIN (element)) {
        gst_bin_create_plan (GST_BIN (element));
      }
      if (GST_IS_SRC (element)) {
        GST_DEBUG (0,"adding '%s' as entry point, because it's a source\n",gst_element_get_name (element));
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
          pad->pushfunc = GST_DEBUG_FUNCPTR(pad->peer->chainfunc);

          // need to walk through and check for outside connections
//FIXME need to do this for all pads
          // get the pad's peer
          peer = gst_pad_get_peer (pad);
          if (!peer) {
	    GST_DEBUG (0,"found SINK pad %s has no peer\n", gst_pad_get_name (pad));
	    break;
	  }
          // get the parent of the peer of the pad
          outside = GST_ELEMENT (gst_pad_get_parent (peer));
          if (!outside) break;
          // if it's a connection and it's not ours...
          if (GST_IS_CONNECTION (outside) &&
               (gst_object_get_parent (GST_OBJECT (outside)) != GST_OBJECT (bin))) {
            gst_info("gstbin: element \"%s\" is the external source Connection "
				    "for internal element \"%s\"\n",
	                  gst_element_get_name (GST_ELEMENT (outside)),
	                  gst_element_get_name (GST_ELEMENT (element)));
	    bin->entries = g_list_prepend (bin->entries, outside);
	    bin->num_entries++;
	  }
	}
	else {
	  GST_DEBUG (0,"found pad %s\n", gst_pad_get_name (pad));
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
        GST_DEBUG (0,"element %s is a loopfunc, must use a cothread\n",gst_element_get_name(element));
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
            pad->pullfunc = GST_DEBUG_FUNCPTR(gst_bin_pullfunc_proxy);
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
                &element->threadstate,gst_element_get_name(element));
        }
        cothread_setfunc (element->threadstate, wrapper_function, 0, (char **)element);
        GST_DEBUG (0,"set wrapper function for \"%s\" to &%s\n",gst_element_get_name(element),
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


