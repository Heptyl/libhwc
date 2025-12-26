#define DEBUG_LOG_TAG "ID_BUFX_GENERATOR"
#define ATRACE_TAG ATRACE_TAG_GRAPHICS

#include "index_buffer_generator.h"

#include "utils/tools.h"
#include "hwc2.h"
#include "grallocdev.h"

#define COLOR_BLACK 0xff000000
#define COLOR_GRAY 0xff808080
#define COLOR_WHITE 0xffffffff
#define COLOR_RED 0xff0000ff
#define COLOR_GREEN 0xff00ff00
#define COLOR_BLUE 0xffff0000

IndexBufferGenerator& IndexBufferGenerator::getInstance()
{
    static IndexBufferGenerator gInstance;
    return gInstance;
}

IndexBufferGenerator::IndexBufferGenerator()
    : m_stop(false)
    , m_free_buffer(3)
    , m_request_index(-1)
    , m_index(-1)
    , m_width(0)
    , m_height(0)
    , m_draw_count(0)
{
    initialBufferInfo(&m_miss_buffer);
}

IndexBufferGenerator::~IndexBufferGenerator()
{
}

void IndexBufferGenerator::start()
{
    std::lock_guard<std::mutex> lock(m_lock);
    m_stop = false;
    m_thread = std::thread(&IndexBufferGenerator::gereratorThread, this);
}

void IndexBufferGenerator::stop()
{
    {
        std::lock_guard<std::mutex> lock(m_lock);
        m_stop = true;
        m_condition.notify_one();
    }
    if (m_thread.joinable())
    {
        m_thread.join();
    }
}

void IndexBufferGenerator::getBuffer(int index, uint32_t width, uint32_t height, int* fd,
        uint64_t* alloc_id)
{
    std::lock_guard<std::mutex> lock(m_lock);
    bool miss = true;
    while (!m_ready_list.empty())
    {
        auto info = m_ready_list.front();
        if (info.index < index)
        {
            if (width == info.width && height == info.height)
            {
                m_ready_list.pop_front();
                m_free_list.emplace_back(info);
            }
            else
            {
                freeBuffer(info);
                m_ready_list.pop_front();
                m_free_buffer++;
            }
        }
        else if (info.index == index)
        {
            if (width == info.width && height == info.height)
            {
                miss = false;
                m_ready_list.pop_front();
                m_used_list.emplace_back(info);
                if (*fd > -1)
                {
                    protectedClose(*fd);
                }
                *fd = dup(info.fd);
                *alloc_id = info.alloc_id;
            }
            else
            {
                freeBuffer(info);
                m_ready_list.pop_front();
                m_free_buffer++;
            }
        }
        else
        {
            break;
        }
    }

    if (miss)
    {
        if (m_miss_buffer.width != width || m_miss_buffer.height != height)
        {
            freeBuffer(m_miss_buffer);
            m_miss_buffer = generateBuffer(width, height);
            drawBuffer(m_miss_buffer, 0);
        }
        *fd = dup(m_miss_buffer.fd);
        *alloc_id = m_miss_buffer.alloc_id;
    }

    m_request_index = index;
    if (m_width != width || m_height != height)
    {
        m_width = width;
        m_height = height;
        while (!m_free_list.empty())
        {
            auto info = m_free_list.front();
            freeBuffer(info);
            m_free_list.pop_front();
            m_free_buffer++;
        }
    }
}

void IndexBufferGenerator::cancelBuffer(int index)
{
    std::lock_guard<std::mutex> lock(m_lock);
    bool valid_index = false;
    BufferList temp = m_used_list;
    while (!temp.empty())
    {
        auto info = temp.front();
        if (info.index == index)
        {
            valid_index = true;
            break;
        }
        temp.pop_front();
    }
    if (valid_index)
    {
        while (!m_used_list.empty())
        {
            auto info = m_used_list.front();
            if (info.index <= index)
            {
                if (info.width == m_width && info.height == m_height)
                {
                    m_used_list.pop_front();
                    m_free_list.emplace_back(info);
                }
                else
                {
                    auto info = m_used_list.front();
                    freeBuffer(info);
                    m_used_list.pop_front();
                    m_free_buffer++;
                }
            }
            else
            {
                break;
            }
        }
    }
    m_condition.notify_one();
}

