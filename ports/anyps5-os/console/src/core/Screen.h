#pragma once
#include "core/Draw.h"
#include "core/Input.h"
#include <nlohmann/json.hpp>
#include <memory>
#include <string>
#include <vector>

namespace ui {

using json = nlohmann::json;

struct Hint {
    std::string label;
    std::string key;
};

class Screen {
public:
    virtual ~Screen() = default;
    virtual bool overlay() const { return false; }
    virtual void enter() {}
    virtual void leave() {}
    virtual void update(double dt) {}
    virtual void draw(Draw& d) = 0;
    virtual bool action(const Action& a) { return false; }
    virtual void host(const json& m) {}
    virtual std::vector<Hint> legend() const { return {}; }
    virtual bool typing() const { return false; }
    std::string id;
};

}

#define ANYPS5_SCREEN(Type, Id) \
    static const bool Type##_registered = ::ui::Router::add(Id, [] { return std::unique_ptr<::ui::Screen>(new Type()); })
