#pragma once

#include <QFile>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QObject>
#include <QPointer>

#include <memory>
#include <vector>

namespace voxlocal {

struct ModelFile {
  QString path;
  qint64 size = 0;
  QByteArray sha256;
};

class ModelManager final : public QObject
{
  Q_OBJECT

public:
  explicit ModelManager(QString modelRoot, QObject *parent = nullptr);

  [[nodiscard]] QString modelRoot() const { return modelRoot_; }
  [[nodiscard]] QString revisionRoot() const;
  [[nodiscard]] bool isInstalled() const;
  [[nodiscard]] bool isInstalling() const { return installing_; }
  [[nodiscard]] qint64 downloadedBytes() const;
  [[nodiscard]] qint64 totalBytes() const;
  [[nodiscard]] static QString formatBytes(qint64 bytes);
  [[nodiscard]] static const std::vector<ModelFile> &manifest();

public slots:
  void install();
  void cancel();

signals:
  void progress(qint64 received, qint64 total, const QString &fileName);
  void installingChanged(bool installing);
  void ready(const QString &path);
  void failed(const QString &message);

private:
  void downloadNext();
  bool prepareResponse();
  bool writeAvailableData();
  bool verifyFile(const ModelFile &file, const QString &path, QString *error) const;
  void setInstalling(bool installing);
  void fail(const QString &message);

  QString modelRoot_;
  QNetworkAccessManager network_;
  QPointer<QNetworkReply> reply_;
  std::unique_ptr<QFile> partFile_;
  qsizetype currentIndex_ = 0;
  qint64 completedBytes_ = 0;
  qint64 currentFileOffset_ = 0;
  QString streamError_;
  bool responsePrepared_ = false;
  bool installing_ = false;
  bool cancelled_ = false;
};

} // namespace voxlocal