IndexBufferGenerator::BufferInfo IndexBufferGenerator::generateBuffer(uint32_t width,
        uint32_t height)
{
    BufferInfo info;
    memset(&info, 0, sizeof(info));
    info.fd = -1;
    GrallocDevice::AllocParam param;
    param.width = width;
    param.height = height;
    param.format = HAL_PIXEL_FORMAT_RGBA_8888;
    param.usage = static_cast<uint64_t>(BufferUsage::COMPOSER_OVERLAY);
    param.usage |= BufferUsage::CPU_READ_OFTEN | BufferUsage::CPU_WRITE_OFTEN;
    if (NO_ERROR != GrallocDevice::getInstance().alloc(param))
    {
        HWC_LOGE("Failed to allocate memory size(w=%d,h=%d,fmt=%d,usage=%" PRIx64 ")",
                param.width, param.height, param.format, param.usage);
    }
    else
    {
        PrivateHandle priv_handle;
        getPrivateHandle(param.handle, &priv_handle);
        getAllocId(param.handle, &priv_handle);
        info.handle = param.handle;
        info.fd = priv_handle.ion_fd;
        info.alloc_id = priv_handle.alloc_id;
        info.index = 0;
        info.width = priv_handle.width;
        info.height = priv_handle.height;
        info.stride = priv_handle.y_stride;
        info.allocate_size = static_cast<size_t>(priv_handle.size);
        info.format = param.format;
    }
    return info;
}

void IndexBufferGenerator::freeBuffer(BufferInfo& info)
{
    if (info.handle != nullptr)
    {
        GrallocDevice::getInstance().free(info.handle);
    }
}

void IndexBufferGenerator::drawBackground(void* addr, uint32_t /*width*/, uint32_t height,
        uint32_t stride, unsigned int /*format*/, uint32_t color)
{
    uint32_t* ptr = reinterpret_cast<uint32_t*>(addr);
    for (uint32_t h = 0; h < height; h++)
    {
        for (uint32_t w = 0; w < stride; w++)
        {
            *ptr = color;
            ptr++;
        }
    }
}

void IndexBufferGenerator::drawIndex(void* addr, uint32_t width, uint32_t height, uint32_t stride,
        unsigned int format, uint32_t color, uint32_t left, uint32_t top, int index)
{
    int temp = index;
    uint32_t count = 0;
    do {
        count++;
        temp /= 10;
    } while(temp != 0);
    temp = index;
    for (uint32_t i = count; i > 0; i--)
    {
        int digit = temp % 10;
        uint32_t left_offset = (m_digit_size + m_digit_interval_size) * (i - 1);
        drawDigit(addr, width, height, stride, format, color, left + left_offset, top, digit);
        temp /= 10;
    }
}

#define DrawLine(segment) drawLine(addr, width, height, stride, format, color, left, top, segment)

