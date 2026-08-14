#include "voxlocal/runtime.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QPointer>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QTemporaryFile>
#include <QtConcurrentRun>
#include <QtEndian>

#include <algorithm>
#include <functional>

namespace voxlocal {
namespace {

QString ffmpegExecutable()
{
  const QString configured = qEnvironmentVariable("VOXLOCAL_FFMPEG").trimmed();
  if (!configured.isEmpty() && QFileInfo(configured).isExecutable())
    return QFileInfo(configured).absoluteFilePath();

#ifdef Q_OS_WIN
  const QString executableName = QStringLiteral("ffmpeg.exe");
#else
  const QString executableName = QStringLiteral("ffmpeg");
#endif
  const QString besideObs = QDir(QCoreApplication::applicationDirPath()).filePath(executableName);
  if (QFileInfo(besideObs).isExecutable())
    return besideObs;
  return QStandardPaths::findExecutable(executableName);
}

bool validateConvertedVoice(const QString &path, QString *error)
{
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    if (error)
      *error = QStringLiteral("Could not read converted voice sample: %1").arg(file.errorString());
    return false;
  }
  const QByteArray header = file.read(12);
  if (header.size() != 12 || header.first(4) != QByteArrayLiteral("RIFF") ||
      header.last(4) != QByteArrayLiteral("WAVE")) {
    if (error)
      *error = QStringLiteral("The converted voice sample is not a valid WAV file.");
    return false;
  }

  quint16 format = 0;
  quint16 channels = 0;
  quint16 bits = 0;
  quint32 sampleRate = 0;
  qint64 dataBytes = 0;
  while (!file.atEnd()) {
    const QByteArray chunkHeader = file.read(8);
    if (chunkHeader.size() != 8)
      break;
    const quint32 chunkSize = qFromLittleEndian<quint32>(reinterpret_cast<const uchar *>(chunkHeader.constData() + 4));
    if (chunkHeader.first(4) == QByteArrayLiteral("fmt ")) {
      const QByteArray chunk = file.read(chunkSize);
      if (chunk.size() >= 16) {
        format = qFromLittleEndian<quint16>(reinterpret_cast<const uchar *>(chunk.constData()));
        channels = qFromLittleEndian<quint16>(reinterpret_cast<const uchar *>(chunk.constData() + 2));
        sampleRate = qFromLittleEndian<quint32>(reinterpret_cast<const uchar *>(chunk.constData() + 4));
        bits = qFromLittleEndian<quint16>(reinterpret_cast<const uchar *>(chunk.constData() + 14));
      }
    } else if (chunkHeader.first(4) == QByteArrayLiteral("data")) {
      dataBytes = chunkSize;
      if (!file.seek(file.pos() + chunkSize))
        break;
    } else if (!file.seek(file.pos() + chunkSize)) {
      break;
    }
    if (chunkSize % 2)
      file.seek(file.pos() + 1);
  }

  if (format != 1 || channels != 1 || sampleRate != 24000 || bits != 16 || dataBytes <= 0) {
    if (error)
      *error = QStringLiteral("Voice conversion did not produce 24 kHz mono PCM audio.");
    return false;
  }
  const qint64 frames = dataBytes / 2;
  if (frames < 24000) {
    if (error)
      *error = QStringLiteral("The media file must contain at least one second of speech.");
    return false;
  }
  if (frames > 24000 * 60) {
    if (error)
      *error = QStringLiteral("The converted voice sample must not exceed 60 seconds.");
    return false;
  }
  return true;
}

bool atomicallyCopy(const QString &source, const QString &destination, QString *error)
{
  QFile input(source);
  QSaveFile output(destination);
  if (!input.open(QIODevice::ReadOnly) || !output.open(QIODevice::WriteOnly)) {
    if (error)
      *error = QStringLiteral("Could not store the converted voice sample.");
    return false;
  }
  while (!input.atEnd()) {
    const QByteArray chunk = input.read(1024 * 1024);
    if (chunk.isEmpty() && input.error() != QFile::NoError) {
      if (error)
        *error = QStringLiteral("Could not read the converted voice sample: %1").arg(input.errorString());
      return false;
    }
    if (output.write(chunk) != chunk.size()) {
      if (error)
        *error = QStringLiteral("Could not store the converted voice sample: %1").arg(output.errorString());
      return false;
    }
  }
  if (!output.commit()) {
    if (error)
      *error = QStringLiteral("Could not finalize the converted voice sample: %1").arg(output.errorString());
    return false;
  }
  return true;
}

} // namespace

