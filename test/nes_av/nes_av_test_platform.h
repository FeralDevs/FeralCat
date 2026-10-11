#pragma once

// Host hardware doubles. They compile the production HAL unchanged and model
// deferred DMA reads, bounded FreeRTOS queues, partial I2S writes and failures.
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>

using TickType_t = uint32_t;
using esp_err_t = int;
constexpr int ESP_OK = 0;
constexpr int ESP_ERR_TIMEOUT = 0x107;
constexpr int ESP_FAIL = -1;
constexpr int pdTRUE = 1;
constexpr int pdFALSE = 0;
constexpr int pdPASS = 1;
constexpr int LOW = 0;
constexpr int HIGH = 1;
constexpr unsigned MALLOC_CAP_INTERNAL = 1;
constexpr unsigned MALLOC_CAP_8BIT = 2;
constexpr unsigned MALLOC_CAP_DMA = 4;
constexpr int ESP_INTR_FLAG_LEVEL1 = 1;
constexpr int HAL_IOEXP_PA_EN = 2;
constexpr int HAL_PIN_I2S_BCLK = 14;
constexpr int HAL_PIN_I2S_WS = 13;
constexpr int HAL_PIN_I2S_DOUT = 16;
inline int In_I2C = 0;
inline TickType_t pdMS_TO_TICKS(uint32_t ms) { return ms; }
inline void vTaskDelay(TickType_t ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }
inline int64_t esp_timer_get_time()
{
    return std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}
struct FakeSerial { void println(const char*) {} };
inline FakeSerial Serial;

namespace avtest {
inline std::atomic<int> allocations{0};
inline std::atomic<int> allocationAttempt{0};
inline std::atomic<int> failAllocationAt{0};
inline std::atomic<int> dmaAllocations{0};
inline std::atomic<int> queues{0};
inline std::atomic<int> tasks{0};
inline std::atomic<bool> failTask{false};
inline std::atomic<bool> holdTask{false};
inline std::atomic<bool> failCodec{false};
inline std::atomic<bool> failI2s{false};
inline std::atomic<bool> failPins{false};
inline std::atomic<bool> stallWrites{false};
inline std::atomic<bool> errorWrites{false};
inline std::atomic<bool> installed{false};
inline std::atomic<size_t> maxWriteBytes{4096};
inline std::atomic<bool> clockedI2s{false};
inline std::atomic<unsigned> starts{0}, zeros{0};
inline std::mutex pcmMutex;
inline std::vector<int16_t> pcm;
// Clocked FIFO model: install starts a zero-filled DMA ring. Its fixed sample
// clock is independent of the producer; writes wait for space, not frame time.
inline std::deque<bool> dmaFrames;
inline size_t dmaCapacity = 0;
inline int64_t dmaUpdated = 0;
inline int64_t dmaRemainder = 0;
inline bool heardPcm = false;
inline uint64_t gapSamples = 0, playedPcm = 0;
inline void advanceDma()
{
    if (!clockedI2s) return;
    const int64_t now = esp_timer_get_time();
    const int64_t numerator = (now-dmaUpdated)*44100 + dmaRemainder;
    size_t consumed = static_cast<size_t>(numerator/1000000);
    dmaRemainder = numerator%1000000; dmaUpdated = now;
    while (consumed && !dmaFrames.empty()) {
        if (dmaFrames.front()) { heardPcm = true; ++playedPcm; }
        else if (heardPcm) ++gapSamples;
        dmaFrames.pop_front(); --consumed;
    }
    if (heardPcm) gapSamples += consumed;
}
inline void reset()
{
    allocationAttempt = 0;
    failAllocationAt = 0;
    dmaAllocations = 0;
    failTask = false; holdTask = false; failCodec = false;
    failI2s = false; failPins = false; stallWrites = false; errorWrites = false;
    maxWriteBytes = 4096;
    clockedI2s = false; starts = 0; zeros = 0;
    std::lock_guard<std::mutex> guard(pcmMutex);
    pcm.clear(); dmaFrames.clear(); dmaCapacity=0; dmaRemainder=0;
    heardPcm=false; gapSamples=playedPcm=0;
}
}

inline void* heap_caps_malloc(size_t bytes, unsigned caps)
{
    const int attempt = ++avtest::allocationAttempt;
    if (attempt == avtest::failAllocationAt) return nullptr;
    if (caps & MALLOC_CAP_DMA) {
        if (!(caps & MALLOC_CAP_INTERNAL)) throw std::runtime_error("DMA buffer is not internal");
        ++avtest::dmaAllocations;
    }
    auto* p = std::malloc(bytes);
    if (p) ++avtest::allocations;
    return p;
}
inline void heap_caps_free(void* p)
{
    if (p) { --avtest::allocations; std::free(p); }
}

