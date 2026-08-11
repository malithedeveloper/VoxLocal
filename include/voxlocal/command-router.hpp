#pragma once

#include "voxlocal/types.hpp"

namespace voxlocal {

class LanguageDetector
{
public:
  virtual ~LanguageDetector() = default;
  virtual std::optional<std::pair<QString, float>> detect(const QString &text) const = 0;
};

class CommandRouter
{
public:
  explicit CommandRouter(const LanguageDetector *detector = nullptr);

  RouteResult route(const ChatMessage &message, const Settings &settings,
                    const QDateTime &now = QDateTime::currentDateTimeUtc());
  static QString normalizeCommand(QString command);
  static QString sanitizeText(QString text, bool readUrls);
  static UserRole rolesFor(const ChatUser &user, const AccessPolicy &policy);

private:
  bool isAllowed(const ChatUser &user, const AccessPolicy &policy) const;
  QString chooseLanguage(const Persona &persona, const QString &text) const;

  const LanguageDetector *detector_ = nullptr;
};

} // namespace voxlocal
