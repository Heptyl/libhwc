#ifndef HWC_VSYNC_LISTENER_H_
#define HWC_VSYNC_LISTENER_H_

#include <utils/RefBase.h>

struct HWCVSyncListener : public virtual RefBase {
    // onVSync() is used to receive the vsync signal
    virtual void onVSync() = 0;
};
#endif // HWC_VSYNC_LISTENER_H_