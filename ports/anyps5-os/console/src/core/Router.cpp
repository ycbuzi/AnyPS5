#include "core/Router.h"
#include "core/Pointer.h"
#include <SDL3/SDL.h>

namespace ui {

std::map<std::string, Router::Factory>& Router::registry() {
    static std::map<std::string, Factory> r;
    return r;
}

bool Router::add(const std::string& id, Factory f) {
    registry()[id] = std::move(f);
    return true;
}

bool Router::has(const std::string& id) { return registry().count(id) > 0; }

Screen* Router::get(const std::string& id) {
    auto it = made_.find(id);
    if (it != made_.end()) return it->second.get();
    auto r = registry().find(id);
    if (r == registry().end()) {
        SDL_Log("no screen registered as '%s'", id.c_str());
        return nullptr;
    }
    auto s = r->second();
    s->id = id;
    Screen* p = s.get();
    made_[id] = std::move(s);
    return p;
}

Screen* Router::go(const std::string& id, bool instant) {
    Screen* s = get(id);
    if (!s) return nullptr;
    for (size_t i = stack_.size(); i-- > 1;) stack_[i].s->leave();
    Screen* old = stack_.empty() ? nullptr : stack_[0].s;
    stack_.clear();
    if (old && old != s) old->leave();
    stack_.push_back({s, {}, false});
    stack_[0].fade.snap(1);
    leaving_ = instant || old == s ? nullptr : old;
    if (leaving_) swap_.restart(0, 1, .45f, easeCss);
    else swap_.snap(1);
    SDL_Log("screen: %s", id.c_str());
    s->enter();
    return s;
}

Screen* Router::push(const std::string& id) {
    Screen* s = get(id);
    if (!s) return nullptr;
    for (auto& l : stack_)
        if (l.s == s && !l.closing) return s;
    if (stack_.empty()) return go(id, true);
    Layer l{s, {}, false};
    l.fade.restart(0, 1, .2f, easeCss);
    stack_.push_back(l);
    SDL_Log("screen: %s (over %s)", id.c_str(), stack_[0].s->id.c_str());
    s->enter();
    return s;
}

Screen* Router::open(const std::string& id) {
    Screen* s = get(id);
    if (!s) return nullptr;
    return s->overlay() ? push(id) : go(id);
}

void Router::pop() {
    for (size_t i = stack_.size(); i-- > 1;) {
        if (stack_[i].closing) continue;
        stack_[i].closing = true;
        stack_[i].fade.set(0, .15f, easeCss);
        stack_[i].s->leave();
        return;
    }
}

void Router::close(const std::string& id) {
    for (size_t i = stack_.size(); i-- > 1;) {
        if (stack_[i].s->id == id && !stack_[i].closing) {
            stack_[i].closing = true;
            stack_[i].fade.set(0, .15f, easeCss);
            stack_[i].s->leave();
        }
    }
}

bool Router::isOpen(const std::string& id) const {
    for (auto& l : stack_)
        if (l.s->id == id && !l.closing) return true;
    return false;
}

Screen* Router::top() const {
    for (size_t i = stack_.size(); i-- > 0;)
        if (!stack_[i].closing) return stack_[i].s;
    return nullptr;
}

Screen* Router::base() const { return stack_.empty() ? nullptr : stack_[0].s; }
std::string Router::baseId() const { return stack_.empty() ? std::string() : stack_[0].s->id; }

bool Router::action(const Action& a) {
    Screen* t = top();
    return t ? t->action(a) : false;
}

void Router::update(double dt) {
    for (size_t i = stack_.size(); i-- > 1;)
        if (stack_[i].closing && stack_[i].fade.done()) stack_.erase(stack_.begin() + (long)i);
    if (leaving_ && swap_.done()) leaving_ = nullptr;
    if (leaving_) leaving_->update(dt);
    auto copy = stack_;
    for (auto& l : copy) l.s->update(dt);
}

void Router::draw(Draw& d) {
    if (stack_.empty()) return;
    Screen* tp = top();
    if (leaving_) {
        float t = swap_.value();
        d.beginLayer();
        pointer::layer(false);
        leaving_->draw(d);
        d.endLayer(1 - t);
        d.beginLayer();
        pointer::layer(stack_[0].s == tp);
        stack_[0].s->draw(d);
        d.endLayer(t, 1.012f - .012f * t);
    } else {
        pointer::layer(stack_[0].s == tp);
        stack_[0].s->draw(d);
    }
    auto copy = stack_;
    for (size_t i = 1; i < copy.size(); i++) {
        float a = copy[i].fade.value();
        pointer::layer(copy[i].s == tp && !copy[i].closing);
        if (a >= 1) {
            copy[i].s->draw(d);
        } else if (a > 0) {
            d.beginLayer();
            copy[i].s->draw(d);
            d.endLayer(a);
        }
    }
    pointer::layer(true);
}

void Router::host(const json& m) {
    for (auto& [id, s] : made_) s->host(m);
}

}
