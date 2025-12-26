#ifndef HWC_HRT_H
#define HWC_HRT_H

#include <sstream>
#include <linux/disp_session.h>

#include "hrt_common.h"

class Hrt : public HrtCommon
{
public:
    Hrt()
    {
        memset(m_layer_config_list, 0, sizeof(m_layer_config_list));
        memset(&m_disp_layer, 0, sizeof(m_disp_layer));
    }
    ~Hrt()
    {
        for (int i = 0; i < DisplayManager::MAX_DISPLAYS; ++i)
        {
            if (m_layer_config_list[i])
                free(m_layer_config_list[i]);
        }
    }

    bool isEnabled() const;

    bool isRPOEnabled() const;

    void printQueryValidLayerResult();

    void fillLayerConfigList();

    void fillDispLayer();

    void fillLayerInfoOfDispatcherJob();

    bool queryValidLayer();
private:
    layer_config* m_layer_config_list[DisplayManager::MAX_DISPLAYS];
    disp_layer_info m_disp_layer;
};

#endif
