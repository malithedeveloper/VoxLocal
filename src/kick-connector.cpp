#include "voxlocal/kick-connector.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QUrlQuery>

#include <algorithm>

namespace voxlocal {
namespace {

QJsonObject objectFromVariant(const QJsonValue &value)
{
  if (value.isObject())
    return value.toObject();
  if (value.isString())
    return QJsonDocument::fromJson(value.toString().toUtf8()).object();
  return {};
}

QString stringValue(const QJsonObject &object, const QString &key)
{
  const auto value = object.value(key);
  if (value.isString())
    return value.toString();
  if (value.isDouble())
    return QString::number(static_cast<qint64>(value.toDouble()));
  return {};
}

QJsonObject eventPayload(const QJsonObject &frame)
{
  auto payload = objectFromVariant(frame.value(QStringLiteral("data")));
  if (payload.contains(QStringLiteral("message")) && payload.value(QStringLiteral("message")).isObject())
    payload = payload.value(QStringLiteral("message")).toObject();
  return payload;
}

} // namespace

KickConnector::KickConnector(QObject *parent) : QObject(parent)
{
  reconnectTimer_.setSingleShot(true);
  connect(&reconnectTimer_, &QTimer::timeout, this, &KickConnector::openSocket);
  pingTimer_.setInterval(55000);
  connect(&pingTimer_, &QTimer::timeout, this, [this] {
    if (socket_.state() == QAbstractSocket::ConnectedState) {
      socket_.sendTextMessage(QStringLiteral(R"({"event":"pusher:ping","data":{}})"));
      socket_.ping(QByteArrayLiteral("voxlocal"));
    }
  });
  connect(&socket_, &QWebSocket::connected, this, [this] {
    reconnectAttempt_ = 0;
    pingTimer_.start();
    emit statusChanged(QStringLiteral("connected"));
  });
  connect(&socket_, &QWebSocket::disconnected, this, [this] {
    pingTimer_.stop();
    if (!stopping_)
      scheduleReconnect(QStringLiteral("socket-disconnected"));
  });
  connect(&socket_, &QWebSocket::textMessageReceived, this, &KickConnector::handleFrame);
  connect(&socket_, &QWebSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) {
    if (!stopping_)
      emit errorOccurred(socket_.errorString());
  });
}

KickConnector::~KickConnector() { stop(); }

void KickConnector::start(const KickSettings &settings)
{
  stop();
  settings_ = settings;
  settings_.channelSlug = settings_.channelSlug.trimmed().toLower();
  stopping_ = false;
  seenMessages_.clear();
  seenOrder_.clear();
  reconnectAttempt_ = 0;
  if (settings_.channelSlug.isEmpty() && (settings_.channelId.isEmpty() || settings_.chatroomId.isEmpty())) {
    emit errorOccurred(QStringLiteral("A Kick channel slug or manual channel/chatroom IDs are required."));
    return;
  }
  if (settings_.channelId.isEmpty() || settings_.chatroomId.isEmpty())
    resolveChannel();
  else
    openSocket();
}

void KickConnector::stop()
{
  stopping_ = true;
  reconnectTimer_.stop();
  pingTimer_.stop();
  if (socket_.state() != QAbstractSocket::UnconnectedState)
    socket_.close();
  emit statusChanged(QStringLiteral("stopped"));
}

bool KickConnector::isConnected() const { return socket_.state() == QAbstractSocket::ConnectedState; }

void KickConnector::resolveChannel()
{
  emit statusChanged(QStringLiteral("resolving-channel"));
  resolveFromEndpoint(QUrl(QStringLiteral("https://kick.com/api/v2/channels/%1").arg(settings_.channelSlug)), true);
}

