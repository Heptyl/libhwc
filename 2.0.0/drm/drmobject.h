#ifndef __MTK_HWC_DRM_OBJECT_H__
#define __MTK_HWC_DRM_OBJECT_H__

#include <stdint.h>
#include <vector>

#include "drmmodeproperty.h"

class DrmObject
{
public:
    DrmObject(uint32_t type, uint32_t id);
    virtual ~DrmObject() {};

    virtual const DrmModeProperty& getProperty(int prop) const;
    virtual int addProperty(drmModeAtomicReqPtr req, int prop, uint64_t value) const;

    uint32_t getId() const;

protected:
    virtual void initObject() = 0;
    virtual int checkProperty();
    virtual int initProperty(int fd);

protected:
    uint32_t m_obj_type;
    uint32_t m_id;
    size_t m_prop_size;
    std::pair<int, std::string> *m_prop_list;
    DrmModeProperty *m_property;
};

#endif