class VoxLocalRuntime::Detector final : public LanguageDetector
{
public:
  std::optional<std::pair<QString, float>> detect(const QString &text) const override
  {
    if (text.contains(QRegularExpression(QStringLiteral("[\\x{0600}-\\x{06ff}]"))))
      return std::pair{QStringLiteral("ar"), 0.99F};
    if (text.contains(QRegularExpression(QStringLiteral("[\\x{0590}-\\x{05ff}]"))))
      return std::pair{QStringLiteral("he"), 0.99F};
    if (text.contains(QRegularExpression(QStringLiteral("[\\x{0400}-\\x{04ff}]"))))
      return std::pair{QStringLiteral("ru"), 0.96F};
    if (text.contains(QRegularExpression(QStringLiteral("[\\x{0370}-\\x{03ff}]"))))
      return std::pair{QStringLiteral("el"), 0.99F};
    if (text.contains(QRegularExpression(QStringLiteral("[\\x{0900}-\\x{097f}]"))))
      return std::pair{QStringLiteral("hi"), 0.99F};
    if (text.contains(QRegularExpression(QStringLiteral("[\\x{ac00}-\\x{d7af}]"))))
      return std::pair{QStringLiteral("ko"), 0.99F};
    if (text.contains(QRegularExpression(QStringLiteral("[\\x{3040}-\\x{30ff}]"))))
      return std::pair{QStringLiteral("ja"), 0.99F};
    if (text.contains(QRegularExpression(QStringLiteral("[\\x{4e00}-\\x{9fff}]"))))
      return std::pair{QStringLiteral("zh"), 0.90F};
    const auto lowered = text.toCaseFolded();
    int turkish = 0;
    for (const auto &word : {QStringLiteral(" bir "), QStringLiteral(" ve "), QStringLiteral(" için "),
                             QStringLiteral(" bu "), QStringLiteral(" çok "), QStringLiteral(" değil ")})
      turkish += lowered.contains(word) ? 1 : 0;
    turkish += lowered.contains(QRegularExpression(QStringLiteral("[çğıöşü]"))) ? 2 : 0;
    if (turkish >= 2)
      return std::pair{QStringLiteral("tr"), std::min(0.98F, 0.68F + turkish * 0.06F)};
    const std::vector<std::pair<QString, QStringList>> profiles = {
        {QStringLiteral("da"),
         {QStringLiteral(" og "), QStringLiteral(" ikke "), QStringLiteral(" jeg "), QStringLiteral(" det ")}},
        {QStringLiteral("de"),
         {QStringLiteral(" und "), QStringLiteral(" nicht "), QStringLiteral(" ist "), QStringLiteral(" die ")}},
        {QStringLiteral("es"),
         {QStringLiteral(" que "), QStringLiteral(" para "), QStringLiteral(" los "), QStringLiteral(" una ")}},
        {QStringLiteral("fi"),
         {QStringLiteral(" että "), QStringLiteral(" ja "), QStringLiteral(" ei "), QStringLiteral(" tämä ")}},
        {QStringLiteral("fr"),
         {QStringLiteral(" les "), QStringLiteral(" est "), QStringLiteral(" une "), QStringLiteral(" avec ")}},
        {QStringLiteral("it"),
         {QStringLiteral(" che "), QStringLiteral(" non "), QStringLiteral(" una "), QStringLiteral(" per ")}},
        {QStringLiteral("ms"),
         {QStringLiteral(" yang "), QStringLiteral(" dan "), QStringLiteral(" tidak "), QStringLiteral(" untuk ")}},
        {QStringLiteral("nl"),
         {QStringLiteral(" het "), QStringLiteral(" een "), QStringLiteral(" niet "), QStringLiteral(" van ")}},
        {QStringLiteral("no"),
         {QStringLiteral(" og "), QStringLiteral(" ikke "), QStringLiteral(" som "), QStringLiteral(" jeg ")}},
        {QStringLiteral("pl"),
         {QStringLiteral(" nie "), QStringLiteral(" jest "), QStringLiteral(" oraz "), QStringLiteral(" że ")}},
        {QStringLiteral("pt"),
         {QStringLiteral(" não "), QStringLiteral(" uma "), QStringLiteral(" para "), QStringLiteral(" com ")}},
        {QStringLiteral("sv"),
         {QStringLiteral(" och "), QStringLiteral(" inte "), QStringLiteral(" är "), QStringLiteral(" som ")}},
        {QStringLiteral("sw"),
         {QStringLiteral(" kwa "), QStringLiteral(" hii "), QStringLiteral(" katika "), QStringLiteral(" ni ")}},
    };
    QString padded = QLatin1Char(' ') + lowered + QLatin1Char(' ');
    padded.replace(QRegularExpression(QStringLiteral("[^\\p{L}]+")), QStringLiteral(" "));
    QString bestLanguage = QStringLiteral("en");
    int bestScore = 0;
    for (const auto &[language, words] : profiles) {
      int score = 0;
      for (const auto &word : words)
        score += padded.count(word);
      if (score > bestScore) {
        bestScore = score;
        bestLanguage = language;
      }
    }
    return std::pair{bestLanguage, bestScore >= 2 ? 0.82F : 0.68F};
  }
};

