#ifndef APE_APELINK_H
#define APE_APELINK_H

#include "IO.h"
#include "APEInfo.h"

class CAPELink
{
public:
    CAPELink(const char * pFilename);
    ~CAPELink();

public:

    int m_nStartBlock;
    int m_nFinishBlock;
    char m_cImageFile[MAX_PATH];
};

#endif /* APE_APELINK_H */
