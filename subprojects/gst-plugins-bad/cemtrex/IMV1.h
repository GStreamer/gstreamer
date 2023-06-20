//////////////////////////////////////////////////////////////////
//																//
// IMV1.h - header of the IMV1-SDK API							//
// -----------------------------------							//
//																//
// Rev: 2.4.3.0													//
//																//
// Copyright 2000-2018 ImmerVision Canada Inc.					//
//																//
//////////////////////////////////////////////////////////////////

#ifndef IMV1_LIBRARY_001_H__
#define IMV1_LIBRARY_001_H__

typedef struct 
{
	unsigned long width ;
	unsigned long height ;
	unsigned long frameX ;
	unsigned long frameY ;
	unsigned long frameWidth ;
	unsigned long frameHeight ;
	unsigned char *data ;
} IMV_Buffer ;

typedef struct{
	float x;
	float y;
} Vertex2D;

typedef struct 
{
	char *RPL;
	char *Name;
} SLensDescription;

class IMV_Defs
{
public :
	enum
	{
		E_RGB_16 = 0,
		E_RGB_24,
		E_RGB_32,
		E_RGB_16_STD,
		E_RGB_24_STD,
		E_RGB_32_STD,
		E_YUV_YV12,
		E_YUV_IYUV,
		E_YUV_I420,
		E_YUV_YV12_STD,
		E_YUV_IYUV_STD,
		E_YUV_I420_STD,
		E_YUV_UYVY,
		E_YUV_YUYV,
		E_YUV_YVYU,
		E_YUV_YUY2,
		E_YUV_UYVY_STD,
		E_YUV_YUYV_STD,
		E_YUV_YVYU_STD,
		E_YUV_YUY2_STD,
		E_ARGB_32,
		E_ARGB_32_STD,
		E_RGBA_32,
		E_RGBA_32_STD,
		E_YUV_NV12,
		E_YUV_NV12_STD,
		E_YUV_NV21,
		E_YUV_NV21_STD,
		E_BGR_16,
		E_BGR_24,
		E_BGR_32,
		E_BGR_16_STD,
		E_BGR_24_STD,
		E_BGR_32_STD,
		E_ABGR_32,
		E_ABGR_32_STD,
		E_BGRA_32,
		E_BGRA_32_STD,
	} eColorFormat ;

	enum
	{
		E_OBUF_TOPBOTTOM = 256,
		E_OBUF_BOTTOMUP = 512

	} eOutputBufferOrientation;

	enum
	{
		E_VTYPE_PTZ = 0,
		E_VTYPE_QUAD = 1,
		E_VTYPE_PERI = 2,
		E_VTYPE_PERI_CUSTOM = 4,
		E_VTYPE_VERTICAL_SELFIE = 5,
		
		E_VTYPE_ZOOM = 4096 //mask to enable zoom functionality in perimeters view 
	} eViewType ;
	
	enum
	{
		E_CPOS_WALL = 0,
		E_CPOS_CEILING,
		E_CPOS_GROUND
	} eCameraPosition ;
	
	enum
	{
		E_COOR_ABSOLUTE = 0,
		E_COOR_RELATIVE
	} eCoordinates ;
	
	enum
	{
		E_ERR_OK	= 0,
		E_ERR_OUTOFMEMORY,
		E_ERR_NOBUFFER,		
		E_ERR_VTYPEINVALID,
		E_ERR_CPOSINVALID,
		E_ERR_COLORINVALID,
		E_ERR_IBUFINVALID,
		E_ERR_OBUFINVALID,
		E_ERR_INDEXINVALID,
		E_ERR_NOTINITALIZED,
		E_ERR_NOTPANOMORPH,
		E_ERR_PARAMINVALID,
		E_ERR_NOTALLOWED,
		E_ERR_VERSIONINVALID,
		E_ERR_QRCODEINVALID,
		E_ERR_NOPARAMETERS,
		E_ERR_NAVTYPEINVALID
	} eErrorCode ;
	
