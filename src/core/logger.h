#pragma once
#include <string>
#include <functional>

namespace core {
using LogFn = std::function<void(const std::string&)>;
} // namespace core
