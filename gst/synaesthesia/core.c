/* Synaesthesia - program to display sound graphically
   Copyright (C) 1997  Paul Francis Harrison

  This program is free software; you can redistribute it and/or modify it
  under the terms of the GNU General Public License as published by the
  Free Software Foundation; either version 2 of the License, or (at your
  option) any later version.

  This program is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  General Public License for more details.

  You should have received a copy of the GNU General Public License along
  with this program; if not, write to the Free Software Foundation, Inc.,
  675 Mass Ave, Cambridge, MA 02139, USA.

  The author may be contacted at:
    pfh@yoyo.cc.monash.edu.au
  or
    27 Bond St., Mt. Waverley, 3149, Melbourne, Australia
*/

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>

#include <core.h>

inline int bitReverser(int i) {
  int sum=0,j;
  for(j=0;j<SYNA_BITS;j++) {
    sum = (i&1)+sum*2;
    i >>= 1;
  }
  return sum;
}

static void fft(struct syna_priv *sp,double *x,double *y) {
  int n2 = SYNA_SIZE, n1;
  int twoToTheK;
  int i,j;
  for(twoToTheK=1;twoToTheK<SYNA_SIZE;twoToTheK*=2) {
    n1 = n2;
    n2 /= 2;
    for(j=0;j<n2;j++) {
      double c = sp->cosTable[j*twoToTheK&(SYNA_SIZE-1)], 
             s = sp->negSinTable[j*twoToTheK&(SYNA_SIZE-1)];
      for(i=j;i<SYNA_SIZE;i+=n1) {
        int l = i+n2;
        double xt = x[i] - x[l];
	double yt = y[i] - y[l];
	x[i] = (x[i] + x[l]);
	y[i] = (y[i] + y[l]);
	x[l] = xt*c - yt*s;
	y[l] = xt*s + yt*c;
      }
    }
  }
}

void coreInit(struct syna_priv *sp,int w,int h) {
  gint i;

  for(i=0;i<SYNA_SIZE;i++) {
    sp->negSinTable[i] = -sin(3.141592*2.0/SYNA_SIZE*i);
    sp->cosTable[i] = cos(3.141592*2.0/SYNA_SIZE*i);
    sp->bitReverse[i] = bitReverser(i);
  }

  sp->outWidth = w;
  sp->outHeight = h;

  sp->output = g_malloc(w*h);
  sp->lastOutput = g_malloc(w*h);
  sp->lastLastOutput = g_malloc(w*h);
  memset(sp->output,0,w*h);
  memset(sp->lastOutput,0,w*h);
  memset(sp->lastLastOutput,0,w*h);

  sp->fadeMode = FADE_STARS;
  sp->pointsAreDiamonds = TRUE;
  sp->brightnessTwiddler = 0.33;
  sp->starSize = 0.125;
  sp->fgRedSlider = 0.0;
  sp->fgGreenSlider = 0.5;
  sp->bgRedSlider = 1.0;
  sp->bgGreenSlider = 0.2;
}

void setStarSize(struct syna_priv *sp,gdouble size) {
  gdouble fadeModeFudge = (sp->fadeMode == FADE_WAVE ? 0.4 : 
                          (sp->fadeMode == FADE_FLAME ? 0.6 : 0.78));

  gint factor;
  gint i;
  if (size > 0.0)
    factor = (int)(exp(log(fadeModeFudge) / (size*8.0))*255);
  else
    factor = 0;

  if (factor > 255) factor = 255;

  for(i=0;i<256;i++)
    sp->scaleDown[i] = i*factor>>8;

  sp->maxStarRadius = 1;
  for(i=255;i;i = sp->scaleDown[i])
    sp->maxStarRadius++;
}

inline void addPixel(struct syna_priv *sp,int x,int y,int br1,int br2)
{
  unsigned char *p;

  if (x < 0 || x >= sp->outWidth || y < 0 || y >= sp->outHeight) return;

  p = sp->output+x*2+y*sp->outWidth*2;
  if (p[0] < 255-br1) p[0] += br1; else p[0] = 255;
  if (p[1] < 255-br2) p[1] += br2; else p[1] = 255;
}

