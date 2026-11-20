#pragma once
#include <SDL3/SDL.h>
#include <functional>
#include <optional>
#include <string>

namespace ui::images {

using Fetch = std::function<std::optional<std::string>(const std::string& url)>;

void init(SDL_Renderer* r, Fetch fetch, const std::string& assetDir);
void shutdown();
void setRenderer(SDL_Renderer* r);
SDL_Texture* get(const std::string& url);
bool failed(const std::string& url);
void pump();
void waitIdle();
SDL_Texture* icon(const std::string& name, float px);
SDL_Texture* asset(const std::string& file);
const std::string& assetDir();

}
