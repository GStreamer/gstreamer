/*###########################################################################
  # Copyright Intel Corporation
  #
  # SPDX-License-Identifier: MIT
  ###########################################################################*/

#ifndef __MFXVIDEOPLUSPLUS_H
#define __MFXVIDEOPLUSPLUS_H

#include "mfxvideo.h"

class MFXVideoSessionBase {
public:
    virtual ~MFXVideoSessionBase() {}

    virtual mfxStatus Init(mfxIMPL impl, mfxVersion* ver) = 0;
    virtual mfxStatus InitEx(mfxInitParam par) = 0;
    virtual mfxStatus Close(void) = 0;

    virtual mfxStatus QueryIMPL(mfxIMPL* impl) = 0;
    virtual mfxStatus QueryVersion(mfxVersion* version) = 0;

    virtual mfxStatus JoinSession(mfxSession child_session) = 0;
    virtual mfxStatus DisjoinSession() = 0;
    virtual mfxStatus CloneSession(mfxSession* clone) = 0;
    virtual mfxStatus SetPriority(mfxPriority priority) = 0;
    virtual mfxStatus GetPriority(mfxPriority* priority) = 0;

    virtual mfxStatus SetFrameAllocator(mfxFrameAllocator* allocator) = 0;
    virtual mfxStatus SetHandle(mfxHandleType type, mfxHDL hdl) = 0;
    virtual mfxStatus GetHandle(mfxHandleType type, mfxHDL* hdl) = 0;
    virtual mfxStatus QueryPlatform(mfxPlatform* platform) = 0;

    virtual mfxStatus SyncOperation(mfxSyncPoint syncp, mfxU32 wait) = 0;

    virtual mfxStatus GetSurfaceForEncode(mfxFrameSurface1** output_surf) = 0;
    virtual mfxStatus GetSurfaceForDecode(mfxFrameSurface1** output_surf) = 0;
    virtual mfxStatus GetSurfaceForVPP(mfxFrameSurface1** output_surf) = 0;
    virtual mfxStatus GetSurfaceForVPPOut(mfxFrameSurface1** output_surf) = 0;

    virtual operator mfxSession(void) = 0;
};

class MFXVideoENCODEBase {
public:
    virtual ~MFXVideoENCODEBase() {}

    virtual mfxStatus Query(mfxVideoParam* in, mfxVideoParam* out) = 0;
    virtual mfxStatus QueryIOSurf(mfxVideoParam* par, mfxFrameAllocRequest* request) = 0;
    virtual mfxStatus Init(mfxVideoParam* par) = 0;
    virtual mfxStatus Reset(mfxVideoParam* par) = 0;
    virtual mfxStatus Close(void) = 0;

    virtual mfxStatus GetVideoParam(mfxVideoParam* par) = 0;
    virtual mfxStatus GetEncodeStat(mfxEncodeStat* stat) = 0;

    virtual mfxStatus EncodeFrameAsync(mfxEncodeCtrl* ctrl,
        mfxFrameSurface1* surface,
        mfxBitstream* bs,
        mfxSyncPoint* syncp) = 0;

    virtual mfxStatus GetSurface(mfxFrameSurface1** output_surf) = 0;
};

class MFXVideoDECODEBase {
public:
    virtual ~MFXVideoDECODEBase() {}

    virtual mfxStatus Query(mfxVideoParam* in, mfxVideoParam* out) = 0;
    virtual mfxStatus DecodeHeader(mfxBitstream* bs, mfxVideoParam* par) = 0;
    virtual mfxStatus QueryIOSurf(mfxVideoParam* par, mfxFrameAllocRequest* request) = 0;
    virtual mfxStatus Init(mfxVideoParam* par) = 0;
    virtual mfxStatus Reset(mfxVideoParam* par) = 0;
    virtual mfxStatus Close(void) = 0;

    virtual mfxStatus GetVideoParam(mfxVideoParam* par) = 0;

    virtual mfxStatus GetDecodeStat(mfxDecodeStat* stat) = 0;
    virtual mfxStatus GetPayload(mfxU64* ts, mfxPayload* payload) = 0;
    virtual mfxStatus SetSkipMode(mfxSkipMode mode) = 0;
    virtual mfxStatus DecodeFrameAsync(mfxBitstream* bs,
        mfxFrameSurface1* surface_work,
        mfxFrameSurface1** surface_out,
        mfxSyncPoint* syncp) = 0;

    virtual mfxStatus GetSurface(mfxFrameSurface1** output_surf) = 0;
};

class MFXVideoVPPBase {
public:
    virtual ~MFXVideoVPPBase() {}