struct FakeQueue {
    FakeQueue(size_t capacity, size_t itemSize) : capacity(capacity), itemSize(itemSize) {}
    size_t capacity, itemSize;
    std::mutex mutex;
    std::condition_variable changed;
    std::deque<std::vector<uint8_t>> items;
};
using QueueHandle_t = FakeQueue*;
using SemaphoreHandle_t = FakeQueue*;
using TaskHandle_t = void*;
inline QueueHandle_t xQueueCreate(size_t capacity, size_t itemSize)
{
    ++avtest::queues;
    return new FakeQueue(capacity, itemSize);
}
inline int xQueueSend(QueueHandle_t queue, const void* value, TickType_t timeout)
{
    std::unique_lock<std::mutex> lock(queue->mutex);
    if (!queue->changed.wait_for(lock, std::chrono::milliseconds(timeout),
                                [&] { return queue->items.size() < queue->capacity; })) return pdFALSE;
    const auto* data = static_cast<const uint8_t*>(value);
    queue->items.emplace_back(data, data + queue->itemSize);
    queue->changed.notify_all();
    return pdTRUE;
}
inline int xQueueReceive(QueueHandle_t queue, void* value, TickType_t timeout)
{
    std::unique_lock<std::mutex> lock(queue->mutex);
    if (!queue->changed.wait_for(lock, std::chrono::milliseconds(timeout),
                                [&] { return !queue->items.empty(); })) return pdFALSE;
    std::memcpy(value, queue->items.front().data(), queue->itemSize);
    queue->items.pop_front();
    queue->changed.notify_all();
    return pdTRUE;
}
inline void vQueueDelete(QueueHandle_t queue) { --avtest::queues; delete queue; }
inline SemaphoreHandle_t xSemaphoreCreateBinary() { return xQueueCreate(1, 1); }
inline int xSemaphoreGive(SemaphoreHandle_t semaphore) { uint8_t value = 1; return xQueueSend(semaphore, &value, 0); }
inline int xSemaphoreTake(SemaphoreHandle_t semaphore, TickType_t timeout) { uint8_t value; return xQueueReceive(semaphore, &value, timeout); }
inline void vSemaphoreDelete(SemaphoreHandle_t semaphore) { vQueueDelete(semaphore); }
inline int xTaskCreatePinnedToCore(void (*entry)(void*), const char*, uint32_t,
                                 void* context, int, TaskHandle_t* handle, int)
{
    if (avtest::failTask) return pdFALSE;
    *handle = reinterpret_cast<void*>(1);
    ++avtest::tasks;
    std::thread([=] {
        while (avtest::holdTask) vTaskDelay(1);
        entry(context);
        --avtest::tasks;
    }).detach();
    return pdPASS;
}
inline void vTaskDelete(void*) {}