void IndexBufferGenerator::drawDigit(void* addr, uint32_t width, uint32_t height, uint32_t stride,
        unsigned int format, uint32_t color, uint32_t left, uint32_t top, int digit)
{
    switch (digit)
    {
        case 0:
            DrawLine(SEVEN_SEGMEMT_UP);
            DrawLine(SEVEN_SEGMEMT_UP_LEFT);
            DrawLine(SEVEN_SEGMEMT_UP_RIGHT);
            DrawLine(SEVEN_SEGMEMT_DOWN_LEFT);
            DrawLine(SEVEN_SEGMEMT_DOWN_RIGHT);
            DrawLine(SEVEN_SEGMEMT_DOWN);
            break;
        case 1:
            DrawLine(SEVEN_SEGMEMT_UP_RIGHT);
            DrawLine(SEVEN_SEGMEMT_DOWN_RIGHT);
            break;
        case 2:
            DrawLine(SEVEN_SEGMEMT_UP);
            DrawLine(SEVEN_SEGMEMT_UP_RIGHT);
            DrawLine(SEVEN_SEGMEMT_MIDDLE);
            DrawLine(SEVEN_SEGMEMT_DOWN_LEFT);
            DrawLine(SEVEN_SEGMEMT_DOWN);
            break;
        case 3:
            DrawLine(SEVEN_SEGMEMT_UP);
            DrawLine(SEVEN_SEGMEMT_UP_RIGHT);
            DrawLine(SEVEN_SEGMEMT_MIDDLE);
            DrawLine(SEVEN_SEGMEMT_DOWN_RIGHT);
            DrawLine(SEVEN_SEGMEMT_DOWN);
            break;
        case 4:
            DrawLine(SEVEN_SEGMEMT_UP_LEFT);
            DrawLine(SEVEN_SEGMEMT_UP_RIGHT);
            DrawLine(SEVEN_SEGMEMT_MIDDLE);
            DrawLine(SEVEN_SEGMEMT_DOWN_RIGHT);
            break;
        case 5:
            DrawLine(SEVEN_SEGMEMT_UP);
            DrawLine(SEVEN_SEGMEMT_UP_LEFT);
            DrawLine(SEVEN_SEGMEMT_MIDDLE);
            DrawLine(SEVEN_SEGMEMT_DOWN_RIGHT);
            DrawLine(SEVEN_SEGMEMT_DOWN);
            break;
        case 6:
            DrawLine(SEVEN_SEGMEMT_UP_LEFT);
            DrawLine(SEVEN_SEGMEMT_MIDDLE);
            DrawLine(SEVEN_SEGMEMT_DOWN_LEFT);
            DrawLine(SEVEN_SEGMEMT_DOWN_RIGHT);
            DrawLine(SEVEN_SEGMEMT_DOWN);
            break;
        case 7:
            DrawLine(SEVEN_SEGMEMT_UP);
            DrawLine(SEVEN_SEGMEMT_UP_RIGHT);
            DrawLine(SEVEN_SEGMEMT_DOWN_RIGHT);
            break;
        case 8:
            DrawLine(SEVEN_SEGMEMT_UP);
            DrawLine(SEVEN_SEGMEMT_UP_LEFT);
            DrawLine(SEVEN_SEGMEMT_UP_RIGHT);
            DrawLine(SEVEN_SEGMEMT_MIDDLE);
            DrawLine(SEVEN_SEGMEMT_DOWN_LEFT);
            DrawLine(SEVEN_SEGMEMT_DOWN_RIGHT);
            DrawLine(SEVEN_SEGMEMT_DOWN);
            break;
        case 9:
            DrawLine(SEVEN_SEGMEMT_UP);
            DrawLine(SEVEN_SEGMEMT_UP_LEFT);
            DrawLine(SEVEN_SEGMEMT_UP_RIGHT);
            DrawLine(SEVEN_SEGMEMT_MIDDLE);
            DrawLine(SEVEN_SEGMEMT_DOWN_RIGHT);
            break;
    }
}

void IndexBufferGenerator::drawLine(void* addr, uint32_t width, uint32_t height, uint32_t stride,
        unsigned int format, uint32_t color, uint32_t left, uint32_t top, int segment)
{
    uint32_t s_left = 0;
    uint32_t s_right = 0;
    uint32_t s_top = 0;
    uint32_t s_bottom = 0;
    switch (segment)
    {
        case SEVEN_SEGMEMT_UP:
            s_left = left;
            s_right = s_left + m_digit_size;
            s_top = top;
            s_bottom = s_top + m_digit_line_size;
            break;
        case SEVEN_SEGMEMT_UP_LEFT:
            s_left = left;
            s_right = s_left + m_digit_line_size;
            s_top = top;
            s_bottom = s_top + static_cast<uint32_t>((m_digit_size / 2));
            break;
        case SEVEN_SEGMEMT_UP_RIGHT:
            s_right = left + m_digit_size;
            s_left = s_right - m_digit_line_size;
            s_top = top;
            s_bottom = s_top + static_cast<uint32_t>((m_digit_size / 2));
            break;
        case SEVEN_SEGMEMT_MIDDLE:
            s_left = left;
            s_right = s_left + m_digit_size;
            s_top = top + static_cast<uint32_t>((m_digit_size / 2));
            s_bottom = s_top + m_digit_line_size;
            break;
        case SEVEN_SEGMEMT_DOWN_LEFT:
            s_left = left;
            s_right = s_left + m_digit_line_size;
            s_top = top + static_cast<uint32_t>((m_digit_size / 2));
            s_bottom = top + m_digit_size;
            break;
        case SEVEN_SEGMEMT_DOWN_RIGHT:
            s_right = left + m_digit_size;
            s_left = s_right - m_digit_line_size;
            s_top = top + static_cast<uint32_t>((m_digit_size / 2));
            s_bottom = top + m_digit_size;
            break;
        case SEVEN_SEGMEMT_DOWN:
            s_left = left;
            s_right = s_left + m_digit_size;
            s_bottom = top + m_digit_size;
            s_top = s_bottom - m_digit_line_size;
            break;
       default:
            return;
    }

    drawRegion(addr, width, height, stride, format, color, s_left, s_top, s_right, s_bottom);
}

