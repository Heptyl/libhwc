#ifndef HWC_HWC2_DEFS_H
#define HWC_HWC2_DEFS_H

#include <utils/Timers.h>

#define HWC_NO_ION_FD ((int)(~0U>>1))

#define DEFAULT_PQ_MODE_ID -1

typedef enum {
    HWC_SKIP_VALIDATE_NOT_SKIP = 0,
    HWC_SKIP_VALIDATE_SKIP = 1
} SKIP_VALI_STATE;

enum { NUM_DECOUPLE_FB_ID_BACKUP_SLOTS = 3 };

// Track the sequence state from validate to present
// +-----------------+------------------------------------+----------------------------+
// | State           | Define                             | Next State                 |
// +-----------------+------------------------------------+----------------------------+
// | PRESENT_DONE    | SF get release fence / Initinal    | CHECK_SKIP_VALI / VALIDATE |
// | CHECK_SKIP_VALI | Check or Check done skip validate  | VALIDATE_DONE / VALIDATE   |
// | VALIDATE        | Doing or done validate             | VALIDATE_DONE]             |
// | VALIDATE_DONE   | SF get validate result             | PRESENT                    |
// | PRESENT         | Doing or done validate             | PRESENT_DONE               |
// +-----------------+------------------------------------+----------------------------+

// Track the sequence state from validate to present for separate validate
// +-------------------+------------------------------------+--------------------------------------------------+
// | State             | Define                             | Next State                                       |
// +-------------------+------------------------------------+--------------------------------------------------+
// | PRESENT_DONE      | SF get release fence / Initinal    | PRESENT_DONE / FALLBACK_VALIDATE / VALIDATE_DONE |
// | FALLBACK_VALIDATE | fallback to validate               | FALLBACK_VALIDATE / VALIDATE_DONE                |
// | VALIDATE_DONE     | SF get validate result             | PRESENT_DONE / VALIDATE_DONE                     |
// +-------------------+------------------------------------+--------------------------------------------------+

typedef enum {
    HWC_VALI_PRESENT_STATE_PRESENT_DONE = 0,
    HWC_VALI_PRESENT_STATE_CHECK_SKIP_VALI = 1,
    HWC_VALI_PRESENT_STATE_VALIDATE = 2,
    HWC_VALI_PRESENT_STATE_VALIDATE_DONE = 3,
    HWC_VALI_PRESENT_STATE_PRESENT = 4,
    HWC_VALI_PRESENT_STATE_FALLBACK_VALIDATE = 5
} HWC_VALI_PRESENT_STATE;

const char* getPresentValiStateString(const HWC_VALI_PRESENT_STATE& state);

typedef enum {
    HWC_COMP_FILE_UNK = 0,
    HWC_COMP_FILE_NSET,
    HWC_COMP_FILE_HWC,
    HWC_COMP_FILE_HWCD,
    HWC_COMP_FILE_HWCL,
    HWC_COMP_FILE_PF,
    HWC_COMP_FILE_PFC,
    HWC_COMP_FILE_HRT,
} HWC_COMP_FILE;

const char* getCompFileString(const HWC_COMP_FILE& comp_file);

enum {
    HWC_COMPRESSION_TYPE_NONE = 0,
    HWC_COMPRESSION_TYPE_AFBC = 1,
    HWC_COMPRESSION_TYPE_PVRIC = 2,
    HWC_COMPRESSION_TYPE_HYFBC = 3,
};

// perf
#define PMQOS_DISPLAY_DRIVER_EXECUTE_TIME 3 * 1000 * 1000
// add some tolerance when get next hw vsync, since sf may have diff to hw vsync, should smaller than 0.5 vsync(180hz)
#define PMQOS_VSYNC_TOLERANCE_NS ((1000 * 1000 * 1000) / 180 / 2)

struct UClampCpuTable
{
    uint32_t uclamp;
    uint32_t cpu_mhz;
};

enum HWC_MC_TYPE {
    HWC_MC_NONE = 0,
    HWC_MC_1U = 1,      // number for better trace instinct
    HWC_MC_2U = 2,
    HWC_MC_3U = 3,
    HWC_MC_4U = 4,
    HWC_MC_5U = 5,
    HWC_MC_6U = 6,
    HWC_MC_0U_1M = 10,
    HWC_MC_1U_1M = 11,  // 1 MM start from 1x
    HWC_MC_2U_1M = 12,
    HWC_MC_3U_1M = 13,
    HWC_MC_4U_1M = 14,
    HWC_MC_5U_1M = 15,
};

struct HwcMCycleInfo
{
    int id;
    float main_mc;
    float dispatcher_mc;
    float ovl_mc;

    float ovl_mc_atomic_ratio; // atomic commit ratio of total ovl_mc
    nsecs_t dispatcher_target_work_time;
    nsecs_t ovl_wo_atomic_work_time;    // if <= 0,  use remain time calculate
};

enum {
    HWC_CPUSET_NONE = 0,
    HWC_CPUSET_LITTLE = 1 << 0,
    HWC_CPUSET_MIDDLE = 1 << 1,
    HWC_CPUSET_BIG = 1 << 2,
};

enum {
    HWC_DEBUG_LAYER_TYPE_NONE = 0,
    HWC_DEBUG_LAYER_TYPE_PRESENT_IDX,
    HWC_DEBUG_LAYER_TYPE_SUBSTITUTE_PRESENT_IDX,
};

#endif
