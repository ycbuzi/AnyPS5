#pragma once
#include "core/Types.h"
#include <SDL3/SDL.h>
#include <functional>
#include <vector>

namespace ui {
struct Action;
class Draw;
}

namespace ui::pointer {

void target(const Rect& r, std::function<void()> onHover, std::function<void()> onClick, float radius = 0);
void scrollArea(const Rect& r, std::function<void(float)> onScroll);
bool active();

bool visible();
float x();
float y();

void init(SDL_Window* w, SDL_Renderer* r, bool drawnCursor);
void handle(const SDL_Event& e, std::vector<Action>& out);
void keyUsed();
void beginFrame(Draw& d);
void layer(bool hittable);
void endFrame(std::vector<Action>& out);
void drawCursor(Draw& d);

void simMove(float x, float y);
void simClick(float x, float y, int button = 1);
void simWheel(float rows);

}
