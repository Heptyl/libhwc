#pragma once

#include <unistd.h>
#include <thread>
#include <list>

#include <cutils/native_handle.h>

class IndexBufferGenerator
{
public:
    static IndexBufferGenerator& getInstance();
    virtual ~IndexBufferGenerator();

    // start the gereratorThread to generate index buffer
    void start();

    // stop the gereratorThread
    void stop();

    // the callers use this function to get the buffer which content is assigned index
    // when the assigned index does not exist in IndexBufferGenerator, it return a buffer with
    // empty content
    void getBuffer(int index, uint32_t width, uint32_t height, int* fd, uint64_t* alloc_id);

    // when the caller does not need the index buffer, call this function to release it
    void cancelBuffer(int index);

private:
    IndexBufferGenerator();

    struct BufferInfo
    {
        buffer_handle_t handle;
        int fd;
        uint64_t alloc_id;
        int index;
        uint32_t width;
        uint32_t height;
        uint32_t stride;
        size_t allocate_size;
        unsigned int format;
    };

    enum {
        SEVEN_SEGMEMT_UP = 0,
        SEVEN_SEGMEMT_UP_LEFT,
        SEVEN_SEGMEMT_UP_RIGHT,
        SEVEN_SEGMEMT_MIDDLE,
        SEVEN_SEGMEMT_DOWN_LEFT,
        SEVEN_SEGMEMT_DOWN_RIGHT,
        SEVEN_SEGMEMT_DOWN,
    };

    // IndexBufferGenerator use this function to geterate index buffer
    void gereratorThread();

    // create a buffer with assigned width and height
    BufferInfo generateBuffer(uint32_t width, uint32_t height);

    // free the assigned buffer
    void freeBuffer(BufferInfo& info);

    // draw assigned index on the buffer
    void drawBuffer(BufferInfo& info, int index);

    // draw background color on the buffer
    void drawBackground(void* addr, uint32_t width, uint32_t height, uint32_t stride,
            unsigned int format, uint32_t color);

    // draw the index on the buffer
    void drawIndex(void* addr, uint32_t width, uint32_t height, uint32_t stride,
            unsigned int format, uint32_t color, uint32_t left, uint32_t top, int index);

    // draw the assigned digit on the buffer
    void drawDigit(void* addr, uint32_t width, uint32_t height, uint32_t stride,
            unsigned int format, uint32_t color, uint32_t left, uint32_t top, int digit);

    // draw the assigned line on the buffer
    void drawLine(void* addr, uint32_t width, uint32_t height, uint32_t stride,
            unsigned int format, uint32_t color, uint32_t left, uint32_t top, int segment);

    // draw a region with assigned color on the buffer
    void drawRegion(void* addr, uint32_t width, uint32_t height, uint32_t stride,
            unsigned int format, uint32_t color, uint32_t left, uint32_t top, uint32_t right,
            uint32_t bottom);

    void initialBufferInfo(BufferInfo* info);

private:
    typedef std::list<BufferInfo> BufferList;
    std::mutex m_lock;
    std::condition_variable m_condition;
    std::thread m_thread;
    bool m_stop;
    BufferList m_ready_list;
    BufferList m_free_list;
    BufferList m_used_list;
    BufferInfo m_miss_buffer;
    size_t m_free_buffer;

    int m_request_index;
    int m_index;
    uint32_t m_width;
    uint32_t m_height;
    uint32_t m_draw_count;

    const uint32_t m_digit_size = 64;
    const uint32_t m_digit_interval_size = 10;
    const uint32_t m_digit_line_size = 10;
    const uint32_t m_num_bar = 3;
    const int m_index_interval = 3;
};