inline void addPixelFast(unsigned char *p,int br1,int br2) {
  if (p[0] < 255-br1) p[0] += br1; else p[0] = 255;
  if (p[1] < 255-br2) p[1] += br2; else p[1] = 255;
}

void fadeFade(struct syna_priv *sp) {
  register unsigned long *ptr = (unsigned long*)sp->output;
  int i = sp->outWidth*sp->outHeight*2/4;
  do {
    if (*ptr)
      *(ptr++) -= ((*ptr & 0xf0f0f0f0ul) >> 4) + ((*ptr & 0xe0e0e0e0ul) >> 5);
    else
      ptr++;
  } while(--i > 0);
}
 
inline unsigned char getPixel(struct syna_priv *sp,int x,int y,int where) {
  if (x < 0 || y < 0 || x >= sp->outWidth || y >= sp->outHeight) return 0;
  return sp->lastOutput[where];
}

inline void fadePixelWave(struct syna_priv *sp,int x,int y,int where,int step) {
  short j = 
    (short)((getPixel(sp,x-1,y,where-2)+
             getPixel(sp,x+1,y,where+2)+
             getPixel(sp,x,y-1,where-step)+
             getPixel(sp,x,y+1,where+step)) >> 2)
    + sp->lastOutput[where];
  if (!j) { sp->output[where] = 0; return; }
  j = j - sp->lastLastOutput[where] - 1;
  if (j < 0) sp->output[where] = 0;
  else if (j & (255*256)) sp->output[where] = 255;
  else sp->output[where] = j;
}

void fadeWave(struct syna_priv *sp) {
  int x,y,i,j,start,end;
  int step = sp->outWidth*2;
  unsigned char *t = sp->lastLastOutput;
  sp->lastLastOutput = sp->lastOutput;
  sp->lastOutput = sp->output;
  sp->output = t;

  for(x=0,i=0,j=sp->outWidth*(sp->outHeight-1)*2;x<sp->outWidth;x++,i+=2,j+=2) {
    fadePixelWave(sp,x,0,i,step);
    fadePixelWave(sp,x,0,i+1,step);
    fadePixelWave(sp,x,sp->outHeight-1,j,step);
    fadePixelWave(sp,x,sp->outHeight-1,j+1,step);
  }

  for(y=1,i=sp->outWidth*2,j=sp->outWidth*4-2;y<sp->outHeight;y++,i+=step,j+=step) {
    fadePixelWave(sp,0,y,i,step);
    fadePixelWave(sp,0,y,i+1,step);
    fadePixelWave(sp,sp->outWidth-1,y,j,step);
    fadePixelWave(sp,sp->outWidth-1,y,j+1,step);
  }

  for(y=1,
      start=sp->outWidth*2+2,
      end=sp->outWidth*4-2; y<sp->outHeight-1; y++,start+=step,end+=step) {
    int i = start;
    do {
      short j = 
	(short)((sp->lastOutput[i-2]+
                 sp->lastOutput[i+2]+
                 sp->lastOutput[i-step]+
                 sp->lastOutput[i+step]) >> 2)
        + sp->lastOutput[i];
      if (!j) {
        sp->output[i] = 0; 
      } else {
        j = j - sp->lastLastOutput[i] - 1;
        if (j < 0) sp->output[i] = 0;
        else if (j & (255*256)) sp->output[i] = 255;
        else sp->output[i] = j; 
      }
    } while(++i < end);
  }
}

inline void fadePixelHeat(struct syna_priv *sp,int x,int y,int where,int step) {
  short j = 
    (short)((getPixel(sp,x-1,y,where-2)+
             getPixel(sp,x+1,y,where+2)+
             getPixel(sp,x,y-1,where-step)+
             getPixel(sp,x,y+1,where+step)) >> 2)
    + sp->lastOutput[where];
  if (!j) { sp->output[where] = 0; return; }
  j = j - sp->lastLastOutput[where] - 1;
  if (j < 0) sp->output[where] = 0;
  else if (j & (255*256)) sp->output[where] = 255;
  else sp->output[where] = j;
}

