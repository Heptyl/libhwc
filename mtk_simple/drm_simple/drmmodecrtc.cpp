#define DEBUG_LOG_TAG "drmmodecrtc"

#define ATRACE_TAG ATRACE_TAG_GRAPHICS
#include "drmmodecrtc.h"

#include <cutils/log.h>
#include <errno.h>
#include <drm_fourcc.h>

#include "drmmoderesource.h"
#include "drmmodeplane.h"
#include "drmmodeencoder.h"
#include "drmmodeconnector.h"

namespace simplehwc {

//#define ALIGN(x,a) (((x)+(a)-1)&~((a)-1))

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
{
    initObject();
    memset(&m_fb_bo, 0, sizeof(m_fb_bo));
}

DrmModeCrtc::~DrmModeCrtc()
{
    int res = destroyFb();
    if (res != 0)
    {
        ALOGE("%s destroyFb fail err:%d", __func__, res);
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
        ALOGE("failed to init crtc[id=%d|pipe=0x%x] property: errno[%d]", m_id, m_pipe, res);
        return res;
    }

    res = checkProperty();
    if (res)
    {
        ALOGE("failed to check crtc[id=%d|pipe=0x%x] property: error=%d", m_id, m_pipe, res);
        return res;
    }

    return res;
}

int DrmModeCrtc::prepareFb()
{
    int res = 0;
    static const uint32_t colors[2][3] = {{0xff0000ff, 0xff00ff00, 0xffff0000},
                                          {0xff000080, 0xff008000, 0xff800000}};
    for (int i  = 0; i < MAX_BO_SIZE; i++)
    {
        //m_fb_bo[i].width = (m_req_width == 0) ? ALIGN(getVirWidth(), 32u) : m_req_width;
        m_fb_bo[i].width = (m_req_width == 0) ? getVirWidth() : m_req_width;
        m_fb_bo[i].height = (m_req_height == 0) ? getVirHeight() : m_req_height;
        m_fb_bo[i].format = DRM_FORMAT_ABGR8888;//FrameBuffer pixel format must be HAL_PIXEL_FORMAT_RGBA_8888
        res = m_drm->allocateBuffer(&m_fb_bo[i]);
        if (res != 0)
        {
            m_fb_bo[i].fb_id = 0;
            m_fb_bo[i].width = 0;
            m_fb_bo[i].height = 0;
            ALOGW("failed to prepare FB for CRTC[id=%d|pipe=0x%x]: %d", m_id, m_pipe, res);
            break;
        }
        // paint some test pattern
        hwc_drm_bo bo = getDumbBuffer(static_cast<uint32_t>(i));
        void* fb_vaddr = mmap(0, bo.size_page, PROT_WRITE, MAP_SHARED, bo.fd, 0);
        if (fb_vaddr != MAP_FAILED)
        {
            uint32_t* fb_shifted_vaddr = static_cast<uint32_t*>(fb_vaddr);
            uint32_t stride = bo.pitches[0] / 4;
            uint32_t line_bytes = bo.width * 4;
            memset(static_cast<void*>(fb_shifted_vaddr), 255, line_bytes);
            memset(static_cast<void*>(fb_shifted_vaddr + stride * (bo.height - 1)), 255, line_bytes);
            for (uint32_t r = 1; r < bo.height - 1; ++r)
            {
                if (r % 32 == 0)
                {
                    uint32_t color = colors[i % 2][(r / 32) % 3];
                    for (uint32_t c = 1; c < bo.width - 1; ++c)
                    {
                        fb_shifted_vaddr[c] = color;
                    }
                }
                else
                {
                    fb_shifted_vaddr[0] = 0xffffffff;
                    fb_shifted_vaddr[bo.width - 1] = 0xffffffff;
                }
                fb_shifted_vaddr += stride;
            }
            if (munmap(fb_vaddr, bo.size_page))
            {
                ALOGE("failed to unmap framebuffer");
            }
        }
        else
        {
            ALOGE("failed to map framebuffer");
        }
    }

    return res;
}

int DrmModeCrtc::destroyFb()
{
    int res = 0;
    for (int i  = 0; i < MAX_BO_SIZE; i++)
    {
        res |= destroyFb(m_fb_bo[i]);
    }
    return res;
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

uint32_t DrmModeCrtc::getFbId(uint32_t pinpon)
{
    return m_fb_bo[pinpon].fb_id;
}

uint32_t DrmModeCrtc::getId()
{
    return m_id;
}

uint32_t DrmModeCrtc::getPipe()
{
    return m_pipe;
}

void DrmModeCrtc::dump()
{
    ALOGI("DrmModeCrtc_0x%x: id=%d planes=%zu WxH=%dx%d mode:(%d|%ux%u)",
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

struct hwc_drm_bo DrmModeCrtc::getDumbBuffer(uint32_t pinpon)
{
    return m_fb_bo[pinpon];
}

uint32_t DrmModeCrtc::getCurrentModeRefresh()
{
    return m_mode.getVRefresh();
}
}  // namespace simplehwc

