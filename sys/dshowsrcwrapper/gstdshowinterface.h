/* GStreamer
 * Copyright (C) 2007 Sebastien Moutte <sebastien@moutte.net>
 *
 * gstdshowinterface.h:
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

#ifndef __GST_DHOW_INTERFACE_H__
#define __GST_DHOW_INTERFACE_H__

#include "gstdshow.h"

#ifdef  __cplusplus
typedef bool (*push_buffer_func) (byte *buffer, long size, byte *src_object, UINT64 start, UINT64 stop);
#endif

/* verify that the <rpcndr.h> version is high enough to compile this file*/
#ifndef __REQUIRED_RPCNDR_H_VERSION__
#define __REQUIRED_RPCNDR_H_VERSION__ 440
#endif

#include "rpc.h"
#include "rpcndr.h"

#ifndef __RPCNDR_H_VERSION__
#error this stub requires an updated version of <rpcndr.h>
#endif // __RPCNDR_H_VERSION__

#ifndef COM_NO_WINDOWS_H
#include "windows.h"
#include "ole2.h"
#endif /*COM_NO_WINDOWS_H*/

//{6A780808-9725-4d0b-8695-A4DD8D210773}
static const GUID CLSID_DshowFakeSink
  = { 0x6a780808, 0x9725, 0x4d0b, { 0x86, 0x95,  0xa4,  0xdd,  0x8d,  0x21,  0x7,  0x73 } };

// {FC36764C-6CD4-4C73-900F-3F40BF3F191A}
static const GUID IID_IGstDshowInterface = 
  { 0xfc36764c, 0x6cd4, 0x4c73, { 0x90, 0xf, 0x3f, 0x40, 0xbf, 0x3f, 0x19, 0x1a } };

#define CLSID_DSHOWFAKESINK_STRING "{6A780808-9725-4d0b-8695-A4DD8D210773}"

typedef interface IGstDshowInterface IGstDshowInterface;

/* header files for imported files */
#include "oaidl.h"
#include "ocidl.h"

void __RPC_FAR * __RPC_USER MIDL_user_allocate(size_t);
void __RPC_USER MIDL_user_free( void __RPC_FAR * ); 

#ifndef __IGstDshowInterface_INTERFACE_DEFINED__
#define __IGstDshowInterface_INTERFACE_DEFINED__

#if defined(__cplusplus) && !defined(CINTERFACE)
    
    MIDL_INTERFACE("542C0A24-8BD1-46cb-AA57-3E46D006D2F3")
    IGstDshowInterface : public IUnknown
    {
    public:
        virtual HRESULT STDMETHODCALLTYPE gst_set_media_type( 
            AM_MEDIA_TYPE __RPC_FAR *pmt) = 0;

        virtual HRESULT STDMETHODCALLTYPE gst_set_buffer_callback( 
            push_buffer_func push, byte *data) = 0;

        virtual HRESULT STDMETHODCALLTYPE gst_push_buffer( 
            byte *buffer, __int64 start, __int64 stop, unsigned int size, bool discount) = 0;
        
        virtual HRESULT STDMETHODCALLTYPE gst_flush() = 0;

        virtual HRESULT STDMETHODCALLTYPE gst_set_sample_size(unsigned int size) = 0;
    };
    
#else 	/* C style interface */

    typedef struct IGstDshowInterfaceVtbl
    {
        BEGIN_INTERFACE
        
        HRESULT ( STDMETHODCALLTYPE __RPC_FAR *QueryInterface )( 
            IGstDshowInterface __RPC_FAR * This,
            REFIID riid,
            void __RPC_FAR *__RPC_FAR *ppvObject);
        
        ULONG ( STDMETHODCALLTYPE __RPC_FAR *AddRef )( 
            IGstDshowInterface __RPC_FAR * This);
        
        ULONG ( STDMETHODCALLTYPE __RPC_FAR *Release )( 
            IGstDshowInterface __RPC_FAR * This);
        
        HRESULT (STDMETHODCALLTYPE  *gst_set_media_type )( 
            IGstDshowInterface __RPC_FAR * This,
            AM_MEDIA_TYPE *pmt);

        HRESULT (STDMETHODCALLTYPE *gst_set_buffer_callback) ( 
            IGstDshowInterface __RPC_FAR * This,
            byte * push, byte *data);

        HRESULT (STDMETHODCALLTYPE *gst_push_buffer) ( 
            IGstDshowInterface __RPC_FAR * This,
            byte *buffer, __int64 start, __int64 stop,
            unsigned int size, boolean discount);

        HRESULT (STDMETHODCALLTYPE *gst_flush) ( 
            IGstDshowInterface __RPC_FAR * This);
        
        HRESULT (STDMETHODCALLTYPE *gst_set_sample_size) ( 
            IGstDshowInterface __RPC_FAR * This,
            unsigned int size);

        END_INTERFACE
    } IGstDshowInterfaceVtbl;

    interface IGstDshowInterface
    {
        CONST_VTBL struct IGstDshowInterfaceVtbl __RPC_FAR *lpVtbl;
    };

#define IGstDshowInterface_QueryInterface(This,riid,ppvObject)	\
    (This)->lpVtbl -> QueryInterface(This,riid,ppvObject)

#define IGstDshowInterface_AddRef(This)	\
    (This)->lpVtbl -> AddRef(This)

#define IGstDshowInterface_Release(This)	\
    (This)->lpVtbl -> Release(This)

#define IGstDshowInterface_gst_set_media_type(This, mediatype)	\
    (This)->lpVtbl -> gst_set_media_type(This, mediatype)

#define IGstDshowInterface_gst_set_buffer_callback(This, push, data)	\
    (This)->lpVtbl -> gst_set_buffer_callback(This, push, data)

#define IGstDshowInterface_gst_push_buffer(This, buffer, start, stop, size, discount)	\
    (This)->lpVtbl -> gst_push_buffer(This, buffer, start, stop, size, discount)

#define IGstDshowInterface_gst_flush(This)	\
    (This)->lpVtbl -> gst_flush(This)

#define IGstDshowInterface_gst_set_sample_size(This, size)	\
    (This)->lpVtbl -> gst_set_sample_size(This, size)

#endif 	/* C style interface */

#endif 	/* __IGstDshowInterface_INTERFACE_DEFINED__ */

#endif /* __GST_DSHOW_INTERFACE_H__ */