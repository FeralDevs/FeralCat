#include "../../src/system/emulation/nes_input.h"
#include <cstdlib>
#include <cstdio>
using meow::nes::Input;
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr,"check failed line %d\n",__LINE__); return EXIT_FAILURE; } } while (0)
int main() {
    Input input;
    CHECK(input.update(0,0)==0);
    CHECK(input.update(0x82,10)==0); // Run right while holding B.
    CHECK(input.update(0x82,15)==0x82);
    for(uint32_t ms=16;ms<12000;ms+=16) CHECK(input.update(0x82,ms)==0x82);
    CHECK(input.update(0x83,12000)==0x82);
    CHECK(input.update(0x83,12005)==0x83); // Jump keeps run/direction held.
    CHECK(input.update(0x82,12006)==0x83);
    CHECK(input.update(0x83,12008)==0x83); // Contact bounce does not release A.
    CHECK(input.update(0,12020)==0x83);
    CHECK(input.update(0,12025)==0);
    CHECK(Input::clean(0xff)==0x0f); // Contradictory directions neutral, face buttons survive.
    input.reset();
    CHECK(input.update(2,0xfffffffcU)==0);
    CHECK(input.update(2,2)==2); // millis() rollover.
    input.reset();
    CHECK(input.update(0,8)==0);
    std::puts("NES held input: combinations, 12s B-hold, bounce, release, wrap passed");
    return EXIT_SUCCESS;
}
