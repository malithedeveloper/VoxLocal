#include "voxlocal/chatterbox-engine.hpp"
#include "voxlocal/chatterbox-tokenizer.hpp"
#include "voxlocal/command-router.hpp"
#include "voxlocal/config-store.hpp"
#include "voxlocal/kick-connector.hpp"
#include "voxlocal/model-manager.hpp"
#include "voxlocal/runtime.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimeZone>
#include <QTimer>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {

void require(bool condition, const char *message)
{
  if (!condition)
    throw std::runtime_error(message);
}

class TurkishDetector final : public voxlocal::LanguageDetector
{
public:
  std::optional<std::pair<QString, float>> detect(const QString &) const override
  {
    return std::pair{QStringLiteral("tr"), 0.97F};
  }
};

void configRoundTrip()
{
  QTemporaryDir directory;
  require(directory.isValid(), "temporary directory unavailable");
  voxlocal::ConfigStore store(directory.filePath(QStringLiteral("settings.json")));
  auto settings = voxlocal::ConfigStore::defaults();
  require(settings.modelStartupBehavior == voxlocal::ModelStartupBehavior::Ask,
          "the default model startup behavior must ask the user");
  settings.interfaceLanguage = voxlocal::InterfaceLanguage::Turkish;
  settings.kick.channelSlug = QStringLiteral("voxlocal");
  settings.overlay.preset = QStringLiteral("subtitle");
  settings.modelStartupBehavior = voxlocal::ModelStartupBehavior::NeverLoad;
  voxlocal::Persona persona;
  persona.name = QStringLiteral("Türkçe Ses");
  persona.command = QStringLiteral("ses");
  persona.language = QStringLiteral("tr");
  persona.access.allowed = voxlocal::UserRole::Moderator | voxlocal::UserRole::Broadcaster;
  settings.ttsEnabled = false;
  settings.globalCooldownSeconds = 17;
  settings.maxTextLength = 180;
  settings.personas.push_back(persona);
  QString error;
  require(store.save(settings, &error), error.toUtf8().constData());
  const auto loaded = store.load(&error);
  require(error.isEmpty(), "settings load reported an error");
  require(loaded.interfaceLanguage == voxlocal::InterfaceLanguage::Turkish, "interface language was not persisted");
  require(loaded.personas.size() == 1 && loaded.personas[0].language == QStringLiteral("tr"),
          "persona was not persisted");
  require(!loaded.ttsEnabled && loaded.globalCooldownSeconds == 17 && loaded.maxTextLength == 180,
          "global TTS controls were not persisted");
  require(loaded.overlay.preset == QStringLiteral("subtitle"), "overlay preset was not persisted");
  require(loaded.modelStartupBehavior == voxlocal::ModelStartupBehavior::NeverLoad,
          "model startup behavior was not persisted");
  require(voxlocal::hasRole(loaded.personas[0].access.allowed, voxlocal::UserRole::Moderator),
          "role was not persisted");
}

void commandRouting()
{
  TurkishDetector detector;
  voxlocal::CommandRouter router(&detector);
  voxlocal::Settings settings;
  voxlocal::Persona persona;
  persona.command = QStringLiteral("SeS");
  persona.name = QStringLiteral("Test");
  persona.languageMode = voxlocal::LanguageMode::Automatic;
  persona.automaticFallbackLanguage = QStringLiteral("en");
  settings.maxTextLength = 40;
  settings.personas.push_back(persona);
  voxlocal::ChatMessage message;
  message.sender.id = QStringLiteral("42");
  message.sender.username = QStringLiteral("Viewer");
  message.text =
      QStringLiteral("!ses Bu gerçekten yeterince uzun bir Türkçe cümledir https://example.com [emote:1:test]");
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
  const auto now = QDateTime::fromSecsSinceEpoch(1700000000, QTimeZone::UTC);
#else
  const auto now = QDateTime::fromSecsSinceEpoch(1700000000, Qt::UTC);
#endif
  const auto accepted = router.route(message, settings, now);
  require(accepted.request.has_value(), "valid command was rejected");
  require(accepted.request->language == QStringLiteral("tr"), "automatic language was not selected");
  require(!accepted.request->text.contains(QStringLiteral("http")), "URL was not removed");
  require(!accepted.request->text.contains(QStringLiteral("emote")), "emote token was not removed");
  require(accepted.request->text.size() <= settings.maxTextLength, "global character limit was not enforced");
  settings.personas[0].access.allowed = voxlocal::UserRole::Moderator;
  require(router.route(message, settings, now).rejectionReason == QStringLiteral("access-denied"),
          "role policy allowed a viewer");
  message.sender.badges.push_back({QStringLiteral("moderator"), {}, 0});
  require(router.route(message, settings, now).request.has_value(), "moderator badge was not recognized");
}

