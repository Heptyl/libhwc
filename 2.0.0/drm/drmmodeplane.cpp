#define DEBUG_LOG_TAG "DRMDEV"
#include "drmmodeplane.h"

#include <cutils/log.h>
#include <errno.h>

#include "utils/debug.h"

DrmModePlane::DrmModePlane(drmModePlanePtr p)
    : DrmObject(DRM_MODE_OBJECT_PLANE, p->plane_id)
    , m_crtc_id(p->crtc_id)
    , m_possible_crtcs(p->possible_crtcs)
    , m_crtc(nullptr)
{
    initObject();
}

DrmModePlane::~DrmModePlane()
{
}

void DrmModePlane::initObject()
{
    m_prop_size = sizeof(m_prop) / sizeof(*m_prop);
    m_prop_list = m_prop_table;
    m_property = m_prop;
}

int DrmModePlane::init(int fd)
{
    int res = 0;

    res = initProperty(fd);
    if (res)
    {
        HWC_LOGE("failed to init plane[%d] property: errno[%d]", m_id, res);
        return res;
    }

    res = checkProperty();
    if (res)
    {
        HWC_LOGE("failed to check plane[%d] property: error=%d", m_id, res);
        return res;
    }

    return res;
}

uint32_t DrmModePlane::getCrtcId() const
{
    return m_crtc_id;
}

void DrmModePlane::dump()
{
    HWC_LOGI("DrmModePlane: id=%d crtc_id=%d possible_crtc=%08x",
            m_id, m_crtc_id, m_possible_crtcs);
}

void DrmModePlane::arrangeCrtc(std::vector<DrmModeCrtc*>& crtcs)
{
    for (size_t i = 0; i < crtcs.size(); i++)
    {
        if (crtcs[i]->getId() == m_crtc_id)
        {
            m_crtc = crtcs[i];
        }

        if ((1 << crtcs[i]->getPipe()) & m_possible_crtcs)
        {
            m_possible_crtc_list.push_back(crtcs[i]);
        }
    }
}

int DrmModePlane::connectCrtc(DrmModeCrtc *crtc)
{
    int res = -1;
    if (crtc == nullptr)
    {
        HWC_LOGW("try to connect plane_%d with nullptr crtc", m_id);
        return res;
    }
    if ((1 << crtc->getPipe()) & m_possible_crtcs)
    {
        m_crtc = crtc;
        m_crtc_id = crtc->getId();
        m_crtc->addPlane(this);
        res = 0;
    }

    return res;
}
