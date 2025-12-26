#define DEBUG_LOG_TAG "DRMDEV"
#include "drmmodecrtc.h"

#include <cutils/log.h>
#include <errno.h>
#include <drm_fourcc.h>

#include "utils/debug.h"

#include "drmmoderesource.h"
#include "drmmodeplane.h"
#include "drmmodeencoder.h"
#include "drmmodeconnector.h"

DrmModeCrtc::DrmModeCrtc(DrmModeResource* drm, drmModeCrtcPtr crtc)
    : DrmObject(DRM_MODE_OBJECT_CRTC, crtc->crtc_id)
    , m_drm(drm)
    , m_pipe(0)
    , m_width(crtc->width)
    , m_height(crtc->height)
    , m_req_width(0)
    , m_req_height(0)
    , m_mode(&crtc->mode)
    , m_mode_valid(crtc->mode_valid)
    , m_encoder(nullptr)
    , m_main_crtc(false)
    , m_is_support_mml(false)
    , m_is_support_rpo(false)
    , m_session_id(MTK_DRM_INVALID_SESSION_ID)
    , m_session_mode(MTK_DRM_SESSION_INVALID)
{
    initObject();
    memset(&m_fb_bo, 0, sizeof(m_fb_bo));
    memset(&m_caps, 0, sizeof(m_caps));
}

DrmModeCrtc::~DrmModeCrtc()
{
    int res = destroyFb();
    if (res != 0)
    {
        HWC_LOGE("%s destroyFb fail err:%d", __func__, res);
    }
}

void DrmModeCrtc::initObject()
{
    m_prop_size = sizeof(m_prop) / sizeof(*m_prop);
    m_prop_list = m_prop_table;
    m_property = m_prop;
}

int DrmModeCrtc::init(int fd, uint32_t pipe)
{
    int res = 0;

    m_pipe = pipe;
    res = initProperty(fd);
    if (res)
    {
        HWC_LOGE("failed to init crtc[id=%d|pipe=0x%x] property: errno[%d]", m_id, m_pipe, res);
        return res;
    }

    // getCaps before checkProperty because if props not match with kernel
    // maybe hwc have one more prop, will lead to error, then it won't get cap
    getCaps(fd);

    res = checkProperty();
    if (res)
    {
        HWC_LOGE("failed to check crtc[id=%d|pipe=0x%x] property: error=%d", m_id, m_pipe, res);
        return res;
    }

    return res;
}

int DrmModeCrtc::prepareFb()
{
    int res = 0;

    m_fb_bo.width = (m_req_width == 0) ? getVirWidth() : m_req_width;
    m_fb_bo.height = (m_req_height == 0) ? getVirHeight() : m_req_height;
    m_fb_bo.format = DRM_FORMAT_RGB565;
    res = m_drm->allocateBuffer(&m_fb_bo);
    if (res != 0) {
        m_fb_bo.fb_id = 0;
        m_fb_bo.width = 0;
        m_fb_bo.height = 0;
        HWC_LOGW("failed to prepare FB for CRTC[id=%d|pipe=0x%x]: %d", m_id, m_pipe, res);
    }

    return res;
}

int DrmModeCrtc::destroyFb()
{
    return destroyFb(m_fb_bo);
}

int DrmModeCrtc::destroyFb(struct hwc_drm_bo &fb_bo)
{
    int res = 0;

    res = m_drm->freeBuffer(fb_bo);
    if (res != 0) {
        return res;
    }

    fb_bo.fb_id = 0;
    fb_bo.width = 0;
    fb_bo.height = 0;
    return res;
}

uint32_t DrmModeCrtc::getFbId()
{
    return m_fb_bo.fb_id;
}

uint32_t DrmModeCrtc::getPipe()
{
    return m_pipe;
}

void DrmModeCrtc::dump()
{
    HWC_LOGI("DrmModeCrtc_0x%x: id=%d planes=%zu WxH=%dx%d mode:(%d|%ux%u)",
            m_pipe, m_id, m_planes.size(), m_width, m_height, m_mode_valid, m_mode.getDisplayH(), m_mode.getDisplayV());
}

void DrmModeCrtc::setEncoder(DrmModeEncoder *encoder)
{
    m_encoder = encoder;
}

DrmModeEncoder* DrmModeCrtc::getEncoder()
{
    return m_encoder;
}

void DrmModeCrtc::addPlane(DrmModePlane *plane)
{
    m_planes.push_back(plane);
}