    virtual mfxStatus Query(mfxVideoParam* in, mfxVideoParam* out) = 0;
    virtual mfxStatus QueryIOSurf(mfxVideoParam* par, mfxFrameAllocRequest request[2]) = 0;
    virtual mfxStatus Init(mfxVideoParam* par) = 0;
    virtual mfxStatus Reset(mfxVideoParam* par) = 0;
    virtual mfxStatus Close(void) = 0;

    virtual mfxStatus GetVideoParam(mfxVideoParam* par) = 0;
    virtual mfxStatus GetVPPStat(mfxVPPStat* stat) = 0;
    virtual mfxStatus RunFrameVPPAsync(mfxFrameSurface1* in,
        mfxFrameSurface1* out,
        mfxExtVppAuxData* aux,
        mfxSyncPoint* syncp) = 0;

    virtual mfxStatus GetSurfaceIn(mfxFrameSurface1** output_surf) = 0;
    virtual mfxStatus GetSurfaceOut(mfxFrameSurface1** output_surf) = 0;
    virtual mfxStatus ProcessFrameAsync(mfxFrameSurface1* in, mfxFrameSurface1** out) = 0;
};

class MFXVideoSession : public MFXVideoSessionBase {
public:
    MFXVideoSession(void) {
        m_session = (mfxSession)0;
    }
    virtual ~MFXVideoSession(void) {
        Close();
    }

    virtual mfxStatus Init(mfxIMPL impl, mfxVersion *ver) override {
        return MFXInit(impl, ver, &m_session);
    }
    virtual mfxStatus InitEx(mfxInitParam par) override {
        return MFXInitEx(par, &m_session);
    }
    virtual mfxStatus Close(void) override {
        mfxStatus mfxRes;
        mfxRes    = MFXClose(m_session);
        m_session = (mfxSession)0;
        return mfxRes;
    }

    virtual mfxStatus QueryIMPL(mfxIMPL *impl) override {
        return MFXQueryIMPL(m_session, impl);
    }
    virtual mfxStatus QueryVersion(mfxVersion *version) override {
        return MFXQueryVersion(m_session, version);
    }

    virtual mfxStatus JoinSession(mfxSession child_session) override {
        return MFXJoinSession(m_session, child_session);
    }
    virtual mfxStatus DisjoinSession() override {
        return MFXDisjoinSession(m_session);
    }
    virtual mfxStatus CloneSession(mfxSession *clone) override {
        return MFXCloneSession(m_session, clone);
    }
    virtual mfxStatus SetPriority(mfxPriority priority) override {
        return MFXSetPriority(m_session, priority);
    }
    virtual mfxStatus GetPriority(mfxPriority *priority) override {
        return MFXGetPriority(m_session, priority);
    }

    virtual mfxStatus SetFrameAllocator(mfxFrameAllocator *allocator) override {
        return MFXVideoCORE_SetFrameAllocator(m_session, allocator);
    }
    virtual mfxStatus SetHandle(mfxHandleType type, mfxHDL hdl) override {
        return MFXVideoCORE_SetHandle(m_session, type, hdl);
    }
    virtual mfxStatus GetHandle(mfxHandleType type, mfxHDL *hdl) override {
        return MFXVideoCORE_GetHandle(m_session, type, hdl);
    }
    virtual mfxStatus QueryPlatform(mfxPlatform *platform) override {
        return MFXVideoCORE_QueryPlatform(m_session, platform);
    }

    virtual mfxStatus SyncOperation(mfxSyncPoint syncp, mfxU32 wait) override {
        return MFXVideoCORE_SyncOperation(m_session, syncp, wait);
    }

    virtual mfxStatus GetSurfaceForEncode(mfxFrameSurface1** output_surf) override {
        return MFXMemory_GetSurfaceForEncode(m_session, output_surf);
    }
    virtual mfxStatus GetSurfaceForDecode(mfxFrameSurface1** output_surf) override {
        return MFXMemory_GetSurfaceForDecode(m_session, output_surf);
    }
    virtual mfxStatus GetSurfaceForVPP   (mfxFrameSurface1** output_surf) override {
        return MFXMemory_GetSurfaceForVPP   (m_session, output_surf);
    }
    virtual mfxStatus GetSurfaceForVPPOut(mfxFrameSurface1** output_surf) override {
        return MFXMemory_GetSurfaceForVPPOut(m_session, output_surf);
    }

    virtual operator mfxSession(void) override {
        return m_session;
    }

protected:
    mfxSession m_session; // (mfxSession) handle to the owning session
private:
    MFXVideoSession(const MFXVideoSession &);
    void operator=(MFXVideoSession &);
};

