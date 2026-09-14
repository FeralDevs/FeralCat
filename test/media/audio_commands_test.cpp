#include "audio_commands.h"
#include <cstdio>
#include <cstdlib>
#include <deque>

using namespace meow::media;
namespace {
unsigned checks = 0;
void require(bool value, const char* label)
{
    ++checks;
    if (!value) { std::fprintf(stderr, "FAIL: %s\n", label); std::exit(1); }
}
struct Request { Command command{}; uint32_t generation = 0; };
std::deque<Request> queue;
Request next()
{
    Request request = queue.front(); queue.pop_front();
    coalesceVolume(request,
        [](Request& result) { if (queue.empty()) return false; result = queue.front(); return true; },
        [](Request& result) { if (queue.empty()) return false; result = queue.front(); queue.pop_front(); return true; });
    return request;
}
}
int main()
{
    for (int i = 0; i < 8; ++i) queue.push_back({{Action::Volume, 35 + i * 5}, 7});
    auto request = next();
    require(request.command.value == 70 && queue.empty(), "eight rapid volume requests collapse to final value");
    queue = {{{Action::Volume, 35}, 1}, {{Action::Volume, 40}, 1}, {{Action::Play, 9}, 4},
             {{Action::Volume, 45}, 4}, {{Action::Equalizer, 2}, 4}, {{Action::Volume, 50}, 4}};
    require(next().command.value == 40, "volume run stops at Play");
    request = next();
    require(request.command.action == Action::Play && request.command.value == 9 && request.generation == 4,
            "Play ordering and catalog generation preserved");
    require(next().command.value == 45, "volume before EQ kept before EQ");
    require(next().command.action == Action::Equalizer, "EQ acts as ordering barrier");
    require(next().command.value == 50 && queue.empty(), "volume after EQ kept after EQ");
    queue = {{{Action::Volume, 90}, 2}, {{Action::Pause, 0}, 2}, {{Action::Stop, 0}, 2}};
    require(next().command.value == 90, "pause is not eaten by volume coalescing");
    require(next().command.action == Action::Pause, "pause preserved");
    require(next().command.action == Action::Stop && queue.empty(), "stop preserved");
    // The production loop is bounded even if a producer keeps filling its
    // queue while the worker drains. There is still a PCM pump after <=8 pops.
    unsigned peeks = 0, pops = 0;
    request = {{Action::Volume, 0}, 0};
    coalesceVolume(request, [&](Request& out) { ++peeks; out = {{Action::Volume, 100}, 0}; return true; },
        [&](Request& out) { ++pops; out = {{Action::Volume, 100}, 0}; return true; });
    require(peeks == 7 && pops == 7 && request.command.value == 100, "continuous volume producer cannot starve PCM pumping");
    for (unsigned burst = 0; burst < 100000; ++burst) {
        for (int volume = 65; volume <= 100; volume += 5) queue.push_back({{Action::Volume, volume}, burst});
        request = next();
        if (request.command.action != Action::Volume || request.command.value != 100 || !queue.empty())
            require(false, "sustained burst result");
    }
    require(queue.empty(), "eight hundred thousand volume commands leave no queued residue");
    std::printf("Audio commands: %u checks passed; 800000 volume commands.\n", checks);
}
