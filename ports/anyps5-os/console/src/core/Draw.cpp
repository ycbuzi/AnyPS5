#include "core/Draw.h"
#include <cmath>

namespace ui {

static constexpr float PI = 3.14159265358979f;

Draw::Draw(SDL_Renderer* r) : r_(r) {
    SDL_SetRenderDrawBlendMode(r_, SDL_BLENDMODE_BLEND);
}

static unsigned gGeneration = 0;

unsigned Draw::generation() { return gGeneration; }

Draw::~Draw() {
    for (auto& [k, t] : cache_) SDL_DestroyTexture(t);
    for (auto* t : pool_) SDL_DestroyTexture(t);
}

void Draw::reset(SDL_Renderer* r) {
    for (auto& [k, t] : cache_) SDL_DestroyTexture(t);
    for (auto* t : pool_) SDL_DestroyTexture(t);
    for (auto* t : layers_) SDL_DestroyTexture(t);
    cache_.clear();
    pool_.clear();
    layers_.clear();
    targets_.clear();
    clip_.clear();
    alpha_.clear();
    gGeneration++;
    r_ = r;
    if (r_) SDL_SetRenderDrawBlendMode(r_, SDL_BLENDMODE_BLEND);
}

SDL_FColor Draw::fc(Color c) const {
    return {c.r, c.g, c.b, std::clamp(c.a * alphaMul(), 0.f, 1.f)};
}

void Draw::clear(Color c) {
    SDL_SetRenderDrawColorFloat(r_, c.r, c.g, c.b, c.a);
    SDL_RenderClear(r_);
}

void Draw::rect(Rect r, Color c) {
    SDL_FColor f = fc(c);
    if (f.a <= 0) return;
    SDL_SetRenderDrawColorFloat(r_, f.r, f.g, f.b, f.a);
    SDL_RenderFillRect(r_, &r);
}

std::vector<SDL_FPoint> Draw::roundPath(Rect r, float radius, float grow) {
    std::vector<SDL_FPoint> pts;
    float x0 = r.x - grow, y0 = r.y - grow, x1 = r.x + r.w + grow, y1 = r.y + r.h + grow;
    if (x1 - x0 < .01f) x0 = x1 = r.x + r.w / 2;
    if (y1 - y0 < .01f) y0 = y1 = r.y + r.h / 2;
    float rad = std::max(0.f, std::min(radius + grow, std::min(x1 - x0, y1 - y0) * .5f));
    int seg = radius < .5f ? 0 : std::clamp((int)(std::max(radius, 1.f) * .6f), 3, 20);
    const float cx[4] = {x1 - rad, x1 - rad, x0 + rad, x0 + rad};
    const float cy[4] = {y0 + rad, y1 - rad, y1 - rad, y0 + rad};
    const float a0[4] = {-PI / 2, 0, PI / 2, PI};
    for (int c = 0; c < 4; c++) {
        if (seg == 0) {
            pts.push_back({cx[c], cy[c]});
            continue;
        }
        for (int i = 0; i <= seg; i++) {
            float a = a0[c] + (PI / 2) * i / seg;
            pts.push_back({cx[c] + std::cos(a) * rad, cy[c] + std::sin(a) * rad});
        }
    }
    return pts;
}

static void band(std::vector<int>& idx, int A, int B, size_t n) {
    for (size_t i = 0; i < n; i++) {
        int a = A + (int)i, b = A + (int)((i + 1) % n), ao = B + (int)i, bo = B + (int)((i + 1) % n);
        idx.insert(idx.end(), {a, ao, bo, a, bo, b});
    }
}

void Draw::fillPath(const std::vector<SDL_FPoint>& pts, SDL_FPoint center, const std::function<SDL_FColor(SDL_FPoint)>& colorAt, bool feather) {
    if (pts.size() < 3) return;
    size_t n = pts.size();
    std::vector<SDL_Vertex> v;
    std::vector<int> idx;
    v.reserve(n * 2 + 1);
    v.push_back({center, colorAt(center), {0, 0}});
    for (auto& p : pts) v.push_back({p, colorAt(p), {0, 0}});
    for (size_t i = 0; i < n; i++) idx.insert(idx.end(), {0, 1 + (int)i, 1 + (int)((i + 1) % n)});
    if (feather) {
        for (size_t i = 0; i < n; i++) {
            SDL_FPoint p = pts[i];
            SDL_FPoint prev = pts[(i + n - 1) % n], next = pts[(i + 1) % n];
            float tx = next.x - prev.x, ty = next.y - prev.y;
            float l = std::sqrt(tx * tx + ty * ty);
            SDL_FPoint q = l > 0 ? SDL_FPoint{p.x + ty / l, p.y - tx / l} : p;
            SDL_FColor c = colorAt(p);
            c.a = 0;
            v.push_back({q, c, {0, 0}});
        }
        band(idx, 1, 1 + (int)n, n);
    }
    SDL_RenderGeometry(r_, nullptr, v.data(), (int)v.size(), idx.data(), (int)idx.size());
}

void Draw::round(Rect r, float radius, Color c) {
    SDL_FColor f = fc(c);
    if (f.a <= 0 || r.w <= 0 || r.h <= 0) return;
    if (radius <= 0) { rect(r, c); return; }
    auto pts = roundPath(r, radius, -.5f);
    fillPath(pts, {r.x + r.w / 2, r.y + r.h / 2}, [&](SDL_FPoint) { return f; }, true);
}

void Draw::roundGradient(Rect r, float radius, float angleDeg, Color a, Color b) {
    if (r.w <= 0 || r.h <= 0) return;
    float ang = angleDeg * PI / 180.f;
    float dx = std::sin(ang), dy = -std::cos(ang);
    float len = std::fabs(r.w * dx) + std::fabs(r.h * dy);
    float cx = r.x + r.w / 2, cy = r.y + r.h / 2;
    auto pts = roundPath(r, radius, -.5f);
    fillPath(pts, {cx, cy}, [&](SDL_FPoint p) {
        float t = std::clamp(((p.x - cx) * dx + (p.y - cy) * dy) / len + .5f, 0.f, 1.f);
        return fc(mix(a, b, t));
    }, true);
}

void Draw::ring(Rect r, float radius, float inner, float outer, Color c) {
    SDL_FColor f = fc(c);
    if (f.a <= 0) return;
    auto pi = roundPath(r, radius, inner);
    auto po = roundPath(r, radius, outer);
    auto pof = roundPath(r, radius, outer + .7f);
    auto pif = roundPath(r, radius, inner - .7f);
    size_t n = pi.size();
    if (n < 3 || po.size() != n || pof.size() != n) return;
    bool fi = pif.size() == n;
    std::vector<SDL_Vertex> v;
    std::vector<int> idx;
    SDL_FColor z = f;
    z.a = 0;
    for (auto& p : pi) v.push_back({p, f, {0, 0}});
    for (auto& p : po) v.push_back({p, f, {0, 0}});
    for (auto& p : pof) v.push_back({p, z, {0, 0}});
    if (fi) for (auto& p : pif) v.push_back({p, z, {0, 0}});
    band(idx, 0, (int)n, n);
    band(idx, (int)n, (int)(2 * n), n);
    if (fi) band(idx, 0, (int)(3 * n), n);
    SDL_RenderGeometry(r_, nullptr, v.data(), (int)v.size(), idx.data(), (int)idx.size());
}

void Draw::outline(Rect r, float radius, float width, Color c) { ring(r, radius, 0, width, c); }
void Draw::stroke(Rect r, float radius, float width, Color c) { ring(r, radius, -width, 0, c); }

void Draw::soft(Rect r, float radius, float blur, Color c) {
    SDL_FColor f = fc(c);
    if (f.a <= 0) return;
    if (blur < 1) { round(r, radius, c); return; }
    const int steps = 8;
    std::vector<std::vector<SDL_FPoint>> rings;
    float rr = std::max(radius, 1.f);
    for (int i = 0; i <= steps; i++) rings.push_back(roundPath(r, rr, -blur + 2 * blur * i / steps));
    size_t n = rings[steps].size();
    for (auto& ring : rings) if (ring.size() != n) return;
    std::vector<SDL_Vertex> v;
    std::vector<int> idx;
    SDL_FPoint center{r.x + r.w / 2, r.y + r.h / 2};
    v.push_back({center, f, {0, 0}});
    for (int i = 0; i <= steps; i++) {
        float t = (float)i / steps;
        SDL_FColor ci = f;
        ci.a = f.a * (1.f - t * t * (3 - 2 * t));
        for (auto& p : rings[i]) v.push_back({p, ci, {0, 0}});
    }
    for (size_t i = 0; i < n; i++) idx.insert(idx.end(), {0, 1 + (int)i, 1 + (int)((i + 1) % n)});
    for (int s = 0; s < steps; s++) band(idx, 1 + s * (int)n, 1 + (s + 1) * (int)n, n);
    SDL_RenderGeometry(r_, nullptr, v.data(), (int)v.size(), idx.data(), (int)idx.size());
}

void Draw::shadow(Rect r, float radius, float blur, Color c, float dx, float dy, float spread) {
    Rect s{r.x + dx - spread, r.y + dy - spread, r.w + 2 * spread, r.h + 2 * spread};
    soft(s, radius + spread, blur * .5f, c);
}

void Draw::circle(float cx, float cy, float radius, Color c) {
    round({cx - radius, cy - radius, radius * 2, radius * 2}, radius, c);
}

void Draw::dot(float cx, float cy, float radius, Color inner, Color outer) {
    int seg = std::clamp((int)(radius * 1.5f), 10, 48);
    std::vector<SDL_Vertex> v;
    std::vector<int> idx;
    v.push_back({{cx, cy}, fc(inner), {0, 0}});
    SDL_FColor o = fc(outer);
    for (int i = 0; i < seg; i++) {
        float a = 2 * PI * i / seg;
        v.push_back({{cx + std::cos(a) * radius, cy + std::sin(a) * radius}, o, {0, 0}});
        idx.insert(idx.end(), {0, 1 + i, 1 + (i + 1) % seg});
    }
    SDL_RenderGeometry(r_, nullptr, v.data(), (int)v.size(), idx.data(), (int)idx.size());
}

void Draw::line(float x1, float y1, float x2, float y2, float width, Color c) {
    float dx = x2 - x1, dy = y2 - y1, l = std::sqrt(dx * dx + dy * dy);
    if (l <= 0) return;
    float nx = -dy / l * width / 2, ny = dx / l * width / 2;
    SDL_FColor f = fc(c);
    SDL_Vertex v[4] = {{{x1 + nx, y1 + ny}, f, {0, 0}}, {{x2 + nx, y2 + ny}, f, {0, 0}}, {{x2 - nx, y2 - ny}, f, {0, 0}}, {{x1 - nx, y1 - ny}, f, {0, 0}}};
    int idx[6] = {0, 1, 2, 0, 2, 3};
    SDL_RenderGeometry(r_, nullptr, v, 4, idx, 6);
}

static Color stopAt(const std::vector<Stop>& s, float t) {
    if (s.empty()) return {0, 0, 0, 0};
    if (t <= s.front().pos) return s.front().color;
    for (size_t i = 1; i < s.size(); i++) {
        if (t <= s[i].pos) {
            float span = s[i].pos - s[i - 1].pos;
            float k = span > 0 ? (t - s[i - 1].pos) / span : 1.f;
            return mix(s[i - 1].color, s[i].color, k);
        }
    }
    return s.back().color;
}

void Draw::linear(Rect r, float angleDeg, std::initializer_list<Stop> stops) {
    linear(r, angleDeg, std::vector<Stop>(stops));
}

void Draw::linear(Rect r, float angleDeg, const std::vector<Stop>& stops) {
    float a = std::fmod(std::fmod(angleDeg, 360.f) + 360.f, 360.f);
    if (std::fmod(a, 90.f) != 0.f) {
        float ang = a * PI / 180.f, dx = std::sin(ang), dy = -std::cos(ang);
        float len = std::fabs(r.w * dx) + std::fabs(r.h * dy);
        float cx = r.x + r.w / 2, cy = r.y + r.h / 2;
        field(r, 24, 24, [&](float x, float y) { return stopAt(stops, ((x - cx) * dx + (y - cy) * dy) / len + .5f); });
        return;
    }
    bool vertical = a == 0.f || a == 180.f;
    bool reverse = a == 0.f || a == 270.f;
    std::vector<float> cuts{0.f};
    for (auto& st : stops) if (st.pos > 0 && st.pos < 1) cuts.push_back(st.pos);
    cuts.push_back(1.f);
    std::vector<SDL_Vertex> v;
    std::vector<int> idx;
    for (size_t i = 0; i < cuts.size(); i++) {
        float t = cuts[i];
        SDL_FColor c = fc(stopAt(stops, t));
        float p = reverse ? 1 - t : t;
        if (vertical) {
            float y = r.y + r.h * p;
            v.push_back({{r.x, y}, c, {0, 0}});
            v.push_back({{r.x + r.w, y}, c, {0, 0}});
        } else {
            float x = r.x + r.w * p;
            v.push_back({{x, r.y}, c, {0, 0}});
            v.push_back({{x, r.y + r.h}, c, {0, 0}});
        }
        if (i > 0) {
            int b = (int)(i - 1) * 2;
            idx.insert(idx.end(), {b, b + 1, b + 3, b, b + 3, b + 2});
        }
    }
    SDL_RenderGeometry(r_, nullptr, v.data(), (int)v.size(), idx.data(), (int)idx.size());
}

void Draw::field(Rect r, int cols, int rows, const std::function<Color(float, float)>& f) {
    std::vector<SDL_Vertex> v;
    std::vector<int> idx;
    v.reserve((size_t)(cols + 1) * (rows + 1));
    for (int j = 0; j <= rows; j++)
        for (int i = 0; i <= cols; i++) {
            float x = r.x + r.w * i / cols, y = r.y + r.h * j / rows;
            v.push_back({{x, y}, fc(f(x, y)), {0, 0}});
        }
    for (int j = 0; j < rows; j++)
        for (int i = 0; i < cols; i++) {
            int a = j * (cols + 1) + i, b = a + 1, c = a + cols + 1, d = c + 1;
            idx.insert(idx.end(), {a, b, d, a, d, c});
        }
    SDL_RenderGeometry(r_, nullptr, v.data(), (int)v.size(), idx.data(), (int)idx.size());
}

void Draw::image(SDL_Texture* t, Rect dst, Fit fit, float alpha, float radius, Color tint, float focusX, float focusY) {
    if (!t || dst.w <= 0 || dst.h <= 0) return;
    float tw = 0, th = 0;
    SDL_GetTextureSize(t, &tw, &th);
    if (tw <= 0 || th <= 0) return;
    float u0 = 0, v0 = 0, u1 = 1, v1 = 1;
    Rect d = dst;
    if (fit == Fit::Cover) {
        float s = std::max(dst.w / tw, dst.h / th);
        float vw = dst.w / s / tw, vh = dst.h / s / th;
        u0 = (1 - vw) * focusX;
        u1 = u0 + vw;
        v0 = (1 - vh) * focusY;
        v1 = v0 + vh;
    } else if (fit == Fit::Contain) {
        float s = std::min(dst.w / tw, dst.h / th);
        d = {dst.x + (dst.w - tw * s) * focusX, dst.y + (dst.h - th * s) * focusY, tw * s, th * s};
    }
    SDL_FColor c = fc({tint.r, tint.g, tint.b, tint.a * alpha});
    if (c.a <= 0) return;
    auto uv = [&](SDL_FPoint p) {
        return SDL_FPoint{u0 + (u1 - u0) * (p.x - d.x) / d.w, v0 + (v1 - v0) * (p.y - d.y) / d.h};
    };
    if (radius <= 0) {
        SDL_FPoint q[4] = {{d.x, d.y}, {d.x + d.w, d.y}, {d.x + d.w, d.y + d.h}, {d.x, d.y + d.h}};
        SDL_Vertex qv[4];
        for (int i = 0; i < 4; i++) qv[i] = {q[i], c, uv(q[i])};
        const int qi[6] = {0, 1, 2, 0, 2, 3};
        SDL_RenderGeometry(r_, t, qv, 4, qi, 6);
        return;
    }
    std::vector<SDL_FPoint> pts = roundPath(d, radius, -.5f);
    size_t n = pts.size();
    if (n < 3) return;
    SDL_FPoint center{d.x + d.w / 2, d.y + d.h / 2};
    std::vector<SDL_Vertex> v;
    std::vector<int> idx;
    v.push_back({center, c, uv(center)});
    for (auto& p : pts) v.push_back({p, c, uv(p)});
    for (size_t i = 0; i < n; i++) idx.insert(idx.end(), {0, 1 + (int)i, 1 + (int)((i + 1) % n)});
    if (radius > 0) {
        auto outer = roundPath(d, radius, .5f);
        if (outer.size() == n) {
            SDL_FColor z = c;
            z.a = 0;
            for (auto& p : outer) {
                SDL_FPoint q{std::clamp(p.x, d.x, d.x + d.w), std::clamp(p.y, d.y, d.y + d.h)};
                v.push_back({p, z, uv(q)});
            }
            band(idx, 1, 1 + (int)n, n);
        }
    }
    SDL_RenderGeometry(r_, t, v.data(), (int)v.size(), idx.data(), (int)idx.size());
}

void Draw::icon(SDL_Texture* t, float cx, float cy, float size, Color tint) {
    if (!t) return;
    image(t, {cx - size / 2, cy - size / 2, size, size}, Fit::Contain, 1.f, 0.f, tint);
}

void Draw::pushClip(Rect r) {
    SDL_Rect c{(int)std::floor(r.x), (int)std::floor(r.y), (int)std::ceil(r.w), (int)std::ceil(r.h)};
    if (!clip_.empty()) {
        SDL_Rect o;
        if (!SDL_GetRectIntersection(&c, &clip_.back(), &o)) o = {0, 0, 0, 0};
        c = o;
    }
    clip_.push_back(c);
    SDL_SetRenderClipRect(r_, &c);
}

bool Draw::clipTo(Rect& r) const {
    if (clip_.empty()) return r.w > 0 && r.h > 0;
    const SDL_Rect& c = clip_.back();
    float x0 = std::max(r.x, (float)c.x), y0 = std::max(r.y, (float)c.y);
    float x1 = std::min(r.x + r.w, (float)(c.x + c.w)), y1 = std::min(r.y + r.h, (float)(c.y + c.h));
    if (x1 <= x0 || y1 <= y0) return false;
    r = {x0, y0, x1 - x0, y1 - y0};
    return true;
}

void Draw::popClip() {
    if (!clip_.empty()) clip_.pop_back();
    SDL_SetRenderClipRect(r_, clip_.empty() ? nullptr : &clip_.back());
}

void Draw::pushAlpha(float a) { alpha_.push_back(alphaMul() * a); }
void Draw::popAlpha() { if (!alpha_.empty()) alpha_.pop_back(); }

SDL_Texture* Draw::cached(const std::string& key, int w, int h, const std::function<void(Draw&)>& paint) {
    std::string k = key + "@" + std::to_string(w) + "x" + std::to_string(h);
    auto it = cache_.find(k);
    if (it != cache_.end()) return it->second;
    SDL_Texture* t = SDL_CreateTexture(r_, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, w, h);
    if (!t) return nullptr;
    SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND_PREMULTIPLIED);
    SDL_Texture* prev = SDL_GetRenderTarget(r_);
    auto savedClip = clip_;
    auto savedAlpha = alpha_;
    clip_.clear();
    alpha_.clear();
    SDL_SetRenderTarget(r_, t);
    SDL_SetRenderClipRect(r_, nullptr);
    clear({0, 0, 0, 0});
    paint(*this);
    SDL_SetRenderTarget(r_, prev);
    clip_ = savedClip;
    alpha_ = savedAlpha;
    SDL_SetRenderClipRect(r_, clip_.empty() ? nullptr : &clip_.back());
    cache_[k] = t;
    return t;
}