class MFXVideoENCODE : public MFXVideoENCODEBase {
public:
    explicit MFXVideoENCODE(mfxSession session) {
        m_session = session;
    }
    virtual ~MFXVideoENCODE(void) {
        Close();
    }

    virtual mfxStatus Query(mfxVideoParam *in, mfxVideoParam *out) override {
        return MFXVideoENCODE_Query(m_session, in, out);
    }
    virtual mfxStatus QueryIOSurf(mfxVideoParam *par, mfxFrameAllocRequest *request) override {
        return MFXVideoENCODE_QueryIOSurf(m_session, par, request);
    }
    virtual mfxStatus Init(mfxVideoParam *par) override {
        return MFXVideoENCODE_Init(m_session, par);
    }
    virtual mfxStatus Reset(mfxVideoParam *par) override {
        return MFXVideoENCODE_Reset(m_session, par);
    }
    virtual mfxStatus Close(void) override {
        return MFXVideoENCODE_Close(m_session);
    }

    virtual mfxStatus GetVideoParam(mfxVideoParam *par) override {
        return MFXVideoENCODE_GetVideoParam(m_session, par);
    }
    virtual mfxStatus GetEncodeStat(mfxEncodeStat *stat) override {
        return MFXVideoENCODE_GetEncodeStat(m_session, stat);
    }

    virtual mfxStatus EncodeFrameAsync(mfxEncodeCtrl *ctrl,
                                       mfxFrameSurface1 *surface,
                                       mfxBitstream *bs,
                                       mfxSyncPoint *syncp) override {
        return MFXVideoENCODE_EncodeFrameAsync(m_session, ctrl, surface, bs, syncp);
    }

    virtual mfxStatus GetSurface(mfxFrameSurface1** output_surf) override {
        return MFXMemory_GetSurfaceForEncode(m_session, output_surf);
    }

protected:
    mfxSession m_session; // (mfxSession) handle to the owning session
};

class MFXVideoDECODE : public MFXVideoDECODEBase {
public:
    explicit MFXVideoDECODE(mfxSession session) {
        m_session = session;
    }
    virtual ~MFXVideoDECODE(void) {
        Close();
    }

    virtual mfxStatus Query(mfxVideoParam *in, mfxVideoParam *out) override {
        return MFXVideoDECODE_Query(m_session, in, out);
    }
    virtual mfxStatus DecodeHeader(mfxBitstream *bs, mfxVideoParam *par) override {
        return MFXVideoDECODE_DecodeHeader(m_session, bs, par);
    }
    virtual mfxStatus QueryIOSurf(mfxVideoParam *par, mfxFrameAllocRequest *request) override {
        return MFXVideoDECODE_QueryIOSurf(m_session, par, request);
    }
    virtual mfxStatus Init(mfxVideoParam *par) override {
        return MFXVideoDECODE_Init(m_session, par);
    }
    virtual mfxStatus Reset(mfxVideoParam *par) override {
        return MFXVideoDECODE_Reset(m_session, par);
    }
    virtual mfxStatus Close(void) override {
        return MFXVideoDECODE_Close(m_session);
    }

    virtual mfxStatus GetVideoParam(mfxVideoParam *par) override {
        return MFXVideoDECODE_GetVideoParam(m_session, par);
    }

    virtual mfxStatus GetDecodeStat(mfxDecodeStat *stat) override {
        return MFXVideoDECODE_GetDecodeStat(m_session, stat);
    }
    virtual mfxStatus GetPayload(mfxU64 *ts, mfxPayload *payload) override {
        return MFXVideoDECODE_GetPayload(m_session, ts, payload);
    }
    virtual mfxStatus SetSkipMode(mfxSkipMode mode) override {
        return MFXVideoDECODE_SetSkipMode(m_session, mode);
    }
    virtual mfxStatus DecodeFrameAsync(mfxBitstream *bs,
                                       mfxFrameSurface1 *surface_work,
                                       mfxFrameSurface1 **surface_out,
                                       mfxSyncPoint *syncp) override {
        return MFXVideoDECODE_DecodeFrameAsync(m_session, bs, surface_work, surface_out, syncp);
    }

    virtual mfxStatus GetSurface(mfxFrameSurface1** output_surf) override {
        return MFXMemory_GetSurfaceForDecode(m_session, output_surf);
    }

protected:
    mfxSession m_session; // (mfxSession) handle to the owning session
};

class MFXVideoVPP : public MFXVideoVPPBase {
public:
    explicit MFXVideoVPP(mfxSession session) {
        m_session = session;
    }
    virtual ~MFXVideoVPP(void) {
        Close();
    }