void kickParsing()
{
  const QJsonObject payload{
      {QStringLiteral("id"), QStringLiteral("message-1")},
      {QStringLiteral("chatroom_id"), 456},
      {QStringLiteral("content"), QStringLiteral("!voice hello")},
      {QStringLiteral("sender"),
       QJsonObject{{QStringLiteral("id"), 123},
                   {QStringLiteral("username"), QStringLiteral("mali")},
                   {QStringLiteral("identity"),
                    QJsonObject{{QStringLiteral("color"), QStringLiteral("#53FC18")},
                                {QStringLiteral("badges"),
                                 QJsonArray{QJsonObject{{QStringLiteral("type"), QStringLiteral("moderator")}}}}}}}}};
  const QJsonObject frame{
      {QStringLiteral("event"), QStringLiteral("App\\Events\\ChatMessageEvent")},
      {QStringLiteral("data"), QString::fromUtf8(QJsonDocument(payload).toJson(QJsonDocument::Compact))}};
  const auto parsed = voxlocal::KickConnector::parseChatEvent(QJsonDocument(frame).toJson(QJsonDocument::Compact));
  require(parsed.has_value(), "Kick frame was not parsed");
  require(parsed->sender.username == QStringLiteral("mali"), "Kick sender was lost");
  require(parsed->sender.badges.size() == 1, "Kick badges were lost");
  require(parsed->sender.color == QStringLiteral("#53FC18"), "Kick chat color was lost");
}

void moderatorTtsControls()
{
  QTemporaryDir directory;
  require(directory.isValid(), "temporary runtime directory unavailable");
  voxlocal::VoxLocalRuntime runtime(directory.filePath(QStringLiteral("settings.json")),
                                    directory.filePath(QStringLiteral("models")));
  auto settings = runtime.settings();
  settings.ttsEnabled = true;
  QString error;
  require(runtime.applySettings(settings, &error), error.toUtf8().constData());
  voxlocal::ChatMessage message;
  message.sender.username = QStringLiteral("mod");
  message.sender.badges.push_back({QStringLiteral("moderator"), {}, 0});
  message.text = QStringLiteral("!ttsoff");
  runtime.kickConnector()->messageReceived(message);
  require(!runtime.settings().ttsEnabled, "moderator could not disable TTS");
  message.text = QStringLiteral("!ttson");
  runtime.kickConnector()->messageReceived(message);
  require(runtime.settings().ttsEnabled, "moderator could not enable TTS");
}

void modelManifest()
{
  qint64 total = 0;
  for (const auto &file : voxlocal::ModelManager::manifest()) {
    total += file.size;
    require(file.sha256.size() == 64, "model manifest hash is not SHA-256");
  }
  require(total == 1557740634, "model manifest size changed unexpectedly");
  require(voxlocal::ConfigStore::isSupportedLanguage(QStringLiteral("tr")),
          "Turkish is missing from supported languages");
  require(voxlocal::ConfigStore::supportedLanguages().size() == 23, "supported language count is wrong");
}