namespace {

struct EngineLoadResult {
  std::shared_ptr<ChatterboxEngine> engine;
  QString error;
};

} // namespace

VoxLocalRuntime::VoxLocalRuntime(QString settingsPath, QString modelRoot, QObject *parent)
    : QObject(parent), store_(std::move(settingsPath)), settings_(store_.load()), models_(std::move(modelRoot)),
      detector_(std::make_unique<Detector>()), router_(detector_.get()), engine_(std::make_shared<ChatterboxEngine>()),
      queue_(engine_)
{
  voiceImportPool_.setMaxThreadCount(1);
  modelLoadPool_.setMaxThreadCount(1);
  queue_.setCapacity(settings_.queueCapacity);
  store_.save(settings_);
  connect(&kick_, &KickConnector::messageReceived, this, &VoxLocalRuntime::handleMessage);
  connect(&kick_, &KickConnector::statusChanged, this, &VoxLocalRuntime::setStatus);
  connect(&kick_, &KickConnector::errorOccurred, this, &VoxLocalRuntime::errorOccurred);
  connect(&kick_, &KickConnector::configurationResolved, this, [this](const KickSettings &kick) {
    settings_.kick = kick;
    store_.save(settings_);
    emit settingsChanged();
  });
  connect(&models_, &ModelManager::progress, this, &VoxLocalRuntime::modelProgress);
  connect(&models_, &ModelManager::failed, this, &VoxLocalRuntime::errorOccurred);
  connect(&queue_, &SpeechQueue::queueChanged, this, &VoxLocalRuntime::queueChanged);
  connect(&queue_, &SpeechQueue::itemStarted, this, [this](const QueueItem &item) {
    setStatus(QStringLiteral("Generating speech locally…"));
    emit speechStarted(item.request.personaId);
  });
  connect(&queue_, &SpeechQueue::audioReady, this, [this](const QueueItem &item, const TtsAudio &audio) {
    setStatus(QStringLiteral("Speech ready — playing in OBS"));
    if (item.request.requestId == activeChatRequestId_) {
      chatGenerationActive_ = false;
      activeChatRequestId_.clear();
      globalCooldownUntil_ = QDateTime::currentDateTimeUtc().addSecs(settings_.globalCooldownSeconds);
    }
    const auto durationMilliseconds =
        audio.sampleRate > 0
            ? static_cast<int>(audio.samples.size() * 1000 / static_cast<std::size_t>(audio.sampleRate))
            : settings_.overlay.visibleMilliseconds;
    emit overlayEvent({{QStringLiteral("type"), QStringLiteral("speech-start")},
                       {QStringLiteral("persona"), item.request.personaName},
                       {QStringLiteral("requester"), item.request.requesterName},
                       {QStringLiteral("requesterColor"), item.request.requesterColor},
                       {QStringLiteral("text"), item.request.text},
                       {QStringLiteral("language"), item.request.language},
                       {QStringLiteral("durationMilliseconds"), durationMilliseconds}});
    emit audioProduced(audio);
    emit speechFinished(item.request.personaId);
    emit overlayEvent({{QStringLiteral("type"), QStringLiteral("speech-audio")},
                       {QStringLiteral("requestId"), item.request.requestId}});
  });
  connect(&queue_, &SpeechQueue::itemFailed, this, [this](const QueueItem &item, const QString &error) {
    if (item.request.requestId == activeChatRequestId_) {
      chatGenerationActive_ = false;
      activeChatRequestId_.clear();
    }
    setStatus(QStringLiteral("Speech generation failed"));
    emit speechFailed(item.request.personaId, error);
    emit errorOccurred(error);
    emit overlayEvent({{QStringLiteral("type"), QStringLiteral("speech-stop")}});
  });
}

