#ifndef APE_NNFILTER_H
#define APE_NNFILTER_H

#include "RollBuffer.h"
#define NN_WINDOW_ELEMENTS  512

#ifdef __GNUC_IA32__
extern "C" {
    __inline void   Adapt                    ( short* pM, const short* pAdapt, int nDirection, int nOrder );
    __inline int    CalculateDotProduct      ( const short* pA, const short* pB, int nOrder );
};
#endif

class CNNFilter
{
public:

    CNNFilter(int nOrder, int nShift);
    ~CNNFilter();

    int Compress(int nInput);
    int Decompress(int nInput);
    void Flush();

private:

    int m_nOrder;
    int m_nShift;
    int m_nRoundAdd;
    BOOL m_bMMXAvailable;

    CRollBuffer<short> m_rbInput;
    CRollBuffer<short> m_rbDeltaM;

    short * m_paryM;

    __inline short  GetSaturatedShortFromInt ( int nValue );
#ifndef __GNUC_IA32__
    __inline void   Adapt                    ( short* pM, const short* pAdapt, int nDirection, int nOrder );
    __inline int    CalculateDotProduct      ( const short* pA, const short* pB, int nOrder );
#endif
};

#endif /* APE_NNFILTER_H */
