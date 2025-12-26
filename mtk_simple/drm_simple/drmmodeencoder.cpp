#define DEBUG_LOG_TAG "drmmodeencoder"
#define ATRACE_TAG ATRACE_TAG_GRAPHICS

#include "drmmodeencoder.h"
#include <cutils/log.h>
#include "drmmoderesource.h"
#include "drmmodeconnector.h"

namespace simplehwc {

DrmModeEncoder::DrmModeEncoder(drmModeEncoderPtr e)
    : DrmObject(DRM_MODE_OBJECT_ENCODER, e->encoder_id)
    , m_type(e->encoder_type)
    , m_crtc_id(e->crtc_id)
    , m_possible_crtcs(e->possible_crtcs)
    , m_crtc(nullptr)
    , m_connector(nullptr)
{
    initObject();
}

DrmModeEncoder::~DrmModeEncoder()
{
}

void DrmModeEncoder::initObject()
{
    m_prop_size = 0;
    m_prop_list = nullptr;
    m_property = nullptr;
}

int DrmModeEncoder::init(int /*fd*/)
{
    return 0;
}

uint32_t DrmModeEncoder::getId()
{
    return m_id;
}

uint32_t DrmModeEncoder::getCrtcId()
{
    return m_crtc_id;
}

uint32_t DrmModeEncoder::getPossibleCrtc()
{
    return m_possible_crtcs;
}

void DrmModeEncoder::arrangeCrtc(std::vector<DrmModeCrtc*>& crtcs)
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

int DrmModeEncoder::connectCrtc(DrmModeCrtc *crtc)
{
    int res = -1;
    if (crtc == nullptr)
    {
        ALOGW("try to connect encoder_%d with nullptr crtc", m_id);
        return res;
    }
    if ((1 << crtc->getPipe()) & m_possible_crtcs)
    {
        m_crtc = crtc;
        m_crtc_id = crtc->getId();
        m_crtc->setEncoder(this);
        res = 0;
    }

    return res;
}

void DrmModeEncoder::dump()
{
    ALOGI("DrmModeEncoder: id=%d crtc_id=%d type=%d possible_crtc=%08x",
            m_id, m_crtc_id, m_type, m_possible_crtcs);
}

void DrmModeEncoder::setConnector(DrmModeConnector *connector)
{
    m_connector = connector;
}

DrmModeConnector* DrmModeEncoder::getConnector()
{
    return m_connector;
}

}  // namespace simplehwc

