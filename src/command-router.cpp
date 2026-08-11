#include "voxlocal/command-router.hpp"

#include "voxlocal/config-store.hpp"

#include <QRegularExpression>

#include <algorithm>

namespace voxlocal {
namespace {

QString identity(QString value) { return value.trimmed().normalized(QString::NormalizationForm_KC).toCaseFolded(); }

bool hasBadge(const ChatUser &user, const QString &badge)
{
  return std::ranges::any_of(
      user.badges, [&](const ChatBadge &value) { return value.type.compare(badge, Qt::CaseInsensitive) == 0; });
}

} // namespace

CommandRouter::CommandRouter(const LanguageDetector *detector) : detector_(detector) {}

RouteResult CommandRouter::route(const ChatMessage &message, const Settings &settings, const QDateTime &)
{
  RouteResult result;
  if (message.type != QStringLiteral("message")) {
    result.rejectionReason = QStringLiteral("unsupported-message-type");
    return result;
  }
  const auto simplified = message.text.trimmed();
  if (!simplified.startsWith(QLatin1Char('!'))) {
    result.rejectionReason = QStringLiteral("not-a-command");
    return result;
  }
  const auto separator = simplified.indexOf(QRegularExpression(QStringLiteral("\\s")));
  const auto command = normalizeCommand(separator < 0 ? simplified : simplified.left(separator));
  auto text = separator < 0 ? QString{} : simplified.mid(separator + 1);

  const Persona *persona = nullptr;
  for (const auto &candidate : settings.personas) {
    if (candidate.enabled && normalizeCommand(candidate.command) == command) {
      persona = &candidate;
      break;
    }
  }
  if (!persona) {
    result.rejectionReason = QStringLiteral("unknown-command");
    return result;
  }
  if (!isAllowed(message.sender, persona->access)) {
    result.rejectionReason = QStringLiteral("access-denied");
    return result;
  }

  text = sanitizeText(std::move(text), settings.readUrls).left(settings.maxTextLength).trimmed();
  if (text.isEmpty()) {
    result.rejectionReason = QStringLiteral("empty-text");
    return result;
  }

  TtsRequest request;
  request.personaId = persona->id;
  request.personaName = persona->name;
  request.requesterId = message.sender.id;
  request.requesterName = message.sender.username;
  request.requesterColor = message.sender.color;
  request.text = text;
  request.language = chooseLanguage(*persona, text);
  request.referenceAudioPath = persona->referenceAudioPath;
  request.exaggeration = persona->exaggeration;
  result.request = std::move(request);
  return result;
}

QString CommandRouter::normalizeCommand(QString command)
{
  command = identity(std::move(command));
  while (command.startsWith(QLatin1Char('!')))
    command.remove(0, 1);
  command.remove(QRegularExpression(QStringLiteral("[^\\p{L}\\p{N}_-]")));
  return command;
}

QString CommandRouter::sanitizeText(QString text, bool readUrls)
{
  text.remove(
      QRegularExpression(QStringLiteral("\\[(?:emote|emoji):[^\\]]+\\]"), QRegularExpression::CaseInsensitiveOption));
  if (!readUrls)
    text.remove(
        QRegularExpression(QStringLiteral("(?:https?://|www\\.)\\S+"), QRegularExpression::CaseInsensitiveOption));
  text.remove(
      QRegularExpression(QStringLiteral("[\\x{0000}-\\x{0008}\\x{000B}\\x{000C}\\x{000E}-\\x{001F}\\x{007F}]")));
  return text.simplified();
}

UserRole CommandRouter::rolesFor(const ChatUser &user, const AccessPolicy &)
{
  UserRole roles = UserRole::None;
  if (user.isBroadcaster || hasBadge(user, QStringLiteral("broadcaster")))
    roles = roles | UserRole::Broadcaster;
  if (hasBadge(user, QStringLiteral("moderator")) || hasBadge(user, QStringLiteral("mod")))
    roles = roles | UserRole::Moderator;
  if (hasBadge(user, QStringLiteral("subscriber")) || hasBadge(user, QStringLiteral("sub")))
    roles = roles | UserRole::Subscriber;
  return roles;
}

bool CommandRouter::isAllowed(const ChatUser &user, const AccessPolicy &policy) const
{
  if (hasRole(policy.allowed, UserRole::Everyone))
    return true;
  const auto roles = rolesFor(user, policy);
  const auto requested = static_cast<std::uint32_t>(policy.allowed);
  return (static_cast<std::uint32_t>(roles) & requested) != 0;
}

QString CommandRouter::chooseLanguage(const Persona &persona, const QString &text) const
{
  const auto fallback = ConfigStore::isSupportedLanguage(persona.automaticFallbackLanguage)
                            ? persona.automaticFallbackLanguage.toLower()
                            : QStringLiteral("en");
  if (persona.languageMode == LanguageMode::Fixed)
    return ConfigStore::isSupportedLanguage(persona.language) ? persona.language.toLower() : fallback;
  if (!detector_)
    return fallback;
  int letters = 0;
  for (const auto character : text) {
    if (character.isLetter())
      ++letters;
  }
  if (letters < 12)
    return fallback;
  const auto detected = detector_->detect(text);
  if (!detected || detected->second < 0.65F || !ConfigStore::isSupportedLanguage(detected->first))
    return fallback;
  return detected->first.toLower();
}

} // namespace voxlocal
