#include "All.h"
#include "GlobalFunctions.h"
#include "NNFilter.h"

CNNFilter::CNNFilter(int nOrder, int nShift)
{
    if ( nOrder <= 0  ||  (nOrder & 15) != 0 )
        throw (1);
    m_nOrder    = nOrder;
    m_nShift    = nShift;
    m_nRoundAdd = 1 << (nShift - 1);

    m_bMMXAvailable = GetMMXAvailable();

    m_rbInput  . Create ( NN_WINDOW_ELEMENTS, m_nOrder );
    m_rbDeltaM . Create ( NN_WINDOW_ELEMENTS, m_nOrder );
    m_paryM = new short [m_nOrder];

    if ( ( (int) & m_rbInput [0] ) & 7 )
        fprintf ( stderr, "m_rbInput misaligned, performance loss\n" );
    if ( ( (int) & m_rbDeltaM [0] ) & 7 )
        fprintf ( stderr, "m_rbDeltaM misaligned, performance loss\n" );
    if ( ( (int) & m_paryM [0] ) & 7 )
        fprintf ( stderr, "m_paryM misaligned, performance loss\n" );
}

CNNFilter::~CNNFilter()
{
    SAFE_ARRAY_DELETE(m_paryM)
}

void CNNFilter::Flush()
{
    memset(&m_paryM[0], 0, m_nOrder * sizeof(short));
    m_rbInput.Flush();
    m_rbDeltaM.Flush();
}

short CNNFilter ::
GetSaturatedShortFromInt ( int nValue )
{
    if ( (short) nValue != nValue )
        nValue = (nValue >> 31) ^ 0x7FFF;
    return (short) nValue;
}

int CNNFilter::Compress(int nInput)
{
    int  nDotProduct;
    int  nOutput;

    // convert the input to a short and store it
    m_rbInput[0] = GetSaturatedShortFromInt ( nInput );

    //fprintf ( stderr, "Enter Dot\n" );
    nDotProduct = CalculateDotProduct ( & m_rbInput [-m_nOrder], & m_paryM [0], m_nOrder );              // figure a dot product
    //fprintf ( stderr, "Leave Dot\n" );

    // calculate the output
    nOutput = nInput - ((nDotProduct + m_nRoundAdd) >> m_nShift);

    // adapt
    //fprintf ( stderr, "Enter Adapt\n" );
    Adapt ( & m_paryM [0], & m_rbDeltaM [-m_nOrder], -nOutput, m_nOrder );
    //fprintf ( stderr, "Leave Adapt\n" );
    m_rbDeltaM[ 0]   = (nInput == 0) ? 0 : ((nInput >> 28) & 8) - 4;    // -4, 0, +4
    m_rbDeltaM[-4] >>= 1;
    m_rbDeltaM[-8] >>= 1;

    // increment and roll if necessary
    m_rbInput .IncrementSafe ();
    m_rbDeltaM.IncrementSafe ();

    return nOutput;
}


int CNNFilter::Decompress(int nInput)
{
    int  nDotProduct;
    int  nOutput;

    //fprintf ( stderr, "Enter Dot\n" );
    nDotProduct = CalculateDotProduct ( & m_rbInput [-m_nOrder], & m_paryM [0], m_nOrder );     // figure a dot product
    //fprintf ( stderr, "Leave Dot\n" );

    // adapt
    //fprintf ( stderr, "Enter Adapt\n" );
    Adapt ( & m_paryM [0], & m_rbDeltaM [-m_nOrder], -nInput, m_nOrder );
    //fprintf ( stderr, "Leave Adapt\n" );

    // store the output value
    nOutput = nInput + ((nDotProduct + m_nRoundAdd) >> m_nShift);

    // update the input buffer
    m_rbInput [ 0]   = GetSaturatedShortFromInt (nOutput);

    m_rbDeltaM[ 0]   = (nOutput == 0) ? 0 : ((nOutput >> 28) & 8) - 4;  // -4, 0, +4
    m_rbDeltaM[-4] >>= 1;
    m_rbDeltaM[-8] >>= 1;

    // increment and roll if necessary
    m_rbInput .IncrementSafe ();
    m_rbDeltaM.IncrementSafe ();

    return nOutput;
}


