#pragma once
#include "media_api.h"

namespace meow::media {
// Coalesce only adjacent volume requests already at the queue head. A pause,
// play, seek or EQ is an ordering barrier and must never be skipped/reordered.
// At most seven extra entries are consumed from the eight-entry native queue.
template<class Request, class Peek, class Pop>
void coalesceVolume(Request& current, Peek peek, Pop pop)
{
    if (current.command.action != Action::Volume) return;
    for (unsigned i = 0; i < 7; ++i) {
        Request next{};
        if (!peek(next) || next.command.action != Action::Volume) break;
        if (!pop(next)) break;
        current = next;
    }
}
} // namespace meow::media