void KickConnector::resolveFromEndpoint(const QUrl &url, bool allowFallback)
{
  QNetworkRequest request(url);
  request.setRawHeader("Accept", "application/json");
  request.setRawHeader("User-Agent", "Mozilla/5.0 VoxLocal/" VOXLOCAL_VERSION);
  request.setRawHeader("Referer", "https://kick.com/");
  request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
  auto *reply = network_.get(request);
  connect(reply, &QNetworkReply::finished, this, [this, reply, allowFallback] {
    const auto data = reply->readAll();
    const auto failed = reply->error() != QNetworkReply::NoError;
    const auto error = reply->errorString();
    reply->deleteLater();
    if (failed) {
      if (allowFallback) {
        resolveFromEndpoint(QUrl(QStringLiteral("https://kick.com/api/v1/channels/%1").arg(settings_.channelSlug)),
                            false);
      } else {
        emit errorOccurred(QStringLiteral("Kick channel lookup failed: %1").arg(error));
        scheduleReconnect(QStringLiteral("channel-lookup-failed"));
      }
      return;
    }
    const auto root = QJsonDocument::fromJson(data).object();
    settings_.channelId = stringValue(root, QStringLiteral("id"));
    settings_.chatroomId = stringValue(root.value(QStringLiteral("chatroom")).toObject(), QStringLiteral("id"));
    settings_.broadcasterUserId = stringValue(root, QStringLiteral("user_id"));
    if (settings_.broadcasterUserId.isEmpty())
      settings_.broadcasterUserId = stringValue(root.value(QStringLiteral("user")).toObject(), QStringLiteral("id"));
    if (settings_.channelId.isEmpty() || settings_.chatroomId.isEmpty()) {
      if (allowFallback) {
        resolveFromEndpoint(QUrl(QStringLiteral("https://kick.com/api/v1/channels/%1").arg(settings_.channelSlug)),
                            false);
      } else {
        emit errorOccurred(QStringLiteral(
            "Kick returned incomplete channel data. Set channel and chatroom IDs in Advanced settings."));
      }
      return;
    }
    emit configurationResolved(settings_);
    openSocket();
  });
}

void KickConnector::openSocket()
{
  if (stopping_)
    return;
  if (settings_.channelId.isEmpty() || settings_.chatroomId.isEmpty()) {
    resolveChannel();
    return;
  }
  emit statusChanged(QStringLiteral("connecting"));
  const auto host = QStringLiteral("ws-%1.pusher.com").arg(settings_.pusherCluster);
  QUrl url(QStringLiteral("wss://%1/app/%2").arg(host, settings_.pusherKey));
  QUrlQuery query;
  query.addQueryItem(QStringLiteral("protocol"), QStringLiteral("7"));
  query.addQueryItem(QStringLiteral("client"), QStringLiteral("js"));
  query.addQueryItem(QStringLiteral("version"), QStringLiteral("7.6.0"));
  query.addQueryItem(QStringLiteral("flash"), QStringLiteral("false"));
  url.setQuery(query);
  socket_.open(url);
}

void KickConnector::subscribe()
{
  const QStringList channels = {
      QStringLiteral("channel_%1").arg(settings_.channelId),
      QStringLiteral("channel.%1").arg(settings_.channelId),
      QStringLiteral("chatroom_%1").arg(settings_.chatroomId),
      QStringLiteral("chatrooms.%1").arg(settings_.chatroomId),
      QStringLiteral("chatrooms.%1.v2").arg(settings_.chatroomId),
  };
  for (const auto &channel : channels) {
    QJsonObject data{{QStringLiteral("channel"), channel}};
    QJsonObject frame{{QStringLiteral("event"), QStringLiteral("pusher:subscribe")}, {QStringLiteral("data"), data}};
    socket_.sendTextMessage(QString::fromUtf8(QJsonDocument(frame).toJson(QJsonDocument::Compact)));
  }
}

void KickConnector::scheduleReconnect(const QString &reason)
{
  if (stopping_ || reconnectTimer_.isActive())
    return;
  const int exponential = std::min(30000, 1000 * (1 << std::min(reconnectAttempt_, 5)));
  const int jitter = QRandomGenerator::global()->bounded(400);
  ++reconnectAttempt_;
  emit statusChanged(QStringLiteral("reconnecting:%1:%2").arg(reason).arg(exponential + jitter));
  reconnectTimer_.start(exponential + jitter);
}

