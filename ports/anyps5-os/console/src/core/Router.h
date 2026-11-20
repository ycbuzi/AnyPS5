#pragma once
#include "core/Anim.h"
#include "core/Screen.h"
#include <functional>
#include <map>

namespace ui {

class Router {
public:
    using Factory = std::function<std::unique_ptr<Screen>()>;
    static bool add(const std::string& id, Factory f);
    static bool has(const std::string& id);

    Screen* get(const std::string& id);
    Screen* go(const std::string& id, bool instant = false);
    Screen* push(const std::string& id);
    Screen* open(const std::string& id);
    void pop();
    void close(const std::string& id);
    bool isOpen(const std::string& id) const;
    Screen* top() const;
    Screen* base() const;
    std::string baseId() const;

    bool action(const Action& a);
    void update(double dt);
    void draw(Draw& d);
    void host(const json& m);

private:
    struct Layer {
        Screen* s = nullptr;
        Tween fade;
        bool closing = false;
    };
    static std::map<std::string, Factory>& registry();
    std::map<std::string, std::unique_ptr<Screen>> made_;
    std::vector<Layer> stack_;
    Screen* leaving_ = nullptr;
    Tween swap_;
};

}
