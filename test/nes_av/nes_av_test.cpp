#include "nes_av_test_platform.h"
#include "nes_av.h"
#include <iostream>
#include <string>
#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>
#endif

using meow::nes::Av;

void require(bool condition, const char* reason)
{
    if (!condition) throw std::runtime_error(reason);
}

template<class Predicate> void until(Predicate predicate, const char* reason)
{
    const auto deadline = esp_timer_get_time() + 1000000;
    while (!predicate() && esp_timer_get_time() < deadline) vTaskDelay(1);
    require(predicate(), reason);
}

void clean()
{
    until([] { return avtest::tasks == 0; }, "audio task did not finish");
    require(avtest::allocations == 0, "HAL leaked internal allocation");
    require(avtest::queues == 0, "HAL leaked queue/semaphore");
    require(!avtest::installed, "HAL leaked I2S ownership");
}

void videoPitchAndDmaLifetime()
{
    avtest::reset();
    DEVICES device;
    Av av;
    require(av.begin(&device, false), "video-only begin failed");
    require(avtest::dmaAllocations == 2, "expected exactly two internal DMA strips");
    std::vector<uint8_t> padded(272 * 240, 255);
    uint16_t palette[256];
    for (unsigned i=0; i<256; ++i) palette[i] = static_cast<uint16_t>((i << 8) | (255-i));
    // Non-symmetric RGB565 values make accidental byte swapping observable.
    palette[0] = 0xf800; palette[1] = 0x07e0; palette[2] = 0x001f;
    for (size_t row=0; row<240; ++row)
        for (size_t col=0; col<256; ++col) padded[row*272+8+col] = static_cast<uint8_t>((row+col)%251);
    const auto before = padded;
    require(av.submitFrame(padded.data()+8, 272, palette), "frame failed");
    require(device.Lcd.x==32 && device.Lcd.y==0 && device.Lcd.w==256 && device.Lcd.h==240,
            "game changed sidebars or viewport");
    require(device.Lcd.pixels.size()==256*240, "frame length is wrong");
    for (size_t row=0; row<240; ++row)
        for (size_t col=0; col<256; ++col)
            require(device.Lcd.pixels[row*256+col]==palette[(row+col)%251], "pitch, palette, byte order or strip lifetime corrupted frame");
    require(padded==before, "renderer modified core-owned frame");
    require(!device.Lcd.dmaBusy() && device.Lcd.transactions==0, "renderer retained LCD transaction");
    require(av.stats().frames==1 && av.stats().dmaWaits>0, "frame stats missing");
    require(!av.submitFrame(padded.data(), 255, palette), "invalid pitch accepted");
    require(av.stop(), "video stop failed");
    require(device.Lcd.getColorDepth()==24, "previous LCD depth not restored");
    clean();
}

void audioCopiesAndPartialWrites()
{
    avtest::reset();
    avtest::holdTask = true;
    avtest::maxWriteBytes = 100; // Force partial writes, always whole stereo frames.
    DEVICES device;
    Av av;
    require(av.begin(&device), "audio begin failed");
    require(avtest::starts==1, "I2S was reset again after install started DMA");
    require(device.speaker.attenuated && device.io_exp.amp, "codec gain/amp path missing");
    std::vector<int16_t> source(1800);
    for (size_t i=0; i<source.size(); ++i) source[i] = static_cast<int16_t>(i*17-15000);
    const auto expected=source;
    require(av.submitAudio(source.data(), source.size(), 8)==source.size(), "PCM queue copy failed");
    require(av.audioPendingSamples()==source.size(), "software reserve accounting missing");
    std::fill(source.begin(), source.end(), static_cast<int16_t>(0)); // Core reuses memory before worker reads it.
    avtest::holdTask = false;
    until([&] { return av.stats().audioSamplesWritten==expected.size(); }, "PCM did not drain");
    require(av.audioPendingSamples()==0 && !av.audioPrefilling(), "drained reserve not accounted");
    {
        std::lock_guard<std::mutex> lock(avtest::pcmMutex);
        require(avtest::pcm.size()==expected.size()*2, "PCM channel/sample count wrong");
        for (size_t i=0; i<expected.size(); ++i)
            require(avtest::pcm[i*2]==expected[i] && avtest::pcm[i*2+1]==expected[i], "PCM memory reused or mono duplication wrong");
    }
    require(av.stop(), "audio stop failed");
    require(!device.speaker.enabled && !device.io_exp.amp, "audio hardware not released");
    require(device.speaker.config().sample_rate==22050, "speaker configuration not restored");
    require(av.stats().audioSamplesQueued==expected.size(), "stats lost after stop");
    require(av.stop(), "stop is not idempotent");
    clean();
}

