/**********************************************************************
*
* Copyright(c) Imagination Technologies Ltd.
*
* The contents of this file are subject to the MIT license as set out below.
*
* Permission is hereby granted, free of charge, to any person obtaining a copy
* of this software and associated documentation files (the "Software"),
* to deal in the Software without restriction, including without limitation
* the rights to use, copy, modify, merge, publish, distribute, sublicense,
* and/or sell copies of the Software, and to permit persons to whom the
* Software is furnished to do so, subject to the following conditions:
* 
* The above copyright notice and this permission notice shall be included
* in all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
* OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, 
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
* DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT
* OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE
* OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
* 
* This License is also included in this distribution in the file called 
* "COPYING".
* 
******************************************************************************/



#if !defined(__DRI2_WS_H__)
#define __DRI2_WS_H__

#define DRI2WS_DISPFLAG_DEFAULT_DISPLAY 0x00000001

/*
// Constants (macros) related to back-buffering.
*/

#define XWS_FLIP_BUFFERS		3
#define DRI2_FLIP_BUFFERS_NUM	XWS_FLIP_BUFFERS
#define XWS_FLIP_BUFFER_INDEX	(XWS_MAX_FLIP_BUFFERS - 1)

#define XWS_BLIT_BUFFERS		2
#define DRI2_BLIT_BUFFERS_NUM	XWS_BLIT_BUFFERS
#define XWS_BLIT_BUFFER_INDEX	(XWS_MAX_BLIT_BUFFERS - 1)

#if 0
#define MIN(a,b) ((a)<(b)?(a):(b))
#define MAX(a,b) ((a)>(b)?(a):(b))
#endif

#define	XWS_MAX_BUFFERS MAX(XWS_FLIP_BUFFERS, XWS_BLIT_BUFFERS)
#define	DRI2_MAX_BUFFERS_NUM  	XWS_MAX_BUFFERS


#define __DRI_BUFFER_EMPTY 103

/** Used for ugly ugly ugly swap interval passing to dri2 driver and receiving current frame index */
#define __DRI_BUFFER_PVR_CTRL  	  0x80 /* 100000XX <- last 2 bits for swap interval value */
#define __DRI_BUFFER_PVR_CTRL_RET 0x90 /* 11000000 */



#define DRI2_BACK_BUFFER_EXPORT_TYPE_BUFFERS 1
#define DRI2_BACK_BUFFER_EXPORT_TYPE_SWAPCHAIN 2

#define UNREFERENCED_PARAMETER(x) (x) = (x)


/*
 * Structure used to pass information about back buffers between client application and
 * X.Org. Watch out for equivalent structure in pvr_video lib 
 */
typedef struct _PVRDRI2BackBuffersExport_
{
	/* Type of export. _BUFFERS mean set of handles, _SWAPCHAIN mean Swap chain ID */
	unsigned int	ui32Type;   
	PVR2D_HANDLE 	hBuffers[DRI2_MAX_BUFFERS_NUM];
	unsigned int	ui32BuffersCount;
	unsigned int	ui32SwapChainID;
} PVRDRI2BackBuffersExport;

/*
// Private window system display information
*/
typedef struct DRI2WS_Display_TAG
{
	unsigned int			ui32RefCount;
	
	Display				*display;
	int				screen;
	unsigned int			ui32Flags;

	unsigned int			ui32Width;
	unsigned int			ui32Height;
	unsigned int			ui32StrideInBytes;
	unsigned int			ui32BytesPerPixel;
	WSEGLPixelFormat		ePixelFormat;

	PVR2DFORMAT				ePVR2DPixelFormat;
	PVR2DCONTEXTHANDLE		hContext;
	PVR2DMEMINFO			*psMemInfo;

	int				iDRMfd;
} DRI2WSDisplay;


typedef enum DRI2WS_DrawableType_TAG 
{
	DRI2_DRAWABLE_UNKNOWN = 0,
	DRI2_DRAWABLE_WINDOW = 1,
	DRI2_DRAWABLE_PIXMAP = 2,
} DRI2WS_DrawableType;


/*
// Private window system drawable information
*/
typedef struct DRI2WS_Drawable_TAG
{
	DRI2WS_DrawableType	eDrawableType;

	Window				nativeWin;

	/** Index of current render-to back buffer (received from Xserver) */
	unsigned int		ui32BackBufferCurrent;

	/** Number of buffers */
	unsigned int		ui32BackBufferNum;

	/** Swap interval (works only in fliping/fullscreen case, values 0-3) */
	unsigned int		ui32SwapInterval;

	/** PVR2D Handles received from Xserver (back buffers export structure) */
	PVR2D_HANDLE		hPVR2DBackBufferExport;

	/** Stamp of current back buffer */
	unsigned char		ucBackBufferExportStamp;

	/** Array of PVR2D Handles received from Xserver (our back buffers) */
	PVR2D_HANDLE		hPVR2DBackBuffer[XWS_MAX_BUFFERS];

	/** Array of PVR2D mapped back buffers */
	PVR2DMEMINFO 		*psMemBackBuffer[XWS_MAX_BUFFERS];

	/** Stamp of current back buffer */
	unsigned char		ucFrontBufferStamp;

	/** Array of PVR2D Handles received from Xserver (our back buffers) */
	PVR2D_HANDLE		hPVR2DFrontBuffer;

	/** Array of PVR2D mapped back buffers */
	PVR2DMEMINFO 		*psMemFrontBuffer;

	/** ID of flip/swap chain received from X.Org */
	unsigned int		ui32FlipChainID;

	/** PVR2D Handle of flip chain used to get buffers to draw to */
	PVR2DFLIPCHAINHANDLE 	hFlipChain;

	int					iWidth;
	int					iHeight;

	WSEGLPixelFormat	ePixelFormat;
	unsigned int		ui32BytesPerPixel;
	unsigned int		ui32StrideInPixels;
	unsigned int		ui32StrideInBytes;
	PVR2DFORMAT			ePVR2DPixelFormat;

	DRI2WSDisplay		*psXWSDisplay;

} DRI2WSDrawable;

#endif /* __DRI2_WS_H__ */
