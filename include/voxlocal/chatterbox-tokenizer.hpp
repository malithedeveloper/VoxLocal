#pragma once

#include <QHash>
#include <QString>
#include <QStringList>

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace voxlocal {

class ChatterboxTokenizer
{
public:
  static constexpr std::int64_t textVocabularySize = 2352;

  bool load(const QString &path, QString *error = nullptr);
  [[nodiscard]] std::vector<std::int64_t> encode(QString text, const QString &language) const;

private:
  void appendBpe(const QString &piece, std::vector<std::int64_t> &ids) const;
  void appendSymbol(const std::string &symbol, std::vector<std::int64_t> &ids) const;

  std::unordered_map<std::string, std::int64_t> vocab_;
  std::unordered_map<std::string, int> merges_;
  QHash<QString, std::int64_t> added_;
  QHash<QString, QString> cangjie_;
  QStringList addedTokens_;
};

} // namespace voxlocal
