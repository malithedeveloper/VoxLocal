#pragma once

#include <QDateTime>
#include <QString>
#include <QStringList>
#include <QUuid>

#include <cstdint>
#include <optional>
#include <vector>

#ifndef VOXLOCAL_MODEL_REVISION
#define VOXLOCAL_MODEL_REVISION "452d3f434aa592098f1eedac9099f33642ab2da5"
#endif

namespace voxlocal {

inline constexpr int kConfigSchemaVersion = 3;
inline constexpr int kDefaultMaxTextLength = 250;
inline constexpr int kDefaultGlobalCooldownSeconds = 10;
inline constexpr int kDefaultQueueCapacity = 10;

enum class InterfaceLanguage { English, Turkish };
enum class LanguageMode { Fixed, Automatic };
enum class UserRole : std::uint32_t {
  None = 0,
  Everyone = 1u << 0,
  Subscriber = 1u << 1,
  Moderator = 1u << 2,
  Broadcaster = 1u << 3,
};

constexpr UserRole operator|(UserRole lhs, UserRole rhs)
{
  return static_cast<UserRole>(static_cast<std::uint32_t>(lhs) | static_cast<std::uint32_t>(rhs));
}

constexpr bool hasRole(UserRole value, UserRole flag)
{
  return (static_cast<std::uint32_t>(value) & static_cast<std::uint32_t>(flag)) != 0;
}

struct ChatBadge {
  QString type;
  QString text;
  int count = 0;
};

struct ChatUser {
  QString id;
  QString username;
  QString color;
  std::vector<ChatBadge> badges;
  bool isBroadcaster = false;
};

struct ChatMessage {
  QString id;
  QString channelId;
  QString text;
  QString type = QStringLiteral("message");
  ChatUser sender;
  QDateTime receivedAt = QDateTime::currentDateTimeUtc();
};

struct AccessPolicy {
  UserRole allowed = UserRole::Everyone;
};

struct Persona {
  QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
  QString name = QStringLiteral("Voice");
  QString command = QStringLiteral("voice");
  QString engineId = QStringLiteral("chatterbox-multilingual-onnx");
  QString modelRevision = QStringLiteral(VOXLOCAL_MODEL_REVISION);
  QString referenceAudioPath;
  QString conditioningCachePath;
  LanguageMode languageMode = LanguageMode::Fixed;
  QString language = QStringLiteral("en");
  QString automaticFallbackLanguage = QStringLiteral("en");
  float exaggeration = 0.5F;
  AccessPolicy access;
  bool enabled = true;
};

struct KickSettings {
  QString channelSlug;
  QString channelId;
  QString chatroomId;
  QString broadcasterUserId;
  QString pusherKey = QStringLiteral("32cbd69e4b950bf97679");
  QString pusherCluster = QStringLiteral("us2");
  bool enabled = true;
};

struct OverlaySettings {
  QString preset = QStringLiteral("minimal");
  QString fontFamily = QStringLiteral("Inter");
  QString background = QStringLiteral("#000000");
  QString foreground = QStringLiteral("#ffffff");
  QString fallbackNameColor = QStringLiteral("#53fc18");
  QString entranceAnimation = QStringLiteral("slide-up");
  int width = 960;
  int height = 260;
  int borderRadius = 24;
  int visibleMilliseconds = 8000;
  bool showName = true;
};

struct Settings {
  int schemaVersion = kConfigSchemaVersion;
  InterfaceLanguage interfaceLanguage = InterfaceLanguage::English;
  QString defaultSpeechLanguage = QStringLiteral("en");
  bool welcomeCompleted = false;
  bool readUrls = false;
  bool ttsEnabled = true;
  int globalCooldownSeconds = kDefaultGlobalCooldownSeconds;
  int maxTextLength = kDefaultMaxTextLength;
  int queueCapacity = kDefaultQueueCapacity;
  KickSettings kick;
  OverlaySettings overlay;
  std::vector<Persona> personas;
};

struct TtsRequest {
  QString requestId = QUuid::createUuid().toString(QUuid::WithoutBraces);
  QString personaId;
  QString personaName;
  QString requesterId;
  QString requesterName;
  QString requesterColor;
  QString text;
  QString language;
  QString referenceAudioPath;
  float exaggeration = 0.5F;
};

struct TtsAudio {
  int sampleRate = 24000;
  int channels = 1;
  std::vector<float> samples;
};

struct QueueItem {
  TtsRequest request;
  ChatMessage message;
};

struct RouteResult {
  std::optional<TtsRequest> request;
  QString rejectionReason;
};

} // namespace voxlocal
