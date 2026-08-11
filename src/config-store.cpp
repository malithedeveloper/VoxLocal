#include "voxlocal/config-store.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

#include <algorithm>
#include <nlohmann/json.hpp>

namespace voxlocal {
namespace {

using json = nlohmann::json;

std::string utf8(const QString &value) { return value.toUtf8().toStdString(); }
QString qstring(const json &value, const QString &fallback = {})
{
  return value.is_string() ? QString::fromUtf8(value.get<std::string>()) : fallback;
}

json accessToJson(const AccessPolicy &access) { return {{"allowed", static_cast<std::uint32_t>(access.allowed)}}; }

AccessPolicy accessFromJson(const json &value)
{
  AccessPolicy result;
  if (!value.is_object())
    return result;
  result.allowed = static_cast<UserRole>(value.value("allowed", static_cast<std::uint32_t>(UserRole::Everyone)));
  return result;
}

json personaToJson(const Persona &persona)
{
  return {{"id", utf8(persona.id)},
          {"name", utf8(persona.name)},
          {"command", utf8(persona.command)},
          {"engineId", utf8(persona.engineId)},
          {"modelRevision", utf8(persona.modelRevision)},
          {"referenceAudioPath", utf8(persona.referenceAudioPath)},
          {"conditioningCachePath", utf8(persona.conditioningCachePath)},
          {"languageMode", persona.languageMode == LanguageMode::Automatic ? "automatic" : "fixed"},
          {"language", utf8(persona.language)},
          {"automaticFallbackLanguage", utf8(persona.automaticFallbackLanguage)},
          {"exaggeration", persona.exaggeration},
          {"access", accessToJson(persona.access)},
          {"enabled", persona.enabled}};
}

Persona personaFromJson(const json &value)
{
  Persona result;
  if (!value.is_object())
    return result;
  result.id = qstring(value.value("id", json()), result.id);
  result.name = qstring(value.value("name", json()), result.name);
  result.command = qstring(value.value("command", json()), result.command);
  result.engineId = qstring(value.value("engineId", json()), result.engineId);
  result.modelRevision = qstring(value.value("modelRevision", json()), result.modelRevision);
  result.referenceAudioPath = qstring(value.value("referenceAudioPath", json()));
  result.conditioningCachePath = qstring(value.value("conditioningCachePath", json()));
  result.languageMode = qstring(value.value("languageMode", json())) == QStringLiteral("automatic")
                            ? LanguageMode::Automatic
                            : LanguageMode::Fixed;
  result.language = qstring(value.value("language", json()), QStringLiteral("en")).toLower();
  result.automaticFallbackLanguage =
      qstring(value.value("automaticFallbackLanguage", json()), QStringLiteral("en")).toLower();
  result.exaggeration = std::clamp(value.value("exaggeration", 0.5F), 0.0F, 1.0F);
  if (value.contains("access"))
    result.access = accessFromJson(value.at("access"));
  result.enabled = value.value("enabled", true);
  return result;
}

json toJson(const Settings &settings)
{
  json personas = json::array();
  for (const auto &persona : settings.personas)
    personas.push_back(personaToJson(persona));

  return {{"schemaVersion", kConfigSchemaVersion},
          {"interfaceLanguage", settings.interfaceLanguage == InterfaceLanguage::Turkish ? "tr" : "en"},
          {"defaultSpeechLanguage", utf8(settings.defaultSpeechLanguage)},
          {"welcomeCompleted", settings.welcomeCompleted},
          {"readUrls", settings.readUrls},
          {"ttsEnabled", settings.ttsEnabled},
          {"globalCooldownSeconds", settings.globalCooldownSeconds},
          {"maxTextLength", settings.maxTextLength},
          {"queueCapacity", settings.queueCapacity},
          {"kick",
           {{"channelSlug", utf8(settings.kick.channelSlug)},
            {"channelId", utf8(settings.kick.channelId)},
            {"chatroomId", utf8(settings.kick.chatroomId)},
            {"broadcasterUserId", utf8(settings.kick.broadcasterUserId)},
            {"pusherKey", utf8(settings.kick.pusherKey)},
            {"pusherCluster", utf8(settings.kick.pusherCluster)},
            {"enabled", settings.kick.enabled}}},
          {"overlay",
           {{"preset", utf8(settings.overlay.preset)},
            {"fontFamily", utf8(settings.overlay.fontFamily)},
            {"background", utf8(settings.overlay.background)},
            {"foreground", utf8(settings.overlay.foreground)},
            {"fallbackNameColor", utf8(settings.overlay.fallbackNameColor)},
            {"entranceAnimation", utf8(settings.overlay.entranceAnimation)},
            {"width", settings.overlay.width},
            {"height", settings.overlay.height},
            {"borderRadius", settings.overlay.borderRadius},
            {"visibleMilliseconds", settings.overlay.visibleMilliseconds},
            {"showName", settings.overlay.showName}}},
          {"personas", personas}};
}

Settings fromJson(const json &root)
{
  Settings result = ConfigStore::defaults();
  if (!root.is_object())
    return result;
  result.schemaVersion = kConfigSchemaVersion;
  result.interfaceLanguage = qstring(root.value("interfaceLanguage", json())) == QStringLiteral("tr")
                                 ? InterfaceLanguage::Turkish
                                 : InterfaceLanguage::English;
  result.defaultSpeechLanguage = qstring(root.value("defaultSpeechLanguage", json()), QStringLiteral("en")).toLower();
  result.welcomeCompleted = root.value("welcomeCompleted", false);
  result.readUrls = root.value("readUrls", false);
  result.ttsEnabled = root.value("ttsEnabled", true);
  result.globalCooldownSeconds =
      std::clamp(root.value("globalCooldownSeconds", kDefaultGlobalCooldownSeconds), 0, 3600);
  result.maxTextLength = std::clamp(root.value("maxTextLength", kDefaultMaxTextLength), 1, 1000);
  result.queueCapacity = std::clamp(root.value("queueCapacity", kDefaultQueueCapacity), 1, 100);

  if (const auto it = root.find("kick"); it != root.end() && it->is_object()) {
    result.kick.channelSlug = qstring(it->value("channelSlug", json()));
    result.kick.channelId = qstring(it->value("channelId", json()));
    result.kick.chatroomId = qstring(it->value("chatroomId", json()));
    result.kick.broadcasterUserId = qstring(it->value("broadcasterUserId", json()));
    result.kick.pusherKey = qstring(it->value("pusherKey", json()), result.kick.pusherKey);
    result.kick.pusherCluster = qstring(it->value("pusherCluster", json()), result.kick.pusherCluster);
    result.kick.enabled = it->value("enabled", true);
  }
  if (const auto it = root.find("overlay"); it != root.end() && it->is_object()) {
    result.overlay.preset = qstring(it->value("preset", json()), result.overlay.preset);
    if (result.overlay.preset != QStringLiteral("minimal") && result.overlay.preset != QStringLiteral("subtitle"))
      result.overlay.preset = QStringLiteral("minimal");
    result.overlay.fontFamily = qstring(it->value("fontFamily", json()), result.overlay.fontFamily);
    result.overlay.background = qstring(it->value("background", json()), result.overlay.background);
    result.overlay.foreground = qstring(it->value("foreground", json()), result.overlay.foreground);
    result.overlay.fallbackNameColor = qstring(it->value("fallbackNameColor", json()),
                                               qstring(it->value("accent", json()), result.overlay.fallbackNameColor));
    result.overlay.entranceAnimation =
        qstring(it->value("entranceAnimation", json()), result.overlay.entranceAnimation);
    static const QStringList animations{QStringLiteral("fade"),        QStringLiteral("slide-left"),
                                        QStringLiteral("slide-right"), QStringLiteral("slide-down"),
                                        QStringLiteral("slide-up"),    QStringLiteral("none")};
    if (!animations.contains(result.overlay.entranceAnimation))
      result.overlay.entranceAnimation = QStringLiteral("slide-up");
    result.overlay.width = std::clamp(it->value("width", 960), 160, 7680);
    result.overlay.height = std::clamp(it->value("height", 260), 80, 4320);
    result.overlay.borderRadius = std::clamp(it->value("borderRadius", 24), 0, 200);
    result.overlay.visibleMilliseconds = std::clamp(it->value("visibleMilliseconds", 8000), 500, 60000);
    result.overlay.showName = it->value("showName", it->value("showRequester", true));
  }
  result.personas.clear();
  if (const auto it = root.find("personas"); it != root.end() && it->is_array()) {
    for (const auto &value : *it)
      result.personas.push_back(personaFromJson(value));
  }
  return result;
}

} // namespace

ConfigStore::ConfigStore(QString filePath) : filePath_(std::move(filePath)) {}

Settings ConfigStore::load(QString *error) const
{
  QFile file(filePath_);
  if (!file.exists())
    return defaults();
  if (!file.open(QIODevice::ReadOnly)) {
    if (error)
      *error = file.errorString();
    return defaults();
  }
  try {
    const auto root = json::parse(file.readAll().toStdString());
    return fromJson(root);
  } catch (const std::exception &exception) {
    if (error)
      *error = QString::fromUtf8(exception.what());
    return defaults();
  }
}

bool ConfigStore::save(const Settings &settings, QString *error) const
{
  const QFileInfo info(filePath_);
  if (!QDir().mkpath(info.absolutePath())) {
    if (error)
      *error = QStringLiteral("Could not create settings directory: %1").arg(info.absolutePath());
    return false;
  }
  QSaveFile file(filePath_);
  if (!file.open(QIODevice::WriteOnly)) {
    if (error)
      *error = file.errorString();
    return false;
  }
  const auto serialized = QByteArray::fromStdString(toJson(settings).dump(2));
  if (file.write(serialized) != serialized.size() || !file.commit()) {
    if (error)
      *error = file.errorString();
    return false;
  }
  return true;
}

Settings ConfigStore::defaults() { return {}; }

QStringList ConfigStore::supportedLanguages()
{
  return {QStringLiteral("ar"), QStringLiteral("da"), QStringLiteral("de"), QStringLiteral("el"), QStringLiteral("en"),
          QStringLiteral("es"), QStringLiteral("fi"), QStringLiteral("fr"), QStringLiteral("he"), QStringLiteral("hi"),
          QStringLiteral("it"), QStringLiteral("ja"), QStringLiteral("ko"), QStringLiteral("ms"), QStringLiteral("nl"),
          QStringLiteral("no"), QStringLiteral("pl"), QStringLiteral("pt"), QStringLiteral("ru"), QStringLiteral("sv"),
          QStringLiteral("sw"), QStringLiteral("tr"), QStringLiteral("zh")};
}

bool ConfigStore::isSupportedLanguage(const QString &code)
{
  return supportedLanguages().contains(code.trimmed().toLower());
}

} // namespace voxlocal