void DrmModeCrtc::clearPlane()
{
    m_planes.clear();
}

void DrmModeCrtc::setMode(DrmModeInfo& mode)
{
    m_mode = mode;
}

uint32_t DrmModeCrtc::getVirWidth()
{
    return m_mode.getDisplayH();
}

uint32_t DrmModeCrtc::getVirHeight()
{
    return m_mode.getDisplayV();
}

uint32_t DrmModeCrtc::getPhyWidth()
{
    if (m_encoder)
    {
        DrmModeConnector *connector = m_encoder->getConnector();
        if (connector)
        {
            return connector->getMmWidth();
        }
    }
    return 0;
}

uint32_t DrmModeCrtc::getPhyHeight()
{
    if (m_encoder)
    {
        DrmModeConnector *connector = m_encoder->getConnector();
        if (connector)
        {
            return connector->getMmHeight();
        }
    }
    return 0;
}

size_t DrmModeCrtc::getPlaneNum()
{
    return m_planes.size();
}

const DrmModePlane* DrmModeCrtc::getPlane(size_t index)
{
    DrmModePlane *plane = nullptr;
    if (m_planes.size() > index)
    {
        plane = m_planes[index];
    }
    return plane;
}

void DrmModeCrtc::setReqSize(uint32_t width, uint32_t height)
{
    m_req_width = width;
    m_req_height = height;
}

uint32_t DrmModeCrtc::getReqWidth()
{
    return m_req_width;
}

uint32_t DrmModeCrtc::getReqHeight()
{
    return m_req_height;
}

struct hwc_drm_bo DrmModeCrtc::getDumbBuffer()
{
    return m_fb_bo;
}

uint32_t DrmModeCrtc::getCurrentModeRefresh()
{
    return m_mode.getVRefresh();
}

void DrmModeCrtc::setMainCrtc()
{
    m_main_crtc = true;
}

bool DrmModeCrtc::isMainCrtc() const
{
    return m_main_crtc;
}

void DrmModeCrtc::setSupportMml()
{
    m_is_support_mml = true;
}

bool DrmModeCrtc::isSupportMml() const
{
    return m_is_support_mml;
}

void DrmModeCrtc::setSupportRpo()
{
    m_is_support_rpo = true;
}

bool DrmModeCrtc::isSupportRpo() const
{
    return m_is_support_rpo;
}

unsigned int DrmModeCrtc::getSessionId() const
{
    return m_session_id;
}

void DrmModeCrtc::setSessionId(unsigned int session_id)
{
    m_session_id = session_id;
}

unsigned int DrmModeCrtc::getSessionMode() const
{
    return m_session_mode;
}

void DrmModeCrtc::setSessionMode(unsigned int session_mode)
{
    m_session_mode = session_mode;
}

void DrmModeCrtc::getCaps(int fd)
{
    HWC_LOGI("%s(), crtc[id=%d|pipe=0x%x]", __FUNCTION__, m_id, m_pipe);

    const DrmModeProperty& prop = getProperty(DRM_PROP_CRTC_CAPS_BLOB_ID);

    if (!prop.hasInit())
    {
        return;
    }

    uint64_t blob_id = 0;
    prop.getValue(&blob_id);
    HWC_LOGV("%s(), blob id %" PRIu64 "", __FUNCTION__, blob_id);
    if (blob_id == 0)
    {
        return;
    }

    drmModePropertyBlobPtr blob = drmModeGetPropertyBlob(fd, static_cast<uint32_t>(blob_id));
    if (!blob)
    {
        return;
    }

    if (blob->data != nullptr)
    {
        m_caps = *(static_cast<mtk_drm_crtc_caps*>(blob->data));
        int wb_cap_size = sizeof(m_caps.wb_caps) / sizeof(*m_caps.wb_caps);
        for (int i = 0; i < wb_cap_size; i++) {
            HWC_LOGI("wb_cap, dump_point: %d, support: %d", i, m_caps.wb_caps[i].support);
            if(m_caps.wb_caps[i].support)
            {
                HWC_LOGI("wb_cap, dump_point: %d, rsz: %d", i, m_caps.wb_caps[i].rsz);
                HWC_LOGI("wb_cap, dump_point: %d, rsz_crop: %d", i, m_caps.wb_caps[i].rsz_crop);
                HWC_LOGI("wb_cap, dump_point: %d, rsz_out_min_w: %d", i, m_caps.wb_caps[i].rsz_out_min_w);
                HWC_LOGI("wb_cap, dump_point: %d, rsz_out_min_h: %d", i, m_caps.wb_caps[i].rsz_out_min_h);
                HWC_LOGI("wb_cap, dump_point: %d, rsz_out_max_w: %d", i, m_caps.wb_caps[i].rsz_out_max_w);
                HWC_LOGI("wb_cap, dump_point: %d, rsz_out_max_h: %d", i, m_caps.wb_caps[i].rsz_out_max_h);
            }
        }
    }

    drmModeFreePropertyBlob(blob);
}