    virtual mfxStatus Query(mfxVideoParam *in, mfxVideoParam *out) override {
        return MFXVideoVPP_Query(m_session, in, out);
    }
    virtual mfxStatus QueryIOSurf(mfxVideoParam *par, mfxFrameAllocRequest request[2]) override {
        return MFXVideoVPP_QueryIOSurf(m_session, par, request);
    }
    virtual mfxStatus Init(mfxVideoParam *par) override {
        return MFXVideoVPP_Init(m_session, par);
    }
    virtual mfxStatus Reset(mfxVideoParam *par) override {
        return MFXVideoVPP_Reset(m_session, par);
    }
    virtual mfxStatus Close(void) override {
        return MFXVideoVPP_Close(m_session);
    }

    virtual mfxStatus GetVideoParam(mfxVideoParam *par) override {
        return MFXVideoVPP_GetVideoParam(m_session, par);
    }
    virtual mfxStatus GetVPPStat(mfxVPPStat *stat) override {
        return MFXVideoVPP_GetVPPStat(m_session, stat);
    }
    virtual mfxStatus RunFrameVPPAsync(mfxFrameSurface1 *in,
                                       mfxFrameSurface1 *out,
                                       mfxExtVppAuxData *aux,
                                       mfxSyncPoint *syncp) override {
        return MFXVideoVPP_RunFrameVPPAsync(m_session, in, out, aux, syncp);
    }

    virtual mfxStatus GetSurfaceIn(mfxFrameSurface1** output_surf) override {
        return MFXMemory_GetSurfaceForVPP(m_session, output_surf);
    }
    virtual mfxStatus GetSurfaceOut(mfxFrameSurface1** output_surf) override {
        return MFXMemory_GetSurfaceForVPPOut(m_session, output_surf);
    }

    virtual mfxStatus ProcessFrameAsync(mfxFrameSurface1 *in, mfxFrameSurface1 **out) override {
        return MFXVideoVPP_ProcessFrameAsync(m_session, in, out);
    }

protected:
    mfxSession m_session; // (mfxSession) handle to the owning session
};

class MFXVideoDECODE_VPP
{
public:
    explicit MFXVideoDECODE_VPP(mfxSession session) { m_session = session; }
    virtual ~MFXVideoDECODE_VPP(void) {
        Close();
    }

    virtual mfxStatus Init(mfxVideoParam* decode_par, mfxVideoChannelParam** vpp_par_array, mfxU32 num_channel_par) {
        return MFXVideoDECODE_VPP_Init(m_session, decode_par, vpp_par_array, num_channel_par);
    }
    virtual mfxStatus Reset(mfxVideoParam* decode_par, mfxVideoChannelParam** vpp_par_array, mfxU32 num_channel_par) {
        return MFXVideoDECODE_VPP_Reset(m_session, decode_par, vpp_par_array, num_channel_par);
    }
    virtual mfxStatus GetChannelParam(mfxVideoChannelParam *par, mfxU32 channel_id) {
        return MFXVideoDECODE_VPP_GetChannelParam(m_session, par, channel_id);
    }
    virtual mfxStatus DecodeFrameAsync(mfxBitstream *bs, mfxU32* skip_channels, mfxU32 num_skip_channels, mfxSurfaceArray **surf_array_out) {
        return MFXVideoDECODE_VPP_DecodeFrameAsync(m_session, bs, skip_channels, num_skip_channels, surf_array_out);
    }

    virtual mfxStatus DecodeHeader(mfxBitstream *bs, mfxVideoParam *par) {
        return MFXVideoDECODE_VPP_DecodeHeader(m_session, bs, par);
    }
    virtual mfxStatus Close(void) {
        return MFXVideoDECODE_VPP_Close(m_session);
    }
    virtual mfxStatus GetVideoParam(mfxVideoParam *par) {
        return MFXVideoDECODE_VPP_GetVideoParam(m_session, par);
    }
    virtual mfxStatus GetDecodeStat(mfxDecodeStat *stat) {
        return MFXVideoDECODE_VPP_GetDecodeStat(m_session, stat);
    }
    virtual mfxStatus GetPayload(mfxU64 *ts, mfxPayload *payload) {
        return MFXVideoDECODE_VPP_GetPayload(m_session, ts, payload);
    }
    virtual mfxStatus SetSkipMode(mfxSkipMode mode) {
        return MFXVideoDECODE_VPP_SetSkipMode(m_session, mode);
    }

protected:
    mfxSession m_session; // (mfxSession) handle to the owning session
};

#endif //__MFXVIDEOPLUSPLUS_H
