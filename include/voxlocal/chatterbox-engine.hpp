#pragma once

#include "voxlocal/tts-engine.hpp"

#include <memory>

namespace voxlocal {

class ChatterboxEngine final : public ITtsEngine
{
public:
  ChatterboxEngine();
  ~ChatterboxEngine() override;

  [[nodiscard]] QString id() const override { return QStringLiteral("chatterbox-multilingual-onnx"); }
  [[nodiscard]] QString displayName() const override { return QStringLiteral("Chatterbox Multilingual"); }
  [[nodiscard]] QString backendName() const override;
  bool initialize(const QString &modelPath, QString *error = nullptr) override;
  [[nodiscard]] bool isReady() const override;
  TtsAudio synthesize(const TtsRequest &request, const std::atomic_bool &cancelled, QString *error = nullptr) override;

private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace voxlocal
