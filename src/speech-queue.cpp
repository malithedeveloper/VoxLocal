#include "voxlocal/speech-queue.hpp"

#include <QMetaObject>
#include <QtConcurrent>

#include <algorithm>

namespace voxlocal {

SpeechQueue::SpeechQueue(std::shared_ptr<ITtsEngine> engine, QObject *parent)
    : QObject(parent), engine_(std::move(engine))
{
}

SpeechQueue::~SpeechQueue()
{
  cancelled_.store(true);
  future_.waitForFinished();
}

void SpeechQueue::setCapacity(int capacity) { capacity_ = std::clamp(capacity, 1, 100); }

bool SpeechQueue::setEngine(std::shared_ptr<ITtsEngine> engine)
{
  if (!engine || speaking_ || !queue_.isEmpty())
    return false;
  engine_ = std::move(engine);
  return true;
}

int SpeechQueue::size() const { return queue_.size() + (speaking_ ? 1 : 0); }

bool SpeechQueue::enqueue(const QueueItem &item)
{
  if (queue_.size() + (speaking_ ? 1 : 0) >= capacity_)
    return false;
  queue_.enqueue(item);
  emit queueChanged(queue_.size(), speaking_);
  startNext();
  return true;
}

void SpeechQueue::clear()
{
  queue_.clear();
  cancelled_.store(true);
  emit queueChanged(0, speaking_);
}

void SpeechQueue::startNext()
{
  if (speaking_ || queue_.isEmpty())
    return;
  const auto item = queue_.dequeue();
  speaking_ = true;
  cancelled_.store(false);
  emit itemStarted(item);
  emit queueChanged(queue_.size(), true);
  future_ = QtConcurrent::run([this, item] {
    QString error;
    const auto audio = engine_->synthesize(item.request, cancelled_, &error);
    QMetaObject::invokeMethod(this, [this, item, audio, error] {
      speaking_ = false;
      if (!error.isEmpty())
        emit itemFailed(item, error);
      else if (!cancelled_.load())
        emit audioReady(item, audio);
      emit queueChanged(queue_.size(), false);
      startNext();
    });
  });
}

} // namespace voxlocal