	enum
	{
		E_WAR_ACS_OK				= 0,
		E_WAR_ACS_NOTDETECTED		= 1,
		E_WAR_ACS_CROPPEDLEFT		= 2,
		E_WAR_ACS_CROPPEDRIGHT		= 4,
		E_WAR_ACS_CROPPEDTOP		= 8,
		E_WAR_ACS_CROPPEDBOTTOM		= 16,
		E_WAR_ACS_NOTCENTERED		= 32,
		E_WAR_ACS_ROTATED			= 64
	} eACSStatusCode ;
	
	enum
	{
		E_FILTER_NONE = 0,
		E_FILTER_BILINEAR,
		E_FILTER_BILINEAR_ONSTOP
	}
	eFilterType;
	
	enum
	{
		E_PROJ_LINEAR = 0,
		E_PROJ_SCENIC
	}
	eProjectionType;
	
	enum
	{
		E_BACK_NONE = 0,
		E_BACK_LINES,
	}
	eBackgroundType;
	
	enum
	{
		E_NAV_360xFOV_LOCKED = 0, 
		E_NAV_360x360_STABILIZED, 
		E_NAV_360x360_STABILIZED_LOCKED
	} eNavigationType;
} ;



class IMV_Camera;

class IMV_CameraInterface
{
protected :
	IMV_Camera* camera;

public :
	IMV_CameraInterface() ;
	~IMV_CameraInterface() ;
	unsigned long CheckCameraType( IMV_Buffer *buffer, unsigned long rgbFormat) ;

	char* GetACS();
	unsigned long GetACSStatus() ;
	char* GetACSStatusString(int eACSStatusCode) ;
	unsigned long GetBackground(unsigned long *backgroundType);	
	unsigned long GetCameraPosition( unsigned long *cameraPosition) ;
	unsigned long GetCameraRotation( float *pan, float *tilt, float *roll) ;
	unsigned long GetDisplayPanLimits( float *panMin, float *panMax);
	unsigned long GetDisplayTiltLimits( float *tiltMin, float *tiltMax);
	char* GetErrorString(int eErrorCode);
	unsigned long GetFiltering( unsigned long *filtering);
	unsigned long GetMarkersInfo(char* tag, void* value, int* nbBytes);
	static unsigned long GetMarkersInfo(IMV_Buffer* buf, unsigned long colorFormat,char* tag, void* value, int* nbBytes);
	unsigned long GetNavigationType( unsigned long *navigationType);
	unsigned long GetPanLimits( float *panMin, float *panMax);
	unsigned long GetPosition( float *pan, float *tilt, float *zoom, unsigned long viewIndex = 1);
	unsigned long GetPosition( float *pan, float *tilt, float *roll, float *zoom, unsigned long viewIndex = 1);
	unsigned long GetProjectionType(unsigned long *projectionType, float *strength);
	unsigned long GetTiltLimits( float *tiltMin, float *tiltMax);
	static const char* GetVersion();
	unsigned long GetViewType( unsigned long *viewType) ;
	unsigned long GetZoomLimits( float *zoomMin, float *zoomMax) ;
	
	unsigned long RestoreMarkersReading();
	
	unsigned long SetACS(char *acs);
	unsigned long SetBackground(unsigned long backgroundType);
	unsigned long SetCameraPosition( unsigned long cameraPosition) ;
	unsigned long SetCameraRotation( float *pan, float *tilt, float *roll) ;
	unsigned long SetFiltering( unsigned long filtering);
	unsigned long SetInputVideoParams( IMV_Buffer *inputBuffer) ;
	unsigned long SetLens(char* RPL);
	unsigned long SetNavigationType( unsigned long navigationType);
	unsigned long SetOutputVideoParams( IMV_Buffer *outputBuffer) ;
	unsigned long SetPanLimits( float panMin, float panMax);
	unsigned long SetPosition( float *pan, float *tilt, float *zoom, unsigned long coordinates = IMV_Defs::E_COOR_ABSOLUTE, unsigned long viewIndex=1) ;
	unsigned long SetPosition( float *pan, float *tilt, float *roll, float *zoom, unsigned long coordinates = IMV_Defs::E_COOR_ABSOLUTE, unsigned long viewIndex=1) ;
	unsigned long SetProjectionType(unsigned long projectionType, float *strength);
	unsigned long SetTiltLimits( float tiltMin, float tiltMax);
	unsigned long SetVideoParams( IMV_Buffer *inputBuffer, IMV_Buffer *outputBuffer, unsigned long colorFormat, unsigned long viewType, unsigned long cameraPosition) ;
	unsigned long SetViewType( unsigned long viewType) ;
	unsigned long SetZoomLimits( float zoomMin, float zoomMax) ;
	