void tokenizerEmbeddingBounds()
{
  QTemporaryDir directory;
  require(directory.isValid(), "temporary tokenizer directory unavailable");
  const auto path = directory.filePath(QStringLiteral("tokenizer.json"));
  QFile file(path);
  require(file.open(QIODevice::WriteOnly), "tokenizer fixture could not be created");
  const QJsonObject vocabulary{{QStringLiteral("[UNK]"), 1},  {QStringLiteral("[SPACE]"), 2},
                               {QStringLiteral("[tr]"), 712}, {QStringLiteral("s"), 10},
                               {QStringLiteral("g"), 11},     {QStringLiteral("I"), 12},
                               {QStringLiteral("ş"), 2408},   {QStringLiteral("ğ"), 2433},
                               {QStringLiteral("İ"), 2434},   {QStringLiteral("€"), 2352}};
  const QJsonArray addedTokens{
      QJsonObject{{QStringLiteral("content"), QStringLiteral("[SPACE]")}, {QStringLiteral("id"), 2}},
      QJsonObject{{QStringLiteral("content"), QStringLiteral("[tr]")}, {QStringLiteral("id"), 712}}};
  file.write(QJsonDocument(QJsonObject{{QStringLiteral("model"), QJsonObject{{QStringLiteral("vocab"), vocabulary},
                                                                             {QStringLiteral("merges"), QJsonArray{}}}},
                                       {QStringLiteral("added_tokens"), addedTokens}})
                 .toJson(QJsonDocument::Compact));
  file.close();

  voxlocal::ChatterboxTokenizer tokenizer;
  QString error;
  require(tokenizer.load(path, &error), error.toUtf8().constData());
  const auto ids = tokenizer.encode(QStringLiteral("ş ğ İ €"), QStringLiteral("tr"));
  require(std::ranges::find(ids, 2408) == ids.end() && std::ranges::find(ids, 2433) == ids.end() &&
              std::ranges::find(ids, 2434) == ids.end() && std::ranges::find(ids, 2352) == ids.end(),
          "tokenizer emitted an index outside the text embedding table");
  require(std::ranges::find(ids, 10) != ids.end() && std::ranges::find(ids, 11) != ids.end() &&
              std::ranges::find(ids, 12) != ids.end(),
          "accented Turkish letters were not decomposed to embeddable tokens");
  require(std::ranges::find(ids, 1) != ids.end(), "unrepresentable token did not fall back to [UNK]");

  const QString modelPath = qEnvironmentVariable("VOXLOCAL_TEST_MODEL");
  if (!modelPath.isEmpty()) {
    voxlocal::ChatterboxTokenizer installedTokenizer;
    const auto tokenizerPath = QDir(modelPath).filePath(QStringLiteral("tokenizer.json"));
    require(installedTokenizer.load(tokenizerPath, &error), error.toUtf8().constData());
    const auto installedIds = installedTokenizer.encode(QStringLiteral("Şu ağacın ışığı güzel."), QStringLiteral("tr"));
    require(std::ranges::none_of(
                installedIds,
                [](std::int64_t id) { return id >= voxlocal::ChatterboxTokenizer::textVocabularySize && id < 6561; }),
            "installed tokenizer emitted an index outside the text embedding table");
  }
}

void partialModelAccounting()
{
  QTemporaryDir directory;
  require(directory.isValid(), "temporary model directory unavailable");
  voxlocal::ModelManager manager(directory.path());
  const auto &entry = voxlocal::ModelManager::manifest().front();
  const QString partPath = QDir(manager.revisionRoot()).filePath(entry.path + QStringLiteral(".part"));
  require(QDir().mkpath(QFileInfo(partPath).absolutePath()), "partial model directory could not be created");
  QFile partial(partPath);
  require(partial.open(QIODevice::WriteOnly), "partial model file could not be created");
  require(partial.resize(123456), "partial model file could not be resized");
  partial.close();
  require(manager.downloadedBytes() == 123456, "partial model bytes were not counted for resume");
  require(voxlocal::ModelManager::formatBytes(1500000) == QStringLiteral("1.50 MB"),
          "human-readable model size formatting changed");
}

void mediaVoiceImport()
{
  const QString ffmpeg = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
  if (ffmpeg.isEmpty()) {
    std::cout << "Skipping media import test because FFmpeg is unavailable.\n";
    return;
  }

  QTemporaryDir directory;
  require(directory.isValid(), "temporary media directory unavailable");
  const QString source = directory.filePath(QStringLiteral("voice sample.mp4"));
  QProcess generator;
  generator.setProgram(ffmpeg);
  generator.setArguments({QStringLiteral("-nostdin"), QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"),
                          QStringLiteral("error"), QStringLiteral("-y"), QStringLiteral("-f"), QStringLiteral("lavfi"),
                          QStringLiteral("-i"), QStringLiteral("sine=frequency=440:duration=1.25"),
                          QStringLiteral("-c:a"), QStringLiteral("aac"), source});
  generator.start(QIODevice::ReadOnly);
  require(generator.waitForStarted(5000), "FFmpeg test generator did not start");
  require(generator.waitForFinished(30000), "FFmpeg test generator timed out");
  require(generator.exitStatus() == QProcess::NormalExit && generator.exitCode() == 0,
          "FFmpeg could not create the MP4 import fixture");

  voxlocal::VoxLocalRuntime runtime(directory.filePath(QStringLiteral("state/settings.json")),
                                    directory.filePath(QStringLiteral("models")));
  QString error;
  const QString imported = runtime.importVoice(source, QStringLiteral("persona"), &error);
  require(!imported.isEmpty(), error.toUtf8().constData());
  require(imported.endsWith(QStringLiteral("persona.wav")), "imported media did not become a persona WAV");
  QFile wave(imported);
  require(wave.open(QIODevice::ReadOnly), "converted persona WAV could not be opened");
  const QByteArray header = wave.read(12);
  require(header.first(4) == QByteArrayLiteral("RIFF") && header.last(4) == QByteArrayLiteral("WAVE"),
          "converted persona audio is not a WAV file");

  QString asyncPath;
  QString asyncError;
  QStringList operations;
  QEventLoop loop;
  QTimer timeout;
  timeout.setSingleShot(true);
  QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
  QObject::connect(&runtime, &voxlocal::VoxLocalRuntime::voiceImportProgress, &loop,
                   [&](const QString &personaId, int, const QString &operation) {
                     if (personaId == QStringLiteral("async-persona"))
                       operations.push_back(operation);
                   });
  QObject::connect(&runtime, &voxlocal::VoxLocalRuntime::voiceImportFinished, &loop,
                   [&](const QString &personaId, const QString &path) {
                     if (personaId == QStringLiteral("async-persona")) {
                       asyncPath = path;
                       loop.quit();
                     }
                   });
  QObject::connect(&runtime, &voxlocal::VoxLocalRuntime::voiceImportFailed, &loop,
                   [&](const QString &personaId, const QString &failure) {
                     if (personaId == QStringLiteral("async-persona")) {
                       asyncError = failure;
                       loop.quit();
                     }
                   });
  runtime.importVoiceAsync(source, QStringLiteral("async-persona"));
  timeout.start(30000);
  loop.exec();
  require(asyncError.isEmpty(), asyncError.toUtf8().constData());
  require(!asyncPath.isEmpty() && QFileInfo::exists(asyncPath), "asynchronous voice import did not finish");
  require(operations.size() >= 5 && operations.last() == QStringLiteral("Voice preparation complete"),
          "asynchronous voice operations were not reported in order");
}

