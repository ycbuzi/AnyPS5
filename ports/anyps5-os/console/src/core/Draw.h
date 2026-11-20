#pragma once
#include "core/Types.h"
#include <functional>
#include <initializer_list>
#include <string>
#include <unordered_map>
#include <vector>

namespace ui {

enum class Fit { Cover, Contain, Fill };

class Draw {
public:
    explicit Draw(SDL_Renderer* r);
    ~Draw();
    SDL_Renderer* sdl() const { return r_; }
    void reset(SDL_Renderer* r);
    static unsigned generation();

    void clear(Color c);
    void rect(Rect r, Color c);
    void round(Rect r, float radius, Color c);
    void roundGradient(Rect r, float radius, float angleDeg, Color a, Color b);
    void outline(Rect r, float radius, float width, Color c);
    void stroke(Rect r, float radius, float width, Color c);
    void soft(Rect r, float radius, float blur, Color c);
    void shadow(Rect r, float radius, float blur, Color c, float dx = 0, float dy = 0, float spread = 0);
    void circle(float cx, float cy, float radius, Color c);
    void dot(float cx, float cy, float radius, Color inner, Color outer);
    void line(float x1, float y1, float x2, float y2, float width, Color c);
    void linear(Rect r, float angleDeg, std::initializer_list<Stop> stops);
    void linear(Rect r, float angleDeg, const std::vector<Stop>& stops);
    void field(Rect r, int cols, int rows, const std::function<Color(float x, float y)>& f);
    void image(SDL_Texture* t, Rect dst, Fit fit = Fit::Cover, float alpha = 1.f, float radius = 0.f, Color tint = white(), float focusX = .5f, float focusY = .5f);
    void icon(SDL_Texture* t, float cx, float cy, float size, Color tint = white());

    void pushClip(Rect r);
    void popClip();
    bool clipTo(Rect& r) const;
    void pushAlpha(float a);
    void popAlpha();
    float alphaMul() const { return alpha_.empty() ? 1.f : alpha_.back(); }

    SDL_Texture* cached(const std::string& key, int w, int h, const std::function<void(Draw&)>& paint);
    void dropCache(const std::string& key);
    void beginLayer();
    void endLayer(float alpha, float scale = 1.f, float dx = 0, float dy = 0);

    static std::vector<SDL_FPoint> roundPath(Rect r, float radius, float grow);

private:
    void fillPath(const std::vector<SDL_FPoint>& pts, SDL_FPoint center, const std::function<SDL_FColor(SDL_FPoint)>& colorAt, bool feather);
    void ring(Rect r, float radius, float inner, float outer, Color c);
    SDL_FColor fc(Color c) const;
    SDL_Renderer* r_;
    std::vector<SDL_Rect> clip_;
    std::vector<float> alpha_;
    std::vector<SDL_Texture*> layers_;
    std::vector<SDL_Texture*> pool_;
    std::vector<SDL_Texture*> targets_;
    std::unordered_map<std::string, SDL_Texture*> cache_;
};

}