void Draw::dropCache(const std::string& key) {
    for (auto it = cache_.begin(); it != cache_.end();) {
        if (it->first.rfind(key + "@", 0) == 0) {
            SDL_DestroyTexture(it->second);
            it = cache_.erase(it);
        } else {
            ++it;
        }
    }
}

void Draw::beginLayer() {
    int w = 0, h = 0;
    SDL_GetCurrentRenderOutputSize(r_, &w, &h);
    SDL_Texture* t = nullptr;
    for (size_t i = 0; i < pool_.size(); i++) {
        float tw, th;
        SDL_GetTextureSize(pool_[i], &tw, &th);
        if ((int)tw == w && (int)th == h) {
            t = pool_[i];
            pool_.erase(pool_.begin() + (long)i);
            break;
        }
    }
    if (!t) {
        t = SDL_CreateTexture(r_, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, w, h);
        SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND_PREMULTIPLIED);
    }
    targets_.push_back(SDL_GetRenderTarget(r_));
    layers_.push_back(t);
    SDL_SetRenderTarget(r_, t);
    SDL_SetRenderClipRect(r_, nullptr);
    clear({0, 0, 0, 0});
    SDL_SetRenderClipRect(r_, clip_.empty() ? nullptr : &clip_.back());
}

void Draw::endLayer(float alpha, float scale, float dx, float dy) {
    if (layers_.empty()) return;
    SDL_Texture* t = layers_.back();
    layers_.pop_back();
    SDL_SetRenderTarget(r_, targets_.back());
    targets_.pop_back();
    SDL_SetRenderClipRect(r_, clip_.empty() ? nullptr : &clip_.back());
    float tw, th;
    SDL_GetTextureSize(t, &tw, &th);
    float a = std::clamp(alpha * alphaMul(), 0.f, 1.f);
    SDL_SetTextureColorModFloat(t, a, a, a);
    SDL_SetTextureAlphaModFloat(t, a);
    Rect d{tw * (1 - scale) / 2 + dx, th * (1 - scale) / 2 + dy, tw * scale, th * scale};
    if (a > 0) SDL_RenderTexture(r_, t, nullptr, &d);
    SDL_SetTextureColorModFloat(t, 1, 1, 1);
    SDL_SetTextureAlphaModFloat(t, 1);
    pool_.push_back(t);
}

}