VoxLocalRuntime::~VoxLocalRuntime()
{
  modelLoadPool_.waitForDone();
  voiceImportPool_.waitForDone();
  stop();
}

bool VoxLocalRuntime::applySettings(Settings settings, QString *error)
{
  const bool ttsWasEnabled = settings_.ttsEnabled;
  settings.schemaVersion = kConfigSchemaVersion;
  settings.queueCapacity = std::clamp(settings.queueCapacity, 1, 100);
  settings.globalCooldownSeconds = std::clamp(settings.globalCooldownSeconds, 0, 3600);
  settings.maxTextLength = std::clamp(settings.maxTextLength, 1, 1000);
  if (settings.overlay.preset != QStringLiteral("minimal") && settings.overlay.preset != QStringLiteral("subtitle"))
    settings.overlay.preset = QStringLiteral("minimal");
  static const QStringList animations{QStringLiteral("fade"),        QStringLiteral("slide-left"),
                                      QStringLiteral("slide-right"), QStringLiteral("slide-down"),
                                      QStringLiteral("slide-up"),    QStringLiteral("none")};
  if (!animations.contains(settings.overlay.entranceAnimation))
    settings.overlay.entranceAnimation = QStringLiteral("slide-up");
  QSet<QString> commands;
  for (auto &persona : settings.personas) {
    persona.command = CommandRouter::normalizeCommand(persona.command);
    if (persona.command.isEmpty() || commands.contains(persona.command)) {
      if (error)
        *error = QStringLiteral("Each enabled persona needs a unique command.");
      return false;
    }
    commands.insert(persona.command);
  }
  settings_ = std::move(settings);
  queue_.setCapacity(settings_.queueCapacity);
  if (!store_.save(settings_, error))
    return false;
  emit settingsChanged();
  emit overlayEvent({{QStringLiteral("type"), QStringLiteral("configure")},
                     {QStringLiteral("preset"), settings_.overlay.preset},
                     {QStringLiteral("fontFamily"), settings_.overlay.fontFamily},
                     {QStringLiteral("background"), settings_.overlay.background},
                     {QStringLiteral("foreground"), settings_.overlay.foreground},
                     {QStringLiteral("fallbackNameColor"), settings_.overlay.fallbackNameColor},
                     {QStringLiteral("entranceAnimation"), settings_.overlay.entranceAnimation},
                     {QStringLiteral("borderRadius"), settings_.overlay.borderRadius},
                     {QStringLiteral("visibleMilliseconds"), settings_.overlay.visibleMilliseconds},
                     {QStringLiteral("showName"), settings_.overlay.showName}});
  if (ttsWasEnabled != settings_.ttsEnabled) {
    const QString notice =
        settings_.interfaceLanguage == InterfaceLanguage::Turkish
            ? (settings_.ttsEnabled ? QStringLiteral("TTS açıldı") : QStringLiteral("TTS kapatıldı"))
            : (settings_.ttsEnabled ? QStringLiteral("TTS enabled") : QStringLiteral("TTS disabled"));
    emit overlayEvent({{QStringLiteral("type"), QStringLiteral("status")},
                       {QStringLiteral("text"), notice},
                       {QStringLiteral("enabled"), settings_.ttsEnabled}});
  }
  if (settings_.kick.enabled && !settings_.kick.channelSlug.isEmpty())
    kick_.start(settings_.kick);
  else
    kick_.stop();
  return true;
}

