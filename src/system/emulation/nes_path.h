#pragma once

namespace meow::nes {

// Allocation-free lexical check for absolute SD paths beneath /roms/nes.
// Reads at most 256 bytes, accepting a NUL-terminated path of at most 255 bytes.
// Folder mode allows the root itself; ROM mode requires a child with a .nes
// extension (ASCII case-insensitive). Does not open files or resolve links.
bool validPath(const char* path, bool romFile);

} // namespace meow::nes