void fadeHeat(struct syna_priv *sp) {
  int x,y,i,j,start,end;
  int step = sp->outWidth*2;
  unsigned char *t = sp->lastLastOutput;
  sp->lastLastOutput = sp->lastOutput;
  sp->lastOutput = sp->output;
  sp->output = t;

  for(x=0,i=0,j=sp->outWidth*(sp->outHeight-1)*2;x<sp->outWidth;x++,i+=2,j+=2) {
    fadePixelHeat(sp,x,0,i,step);
    fadePixelHeat(sp,x,0,i+1,step);
    fadePixelHeat(sp,x,sp->outHeight-1,j,step);
    fadePixelHeat(sp,x,sp->outHeight-1,j+1,step);
  }

  for(y=1,i=sp->outWidth*2,j=sp->outWidth*4-2;y<sp->outHeight;y++,i+=step,j+=step) {
    fadePixelHeat(sp,0,y,i,step);
    fadePixelHeat(sp,0,y,i+1,step);
    fadePixelHeat(sp,sp->outWidth-1,y,j,step);
    fadePixelHeat(sp,sp->outWidth-1,y,j+1,step);
  }

  for (y=1,start=sp->outWidth*2+2,
       end=sp->outWidth*4-2; y<sp->outHeight-1; y++,start+=step,end+=step) {
    int i = start;
    do {
      short j = 
	(short)((sp->lastOutput[i-2]+
                 sp->lastOutput[i+2]+
                 sp->lastOutput[i-step]+
                 sp->lastOutput[i+step]) >> 2)
	+ sp->lastOutput[i];
      if (!j) { 
        sp->output[i] = 0; 
      } else {
        j = j - sp->lastLastOutput[i] +
            (sp->lastLastOutput[i] - ((sp->lastOutput[i])>>2)) - 1;
        if (j < 0) sp->output[i] = 0;
        else if (j & (255*256)) sp->output[i] = 255;
        else sp->output[i] = j; 
      }
    } while(++i < end);
  }
}

void fade(struct syna_priv *sp) { 
  switch(sp->fadeMode) {
    case FADE_STARS :
      fadeFade(sp); 
      break;
    case FADE_FLAME :
      fadeHeat(sp);
      break;
    case FADE_WAVE :
      fadeWave(sp);
      break;
    default:
      break;
  }
}