namespace {

using VoiceProgress = std::function<void(int, const QString &)>;

struct VoiceImportResult {
  QString path;
  QString error;
};

void reportVoiceProgress(const VoiceProgress &progress, int percent, const QString &operation)
{
  if (progress)
    progress(percent, operation);
}

QString importVoiceFile(const QString &sourcePath, const QString &personaId, const QString &settingsPath,
                        QString *error, const VoiceProgress &progress)
{
  reportVoiceProgress(progress, 5, QStringLiteral("Checking the selected media file"));
  if (sourcePath.isEmpty())
    return {};
  const QFileInfo source(sourcePath);
  if (!source.isFile()) {
    if (error)
      *error = QStringLiteral("The selected audio or video file does not exist.");
    return {};
  }
  reportVoiceProgress(progress, 12, QStringLiteral("Locating the local FFmpeg converter"));
  const QString ffmpeg = ffmpegExecutable();
  if (ffmpeg.isEmpty()) {
    if (error)
      *error = QStringLiteral("FFmpeg is required to import audio and video. Install FFmpeg or set VOXLOCAL_FFMPEG.");
    return {};
  }
  const auto directory = QFileInfo(settingsPath).absoluteDir().filePath(QStringLiteral("voices"));
  if (!QDir().mkpath(directory)) {
    if (error)
      *error = QStringLiteral("Could not create the voice directory.");
    return {};
  }
  const auto destination = QDir(directory).filePath(personaId + QStringLiteral(".wav"));

  QTemporaryFile converted(QDir(directory).filePath(QStringLiteral(".voxlocal-convert-XXXXXX.wav")));
  if (!converted.open()) {
    if (error)
      *error = QStringLiteral("Could not create a temporary voice conversion file: %1").arg(converted.errorString());
    return {};
  }
  const QString convertedPath = converted.fileName();
  converted.close();

  reportVoiceProgress(progress, 25, QStringLiteral("Extracting audio and converting it to 24 kHz mono WAV"));
  QProcess process;
  process.setProgram(ffmpeg);
  process.setArguments({QStringLiteral("-nostdin"),
                        QStringLiteral("-hide_banner"),
                        QStringLiteral("-loglevel"),
                        QStringLiteral("error"),
                        QStringLiteral("-y"),
                        QStringLiteral("-i"),
                        source.absoluteFilePath(),
                        QStringLiteral("-map"),
                        QStringLiteral("0:a:0"),
                        QStringLiteral("-map_metadata"),
                        QStringLiteral("-1"),
                        QStringLiteral("-vn"),
                        QStringLiteral("-t"),
                        QStringLiteral("60"),
                        QStringLiteral("-ac"),
                        QStringLiteral("1"),
                        QStringLiteral("-ar"),
                        QStringLiteral("24000"),
                        QStringLiteral("-c:a"),
                        QStringLiteral("pcm_s16le"),
                        QStringLiteral("-f"),
                        QStringLiteral("wav"),
                        convertedPath});
  process.start(QIODevice::ReadOnly);
  if (!process.waitForStarted(5000)) {
    if (error)
      *error = QStringLiteral("Could not start FFmpeg: %1").arg(process.errorString());
    return {};
  }
  if (!process.waitForFinished(120000)) {
    process.kill();
    process.waitForFinished(5000);
    if (error)
      *error = QStringLiteral("Voice conversion timed out after two minutes.");
    return {};
  }
  if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
    QString details = QString::fromLocal8Bit(process.readAllStandardError()).trimmed();
    if (details.size() > 1000)
      details = details.right(1000);
    if (error)
      *error = details.isEmpty() ? QStringLiteral("The selected file has no supported audio track.")
                                 : QStringLiteral("Could not extract audio from the selected media: %1").arg(details);
    return {};
  }
  reportVoiceProgress(progress, 70, QStringLiteral("Validating the converted voice duration and format"));
  if (!validateConvertedVoice(convertedPath, error))
    return {};
  reportVoiceProgress(progress, 82,
                      QStringLiteral("Preparing the zero-shot voice profile — no model training is required"));
  reportVoiceProgress(progress, 92, QStringLiteral("Saving the prepared persona voice atomically"));
  if (!atomicallyCopy(convertedPath, destination, error))
    return {};
  reportVoiceProgress(progress, 100, QStringLiteral("Voice preparation complete"));
  return destination;
}

} // namespace