bool DrmModeCrtc::isSupportWb() const
{
    HWC_LOGI("%s(), crtc[id=%d|pipe=0x%x]", __FUNCTION__, m_id, m_pipe);
    bool support = false;
    for (int i = 0; i < MTK_DRM_DUMP_POINT_NUM; i++)
    {
        support |= m_caps.wb_caps[i].support;
    }

    return support;
}

bool DrmModeCrtc::isSupportWbParams(uint64_t dump_point,
                                    unsigned int disp_w, unsigned int disp_h,
                                    unsigned int src_w, unsigned int src_h,
                                    unsigned int dst_w, unsigned int dst_h) const
{
    HWC_LOGV("%s(), crtc[id=%d|pipe=0x%x], dump_point(%" PRIu64 "), disp(%d, %d), src(%d, %d), dst(%d, %d)",
             __FUNCTION__, m_id, m_pipe, dump_point, disp_w, disp_h, src_w, src_h, dst_w, dst_h);

    // check dump point support
    if (!m_caps.wb_caps[dump_point].support)
    {
        HWC_LOGI("%s(), crtc[id=%d|pipe=0x%x], dump_point(%" PRIu64 ") not support",
                 __FUNCTION__, m_id, m_pipe, dump_point);
        return false;
    }

    // check if rsz, crop, rsz & crop
    // crop or full screen, supported
    if (dst_w == src_w && dst_h == src_h)
    {
        HWC_LOGV("%s(), crtc[id=%d|pipe=0x%x], crop, supported", __FUNCTION__, m_id, m_pipe);
        return true;
    }

    // only support shrink
    if (dst_w > src_w || dst_h > src_h)
    {
        HWC_LOGI("%s(), crtc[id=%d|pipe=0x%x], resize(enlarge), not supported", __FUNCTION__, m_id, m_pipe);
        return false;
    }

    // resize & crop
    if (src_w != disp_w || src_h != disp_h)
    {
        if (!m_caps.wb_caps[dump_point].rsz_crop)
        {
            HWC_LOGI("%s(), crtc[id=%d|pipe=0x%x], resize(shrank) & crop, not supported", __FUNCTION__, m_id, m_pipe);
            return false;
        }
    }

    // resize, shrank
    if (!m_caps.wb_caps[dump_point].rsz)
    {
        HWC_LOGI("%s(), crtc[id=%d|pipe=0x%x], resize(shrank), not supported", __FUNCTION__, m_id, m_pipe);
        return false;
    }
    if (dst_w <= m_caps.wb_caps[dump_point].rsz_out_max_w && dst_w >= m_caps.wb_caps[dump_point].rsz_out_min_w &&
        dst_h <= m_caps.wb_caps[dump_point].rsz_out_max_h && dst_h >= m_caps.wb_caps[dump_point].rsz_out_min_h)
    {
        HWC_LOGV("%s(), crtc[id=%d|pipe=0x%x], resize(shrank), supported, support resize max(%d, %d), min(%d, %d)",
                 __FUNCTION__, m_id, m_pipe,
                 m_caps.wb_caps[dump_point].rsz_out_max_w, m_caps.wb_caps[dump_point].rsz_out_max_h,
                 m_caps.wb_caps[dump_point].rsz_out_min_w, m_caps.wb_caps[dump_point].rsz_out_min_h);
        return true;
    }

    HWC_LOGI("%s(), crtc[id=%d|pipe=0x%x], resize(shrank), not supported, support resize max(%d, %d), min(%d, %d)",
             __FUNCTION__, m_id, m_pipe,
             m_caps.wb_caps[dump_point].rsz_out_max_w, m_caps.wb_caps[dump_point].rsz_out_max_h,
             m_caps.wb_caps[dump_point].rsz_out_min_w, m_caps.wb_caps[dump_point].rsz_out_min_h);
    return false;
}

