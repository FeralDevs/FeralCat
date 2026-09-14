#pragma once
#include "media_api.h"

class DEVICES;

namespace meow::media {
// One active service owns the shared audio clocks and I2S0. begin/end run on
// the app owner, outside Lua. Getters and commands never access the SD card.
class AudioService final : public Host {
public:
    AudioService() = default;
    ~AudioService();
    AudioService(const AudioService&) = delete;
    AudioService& operator=(const AudioService&) = delete;
    bool begin(DEVICES* devices);
    // Cooperative join: returns only after the worker has closed all files
    // and destroyed the decoder/I2S driver. Never force-deletes a live worker.
    void end();
    bool ready() const;
    bool command(Command command) override;
    bool status(Status& out) override;
    bool tracks(uint16_t playlist, size_t offset, size_t limit, Page& out) override;
    bool playlists(size_t offset, size_t limit, Page& out) override;
    // Main-app UI only, outside Lua: 96x96 RGB565. Returns false if busy.
    // Pixels are copied only when revision changed and a cover is available.
    bool copyCover(uint32_t& revision, bool& available, uint16_t* pixels, size_t count);
private:
    struct Impl;
    Impl* impl_ = nullptr;
};
} // namespace meow::media
