#pragma once
#include <lvgl.h>
#include "../../system/lua_apps/lua_runtime.h"

namespace meow::luaapps {
// Persistent objects and fixed touch rectangles: status updates never rebuild
// the controls beneath a finger. No filesystem access or Lua calls from LVGL.
class PlayerUi {
public:
    using Action = void (*)(void*, const char*);
    void create(lv_obj_t* parent, void* context, Action action, uint16_t* coverPixels);
    void show(const View& view);
    void hide();
    void coverChanged(bool available);
    void move(int delta);
    void activate();
    lv_obj_t* button(unsigned index) const;
private:
    struct Button {
        PlayerUi* owner = nullptr;
        lv_obj_t* object = nullptr;
        lv_obj_t* label = nullptr;
        char id[32]{};
        char pressedId[32]{};
        uint32_t epoch = 0;
        bool pressed = false;
    };
    lv_obj_t* container_ = nullptr;
    lv_obj_t* player_ = nullptr;
    lv_obj_t* menu_ = nullptr;
    lv_obj_t* title_ = nullptr;
    lv_obj_t* subtitle_ = nullptr;
    lv_obj_t* status_ = nullptr;
    lv_obj_t* time_ = nullptr;
    lv_obj_t* volume_ = nullptr;
    lv_obj_t* progress_ = nullptr;
    lv_obj_t* placeholder_ = nullptr;
    lv_obj_t* image_ = nullptr;
    lv_obj_t* menuTitle_ = nullptr;
    lv_obj_t* menuBody_ = nullptr;
    lv_img_dsc_t imageDescriptor_{};
    Button playerButtons_[5], menuButtons_[4];
    void* context_ = nullptr;
    Action action_ = nullptr;
    ViewKind kind_ = ViewKind::List;
    char pageTitle_[96]{};
    uint32_t epoch_ = 1;
    unsigned count_ = 0, selected_ = 0;
    bool visible_ = false, grid_ = false;
    Button* activeButtons();
    void createButton(Button& button, lv_obj_t* parent);
    void highlight();
    static void event(lv_event_t* event);
};
}