void KickConnector::rememberMessage(const QString &id)
{
  if (id.isEmpty() || seenMessages_.contains(id))
    return;
  seenMessages_.insert(id);
  seenOrder_.enqueue(id);
  while (seenOrder_.size() > 2048)
    seenMessages_.remove(seenOrder_.dequeue());
}

void KickConnector::handleFrame(const QString &frame)
{
  if (frame.toUtf8().size() > 2 * 1024 * 1024) {
    emit errorOccurred(QStringLiteral("Kick sent a frame larger than the 2 MiB safety limit."));
    return;
  }
  const auto object = QJsonDocument::fromJson(frame.toUtf8()).object();
  const auto event = object.value(QStringLiteral("event")).toString();
  if (event == QStringLiteral("pusher:connection_established")) {
    subscribe();
    return;
  }
  if (event == QStringLiteral("pusher:ping")) {
    socket_.sendTextMessage(QStringLiteral(R"({"event":"pusher:pong","data":{}})"));
    return;
  }
  if (event == QStringLiteral("pusher:pong") || event.startsWith(QStringLiteral("pusher_internal:")))
    return;
  const auto parsed = parseChatEvent(frame.toUtf8(), settings_.broadcasterUserId);
  if (!parsed || (!parsed->id.isEmpty() && seenMessages_.contains(parsed->id)))
    return;
  rememberMessage(parsed->id);
  emit messageReceived(*parsed);
}

std::optional<ChatMessage> KickConnector::parseChatEvent(const QByteArray &frame, const QString &broadcasterUserId)
{
  QJsonParseError error;
  const auto rootDocument = QJsonDocument::fromJson(frame, &error);
  if (error.error != QJsonParseError::NoError || !rootDocument.isObject())
    return std::nullopt;
  const auto root = rootDocument.object();
  const auto event = root.value(QStringLiteral("event")).toString();
  if (!event.contains(QStringLiteral("ChatMessageEvent")) && !event.contains(QStringLiteral("SentEvent")))
    return std::nullopt;
  const auto payload = eventPayload(root);
  ChatMessage message;
  message.id = stringValue(payload, QStringLiteral("id"));
  message.channelId = stringValue(payload, QStringLiteral("chatroom_id"));
  message.text = payload.value(QStringLiteral("content")).toString();
  message.type = payload.value(QStringLiteral("type")).toString(QStringLiteral("message"));
  const auto sender = payload.value(QStringLiteral("sender")).toObject();
  message.sender.id = stringValue(sender, QStringLiteral("id"));
  message.sender.username = sender.value(QStringLiteral("username")).toString();
  message.sender.isBroadcaster = !broadcasterUserId.isEmpty() && message.sender.id == broadcasterUserId;
  const auto identity = sender.value(QStringLiteral("identity")).toObject();
  message.sender.color = identity.value(QStringLiteral("color")).toString();
  if (message.sender.color.isEmpty())
    message.sender.color = sender.value(QStringLiteral("color")).toString();
  if (!message.sender.color.contains(QRegularExpression(QStringLiteral("^#[0-9A-Fa-f]{6}$"))))
    message.sender.color.clear();
  for (const auto &badgeValue : identity.value(QStringLiteral("badges")).toArray()) {
    const auto badgeObject = badgeValue.toObject();
    ChatBadge badge;
    badge.type = badgeObject.value(QStringLiteral("type")).toString();
    badge.text = badgeObject.value(QStringLiteral("text")).toString();
    badge.count = badgeObject.value(QStringLiteral("count")).toInt();
    message.sender.badges.push_back(std::move(badge));
  }
  if (message.sender.id.isEmpty() && message.sender.username.isEmpty())
    return std::nullopt;
  return message;
}

} // namespace voxlocal
