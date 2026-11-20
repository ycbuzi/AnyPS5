#pragma once
#include <functional>
#include <nlohmann/json.hpp>
#include <string>

namespace ui::pin {

std::string sha256Hex(const std::string& data);
std::string hash(const std::string& name, const std::string& digits);

void open(const std::string& mode, const nlohmann::json& user, const std::string& then = "", std::function<void()> changed = {});

}
