#pragma once

#include <optional>

struct MSync2Data
{
    std::optional<bool> m_msync2_enable;

    struct LevelTable {
        uint32_t level_id;
        uint32_t level_fps;
        uint32_t max_fps;
        uint32_t min_fps;
    };

    struct ParamTable {
        uint32_t max_fps;
        uint32_t min_fps;
        uint32_t level_num;
        std::vector<LevelTable> level_tables;
    };

    std::optional<std::shared_ptr<ParamTable>> m_param_table;
};
