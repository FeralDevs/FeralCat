#pragma once
#include <mooncake.h>
#include <lvgl.h>
#include "../../bsp/devices.h"
#include "../../system/lua_apps/lua_runtime.h"
#include "../../system/lua_apps/sd_app_catalog.h"

namespace MOONCAKE::APPS {
// One native host, separate package identities in the launcher. No Lua state
// or app screen is retained when the user returns to the main app menu.
class AppLua final : public mooncake::AppAbility, public meow::luaapps::Host {
public:
    explicit AppLua(DEVICES* device);
    void onOpen() override;
    void onRunning() override;
    void onClose() override;
    void refreshCatalog();
    void scanStep();
    uint32_t catalogRevision() const { return revision_; }
    const meow::luaapps::SdAppCatalog& catalog() const { return catalog_; }
    void selectPackage(const char* id);
    bool exitRequested() const { return exit_; }
    void show(const meow::luaapps::View& view) override;
    uint32_t uptimeMs() override;
    void requestExit() override;
    meow::media::Host* audio() override;
    void captureBack(bool enabled) override;
private:
    struct Session;
    DEVICES* device_;
    meow::luaapps::SdAppCatalog catalog_;
    Session* session_ = nullptr;
    uint32_t revision_ = 0;
    char requestedId_[32]{};
    bool exit_ = false, direct_ = false;
    lv_obj_t* previous_ = nullptr;
    lv_obj_t* screen_ = nullptr;
    lv_obj_t* title_ = nullptr;
    lv_obj_t* content_ = nullptr;
    lv_obj_t* body_ = nullptr;
    lv_obj_t* footer_ = nullptr;
    lv_obj_t* buttons_[17]{};
    lv_obj_t* labels_[17]{};
    void createScreen();
    void render();
    void activate(int index);
    void launch(size_t index);
    void stopRuntime();
    void fail(const char* message);
    static void clicked(lv_event_t* event);
};
} // namespace MOONCAKE::APPS
