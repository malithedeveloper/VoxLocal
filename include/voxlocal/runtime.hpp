#pragma once

#include "voxlocal/chatterbox-engine.hpp"
#include "voxlocal/command-router.hpp"
#include "voxlocal/config-store.hpp"
#include "voxlocal/kick-connector.hpp"
#include "voxlocal/model-manager.hpp"
#include "voxlocal/speech-queue.hpp"

#include <QJsonObject>
#include <QObject>
#include <QSet>
#include <QThreadPool>

#include <memory>

namespace voxlocal {

class VoxLocalRuntime final : public QObject
{
  Q_OBJECT

public:
  VoxLocalRuntime(QString settingsPath, QString modelRoot, QObject *parent = nullptr);
  ~VoxLocalRuntime() override;

  [[nodiscard]] const Settings &settings() const { return settings_; }
  [[nodiscard]] ModelManager *modelManager() { return &models_; }
  [[nodiscard]] KickConnector *kickConnector() { return &kick_; }
  [[nodiscard]] QString status() const { return status_; }
  [[nodiscard]] bool engineReady() const { return engine_->isReady(); }
  [[nodiscard]] bool isVoiceImporting(const QString &personaId = {}) const;

  bool applySettings(Settings settings, QString *error = nullptr);
  QString importVoice(const QString &sourcePath, const QString &personaId, QString *error = nullptr) const;
  void importVoiceAsync(const QString &sourcePath, const QString &personaId);
  void start();
  void stop();
  bool preview(const QString &personaId, const QString &text, QString *error = nullptr);

signals:
  void settingsChanged();
  void statusChanged(const QString &status);
  void modelProgress(qint64 received, qint64 total, const QString &fileName);
  void voiceImportProgress(const QString &personaId, int percent, const QString &operation);
  void voiceImportFinished(const QString &personaId, const QString &voicePath);
  void voiceImportFailed(const QString &personaId, const QString &error);
  void speechStarted(const QString &personaId);
  void speechFinished(const QString &personaId);
  void speechFailed(const QString &personaId, const QString &error);
  void overlayEvent(const QJsonObject &event);
  void audioProduced(const voxlocal::TtsAudio &audio);
  void queueChanged(int waiting, bool speaking);
  void errorOccurred(const QString &error);

private:
  class Detector;
  void setStatus(const QString &status);
  void initializeEngine(const QString &modelPath);
  void handleMessage(const ChatMessage &message);

  ConfigStore store_;
  Settings settings_;
  ModelManager models_;
  KickConnector kick_;
  std::unique_ptr<Detector> detector_;
  CommandRouter router_;
  std::shared_ptr<ChatterboxEngine> engine_;
  SpeechQueue queue_;
  QThreadPool voiceImportPool_;
  QSet<QString> voiceImports_;
  QString activeChatRequestId_;
  QDateTime globalCooldownUntil_;
  bool chatGenerationActive_ = false;
  QString status_ = QStringLiteral("idle");
};

} // namespace voxlocal