using i2s_mode_t = int;
constexpr int I2S_NUM_0 = 0;
constexpr int I2S_MODE_MASTER = 1;
constexpr int I2S_MODE_TX = 2;
constexpr int I2S_BITS_PER_SAMPLE_16BIT = 16;
constexpr int I2S_CHANNEL_FMT_RIGHT_LEFT = 2;
constexpr int I2S_COMM_FORMAT_STAND_I2S = 1;
constexpr int I2S_PIN_NO_CHANGE = -1;
struct i2s_config_t {
    int mode = 0;
    uint32_t sample_rate = 0;
    int bits_per_sample = 0, channel_format = 0, communication_format = 0;
    int intr_alloc_flags = 0;
    size_t dma_buf_count = 0, dma_buf_len = 0;
    bool use_apll = false, tx_desc_auto_clear = false;
};
struct i2s_pin_config_t { int mck_io_num = 0, bck_io_num = 0, ws_io_num = 0, data_out_num = 0, data_in_num = 0; };
inline esp_err_t i2s_driver_install(int, const i2s_config_t* config, int, void*)
{
    if (avtest::failI2s || avtest::installed) return ESP_FAIL;
    if (config->sample_rate != 44100 || config->bits_per_sample != 16 ||
        config->channel_format != I2S_CHANNEL_FMT_RIGHT_LEFT) throw std::runtime_error("Wrong PCM hardware format");
    avtest::installed = true;
    std::lock_guard<std::mutex> guard(avtest::pcmMutex);
    avtest::dmaCapacity = config->dma_buf_len * config->dma_buf_count;
    avtest::dmaFrames.assign(avtest::dmaCapacity,false);
    avtest::dmaUpdated = esp_timer_get_time();
    ++avtest::starts;
    return ESP_OK;
}
inline esp_err_t i2s_set_pin(int, const i2s_pin_config_t*) { return avtest::failPins ? ESP_FAIL : ESP_OK; }
inline esp_err_t i2s_zero_dma_buffer(int) { ++avtest::zeros; return ESP_OK; }
inline esp_err_t i2s_start(int) { ++avtest::starts; return ESP_OK; }
inline esp_err_t i2s_stop(int) { return ESP_OK; }
inline esp_err_t i2s_driver_uninstall(int) { avtest::installed = false; return ESP_OK; }
inline esp_err_t i2s_write(int, const void* data, size_t bytes, size_t* used, TickType_t timeout)
{
    *used = 0;
    if (avtest::errorWrites) return ESP_FAIL;
    if (avtest::stallWrites) { vTaskDelay(timeout); return ESP_ERR_TIMEOUT; }
    const int64_t deadline=esp_timer_get_time()+int64_t(timeout)*1000;
    for (;;) {
        {
            std::lock_guard<std::mutex> guard(avtest::pcmMutex);
            avtest::advanceDma();
            size_t capacity=avtest::clockedI2s ? (avtest::dmaCapacity-avtest::dmaFrames.size())*4 : bytes;
            *used=std::min(std::min(bytes,avtest::maxWriteBytes.load()),capacity);
            *used-=*used%4;
            if (*used) {
                const auto* pcm = static_cast<const int16_t*>(data);
                avtest::pcm.insert(avtest::pcm.end(),pcm,pcm+*used/sizeof(int16_t));
                if (avtest::clockedI2s) avtest::dmaFrames.insert(avtest::dmaFrames.end(),*used/4,true);
                return ESP_OK;
            }
        }
        if (esp_timer_get_time()>=deadline) return ESP_ERR_TIMEOUT;
        vTaskDelay(1);
    }
}

struct speaker_config_t { uint32_t sample_rate = 22050; uint8_t bits_per_sample = 16; };
class FakeSpeaker {
    speaker_config_t config_;
public:
    bool enabled = false;
    bool attenuated = false;
    speaker_config_t& config() { return config_; }
    bool beginCodecOnly(int*) { enabled = true; return !avtest::failCodec; }
    bool setPlayerDacAttenuation() { attenuated = true; return true; }
    bool setMute(bool) { return true; }
    void end() { enabled = false; }
};
class FakeIo {
public:
    bool amp = false;
    bool digitalWrite(int, int level) { amp = level != 0; return true; }
};
class FakeLcd {
    const uint16_t* pending_ = nullptr;
    size_t count_ = 0;
    uint8_t depth_ = 24;
public:
    int x = 0, y = 0, w = 0, h = 0, transactions = 0;
    std::vector<uint16_t> pixels;
    std::vector<const uint16_t*> stripAddresses;
    int width() const { return 320; }
    int height() const { return 240; }
    uint8_t getColorDepth() const { return depth_; }
    void setColorDepth(uint8_t depth) { depth_ = depth; }
    void startWrite() { ++transactions; }
    void endWrite() { if (pending_) throw std::runtime_error("DMA escaped submitFrame"); --transactions; }
    void setAddrWindow(int px, int py, int width, int height) { x=px; y=py; w=width; h=height; pixels.clear(); }
    bool dmaBusy() const { return pending_ != nullptr; }
    void waitDMA()
    {
        // Intentionally read the source only on completion, after the next strip
        // has been converted. Premature reuse corrupts the captured frame.
        for (size_t i=0; i<count_; ++i) {
            const uint16_t value = pending_[i];
            pixels.push_back(static_cast<uint16_t>((value << 8) | (value >> 8)));
        }
        pending_ = nullptr; count_ = 0;
    }
    void writePixelsDMA(const uint16_t* data, size_t count, bool swap)
    {
        if (pending_ || swap || depth_ != 16) throw std::runtime_error("Invalid DMA transfer");
        pending_ = data; count_ = count;
        stripAddresses.push_back(data);
    }
};
class DEVICES {
public:
    FakeLcd Lcd;
    FakeSpeaker speaker;
    FakeIo io_exp;
};