int coreGo(struct syna_priv *sp,guchar *data,gint len) {
  double x[SYNA_SIZE], y[SYNA_SIZE];
  double a[SYNA_SIZE], b[SYNA_SIZE];
  int clarity[SYNA_SIZE]; //Surround sound
  int i,j,k;
  int heightFactor = SYNA_SIZE / 2 / sp->outHeight + 1;
  int actualHeight = SYNA_SIZE / 2 / heightFactor;
  int heightAdd = sp->outHeight + (actualHeight >> 1);
  
  int brightFactor = (int)(150 * sp->brightnessTwiddler / (sp->starSize+0.01));
  double brightFactor2;

  for(i=0;i<SYNA_SIZE;i++) {
    x[i] = data[i*2];
    y[i] = data[i*2+1];
  }

  fft(sp,x,y);

  for(i=0 +1;i<SYNA_SIZE;i++) {
    double x1 = x[sp->bitReverse[i]], 
           y1 = y[sp->bitReverse[i]], 
           x2 = x[sp->bitReverse[SYNA_SIZE-i]], 
           y2 = y[sp->bitReverse[SYNA_SIZE-i]],
	   aa,bb;
    a[i] = sqrt(aa= (x1+x2)*(x1+x2) + (y1-y2)*(y1-y2) );
    b[i] = sqrt(bb= (x1-x2)*(x1-x2) + (y1+y2)*(y1+y2) );
    if (aa+bb != 0.0)
      clarity[i] = (int)(
        ( (x1+x2) * (x1-x2) + (y1+y2) * (y1-y2) )/(aa+bb) * 256 );
    else
      clarity[i] = 0;
  }
   
  /* Correct for window size */
  brightFactor2 = (brightFactor/65536.0/SYNA_SIZE)*
      sqrt(actualHeight*sp->outWidth/(320.0*200.0));

  for(i=1;i<SYNA_SIZE/2;i++) {
    if (a[i] > 0 || b[i] > 0) {
      int h = (int)( b[i]*sp->outWidth / (a[i]+b[i]) );
      int br1, br2, br = (int)( 
          (a[i]+b[i])*i*brightFactor2 );
      int px = h, 
          py = heightAdd - i / heightFactor;
      br1 = br*(clarity[i]+128)>>8;
      br2 = br*(128-clarity[i])>>8;
      if (br1 < 0) br1 = 0; else if (br1 > 255) br1 = 255;
      if (br2 < 0) br2 = 0; else if (br2 > 255) br2 = 255;

      if (sp->pointsAreDiamonds) {
        addPixel(sp,px,py,br1,br2);
        br1=sp->scaleDown[br1];br2=sp->scaleDown[br2];

        //TODO: Use addpixelfast
        for(j=1;br1>0||br2>0;j++,br1=sp->scaleDown[br1],
                             br2=sp->scaleDown[br2]) {
	  for(k=0;k<j;k++) {
	    addPixel(sp,px-j+k,py-k,br1,br2); 
	    addPixel(sp,px+k,py-j+k,br1,br2); 
	    addPixel(sp,px+j-k,py+k,br1,br2); 
	    addPixel(sp,px-k,py+j-k,br1,br2); 
	  }
	}
      } else {
	if (px < sp->maxStarRadius || py < sp->maxStarRadius || 
	    px > sp->outWidth-sp->maxStarRadius || 
            py > sp->outHeight-sp->maxStarRadius) {
	  addPixel(sp,px,py,br1,br2);
	  for (j=1;(br1>0) || (br2>0);j++,br1=sp->scaleDown[br1],
                                          br2=sp->scaleDown[br2]) {
	    addPixel(sp,px+j,py,br1,br2);
	    addPixel(sp,px,py+j,br1,br2);
	    addPixel(sp,px-j,py,br1,br2);
	    addPixel(sp,px,py-j,br1,br2);
	  }
	} else {
	  unsigned char *p = sp->output+px*2+py*sp->outWidth*2;
          unsigned char *p1=p, *p2=p, *p3=p, *p4=p;
	  addPixelFast(p,br1,br2);
	  for(;br1>0||br2>0;br1=sp->scaleDown[br1],br2=sp->scaleDown[br2]) {
	    p1 += 2;
	    addPixelFast(p1,br1,br2);
	    p2 -= 2;
	    addPixelFast(p2,br1,br2);
	    p3 += sp->outWidth*2;
	    addPixelFast(p3,br1,br2);
	    p4 -= sp->outWidth*2;
	    addPixelFast(p4,br1,br2);
	  }
	}
      }
    }
  }
  return 0;
}

void setupPalette(struct syna_priv *sp,guchar *palette) {
  #define BOUND(x) ((x) > 255 ? 255 : (x))
  #define PEAKIFY(x) (int)(BOUND((x) - (x)*(255-(x))/255/2))
#ifndef MAX
  #define MAX(x,y) ((x) > (y) ? (x) : (y))
#endif /* MAX */
  int i,f,b;

  double scale, fgRed, fgGreen, fgBlue, bgRed, bgGreen, bgBlue;
  fgRed = sp->fgRedSlider;
  fgGreen = sp->fgGreenSlider;
  fgBlue = 1.0 - MAX(fgRed,fgGreen);
  scale = MAX(MAX(fgRed,fgGreen),fgBlue);
  fgRed /= scale;
  fgGreen /= scale;
  fgBlue /= scale;

  bgRed = sp->bgRedSlider;
  bgGreen = sp->bgGreenSlider;
  bgBlue = 1.0 - MAX(sp->bgRedSlider,sp->bgGreenSlider);
  scale = MAX(MAX(bgRed,bgGreen),bgBlue);
  bgRed /= scale;
  bgGreen /= scale;
  bgBlue /= scale;

  for(i=0;i<256;i++) {
    f = i&15;
    b = i/16;
    palette[i*4+0] = PEAKIFY(b*bgRed*16+f*fgRed*16);
    palette[i*4+1] = PEAKIFY(b*bgGreen*16+f*fgGreen*16);
    palette[i*4+2] = PEAKIFY(b*bgBlue*16+f*fgBlue*16);
  }
}