QString VoxLocalRuntime::importVoice(const QString &sourcePath, const QString &personaId, QString *error) const
{
  return importVoiceFile(sourcePath, personaId, store_.filePath(), error, {});
}

bool VoxLocalRuntime::isVoiceImporting(const QString &personaId) const
{
  return personaId.isEmpty() ? !voiceImports_.isEmpty() : voiceImports_.contains(personaId);
}

void VoxLocalRuntime::importVoiceAsync(const QString &sourcePath, const QString &personaId)
{
  if (personaId.isEmpty() || voiceImports_.contains(personaId))
    return;
  voiceImports_.insert(personaId);
  emit voiceImportProgress(personaId, 0, QStringLiteral("Voice preparation queued"));

  const QString settingsPath = store_.filePath();
  QPointer<VoxLocalRuntime> guard(this);
  auto *watcher = new QFutureWatcher<VoiceImportResult>(this);
  connect(watcher, &QFutureWatcher<VoiceImportResult>::finished, this, [this, watcher, personaId] {
    const VoiceImportResult result = watcher->result();
    watcher->deleteLater();
    voiceImports_.remove(personaId);
    if (result.path.isEmpty()) {
      emit voiceImportFailed(personaId,
                             result.error.isEmpty() ? QStringLiteral("Voice preparation failed.") : result.error);
      return;
    }
    emit voiceImportFinished(personaId, result.path);
  });
  watcher->setFuture(QtConcurrent::run(&voiceImportPool_, [guard, sourcePath, personaId, settingsPath] {
    VoiceImportResult result;
    const VoiceProgress progress = [guard, personaId](int percent, const QString &operation) {
      if (!guard)
        return;
      QMetaObject::invokeMethod(
          guard,
          [guard, personaId, percent, operation] {
            if (guard)
              emit guard->voiceImportProgress(personaId, percent, operation);
          },
          Qt::QueuedConnection);
    };
    result.path = importVoiceFile(sourcePath, personaId, settingsPath, &result.error, progress);
    return result;
  }));
}

void VoxLocalRuntime::start()
{
  if (settings_.welcomeCompleted && settings_.kick.enabled && !settings_.kick.channelSlug.isEmpty())
    kick_.start(settings_.kick);
  emit settingsChanged();
}

void VoxLocalRuntime::loadModel()
{
  if (engineLoading_ || engine_->isReady())
    return;
  if (!models_.isInstalled()) {
    emit errorOccurred(QStringLiteral("The TTS model is not installed and verified."));
    return;
  }
  initializeEngine(models_.revisionRoot());
}

void VoxLocalRuntime::stop()
{
  kick_.stop();
  queue_.clear();
  chatGenerationActive_ = false;
  activeChatRequestId_.clear();
}

bool VoxLocalRuntime::preview(const QString &personaId, const QString &text, QString *error)
{
  const auto found =
      std::ranges::find_if(settings_.personas, [&](const Persona &persona) { return persona.id == personaId; });
  if (found == settings_.personas.end()) {
    if (error)
      *error = QStringLiteral("Persona not found.");
    return false;
  }
  QueueItem item;
  item.message.sender.id = QStringLiteral("voxlocal-preview");
  item.message.sender.username = QStringLiteral("Preview");
  item.request.personaId = found->id;
  item.request.personaName = found->name;
  item.request.requesterId = item.message.sender.id;
  item.request.requesterName = item.message.sender.username;
  item.request.text = text.left(settings_.maxTextLength).trimmed();
  item.request.language =
      found->languageMode == LanguageMode::Fixed ? found->language : found->automaticFallbackLanguage;
  item.request.referenceAudioPath = found->referenceAudioPath;
  item.request.exaggeration = found->exaggeration;
  if (item.request.text.isEmpty()) {
    if (error)
      *error = QStringLiteral("Preview text is empty.");
    return false;
  }
  if (!engine_->isReady()) {
    if (error)
      *error = QStringLiteral("The TTS model is not ready.");
    return false;
  }
  if (!queue_.enqueue(item)) {
    if (error)
      *error = QStringLiteral("The speech queue is full.");
    return false;
  }
  return true;
}