	unsigned long Update() ;
	
	unsigned long SetThreadCount(int nbThreads);

	//tracking functions
	unsigned long GetPositionFromInputVideoPoint(int xInputVideo,int yInputVideo, float *pan, float *tilt);
	unsigned long GetPositionFromOutputVideoPoint(int xOutputVideo,int yOutputVideo, float *pan, float *tilt);

	unsigned long GetPositionFromInputVideoPolygon(int nbPts, int *xInputVideo,int *yInputVideo, float *pan, float *tilt,float *zoom);
	unsigned long GetPositionFromOutputVideoPolygon(int nbPts,int *xOutputVideo,int *yOutputVideo, float *pan, float *tilt,float *zoom);

	unsigned long GetPositionFromInputVideoPolygon(int nbPts,int *xInputVideo,int *yInputVideo, int widthDestinationViewer, int heightDestinationViewer, float *pan, float *tilt,float *zoom);
	unsigned long GetPositionFromOutputVideoPolygon(int nbPts,int *xOutputVideo,int *yOutputVideo, int widthDestinationViewer, int heightDestinationViewer, float *pan, float *tilt,float *zoom);

	unsigned long GetInputVideoPointFromPosition(float pan, float tilt, int *xInputVideo,int *yInputVideo);
	unsigned long GetOutputVideoPointFromPosition(float pan, float tilt, int *xOutputVideo,int *yOutputVideo, unsigned long viewIndex = 1);
	
	unsigned long GetPositionFrom3D(float x,float y,float z, float *pan, float *tilt);
	unsigned long Get3DFromPosition(float pan, float tilt,float *x,float *y,float *z);
	//------------------
	
	//RectangleSurface functions
	unsigned long AddRectangleSurface(IMV_Buffer *inputBuffer, int *rectangleSurfaceIndex) ;
	unsigned long RemoveRectangleSurface(int rectangleSurfaceIndex);

	unsigned long SetRectangleSurfaceInputBuffer(int rectangleSurfaceIndex, IMV_Buffer *inputBuffer) ;

	unsigned long SetRectangleSurfacePosition(int rectangleSurfaceIndex, float pan1,float tilt1,float pan2,float tilt2,float pan3,float tilt3,float pan4,float tilt4);
	unsigned long SetRectangleSurfacePosition(int rectangleSurfaceIndex, float x1,float y1,float z1,float x2,float y2,float z2,float x3,float y3,float z3,float x4,float y4,float z4);

	unsigned long GetRectangleSurfacePosition(int rectangleSurfaceIndex, float *pan1,float *tilt1,float *pan2,float *tilt2,float *pan3,float *tilt3,float *pan4,float *tilt4);
	unsigned long GetRectangleSurfacePosition(int rectangleSurfaceIndex, float *x1,float *y1,float *z1,float *x2,float *y2,float *z2,float *x3,float *y3,float *z3,float *x4,float *y4,float *z4);

	unsigned long ShowRectangleSurface(int rectangleSurfaceIndex, bool visible=true);
	//------------------
	
	// Static Methods
	static int StaticGetLensDescriptionCount();
	static const SLensDescription* StaticGetLensDescription();
} ;

class IMV_CameraFlatSurfaceInterface: public IMV_CameraInterface
{
public :

	IMV_CameraFlatSurfaceInterface() ;
		
	unsigned long GetFlatSurfaceModel(int flatSurfaceIndex, int* nbVertex2D,Vertex2D** v1, Vertex2D **txCoords);
	unsigned long GetRectangleSurface(int numMesh, int* nbVertex2D, int rectangleSurfaceIndex,Vertex2D** v1, Vertex2D **txCoords);
} ;


#endif
// EOF