void optionalTtsSmokeTest()
{
  const QString modelPath = qEnvironmentVariable("VOXLOCAL_TEST_MODEL");
  const QString voicePath = qEnvironmentVariable("VOXLOCAL_TEST_VOICE");
  if (modelPath.isEmpty() || voicePath.isEmpty())
    return;
  voxlocal::ChatterboxEngine engine;
  QString error;
  require(engine.initialize(modelPath, &error), error.toUtf8().constData());
  voxlocal::TtsRequest request;
  request.text = QStringLiteral("Şu ağacın ışığı güzel.");
  request.language = QStringLiteral("tr");
  request.referenceAudioPath = voicePath;
  std::atomic_bool cancelled{false};
  const auto audio = engine.synthesize(request, cancelled, &error);
  require(error.isEmpty(), error.toUtf8().constData());
  require(audio.sampleRate == 24000 && !audio.samples.empty(), "TTS smoke test returned no audio");
}

void optionalAsyncModelLoadTest()
{
  const QString modelPath = qEnvironmentVariable("VOXLOCAL_TEST_MODEL");
  if (modelPath.isEmpty())
    return;

  QTemporaryDir directory;
  require(directory.isValid(), "temporary async model-load directory unavailable");
  const QString modelRoot = QDir(modelPath).absoluteFilePath(QStringLiteral("../.."));
  voxlocal::VoxLocalRuntime runtime(directory.filePath(QStringLiteral("settings.json")), modelRoot);
  QEventLoop loop;
  QTimer timeout;
  timeout.setSingleShot(true);
  QString failure;
  QObject::connect(&runtime, &voxlocal::VoxLocalRuntime::engineLoadingChanged, &loop, [&](bool loading) {
    if (!loading)
      loop.quit();
  });
  QObject::connect(&runtime, &voxlocal::VoxLocalRuntime::errorOccurred, &loop, [&](const QString &error) {
    failure = error;
    loop.quit();
  });
  QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
  QElapsedTimer returnTimer;
  returnTimer.start();
  runtime.loadModel();
  require(returnTimer.elapsed() < 250, "model loading blocked the UI thread");
  timeout.start(60000);
  loop.exec();
  require(failure.isEmpty(), failure.toUtf8().constData());
  require(runtime.engineReady() && !runtime.engineLoading(), "background model loading did not finish");
}

} // namespace

int main(int argc, char **argv)
{
  QCoreApplication application(argc, argv);
  try {
    configRoundTrip();
    commandRouting();
    kickParsing();
    moderatorTtsControls();
    modelManifest();
    tokenizerEmbeddingBounds();
    partialModelAccounting();
    mediaVoiceImport();
    optionalAsyncModelLoadTest();
    optionalTtsSmokeTest();
    std::cout << "All VoxLocal core tests passed.\n";
    return 0;
  } catch (const std::exception &exception) {
    std::cerr << "Test failure: " << exception.what() << '\n';
    return 1;
  }
}