void VoxLocalRuntime::setStatus(const QString &status)
{
  status_ = status;
  emit statusChanged(status);
}

void VoxLocalRuntime::initializeEngine(const QString &modelPath)
{
  if (engineLoading_)
    return;
  engineLoading_ = true;
  emit engineLoadingChanged(true);
  setStatus(QStringLiteral("loading-model"));
  auto candidate = std::make_shared<ChatterboxEngine>();
  auto *watcher = new QFutureWatcher<EngineLoadResult>(this);
  connect(watcher, &QFutureWatcher<EngineLoadResult>::finished, this, [this, watcher] {
    const EngineLoadResult result = watcher->result();
    watcher->deleteLater();
    engineLoading_ = false;
    emit engineLoadingChanged(false);
    if (!result.error.isEmpty()) {
      setStatus(QStringLiteral("model-error"));
      emit errorOccurred(result.error);
      return;
    }
    if (!queue_.setEngine(result.engine)) {
      const QString error = QStringLiteral("The TTS engine could not be activated while speech was queued.");
      setStatus(QStringLiteral("model-error"));
      emit errorOccurred(error);
      return;
    }
    engine_ = result.engine;
    setStatus(QStringLiteral("ready:%1").arg(engine_->backendName()));
  });
  watcher->setFuture(QtConcurrent::run(&modelLoadPool_, [candidate, modelPath] {
    EngineLoadResult result;
    result.engine = candidate;
    if (!candidate->initialize(modelPath, &result.error) && result.error.isEmpty())
      result.error = QStringLiteral("The TTS model could not be initialized.");
    return result;
  }));
}

void VoxLocalRuntime::handleMessage(const ChatMessage &message)
{
  const QString command =
      CommandRouter::normalizeCommand(message.text.section(QRegularExpression(QStringLiteral("\\s")), 0, 0));
  if (command == QStringLiteral("ttson") || command == QStringLiteral("ttsoff")) {
    const auto roles = CommandRouter::rolesFor(message.sender, AccessPolicy{});
    if (!hasRole(roles, UserRole::Moderator) && !hasRole(roles, UserRole::Broadcaster))
      return;
    settings_.ttsEnabled = command == QStringLiteral("ttson");
    store_.save(settings_);
    emit settingsChanged();
    const QString notice =
        settings_.interfaceLanguage == InterfaceLanguage::Turkish
            ? (settings_.ttsEnabled ? QStringLiteral("TTS açıldı") : QStringLiteral("TTS kapatıldı"))
            : (settings_.ttsEnabled ? QStringLiteral("TTS enabled") : QStringLiteral("TTS disabled"));
    emit overlayEvent({{QStringLiteral("type"), QStringLiteral("status")},
                       {QStringLiteral("text"), notice},
                       {QStringLiteral("enabled"), settings_.ttsEnabled}});
    return;
  }
  if (!settings_.ttsEnabled || chatGenerationActive_ || queue_.size() > 0 ||
      (globalCooldownUntil_.isValid() && QDateTime::currentDateTimeUtc() < globalCooldownUntil_))
    return;
  const auto routed = router_.route(message, settings_);
  if (!routed.request)
    return;
  QueueItem item{*routed.request, message};
  chatGenerationActive_ = true;
  activeChatRequestId_ = item.request.requestId;
  if (!queue_.enqueue(item)) {
    chatGenerationActive_ = false;
    activeChatRequestId_.clear();
    emit errorOccurred(QStringLiteral("Speech queue full; message from %1 was skipped.").arg(message.sender.username));
  }
}

} // namespace voxlocal
