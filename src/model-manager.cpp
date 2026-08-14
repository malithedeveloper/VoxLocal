#include "voxlocal/model-manager.hpp"

#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSaveFile>

#include <algorithm>
#include <iterator>
#include <numeric>

namespace voxlocal {
namespace {

const QString kRepository = QStringLiteral("onnx-community/chatterbox-multilingual-ONNX");

QString readyMarker(const QString &root) { return QDir(root).filePath(QStringLiteral(".ready")); }

} // namespace

ModelManager::ModelManager(QString modelRoot, QObject *parent) : QObject(parent), modelRoot_(std::move(modelRoot)) {}

QString ModelManager::revisionRoot() const
{
  return QDir(modelRoot_)
      .filePath(QStringLiteral("chatterbox-multilingual-onnx/%1").arg(QStringLiteral(VOXLOCAL_MODEL_REVISION)));
}

const std::vector<ModelFile> &ModelManager::manifest()
{
  static const std::vector<ModelFile> files = {
      {QStringLiteral("Cangjie5_TC.json"), 1920163,
       QByteArrayLiteral("7073fd9de919443ae88e0bd2449917a65fe54898a4413ed1edcc4b67f28bce8c")},
      {QStringLiteral("default_voice.wav"), 714320,
       QByteArrayLiteral("3ebc531cdaba358a327099c1c4f0448026719957bcf4d8e9868767f227e02f4e")},
      {QStringLiteral("onnx/conditional_decoder.onnx"), 6350448,
       QByteArrayLiteral("1656d0d31332bae1854839959a3139300ebb67c178651dfa3f8c5fbfa5351351")},
      {QStringLiteral("onnx/conditional_decoder.onnx_data"), 533970816,
       QByteArrayLiteral("51d58345a272747665ec9d5bb61e01835258a940e321a288582ac4c18cf01b5a")},
      {QStringLiteral("onnx/embed_tokens.onnx"), 13286,
       QByteArrayLiteral("f785819ca4f6271262d5bb8971d62796c3a909e3b031982c113dbe83a4c3b854")},
      {QStringLiteral("onnx/embed_tokens.onnx_data"), 68390912,
       QByteArrayLiteral("2a15f7dd73b2ee47f6edf87740324011594b5a528ed6471ae55e327ed6cad68c")},
      {QStringLiteral("onnx/language_model_q4.onnx"), 227911,
       QByteArrayLiteral("7f8cdca83b2493536cbf3acf421199808a3d68736f55f4eabd20ef8a99da4313")},
      {QStringLiteral("onnx/language_model_q4.onnx_data"), 353621248,
       QByteArrayLiteral("e79ab8784122a501718868b9631ff46e151c552d9b24e50f25d721f375e3526c")},
      {QStringLiteral("onnx/speech_encoder.onnx"), 1184608,
       QByteArrayLiteral("8f1c8a0f89b77bf9cd5dd8f2e034eb2c79dc00fe70d41196b28c257643b00ccb")},
      {QStringLiteral("onnx/speech_encoder.onnx_data"), 591274880,
       QByteArrayLiteral("92f8f290fc9720e169bc2412c507209e20b03f6564bc3243739e25c56f7dfb8f")},
      {QStringLiteral("tokenizer.json"), 71798,
       QByteArrayLiteral("29d48c4a178f6af3ad5130097c34744639e9294847b38a7b912c8c68027cb819")},
      {QStringLiteral("tokenizer_config.json"), 244,
       QByteArrayLiteral("b35967f93e30313d05fc9d520721ca9f671aaa5b3edbb03059aed3ff68b4c4c0")},
  };
  return files;
}

qint64 ModelManager::totalBytes() const
{
  return std::accumulate(manifest().begin(), manifest().end(), qint64{0},
                         [](qint64 total, const ModelFile &file) { return total + file.size; });
}

qint64 ModelManager::downloadedBytes() const
{
  qint64 downloaded = 0;
  const QDir root(revisionRoot());
  for (const auto &entry : manifest()) {
    const QFileInfo installed(root.filePath(entry.path));
    if (installed.isFile() && installed.size() == entry.size) {
      downloaded += entry.size;
      continue;
    }
    const QFileInfo partial(root.filePath(entry.path + QStringLiteral(".part")));
    if (partial.isFile())
      downloaded += std::clamp(partial.size(), qint64{0}, entry.size);
  }
  return std::min(downloaded, totalBytes());
}

QString ModelManager::formatBytes(qint64 bytes)
{
  static constexpr const char *units[] = {"B", "KB", "MB", "GB", "TB"};
  double value = static_cast<double>(std::max(bytes, qint64{0}));
  std::size_t unit = 0;
  while (value >= 1000.0 && unit + 1 < std::size(units)) {
    value /= 1000.0;
    ++unit;
  }
  const int decimals = unit == 0 ? 0 : 2;
  return QStringLiteral("%1 %2").arg(QString::number(value, 'f', decimals), QString::fromLatin1(units[unit]));
}

bool ModelManager::isInstalled() const
{
  if (!QFileInfo::exists(readyMarker(revisionRoot())))
    return false;
  for (const auto &file : manifest()) {
    const QFileInfo info(QDir(revisionRoot()).filePath(file.path));
    if (!info.isFile() || info.size() != file.size)
      return false;
  }
  return true;
}

void ModelManager::install()
{
  if (installing_)
    return;
  if (isInstalled()) {
    emit progress(totalBytes(), totalBytes(), {});
    emit ready(revisionRoot());
    return;
  }
  cancelled_ = false;
  currentIndex_ = 0;
  completedBytes_ = 0;
  currentFileOffset_ = 0;
  setInstalling(true);
  if (!QDir().mkpath(revisionRoot())) {
    fail(QStringLiteral("Could not create the model cache directory."));
    return;
  }
  QFile::remove(readyMarker(revisionRoot()));
  downloadNext();
}

void ModelManager::cancel()
{
  if (!installing_)
    return;
  cancelled_ = true;
  if (reply_) {
    reply_->abort();
  } else {
    partFile_.reset();
    setInstalling(false);
  }
}

void ModelManager::downloadNext()
{
  if (cancelled_)
    return;
  if (currentIndex_ >= static_cast<qsizetype>(manifest().size())) {
    QSaveFile marker(readyMarker(revisionRoot()));
    if (!marker.open(QIODevice::WriteOnly) || marker.write(QByteArrayLiteral(VOXLOCAL_MODEL_REVISION)) < 0 ||
        !marker.commit()) {
      fail(QStringLiteral("Could not finalize the model installation."));
      return;
    }
    emit progress(totalBytes(), totalBytes(), {});
    setInstalling(false);
    emit ready(revisionRoot());
    return;
  }

  const auto &entry = manifest().at(static_cast<std::size_t>(currentIndex_));
  const auto destination = QDir(revisionRoot()).filePath(entry.path);
  QString verificationError;
  if (verifyFile(entry, destination, &verificationError)) {
    completedBytes_ += entry.size;
    ++currentIndex_;
    downloadNext();
    return;
  }
  QDir().mkpath(QFileInfo(destination).absolutePath());
  const auto partPath = destination + QStringLiteral(".part");
  QFileInfo partialInfo(partPath);
  if (partialInfo.exists() && partialInfo.size() > entry.size) {
    QFile::remove(partPath);
    partialInfo.refresh();
  }
  if (partialInfo.isFile() && partialInfo.size() == entry.size) {
    if (verifyFile(entry, partPath, &verificationError)) {
      QFile::remove(destination);
      if (!QFile::rename(partPath, destination)) {
        fail(QStringLiteral("Could not move %1 into the model cache.").arg(entry.path));
        return;
      }
      completedBytes_ += entry.size;
      ++currentIndex_;
      downloadNext();
      return;
    }
    QFile::remove(partPath);
    partialInfo.refresh();
  }

  currentFileOffset_ = partialInfo.isFile() ? partialInfo.size() : 0;
  responsePrepared_ = false;
  streamError_.clear();
  partFile_ = std::make_unique<QFile>(partPath);
  if (!partFile_->open(QIODevice::ReadWrite) || !partFile_->seek(currentFileOffset_)) {
    fail(partFile_->errorString());
    return;
  }
  emit progress(completedBytes_ + currentFileOffset_, totalBytes(), entry.path);

  const auto encodedPath = QString::fromUtf8(QUrl::toPercentEncoding(entry.path, QByteArrayLiteral("/")));
  const QUrl url(QStringLiteral("https://huggingface.co/%1/resolve/%2/%3?download=true")
                     .arg(kRepository, QStringLiteral(VOXLOCAL_MODEL_REVISION), encodedPath));
  QNetworkRequest request(url);
  request.setRawHeader("User-Agent", "VoxLocal/" VOXLOCAL_VERSION);
  request.setRawHeader("Accept-Encoding", "identity");
  if (currentFileOffset_ > 0)
    request.setRawHeader("Range",
                         QByteArrayLiteral("bytes=") + QByteArray::number(currentFileOffset_) + QByteArrayLiteral("-"));
  request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
  reply_ = network_.get(request);
  connect(reply_, &QNetworkReply::metaDataChanged, this, [this] {
    if (!prepareResponse() && reply_)
      reply_->abort();
  });
  connect(reply_, &QNetworkReply::readyRead, this, [this] {
    if (!writeAvailableData() && reply_)
      reply_->abort();
  });
  connect(reply_, &QNetworkReply::downloadProgress, this, [this, entry](qint64 received, qint64) {
    emit progress(std::min(completedBytes_ + currentFileOffset_ + received, totalBytes()), totalBytes(), entry.path);
  });
  connect(reply_, &QNetworkReply::finished, this, [this, entry, destination, partPath] {
    writeAvailableData();
    const auto replyError = reply_->error();
    const auto errorText = reply_->errorString();
    reply_->deleteLater();
    reply_.clear();
    if (partFile_) {
      partFile_->flush();
      partFile_->close();
      partFile_.reset();
    }
    if (cancelled_) {
      setInstalling(false);
      return;
    }
    if (!streamError_.isEmpty()) {
      fail(streamError_);
      return;
    }
    if (replyError != QNetworkReply::NoError) {
      fail(QStringLiteral("Model download paused at %1: %2. Retry to resume.")
               .arg(ModelManager::formatBytes(QFileInfo(partPath).size()), errorText));
      return;
    }
    if (QFileInfo(partPath).size() != entry.size) {
      fail(QStringLiteral("Model download paused because %1 is incomplete. Retry to resume.").arg(entry.path));
      return;
    }
    QString verificationError;
    if (!verifyFile(entry, partPath, &verificationError)) {
      QFile::remove(partPath);
      fail(verificationError);
      return;
    }
    QFile::remove(destination);
    if (!QFile::rename(partPath, destination)) {
      fail(QStringLiteral("Could not move %1 into the model cache.").arg(entry.path));
      return;
    }
    completedBytes_ += entry.size;
    ++currentIndex_;
    downloadNext();
  });
}

bool ModelManager::prepareResponse()
{
  if (responsePrepared_ || !reply_)
    return streamError_.isEmpty();

  const int status = reply_->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
  if (status == 0 || (status >= 300 && status < 400))
    return true;
  responsePrepared_ = true;
  if (status >= 400)
    return false;

  if (currentFileOffset_ > 0 && status == 206) {
    const QByteArray expected =
        QByteArrayLiteral("bytes ") + QByteArray::number(currentFileOffset_) + QByteArrayLiteral("-");
    if (!reply_->rawHeader("Content-Range").startsWith(expected)) {
      streamError_ = QStringLiteral("The download server returned an invalid resume range.");
      return false;
    }
  } else if (currentFileOffset_ > 0 && status == 200) {
    if (!partFile_ || !partFile_->resize(0) || !partFile_->seek(0)) {
      streamError_ = QStringLiteral("Could not restart the partial model download.");
      return false;
    }
    currentFileOffset_ = 0;
  }
  return true;
}

bool ModelManager::writeAvailableData()
{
  if (!reply_ || !partFile_ || !prepareResponse())
    return false;
  if (reply_->bytesAvailable() <= 0)
    return true;
  const QByteArray data = reply_->readAll();
  if (data.isEmpty())
    return true;
  if (partFile_->write(data) != data.size() || !partFile_->flush()) {
    streamError_ = QStringLiteral("Could not write the model download to disk: %1").arg(partFile_->errorString());
    return false;
  }
  return true;
}

bool ModelManager::verifyFile(const ModelFile &entry, const QString &path, QString *error) const
{
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    if (error)
      *error = QStringLiteral("Could not read %1 for verification: %2").arg(entry.path, file.errorString());
    return false;
  }
  if (file.size() != entry.size) {
    if (error)
      *error = QStringLiteral("Downloaded size does not match for %1.").arg(entry.path);
    return false;
  }
  QCryptographicHash hash(QCryptographicHash::Sha256);
  while (!file.atEnd())
    hash.addData(file.read(4 * 1024 * 1024));
  if (hash.result().toHex() != entry.sha256) {
    if (error)
      *error = QStringLiteral("Checksum verification failed for %1.").arg(entry.path);
    return false;
  }
  return true;
}

void ModelManager::setInstalling(bool installing)
{
  if (installing_ == installing)
    return;
  installing_ = installing;
  emit installingChanged(installing_);
}

void ModelManager::fail(const QString &message)
{
  partFile_.reset();
  setInstalling(false);
  emit failed(message);
}

} // namespace voxlocal
