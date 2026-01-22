#pragma once
#include <memory>
#include <string>
#include <nlohmann/json.hpp>

namespace core {

struct Connection {
  virtual ~Connection() = default;
  virtual bool send(const nlohmann::json& j) = 0;
  virtual void close() = 0;
  virtual std::string id() const = 0; // unique key
};

using ConnPtr = std::shared_ptr<Connection>;

} // namespace core
