#pragma once

#include "voxlocal/types.hpp"

#include <QString>

namespace voxlocal {

class IChatConnector
{
public:
  virtual ~IChatConnector() = default;
  virtual void start(const KickSettings &settings) = 0;
  virtual void stop() = 0;
  [[nodiscard]] virtual bool isConnected() const = 0;
  [[nodiscard]] virtual QString name() const = 0;
};

} // namespace voxlocal