void boundedQueueAndStopRetry()
{
    avtest::reset();
    avtest::holdTask = true;
    DEVICES device;
    Av av;
    require(av.begin(&device), "begin failed");
    std::vector<int16_t> source(3072, 123);
    require(av.submitAudio(source.data(), source.size(), 8)==source.size(), "pool should fit 3 blocks");
    const int64_t start=esp_timer_get_time();
    require(av.submitAudio(source.data(), 1, 5)==0, "full queue did not time out");
    require(esp_timer_get_time()-start<200000, "queue call blocked without bound");
    require(av.stats().audioQueueTimeouts==1, "queue timeout not measured");
    require(!av.stop(2), "stop should report unacknowledged worker");
    require(av.active() && avtest::installed && avtest::allocations>0,
            "timeout released hardware/memory still owned by worker");
    avtest::holdTask = false;
    require(av.stop(1000), "stop retry failed");
    require(av.stats().stopTimeouts==1, "stop timeout stat lost");
    clean();
}

void audioVolumeRuntimeAndPersistence()
{
    avtest::reset();
    DEVICES device;
    Av av;
    // Include both int16 limits and values that truncate toward zero.
    const int16_t source[] = {32767, -32768, 1000, -1000, 3, -3, 0};
    const int16_t quarter[] = {8191, -8192, 250, -250, 0, 0, 0};
    const int16_t muted[] = {0, 0, 0, 0, 0, 0, 0};
    constexpr size_t count = sizeof(source) / sizeof(source[0]);
    auto verifyBlock = [&](const int16_t* expected) {
        size_t offset;
        {
            std::lock_guard<std::mutex> lock(avtest::pcmMutex);
            offset = avtest::pcm.size();
        }
        const uint32_t written = av.stats().audioSamplesWritten;
        require(av.submitAudio(source, count)==count, "volume PCM submit failed");
        until([&] { return av.stats().audioSamplesWritten==written+count; }, "volume PCM did not drain");
        std::lock_guard<std::mutex> lock(avtest::pcmMutex);
        require(avtest::pcm.size()==offset+count*2, "volume changed sample count");
        for (size_t i=0; i<count; ++i)
            require(avtest::pcm[offset+i*2]==expected[i] && avtest::pcm[offset+i*2+1]==expected[i],
                    "volume scaling, mute, clamping or stereo duplication wrong");
    };
    av.setVolume(25);
    require(av.begin(&device), "volume begin failed");
    auto prime = [&] {
        std::vector<int16_t> silence(1470,0);
        const auto before=av.stats().audioSamplesWritten;
        require(av.submitAudio(silence.data(),silence.size())==silence.size(), "volume prefill failed");
        until([&] { return av.stats().audioSamplesWritten==before+silence.size(); }, "volume prefill did not drain");
    };
    prime();
    verifyBlock(quarter);
    av.setVolume(0);
    verifyBlock(muted);
    av.setVolume(200);
    verifyBlock(source);
    av.setVolume(25);
    require(av.stop(), "volume stop failed");
    clean();
    require(av.begin(&device), "volume restart failed");
    prime();
    verifyBlock(quarter);
    require(av.stop(), "volume final stop failed");
    clean();
}