void IndexBufferGenerator::drawRegion(void* addr, uint32_t width, uint32_t height, uint32_t stride,
        unsigned int format, uint32_t color, uint32_t left, uint32_t top, uint32_t right,
        uint32_t bottom)
{
    unsigned int bpp = getBitsPerPixel(format) / 8;
    for (uint32_t y = top; y < bottom; y++) {
        if (y >= height) {
            break;
        }

        for (uint32_t x = left; x < right; x++) {
            if (x >= width) {
                break;
            }

            uint8_t* ptr = reinterpret_cast<uint8_t*>(addr);
            uint8_t* pixel = ptr + bpp * (x + stride * y);
            pixel[0] = static_cast<uint8_t>(color & 0xff);
            pixel[1] = static_cast<uint8_t>((color >> 8) & 0xff);
            pixel[2] = static_cast<uint8_t>((color >> 16) & 0xff);
            pixel[3] = static_cast<uint8_t>((color >> 24) & 0xff);
        }
    }
}

void IndexBufferGenerator::drawBuffer(BufferInfo& info, int index)
{
    void* ptr = mmap(nullptr, info.allocate_size, PROT_READ | PROT_WRITE, MAP_SHARED, info.fd, 0);
    if (ptr)
    {
        uint32_t color = 0;
        uint32_t remainder = m_draw_count % m_num_bar;
        switch (remainder)
        {
            case 0:
                color = COLOR_BLACK;
                break;
            case 1:
                color = COLOR_GRAY;
                break;
            case 2:
                color = COLOR_WHITE;
                break;
        }
        uint32_t left = m_digit_interval_size;
        uint32_t top = m_digit_interval_size + (m_digit_interval_size + m_digit_size) * remainder;
        drawBackground(ptr, info.width, info.height, info.stride, info.format, color);
        color = COLOR_RED;
        drawIndex(ptr, info.width, info.height, info.stride, info.format, color, left, top, index);
        munmap(ptr, info.allocate_size);
    }
    info.index = index;
    m_draw_count++;
}

void IndexBufferGenerator::gereratorThread()
{
    while (true)
    {
        HWC_ATRACE_NAME("gererateThread");
        BufferInfo info;
        initialBufferInfo(&info);
        int request_index = 0;
        {
            std::unique_lock<std::mutex> lock(m_lock);
            if (m_stop)
            {
                break;
            }

            if (m_request_index == -1)
            {
                m_condition.wait(lock);
                continue;
            }

            request_index = m_request_index;
            if (m_free_buffer)
            {
                info = generateBuffer(m_width, m_height);
                m_free_buffer--;
            }
            else if (!m_free_list.empty())
            {
                info = m_free_list.front();
                m_free_list.pop_front();
            }
            else
            {
                m_condition.wait(lock);
                continue;
            }
        }

        if (m_index <= request_index)
        {
            m_index = request_index + m_index_interval;
        }
        drawBuffer(info, m_index);
        m_index++;

        {
            std::lock_guard<std::mutex> lock(m_lock);
            m_ready_list.emplace_back(info);
        }
    }
}

void IndexBufferGenerator::initialBufferInfo(BufferInfo* info)
{
    if (info)
    {
        memset(info, 0, sizeof(*info));
        info->handle = nullptr;
        info->fd = -1;
    }
}
