#pragma once

#include "voxlocal/tts-engine.hpp"

#include <QFuture>
#include <QObject>
#include <QQueue>

#include <atomic>
#include <memory>

namespace voxlocal {

class SpeechQueue final : public QObject
{
  Q_OBJECT

public:
  explicit SpeechQueue(std::shared_ptr<ITtsEngine> engine, QObject *parent = nullptr);
  ~SpeechQueue() override;

  void setCapacity(int capacity);
  bool setEngine(std::shared_ptr<ITtsEngine> engine);
  [[nodiscard]] int size() const;
  bool enqueue(const QueueItem &item);

public slots:
  void clear();

signals:
  void itemStarted(const voxlocal::QueueItem &item);
  void audioReady(const voxlocal::QueueItem &item, const voxlocal::TtsAudio &audio);
  void itemFailed(const voxlocal::QueueItem &item, const QString &error);
  void queueChanged(int waiting, bool speaking);

private:
  void startNext();

  std::shared_ptr<ITtsEngine> engine_;
  QQueue<QueueItem> queue_;
  QFuture<void> future_;
  std::atomic_bool cancelled_ = false;
  int capacity_ = kDefaultQueueCapacity;
  bool speaking_ = false;
};

} // namespace voxlocal