void stalledDriverAndBeginFailures()
{
    avtest::reset();
    DEVICES device;
    {
        Av av;
        avtest::stallWrites = true;
        require(av.begin(&device), "begin failed");
        const std::vector<int16_t> samples(1470,123);
        require(av.submitAudio(samples.data(),samples.size())==samples.size(), "submit failed");
        until([&] { return av.stats().audioErrors>0; }, "I2S no-progress bound missing");
        require(av.stats().audioWriteTimeouts>0, "I2S timeout not measured");
        require(av.stop(), "stalled I2S prevented cleanup");
    }
    clean();
    for (int failure=1; failure<=7; ++failure) {
        avtest::reset();
        if (failure<=3) avtest::failAllocationAt=failure;
        if (failure==4) avtest::failCodec=true;
        if (failure==5) avtest::failI2s=true;
        if (failure==6) avtest::failPins=true;
        if (failure==7) avtest::failTask=true;
        Av av;
        require(!av.begin(&device), "injected begin failure was ignored");
        require(!av.active(), "partial initialization retained resources");
        require(std::strlen(av.error())>0, "begin failure has no reason");
        require(!device.io_exp.amp, "failed begin left amp on");
        clean();
    }
}

void prefillAndRetainedSuffix()
{
    avtest::reset();
    DEVICES device;
    Av av;
    require(av.begin(&device), "prefill begin failed");
    std::vector<int16_t> source(735,4321);
    require(av.audioPrefilling(), "session did not request PCM reserve");
    require(av.submitAudio(source.data(),source.size())==source.size(), "first prefill failed");
    vTaskDelay(15);
    require(av.stats().audioSamplesWritten==0 && av.audioPendingSamples()==735,
            "worker started before PCM reserve existed");
    require(av.submitAudio(source.data(),source.size())==source.size(), "second prefill failed");
    until([&] { return av.stats().audioSamplesWritten==1470; }, "prefill did not start playback");
    // A real starvation resumes by refilling; queue-empty alone is not enough.
    until([&] { return av.stats().audioUnderruns==1 && av.audioPrefilling(); }, "starvation did not request recovery reserve");
    require(av.submitAudio(source.data(),source.size())==source.size(), "recovery first block failed");
    vTaskDelay(5);
    require(av.stats().audioSamplesWritten==1470, "recovery consumed isolated fragment");
    require(av.submitAudio(source.data(),source.size())==source.size(), "recovery second block failed");
    until([&] { return av.stats().audioSamplesWritten==2940; }, "recovery did not preserve PCM");
    require(avtest::zeros==1, "worker cleared live DMA while recovering");
    require(av.stop(), "prefill stop failed"); clean();

    require(av.begin(&device), "small-block prefill begin failed");
    for(unsigned i=0;i<3;++i) require(av.submitAudio(source.data(),7)==7, "small-block submit failed");
    until([&] { return av.stats().audioSamplesWritten==21; }, "full pool of small blocks deadlocked prefill");
    require(av.stop(), "small-block stop failed"); clean();

    avtest::reset(); avtest::holdTask=true;
    require(av.begin(&device), "retry begin failed");
    std::vector<int16_t> large(4096);
    for(size_t i=0;i<large.size();++i) large[i]=static_cast<int16_t>(i*7-12000);
    const size_t prefix=av.submitAudio(large.data(),large.size(),2);
    require(prefix==3072, "bounded pool did not return accepted prefix");
    avtest::holdTask=false;
    require(av.submitAudio(large.data()+prefix,large.size()-prefix,100)==large.size()-prefix,
            "retained PCM suffix did not resume");
    until([&] { return av.stats().audioSamplesWritten==large.size(); }, "retried PCM did not drain");
    {
        std::lock_guard<std::mutex> lock(avtest::pcmMutex);
        require(avtest::pcm.size()==large.size()*2, "retry duplicated or lost samples");
        for(size_t i=0;i<large.size();++i)
            require(avtest::pcm[2*i]==large[i] && avtest::pcm[2*i+1]==large[i], "retry changed PCM order");
    }
    require(av.stop(), "retry stop failed"); clean();
}

