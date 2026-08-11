#pragma once

#include "voxlocal/types.hpp"

#include <QString>

#include <atomic>

namespace voxlocal {

class ITtsEngine
{
public:
  virtual ~ITtsEngine() = default;
  [[nodiscard]] virtual QString id() const = 0;
  [[nodiscard]] virtual QString displayName() const = 0;
  [[nodiscard]] virtual QString backendName() const = 0;
  virtual bool initialize(const QString &modelPath, QString *error = nullptr) = 0;
  [[nodiscard]] virtual bool isReady() const = 0;
  virtual TtsAudio synthesize(const TtsRequest &request, const std::atomic_bool &cancelled,
                              QString *error = nullptr) = 0;
};

} // namespace voxlocal
