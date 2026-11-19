# NES audio/video HAL verification

The host target compiles the production `nes_av.cpp` against hardware doubles.
It preserves the existing display DMA/pitch/palette and audio ownership,
volume, failure cleanup and stop-retry checks.

```powershell
cmake -S test/nes_av -B C:/Projekte/Meowkit/.work/nes-audio-20260922/av-host -G "Visual Studio 18 2026" -A x64
cmake --build C:/Projekte/Meowkit/.work/nes-audio-20260922/av-host --config Release
ctest --test-dir C:/Projekte/Meowkit/.work/nes-audio-20260922/av-host -C Release --output-on-failure
```

The clocked I2S double consumes a bounded DMA FIFO at 44,100 stereo frames per
second independently of the producer. It starts with the configured zeroed
DMA capacity, blocks writes when full and supports partial writes. A fixed
60-frame PCM sequence verifies every stereo sample in order, with ordinary
LCD work and recurring longer LCD work. A deliberately slow producer is a
positive control that must exhibit starvation; the producer paced through
I2S backpressure must retain every sample without starvation. This is a
supply/demand test, not a replay of measured MeowKit timing or a claim about
device sound quality. The Windows test process requests 1 ms timer resolution
and releases that request on exit; its default ~15.6 ms sleep resolution does
not represent the modeled 1 ms FreeRTOS tick.

Separate tests verify the initial/recovery reserve, small-block full-pool
escape, no DMA zeroing during recovery, accepted-prefix retry with no loss or
duplication, and that software-pending accounting reaches zero after writes.

`audioPendingSamples()` counts only accepted PCM still in the queue/worker,
not samples already copied to hardware DMA. It is a conservative reserve for
the session's display decision. `audioPrefilling()` requests faster production
until two NTSC frames (1,470 samples) or all three software blocks are ready.
Neither software nor DMA capacity was enlarged. The caller must retry a
`submitAudio()` suffix before advancing/reusing the core's PCM buffer.

The pure `audioAllowsDisplay()` policy is tested with a 70 ms LCD outlier:
its requested reserve is capped at two PCM frames, and a stale measurement
permits another draw after 250 ms once audio prefill is complete. Otherwise a
single costly transfer could demand more than the entire software queue and
permanently prevent the next draw that would update that measurement.

The lifecycle follows [ESP-IDF 4.4.3's I2S implementation](https://github.com/espressif/esp-idf/blob/v4.4.3/components/driver/i2s.c):
driver installation starts DMA; calling `i2s_start()` again resets it. DMA must
run to return writable descriptors, so the reserve is formed in software,
with the zeroed hardware ring left running. Underflow recovery never clears
that live ring. This harness does not model codec acoustics, exact descriptor
interrupt phase or display bus throughput; final sound verification needs
the MeowKit.