uint64_t clockedProducer(bool independentFrameWait)
{
    avtest::reset(); avtest::clockedI2s=true; avtest::maxWriteBytes=256*4;
    DEVICES device;
    Av av;
    require(av.begin(&device), "clocked begin failed");
    constexpr unsigned Frames=60, Samples=735;
    std::vector<int16_t> source(Samples);
    for(unsigned frame=0;frame<Frames;++frame) {
        vTaskDelay(2); // Bounded emulation work.
        for(size_t i=0;i<source.size();++i) source[i]=static_cast<int16_t>((frame*Samples+i)%30000+1);
        size_t accepted=0;
        while(accepted<source.size()) {
            accepted+=av.submitAudio(source.data()+accepted,source.size()-accepted,100);
            require(av.stats().audioErrors==0, "clocked driver unexpectedly failed");
        }
        if(!av.audioPrefilling()) vTaskDelay(frame%15==14?24:5); // LCD cost/jitter.
        if(independentFrameWait) vTaskDelay(17); // Deliberately underproduce: supply/demand regression control.
    }
    until([&] { return av.stats().audioSamplesWritten==Frames*Samples; }, "clocked PCM did not drain");
    uint64_t gaps;
    {
        std::lock_guard<std::mutex> lock(avtest::pcmMutex);
        gaps=avtest::gapSamples;
        require(avtest::pcm.size()==Frames*Samples*2, "clocked pipeline lost PCM");
        for(size_t i=0;i<Frames*Samples;++i) {
            const auto expected=static_cast<int16_t>(i%30000+1);
            require(avtest::pcm[2*i]==expected && avtest::pcm[2*i+1]==expected,
                    "clocked pipeline reordered or duplicated PCM");
        }
    }
    require(av.stop(), "clocked stop failed"); clean();
    return gaps;
}

void displayBudgetRecovery()
{
    using meow::nes::audioAllowsDisplay;
    // Ordinary 17 ms work reserve rounds upward to 750 PCM samples.
    require(!audioAllowsDisplay(false,749,735,3000,13000,16), "LCD budget rounded down");
    require(audioAllowsDisplay(false,750,735,3000,13000,16), "ordinary LCD budget rejected");
    // An isolated 70 ms transfer previously requested >3,200 samples, beyond
    // the three 735-sample blocks, and consequently prevented all future draws.
    require(audioAllowsDisplay(false,1470,735,2000,70000,16), "70 ms outlier permanently suppressed LCD");
    require(!audioAllowsDisplay(false,1469,735,2000,70000,249), "outlier cap lost two-frame reserve");
    require(audioAllowsDisplay(false,0,735,2000,70000,250), "stale measurement prevented bounded retry");
    require(!audioAllowsDisplay(true,2205,735,2000,70000,1000), "display interrupted audio prefill");
    require(audioAllowsDisplay(false,1764,882,2000,70000,16), "PAL reserve cap incorrect");
    require(audioAllowsDisplay(false,1470,735,UINT32_MAX,UINT32_MAX,16), "timing overflow broke bounded reserve");
    require(!audioAllowsDisplay(false,0,0,0,0,1000), "invalid frame size accepted");
}

int main()
{
#ifdef _WIN32
    // Windows otherwise rounds short sleeps to ~15.6 ms, unlike the modeled
    // 1 ms FreeRTOS tick. Restore the timer request when the process exits.
    struct TimerResolution {
        TimerResolution() { timeBeginPeriod(1); }
        ~TimerResolution() { timeEndPeriod(1); }
    } timerResolution;
#endif
    try {
        videoPitchAndDmaLifetime();
        audioCopiesAndPartialWrites();
        audioVolumeRuntimeAndPersistence();
        boundedQueueAndStopRetry();
        stalledDriverAndBeginFailures();
        prefillAndRetainedSuffix();
        displayBudgetRecovery();
        const auto oldGaps=clockedProducer(true), clockedGaps=clockedProducer(false);
        require(oldGaps>735, "timing reproduction did not expose slow producer starvation");
        require(clockedGaps==0, "I2S-paced producer starved despite bounded LCD jitter");
        std::cout << "NES AV: DMA/PCM/volume/lifecycle/prefill/retry passed; 44100 Hz timing gaps slow_producer="
                  << oldGaps << " I2S_backpressure=" << clockedGaps << " samples\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
