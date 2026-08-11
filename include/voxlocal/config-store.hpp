#pragma once

#include "voxlocal/types.hpp"

#include <QString>

namespace voxlocal {

class ConfigStore
{
public:
  explicit ConfigStore(QString filePath);

  [[nodiscard]] const QString &filePath() const { return filePath_; }
  [[nodiscard]] Settings load(QString *error = nullptr) const;
  bool save(const Settings &settings, QString *error = nullptr) const;

  static Settings defaults();
  static bool isSupportedLanguage(const QString &code);
  static QStringList supportedLanguages();

private:
  QString filePath_;
};

} // namespace voxlocal
