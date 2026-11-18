#include "nes_path.h"
#include <array>
#include <cstdlib>
#include <iostream>
#include <string>

using meow::nes::validPath;
static void require(bool condition, const char* reason)
{
    if (!condition) { std::cerr << reason << '\n'; std::exit(1); }
}

int main()
{
    require(validPath("/roms/nes",false), "root folder rejected");
    require(!validPath("/roms/nes",true), "root accepted as ROM");
    const char* valid[] = {
        "/roms/nes/game.nes", "/roms/nes/A/B/Game.NES",
        "/roms/nes/Super Mario Bros. + Duck Hunt (USA).nes",
        "/roms/nes/a..b/gAmE.NeS", "/roms/nes/.hidden/game.nes"
    };
    for (const char* path : valid) {
        require(validPath(path,true), "valid ROM rejected");
        require(validPath(path,false), "valid child path rejected in folder mode");
    }
    const char* invalid[] = {
        nullptr, "", "/", "roms/nes/game.nes", "/roms", "/roms/ne",
        "/roms/nesx/game.nes", "/roms/nes-other/game.nes", "/roms/NES/game.nes",
        "/roms/nes/", "/roms/nes//game.nes", "/roms/nes/a//game.nes",
        "/roms/nes/.", "/roms/nes/..", "/roms/nes/./game.nes",
        "/roms/nes/a/../game.nes", "/roms/nes/a/.", "/roms/nes/game.nes/",
        "/roms/nes/a\\game.nes", "/roms/nes/a:game.nes",
        "/roms/nes/a\ngame.nes", "/roms/nes/a\x7f.nes"
    };
    for (const char* path : invalid) {
        require(!validPath(path,true), "invalid path accepted as ROM");
        require(!validPath(path,false), "invalid path accepted as folder");
    }
    require(validPath("/roms/nes/folder",false), "child folder rejected");
    require(!validPath("/roms/nes/folder",true), "missing extension accepted");
    require(!validPath("/roms/nes/game.nes.bak",true), "suffix after .nes accepted");
    require(!validPath("/roms/nes/game.nes/noext",true), "parent extension treated as leaf extension");
    const std::string longest = std::string("/roms/nes/") + std::string(241,'a') + ".nes";
    require(longest.size()==255 && validPath(longest.c_str(),true), "255-byte ROM path rejected");
    require(!validPath((longest+"s").c_str(),true), "256-byte path accepted");
    std::array<char,256> unterminated; unterminated.fill('x');
    require(!validPath(unterminated.data(),false), "unterminated bounded input accepted");
    for (unsigned c=1;c<32;++c) {
        std::string path="/roms/nes/a.nes"; path.insert(11,1,static_cast<char>(c));
        require(!validPath(path.c_str(),true), "ASCII control accepted");
    }
    std::cout << "NES path: root/child/extension/traversal/control/255-byte bounds passed\n";
}
