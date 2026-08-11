#pragma once

#include "voxlocal/chat-connector.hpp"

#include <QNetworkAccessManager>
#include <QQueue>
#include <QSet>
#include <QTimer>
#include <QWebSocket>

namespace voxlocal {

class KickConnector final : public QObject, public IChatConnector
{
  Q_OBJECT

public:
  explicit KickConnector(QObject *parent = nullptr);
  ~KickConnector() override;

  void start(const KickSettings &settings) override;
  void stop() override;
  [[nodiscard]] bool isConnected() const override;
  [[nodiscard]] QString name() const override { return QStringLiteral("Kick"); }

  static std::optional<ChatMessage> parseChatEvent(const QByteArray &frame, const QString &broadcasterUserId = {});

signals:
  void messageReceived(const voxlocal::ChatMessage &message);
  void statusChanged(const QString &status);
  void configurationResolved(const voxlocal::KickSettings &settings);
  void errorOccurred(const QString &message);

private:
  void resolveChannel();
  void resolveFromEndpoint(const QUrl &url, bool allowFallback);
  void openSocket();
  void subscribe();
  void scheduleReconnect(const QString &reason);
  void rememberMessage(const QString &id);
  void handleFrame(const QString &frame);

  QNetworkAccessManager network_;
  QWebSocket socket_;
  QTimer reconnectTimer_;
  QTimer pingTimer_;
  KickSettings settings_;
  QSet<QString> seenMessages_;
  QQueue<QString> seenOrder_;
  int reconnectAttempt_ = 0;
  bool stopping_ = true;
};

} // namespace voxlocal