#ifndef __GNUC_IA32__

void CNNFilter ::
Adapt ( short* pM, const short* pAdapt, int nDirection, int nOrder )
{

#ifdef ENABLE_ASSEMBLY

    if ( m_bMMXAvailable ) {
        _asm {
            mov  eax, pM
            mov  ecx, pAdapt
            mov  edx, nOrder
            shr  edx, 4

            cmp  nDirection, 0
            jle  AdaptSub

AdaptAddLoop:
            movq  mm0, [eax]
            paddw mm0, [ecx]
            movq  [eax], mm0
            movq  mm1, [eax + 8]
            paddw mm1, [ecx + 8]
            movq  [eax + 8], mm1
            movq  mm2, [eax + 16]
            paddw mm2, [ecx + 16]
            movq  [eax + 16], mm2
            movq  mm3, [eax + 24]
            paddw mm3, [ecx + 24]
            movq  [eax + 24], mm3
            add   eax, 32
            add   ecx, 32
            dec   edx
            jnz   AdaptAddLoop

            emms
            jmp   AdaptDone

  AdaptSub: je    AdaptDone

AdaptSubLoop:
            movq  mm0, [eax]
            psubw mm0, [ecx]
            movq  [eax], mm0
            movq  mm1, [eax + 8]
            psubw mm1, [ecx + 8]
            movq  [eax + 8], mm1
            movq  mm2, [eax + 16]
            psubw mm2, [ecx + 16]
            movq  [eax + 16], mm2
            movq  mm3, [eax + 24]
            psubw mm3, [ecx + 24]
            movq  [eax + 24], mm3
            add   eax, 32
            add   ecx, 32
            dec   edx
            jnz   AdaptSubLoop

            emms
AdaptDone:
        }

        return;
    }

#endif /* ENABLE_ASSEMBLY */

    if      ( nDirection > 0 ) {
        nOrder >>= 4;
        while ( nOrder-- ) {
            EXPAND_16_TIMES (*pM++ += *pAdapt++;)
        }
    }
    else if ( nDirection < 0 ) {
        nOrder >>= 4;
        while ( nOrder-- ) {
            EXPAND_16_TIMES (*pM++ -= *pAdapt++;)
        }
    }
}

int CNNFilter ::
CalculateDotProduct ( const short* pA, const short* pB, int nOrder )
{
    int  nDotProduct;

#ifdef ENABLE_ASSEMBLY

    if ( m_bMMXAvailable ) {
        _asm {
            mov     eax, pA
            mov     ecx, pB
            mov     edx, nOrder
            shr     edx, 4
            pxor    mm7, mm7

   loopDot: movq    mm0, [eax]
            pmaddwd mm0, [ecx]
            paddd   mm7, mm0
            movq    mm1, [eax +  8]
            pmaddwd mm1, [ecx +  8]
            paddd   mm7, mm1
            movq    mm2, [eax + 16]
            pmaddwd mm2, [ecx + 16]
            paddd   mm7, mm2
            movq    mm3, [eax + 24]
            pmaddwd mm3, [ecx + 24]
            add     eax, 32
            add     ecx, 32
            paddd   mm7, mm3
            dec     edx
            jnz     loopDot

            movq    mm6, mm7                    // mm7 has the final dot-product (split into two dwords)
            psrlq   mm7, 32
            paddd   mm6, mm7
            movd    nDotProduct, mm6

            emms                                // this emms may be unnecessary, but it's probably better to be safe...
        }

        return nDotProduct;
    }

#endif /* ENABLE_ASSEMBLY */

    nDotProduct = 0;
    nOrder    >>= 4;

    while ( nOrder-- ) {
        EXPAND_16_TIMES ( nDotProduct += *pA++ * *pB++; )
    }

    return nDotProduct;
}

#endif /* __GNUC_IA32__ */

/* end of NNFilter.cpp */
