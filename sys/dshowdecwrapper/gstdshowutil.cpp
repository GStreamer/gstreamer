/* GStreamer
 * Copyright (C) 2007 Sebastien Moutte <sebastien@moutte.net>
 *
 * gstdshow.cpp:
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

#include <atlbase.h>

#include "gstdshowutil.h"
#include "gstdshowfakesrc.h"

IPin * 
gst_dshow_get_pin_from_filter (IBaseFilter *filter, PIN_DIRECTION pindir)
{
  CComPtr<IEnumPins> enumpins;
  CComPtr<IPin> pin;
  HRESULT hres; 

  hres = filter->EnumPins (&enumpins);
  if (FAILED(hres)) {
    return NULL;
  }

  while (enumpins->Next (1, &pin, NULL) == S_OK)
  {
    PIN_DIRECTION pindirtmp;
    hres = pin->QueryDirection (&pindirtmp);
    if (hres == S_OK && pindir == pindirtmp) {
      return pin;
    }
    pin.Release();
  }

  return NULL;
}

gboolean gst_dshow_find_filter(CLSID input_majortype, CLSID input_subtype, 
                               CLSID output_majortype, CLSID output_subtype, 
                               gchar * prefered_filter_name, IBaseFilter **filter)
{
  gboolean ret = FALSE;
  HRESULT hres;
  GUID arrayInTypes[2];
  GUID arrayOutTypes[2];
  IFilterMapper2 *mapper = NULL;
  IEnumMoniker *enum_moniker = NULL;
  IMoniker *moniker = NULL;
  ULONG fetched;
  gchar *prefered_filter_upper = NULL;
  gboolean exit = FALSE;

  /* initialize output parameter */
  if (filter)
    *filter = NULL;

  /* create a private copy of prefered filter substring in upper case */
  if (prefered_filter_name) {
    prefered_filter_upper = g_strdup (prefered_filter_name);
    strupr (prefered_filter_upper);
  }

  hres = CoCreateInstance(CLSID_FilterMapper2, NULL, CLSCTX_INPROC, 
      IID_IFilterMapper2, (void **) &mapper);
  if (FAILED(hres))
    goto clean;
  
  memcpy(&arrayInTypes[0], &input_majortype, sizeof (CLSID));
  memcpy(&arrayInTypes[1], &input_subtype, sizeof (CLSID));
  memcpy(&arrayOutTypes[0], &output_majortype, sizeof (CLSID));
  memcpy(&arrayOutTypes[1], &output_subtype, sizeof (CLSID));

  hres = mapper->EnumMatchingFilters (&enum_moniker, 0, FALSE, MERIT_DO_NOT_USE+1, 
          TRUE, 1, arrayInTypes, NULL, NULL, FALSE, 
          TRUE, 1, arrayOutTypes, NULL, NULL);
  if (FAILED(hres))
    goto clean;
  
  enum_moniker->Reset ();
  
  while(hres = enum_moniker->Next (1, &moniker, &fetched),hres == S_OK
    && !exit) {
    IBaseFilter *filter_temp = NULL;
    IPropertyBag *property_bag = NULL;
    gchar * friendly_name = NULL;

    hres = moniker->BindToStorage (NULL, NULL, IID_IPropertyBag, (void **)&property_bag);
    if(SUCCEEDED(hres) && property_bag) {
      VARIANT varFriendlyName;
      VariantInit (&varFriendlyName);
      
      hres = property_bag->Read (L"FriendlyName", &varFriendlyName, NULL);
      if(hres == S_OK && varFriendlyName.bstrVal) {
         friendly_name = g_utf16_to_utf8((const gunichar2*)varFriendlyName.bstrVal, 
            wcslen(varFriendlyName.bstrVal), NULL, NULL, NULL);        
         if (friendly_name)
           strupr (friendly_name);
        SysFreeString (varFriendlyName.bstrVal);
      }
      property_bag->Release ();
    }

    hres = moniker->BindToObject(NULL, NULL, IID_IBaseFilter, (void**)&filter_temp);    
    if(SUCCEEDED(hres) && filter_temp) {
      ret = TRUE;
      if (filter) {
        if (*filter)
          (*filter)->Release ();

        *filter = filter_temp;
        (*filter)->AddRef ();

        if (prefered_filter_upper && friendly_name &&
          strstr(friendly_name, prefered_filter_upper))
          exit = TRUE;
      }

      /* if we just want to know if the formats are supported OR 
          if we don't care about what will be the filter used 
          => we can stop enumeration */
      if (!filter || !prefered_filter_upper)
        exit = TRUE;

      filter_temp->Release ();
    }

    if (friendly_name)
      g_free (friendly_name);
    moniker->Release ();
  }

clean:
  if (prefered_filter_upper)
    g_free (prefered_filter_upper);
  if (enum_moniker)
    enum_moniker->Release ();
  if (mapper)
    mapper->Release ();

  return ret;
}

