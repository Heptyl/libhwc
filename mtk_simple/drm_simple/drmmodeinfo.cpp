#define DEBUG_LOG_TAG "drmmodeinfo"

#include "drmmodeinfo.h"

namespace simplehwc {

DrmModeInfo::DrmModeInfo(drmModeModeInfoPtr mode)
{
    m_info.clock = mode->clock;
    m_info.hdisplay = mode->hdisplay;
    m_info.hsync_start = mode->hsync_start;
    m_info.hsync_end = mode->hsync_end;
    m_info.htotal = mode->htotal;
    m_info.hskew = mode->hskew;
    m_info.vdisplay = mode->vdisplay;
    m_info.vsync_start = mode->vsync_start;
    m_info.vsync_end = mode->vsync_end;
    m_info.vtotal = mode->vtotal;
    m_info.vscan = mode->vscan;
    m_info.vrefresh = mode->vrefresh;
    m_info.flags = mode->flags;
    m_info.type = mode->type;
    memcpy(m_info.name, mode->name, sizeof(m_info.name));
}

bool DrmModeInfo::operator==(const drmModeModeInfo &mode) const
{
    return m_info.clock == mode.clock && m_info.hdisplay == mode.hdisplay &&
           m_info.hsync_start == mode.hsync_start && m_info.hsync_end == mode.hsync_end &&
           m_info.htotal == mode.htotal && m_info.hskew == mode.hskew &&
           m_info.vdisplay == mode.vdisplay && m_info.vsync_start == mode.vsync_start &&
           m_info.vsync_end == mode.vsync_end && m_info.vtotal == mode.vtotal &&
           m_info.vscan == mode.vscan && m_info.flags == mode.flags && m_info.type == mode.type;
}

DrmModeInfo& DrmModeInfo::operator=(const DrmModeInfo &mode)
{
    m_info.clock = mode.m_info.clock;
    m_info.hdisplay = mode.m_info.hdisplay;
    m_info.hsync_start = mode.m_info.hsync_start;
    m_info.hsync_end = mode.m_info.hsync_end;
    m_info.htotal = mode.m_info.htotal;
    m_info.hskew = mode.m_info.hskew;
    m_info.vdisplay = mode.m_info.vdisplay;
    m_info.vsync_start = mode.m_info.vsync_start;
    m_info.vsync_end = mode.m_info.vsync_end;
    m_info.vtotal = mode.m_info.vtotal;
    m_info.vscan = mode.m_info.vscan;
    m_info.vrefresh = mode.m_info.vrefresh;
    m_info.flags = mode.m_info.flags;
    m_info.type = mode.m_info.type;
    memcpy(m_info.name, mode.m_info.name, sizeof(m_info.name));

    return *this;
}

uint32_t DrmModeInfo::getDisplayH()
{
    return m_info.hdisplay;
}

uint32_t DrmModeInfo::getDisplayV()
{
    return m_info.vdisplay;
}

uint32_t DrmModeInfo::getVRefresh()
{
    return m_info.vrefresh;
}

uint32_t DrmModeInfo::getType()
{
    return m_info.type;
}

void DrmModeInfo::getModeInfo(drmModeModeInfo* mode)
{
    mode->clock = m_info.clock;
    mode->hdisplay = m_info.hdisplay;
    mode->hsync_start = m_info.hsync_start;
    mode->hsync_end = m_info.hsync_end;
    mode->htotal = m_info.htotal;
    mode->hskew = m_info.hskew;
    mode->vdisplay = m_info.vdisplay;
    mode->vsync_start = m_info.vsync_start;
    mode->vsync_end = m_info.vsync_end;
    mode->vtotal = m_info.vtotal;
    mode->vscan = m_info.vscan;
    mode->vrefresh = m_info.vrefresh;
    mode->flags = m_info.flags;
    mode->type = m_info.type;
    memcpy(mode->name, m_info.name, sizeof(mode->name));
}

}  // namespace simplehwc

