#include "voxlocal/chatterbox-engine.hpp"
#include "voxlocal/chatterbox-tokenizer.hpp"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QtEndian>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <random>
#include <stdexcept>
#include <thread>
#include <unordered_map>

#ifdef VOXLOCAL_HAVE_ONNXRUNTIME
#include <onnxruntime_cxx_api.h>
#ifdef VOXLOCAL_ORT_DIRECTML
#include <dml_provider_factory.h>
#include <dxgi1_2.h>
#endif
#endif

namespace voxlocal {
namespace {

constexpr int kSampleRate = 24000;
constexpr std::int64_t kStartSpeechToken = 6561;
constexpr std::int64_t kStopSpeechToken = 6562;

#ifdef VOXLOCAL_ORT_DIRECTML
struct DmlAdapter {
  int index = -1;
  quint64 dedicatedVideoMemory = 0;
  QString name;
};

DmlAdapter preferredDmlAdapter()
{
  IDXGIFactory1 *factory = nullptr;
  if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))))
    return {};

  DmlAdapter preferred;
  for (UINT index = 0;; ++index) {
    IDXGIAdapter1 *adapter = nullptr;
    const HRESULT enumeration = factory->EnumAdapters1(index, &adapter);
    if (enumeration == DXGI_ERROR_NOT_FOUND)
      break;
    if (FAILED(enumeration))
      break;
    DXGI_ADAPTER_DESC1 description{};
    if (SUCCEEDED(adapter->GetDesc1(&description)) && !(description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) &&
        (preferred.index < 0 || description.DedicatedVideoMemory > preferred.dedicatedVideoMemory)) {
      preferred.index = static_cast<int>(index);
      preferred.dedicatedVideoMemory = description.DedicatedVideoMemory;
      preferred.name = QString::fromWCharArray(description.Description).trimmed();
    }
    adapter->Release();
  }
  factory->Release();
  return preferred;
}
#endif

struct ReferenceAudio {
  std::vector<float> samples;
  int sampleRate = 0;
};

template <typename T> T little(const char *data) { return qFromLittleEndian<T>(reinterpret_cast<const uchar *>(data)); }

bool readWave(const QString &path, ReferenceAudio *audio, QString *error)
{
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    if (error)
      *error = QStringLiteral("Could not open reference audio: %1").arg(file.errorString());
    return false;
  }
  const auto header = file.read(12);
  if (header.size() != 12 || header.first(4) != QByteArrayLiteral("RIFF") ||
      header.last(4) != QByteArrayLiteral("WAVE")) {
    if (error)
      *error = QStringLiteral("Reference audio must be an uncompressed WAV file.");
    return false;
  }
  quint16 format = 0, channels = 0, bits = 0;
  quint32 sampleRate = 0;
  QByteArray pcm;
  while (!file.atEnd()) {
    const auto chunkHeader = file.read(8);
    if (chunkHeader.size() != 8)
      break;
    const auto size = little<quint32>(chunkHeader.constData() + 4);
    auto chunk = file.read(size);
    if (size % 2)
      file.read(1);
    if (chunkHeader.first(4) == QByteArrayLiteral("fmt ") && chunk.size() >= 16) {
      format = little<quint16>(chunk.constData());
      channels = little<quint16>(chunk.constData() + 2);
      sampleRate = little<quint32>(chunk.constData() + 4);
      bits = little<quint16>(chunk.constData() + 14);
    } else if (chunkHeader.first(4) == QByteArrayLiteral("data")) {
      pcm = std::move(chunk);
    }
  }
  if ((format != 1 && format != 3) || channels == 0 || sampleRate == 0 || pcm.isEmpty()) {
    if (error)
      *error = QStringLiteral("Unsupported or empty WAV reference audio.");
    return false;
  }
  const int bytesPerSample = bits / 8;
  if (bytesPerSample <= 0 || (format == 3 && bits != 32) || (format == 1 && bits != 16 && bits != 24 && bits != 32)) {
    if (error)
      *error = QStringLiteral("Use 16/24/32-bit PCM or 32-bit float WAV reference audio.");
    return false;
  }
  const auto frames = pcm.size() / (bytesPerSample * channels);
  if (frames < static_cast<int>(sampleRate)) {
    if (error)
      *error = QStringLiteral("Reference audio should contain at least one second of speech.");
    return false;
  }
  if (frames > static_cast<int>(sampleRate) * 60) {
    if (error)
      *error = QStringLiteral("Reference audio must be shorter than 60 seconds.");
    return false;
  }
  std::vector<float> mono(static_cast<std::size_t>(frames));
  for (int frame = 0; frame < frames; ++frame) {
    float sum = 0.0F;
    for (int channel = 0; channel < channels; ++channel) {
      const char *sample = pcm.constData() + (frame * channels + channel) * bytesPerSample;
      if (format == 3) {
        const auto raw = little<quint32>(sample);
        float value;
        std::memcpy(&value, &raw, sizeof(value));
        sum += value;
      } else if (bits == 16) {
        sum += static_cast<float>(little<qint16>(sample)) / 32768.0F;
      } else if (bits == 24) {
        qint32 value = static_cast<uchar>(sample[0]) | (static_cast<uchar>(sample[1]) << 8) |
                       (static_cast<uchar>(sample[2]) << 16);
        if (value & 0x00800000)
          value |= static_cast<qint32>(0xff000000);
        sum += static_cast<float>(value) / 8388608.0F;
      } else {
        sum += static_cast<float>(little<qint32>(sample)) / 2147483648.0F;
      }
    }
    mono[static_cast<std::size_t>(frame)] = std::clamp(sum / channels, -1.0F, 1.0F);
  }
  if (static_cast<int>(sampleRate) == kSampleRate) {
    audio->samples = std::move(mono);
  } else {
    const auto count =
        static_cast<std::size_t>(std::llround(static_cast<double>(mono.size()) * kSampleRate / sampleRate));
    audio->samples.resize(count);
    const auto scale = static_cast<double>(sampleRate) / kSampleRate;
    for (std::size_t index = 0; index < count; ++index) {
      const double source = index * scale;
      const auto left = std::min(static_cast<std::size_t>(source), mono.size() - 1);
      const auto right = std::min(left + 1, mono.size() - 1);
      const auto fraction = static_cast<float>(source - left);
      audio->samples[index] = mono[left] + (mono[right] - mono[left]) * fraction;
    }
  }
  audio->sampleRate = kSampleRate;
  return true;
}

} // namespace

bool ChatterboxTokenizer::load(const QString &path, QString *error)
{
  vocab_.clear();
  merges_.clear();
  added_.clear();
  cangjie_.clear();
  addedTokens_.clear();
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    if (error)
      *error = file.errorString();
    return false;
  }
  QJsonParseError parseError;
  const auto root = QJsonDocument::fromJson(file.readAll(), &parseError).object();
  if (parseError.error != QJsonParseError::NoError) {
    if (error)
      *error = parseError.errorString();
    return false;
  }
  const auto model = root.value(QStringLiteral("model")).toObject();
  const auto vocabulary = model.value(QStringLiteral("vocab")).toObject();
  for (auto it = vocabulary.begin(); it != vocabulary.end(); ++it)
    vocab_.emplace(it.key().toStdString(), it.value().toInt(1));
  int rank = 0;
  for (const auto &value : model.value(QStringLiteral("merges")).toArray()) {
    QStringList pair;
    if (value.isString())
      pair = value.toString().split(QLatin1Char(' '));
    else
      for (const auto &part : value.toArray())
        pair.push_back(part.toString());
    if (pair.size() == 2)
      merges_.emplace(pair[0].toStdString() + '\0' + pair[1].toStdString(), rank++);
  }
  for (const auto &value : root.value(QStringLiteral("added_tokens")).toArray()) {
    const auto object = value.toObject();
    added_.insert(object.value(QStringLiteral("content")).toString(), object.value(QStringLiteral("id")).toInt());
  }
  addedTokens_ = added_.keys();
  std::ranges::sort(addedTokens_, [](const QString &a, const QString &b) { return a.size() > b.size(); });
  QFile cangjieFile(QFileInfo(path).absoluteDir().filePath(QStringLiteral("Cangjie5_TC.json")));
  if (cangjieFile.open(QIODevice::ReadOnly)) {
    const auto mappings = QJsonDocument::fromJson(cangjieFile.readAll()).array();
    QHash<QString, int> codeCounts;
    for (const auto &mapping : mappings) {
      const auto parts = mapping.toString().split(QLatin1Char('\t'));
      if (parts.size() < 2)
        continue;
      const auto index = codeCounts.value(parts[1], 0);
      cangjie_.insert(parts[0], parts[1] + (index > 0 ? QString::number(index) : QString{}));
      codeCounts[parts[1]] = index + 1;
    }
  }
  return !vocab_.empty();
}

std::vector<std::int64_t> ChatterboxTokenizer::encode(QString text, const QString &language) const
{
  if (language.compare(QStringLiteral("ko"), Qt::CaseInsensitive) == 0)
    text = text.normalized(QString::NormalizationForm_D);
  else if (language.compare(QStringLiteral("ja"), Qt::CaseInsensitive) == 0)
    text = text.normalized(QString::NormalizationForm_KD);
  else if (language.compare(QStringLiteral("zh"), Qt::CaseInsensitive) == 0 && !cangjie_.isEmpty()) {
    QString converted;
    for (const auto codepoint : text.toUcs4()) {
      const char32_t scalar = static_cast<char32_t>(codepoint);
      const auto glyph = QString::fromUcs4(&scalar, 1);
      const auto mapping = cangjie_.value(glyph);
      if (mapping.isEmpty()) {
        converted += glyph;
        continue;
      }
      for (const auto character : mapping)
        converted += QStringLiteral("[cj_%1]").arg(character);
      converted += QStringLiteral("[cj_.]");
    }
    text = std::move(converted);
  }
  text = QStringLiteral("[%1]%2").arg(language.toLower(), text);
  text.replace(QLatin1Char(' '), QStringLiteral("[SPACE]"));
  std::vector<std::int64_t> ids{6563, 255};
  QString plain;
  const auto flush = [&] {
    if (plain.isEmpty())
      return;
    static const QRegularExpression pieces(QStringLiteral("[\\p{L}\\p{M}\\p{N}_]+|[^\\p{L}\\p{M}\\p{N}\\s_]+"));
    auto matches = pieces.globalMatch(plain);
    while (matches.hasNext())
      appendBpe(matches.next().captured(), ids);
    plain.clear();
  };
  for (int offset = 0; offset < text.size();) {
    QString token;
    for (const auto &candidate : addedTokens_) {
      if (text.mid(offset, candidate.size()) == candidate) {
        token = candidate;
        break;
      }
    }
    if (!token.isEmpty()) {
      flush();
      const auto id = added_.value(token, 1);
      ids.push_back(id >= 0 && id < textVocabularySize ? id : 1);
      offset += token.size();
    } else if (text.at(offset).isHighSurrogate() && offset + 1 < text.size() && text.at(offset + 1).isLowSurrogate()) {
      plain += text.mid(offset, 2);
      offset += 2;
    } else {
      plain += text.at(offset++);
    }
  }
  flush();
  ids.insert(ids.end(), {0, kStartSpeechToken, kStartSpeechToken});
  return ids;
}

void ChatterboxTokenizer::appendBpe(const QString &piece, std::vector<std::int64_t> &ids) const
{
  std::vector<std::string> symbols;
  for (const auto codepoint : piece.toUcs4()) {
    const char32_t scalar = static_cast<char32_t>(codepoint);
    symbols.push_back(QString::fromUcs4(&scalar, 1).toStdString());
  }
  while (symbols.size() > 1) {
    int rank = std::numeric_limits<int>::max();
    std::size_t best = symbols.size();
    for (std::size_t index = 0; index + 1 < symbols.size(); ++index) {
      const auto found = merges_.find(symbols[index] + '\0' + symbols[index + 1]);
      if (found != merges_.end() && found->second < rank) {
        rank = found->second;
        best = index;
      }
    }
    if (best == symbols.size())
      break;
    symbols[best] += symbols[best + 1];
    symbols.erase(symbols.begin() + static_cast<std::ptrdiff_t>(best + 1));
  }
  for (const auto &symbol : symbols)
    appendSymbol(symbol, ids);
}

void ChatterboxTokenizer::appendSymbol(const std::string &symbol, std::vector<std::int64_t> &ids) const
{
  const auto found = vocab_.find(symbol);
  if (found == vocab_.end()) {
    ids.push_back(1);
    return;
  }
  if (found->second >= 0 && found->second < textVocabularySize) {
    ids.push_back(found->second);
    return;
  }

  // The pinned upstream tokenizer contains 102 tokens that were added after the
  // exported text embedding table. Decompose accented letters (ş -> s, ğ -> g,
  // İ -> I) so they remain pronounceable, and use [UNK] for symbols that have no
  // representation in the model instead of passing an out-of-bounds Gather index.
  const auto original = QString::fromStdString(symbol);
  const auto decomposed = original.normalized(QString::NormalizationForm_D);
  QString fallback;
  for (const auto character : decomposed) {
    if (!character.isMark())
      fallback += character;
  }
  if (!fallback.isEmpty() && fallback != original) {
    appendBpe(fallback, ids);
    return;
  }
  ids.push_back(1);
}

namespace {

#ifdef VOXLOCAL_HAVE_ONNXRUNTIME
std::vector<std::string> names(const Ort::Session &session, bool inputs)
{
  Ort::AllocatorWithDefaultOptions allocator;
  const auto count = inputs ? session.GetInputCount() : session.GetOutputCount();
  std::vector<std::string> result;
  for (std::size_t i = 0; i < count; ++i) {
    auto name = inputs ? session.GetInputNameAllocated(i, allocator) : session.GetOutputNameAllocated(i, allocator);
    result.emplace_back(name.get());
  }
  return result;
}
std::vector<const char *> pointers(const std::vector<std::string> &values)
{
  std::vector<const char *> result;
  for (const auto &value : values)
    result.push_back(value.c_str());
  return result;
}
Ort::Value floats(std::vector<float> &data, const std::vector<std::int64_t> &shape, const Ort::MemoryInfo &memory)
{
  static float empty = 0;
  return Ort::Value::CreateTensor<float>(memory, data.empty() ? &empty : data.data(), data.size(), shape.data(),
                                         shape.size());
}
Ort::Value integers(std::vector<std::int64_t> &data, const std::vector<std::int64_t> &shape,
                    const Ort::MemoryInfo &memory)
{
  static std::int64_t empty = 0;
  return Ort::Value::CreateTensor<std::int64_t>(memory, data.empty() ? &empty : data.data(), data.size(), shape.data(),
                                                shape.size());
}
std::vector<Ort::Value> runAll(Ort::Session &session, std::vector<Ort::Value> inputs)
{
  const auto inputNames = names(session, true), outputNames = names(session, false);
  const auto inputPointers = pointers(inputNames), outputPointers = pointers(outputNames);
  return session.Run(Ort::RunOptions{nullptr}, inputPointers.data(), inputs.data(), inputs.size(),
                     outputPointers.data(), outputPointers.size());
}
std::vector<float> copyFloats(Ort::Value &value)
{
  const auto count = value.GetTensorTypeAndShapeInfo().GetElementCount();
  const auto *data = value.GetTensorData<float>();
  return {data, data + count};
}
std::vector<std::int64_t> copyIntegers(Ort::Value &value)
{
  const auto count = value.GetTensorTypeAndShapeInfo().GetElementCount();
  const auto *data = value.GetTensorData<std::int64_t>();
  return {data, data + count};
}

std::int64_t sampleSpeechToken(const float *logits, std::int64_t vocabulary, const std::vector<std::int64_t> &generated,
                               std::mt19937 &random)
{
  constexpr float temperature = 0.2F;
  constexpr float repetitionPenalty = 1.2F;
  std::vector<double> weights(static_cast<std::size_t>(vocabulary));
  float maximum = -std::numeric_limits<float>::infinity();
  for (std::int64_t token = 0; token < vocabulary; ++token) {
    float score = logits[token];
    if (std::ranges::find(generated, token) != generated.end())
      score = score < 0.0F ? score * repetitionPenalty : score / repetitionPenalty;
    score /= temperature;
    weights[static_cast<std::size_t>(token)] = score;
    maximum = std::max(maximum, score);
  }
  for (auto &weight : weights)
    weight = std::exp(weight - maximum);
  std::discrete_distribution<std::int64_t> distribution(weights.begin(), weights.end());
  return distribution(random);
}
#endif

} // namespace

class ChatterboxEngine::Impl
{
public:
  ChatterboxTokenizer tokenizer;
  QString modelPath;
  QString backend = QStringLiteral("Unavailable");
  bool ready = false;
#ifdef VOXLOCAL_HAVE_ONNXRUNTIME
  Ort::Env environment{ORT_LOGGING_LEVEL_WARNING, "VoxLocal"};
  Ort::MemoryInfo memory = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
  std::unique_ptr<Ort::Session> encoder, embed, languageModel, decoder;
#endif
};

ChatterboxEngine::ChatterboxEngine() : impl_(std::make_unique<Impl>()) {}
ChatterboxEngine::~ChatterboxEngine() = default;
QString ChatterboxEngine::backendName() const { return impl_->backend; }
bool ChatterboxEngine::isReady() const { return impl_->ready; }

bool ChatterboxEngine::initialize(const QString &modelPath, QString *error)
{
  impl_->ready = false;
  impl_->modelPath = modelPath;
  if (!impl_->tokenizer.load(QDir(modelPath).filePath(QStringLiteral("tokenizer.json")), error))
    return false;
#ifndef VOXLOCAL_HAVE_ONNXRUNTIME
  if (error)
    *error = QStringLiteral(
        "This build does not include ONNX Runtime. Install a VoxLocal release package or configure ONNXRUNTIME_ROOT.");
  return false;
#else
  const auto configureBase = [](Ort::SessionOptions &options) {
    options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
    options.SetExecutionMode(ExecutionMode::ORT_SEQUENTIAL);
    options.SetIntraOpNumThreads(static_cast<int>(std::max(1u, std::thread::hardware_concurrency() / 2)));
  };
  const auto createSessions = [&](Ort::SessionOptions &options) {
    const auto session = [&](const QString &relative) {
      const auto path = QDir(modelPath).filePath(relative);
#ifdef Q_OS_WIN
      const auto native = path.toStdWString();
      return std::make_unique<Ort::Session>(impl_->environment, native.c_str(), options);
#else
      const auto native = path.toUtf8();
      return std::make_unique<Ort::Session>(impl_->environment, native.constData(), options);
#endif
    };
    auto encoder = session(QStringLiteral("onnx/speech_encoder.onnx"));
    auto embed = session(QStringLiteral("onnx/embed_tokens.onnx"));
    auto languageModel = session(QStringLiteral("onnx/language_model_q4.onnx"));
    auto decoder = session(QStringLiteral("onnx/conditional_decoder.onnx"));
    impl_->encoder = std::move(encoder);
    impl_->embed = std::move(embed);
    impl_->languageModel = std::move(languageModel);
    impl_->decoder = std::move(decoder);
  };

  QString hardwareFailure;
  try {
#ifdef VOXLOCAL_ORT_DIRECTML
    const auto adapter = preferredDmlAdapter();
    if (adapter.index >= 0) {
      Ort::SessionOptions options;
      configureBase(options);
      // DirectML rejects sessions unless memory patterns are disabled and
      // execution is sequential. Keep both requirements next to EP setup.
      options.DisableMemPattern();
      options.SetExecutionMode(ExecutionMode::ORT_SEQUENTIAL);
      Ort::ThrowOnError(OrtSessionOptionsAppendExecutionProvider_DML(options, adapter.index));
      createSessions(options);
      impl_->backend = adapter.name.isEmpty() ? QStringLiteral("DirectML GPU")
                                              : QStringLiteral("DirectML GPU — %1").arg(adapter.name);
      impl_->ready = true;
      return true;
    }
#elif ORT_API_VERSION >= 16
    const auto providers = Ort::GetAvailableProviders();
#ifdef Q_OS_MACOS
    const auto availableName = std::string("CoreMLExecutionProvider");
    const char *provider = "CoreML";
    const auto backend = QStringLiteral("CoreML GPU/Neural Engine");
#else
    const auto availableName = std::string("CUDAExecutionProvider");
    const char *provider = "CUDA";
    const auto backend = QStringLiteral("CUDA GPU");
#endif
    if (std::ranges::find(providers, availableName) != providers.end()) {
      Ort::SessionOptions options;
      configureBase(options);
      Ort::ThrowOnError(Ort::GetApi().SessionOptionsAppendExecutionProvider(options, provider, nullptr, nullptr, 0));
      createSessions(options);
      impl_->backend = backend;
      impl_->ready = true;
      return true;
    }
#endif
  } catch (const Ort::Exception &exception) {
    hardwareFailure = QString::fromUtf8(exception.what());
    impl_->encoder.reset();
    impl_->embed.reset();
    impl_->languageModel.reset();
    impl_->decoder.reset();
  }

  try {
    Ort::SessionOptions options;
    configureBase(options);
    createSessions(options);
    impl_->backend = hardwareFailure.isEmpty() ? QStringLiteral("CPU") : QStringLiteral("CPU — GPU fallback");
    impl_->ready = true;
    return true;
  } catch (const Ort::Exception &exception) {
    if (error) {
      const QString cpuFailure = QString::fromUtf8(exception.what());
      *error = hardwareFailure.isEmpty()
                   ? QStringLiteral("ONNX Runtime could not load Chatterbox on CPU: %1").arg(cpuFailure)
                   : QStringLiteral("ONNX Runtime could not load Chatterbox. GPU: %1; CPU fallback: %2")
                         .arg(hardwareFailure, cpuFailure);
    }
    return false;
  }
#endif
}

TtsAudio ChatterboxEngine::synthesize(const TtsRequest &request, const std::atomic_bool &cancelled, QString *error)
{
  TtsAudio result;
  if (!impl_->ready) {
    if (error)
      *error = QStringLiteral("Chatterbox is not initialized.");
    return result;
  }
#ifndef VOXLOCAL_HAVE_ONNXRUNTIME
  Q_UNUSED(request)
  Q_UNUSED(cancelled)
  return result;
#else
  try {
    ReferenceAudio reference;
    auto path = request.referenceAudioPath.isEmpty()
                    ? QDir(impl_->modelPath).filePath(QStringLiteral("default_voice.wav"))
                    : request.referenceAudioPath;
    if (!readWave(path, &reference, error))
      return result;
    auto inputIds = impl_->tokenizer.encode(request.text, request.language);
    std::vector<std::int64_t> positions(inputIds.size());
    for (std::size_t i = 0; i < inputIds.size(); ++i)
      positions[i] = inputIds[i] >= kStartSpeechToken ? 0 : static_cast<std::int64_t>(i) - 1;
    std::vector<float> exaggeration{std::clamp(request.exaggeration, 0.0F, 1.0F)};

    std::vector<Ort::Value> encoderInputs;
    encoderInputs.push_back(
        floats(reference.samples, {1, static_cast<std::int64_t>(reference.samples.size())}, impl_->memory));
    auto encoderOutputs = runAll(*impl_->encoder, std::move(encoderInputs));
    if (encoderOutputs.size() < 4)
      throw std::runtime_error("speech encoder returned incomplete outputs");
    auto condition = copyFloats(encoderOutputs[0]);
    const auto conditionShape = encoderOutputs[0].GetTensorTypeAndShapeInfo().GetShape();
    auto prompt = copyIntegers(encoderOutputs[1]);
    auto speaker = copyFloats(encoderOutputs[2]);
    const auto speakerShape = encoderOutputs[2].GetTensorTypeAndShapeInfo().GetShape();
    auto features = copyFloats(encoderOutputs[3]);
    const auto featureShape = encoderOutputs[3].GetTensorTypeAndShapeInfo().GetShape();

    const auto embed = [&](std::vector<std::int64_t> &ids, std::vector<std::int64_t> &position) {
      std::vector<Ort::Value> values;
      values.push_back(integers(ids, {1, static_cast<std::int64_t>(ids.size())}, impl_->memory));
      values.push_back(integers(position, {1, static_cast<std::int64_t>(position.size())}, impl_->memory));
      values.push_back(floats(exaggeration, {1}, impl_->memory));
      auto output = runAll(*impl_->embed, std::move(values));
      return std::make_pair(copyFloats(output[0]), output[0].GetTensorTypeAndShapeInfo().GetShape());
    };
    auto embedded = embed(inputIds, positions);
    if (conditionShape.size() != 3 || embedded.second.size() != 3 || conditionShape[2] != embedded.second[2])
      throw std::runtime_error("embedding shape mismatch");
    std::vector<float> current = std::move(condition);
    current.insert(current.end(), embedded.first.begin(), embedded.first.end());
    std::int64_t sequence = conditionShape[1] + embedded.second[1];
    const auto hidden = embedded.second[2];
    std::vector<std::int64_t> mask(static_cast<std::size_t>(sequence), 1), generated{kStartSpeechToken};
    std::vector<Ort::Value> past;
    const auto inputNames = names(*impl_->languageModel, true), outputNames = names(*impl_->languageModel, false);
    const auto inputPointers = pointers(inputNames), outputPointers = pointers(outputNames);
    const auto logitsFound = std::ranges::find(outputNames, std::string("logits"));
    const auto logitsIndex = static_cast<std::size_t>(
        logitsFound == outputNames.end() ? 0 : std::distance(outputNames.begin(), logitsFound));
    std::random_device seed;
    std::mt19937 random(seed());
    QElapsedTimer generationTimer;
    generationTimer.start();
    bool timedOut = false;

    for (int step = 0; step < 256 && !cancelled.load(); ++step) {
      if (generationTimer.elapsed() >= 120000) {
        timedOut = true;
        break;
      }
      std::vector<Ort::Value> values;
      std::vector<float> empty;
      std::size_t cacheIndex = 0;
      for (const auto &name : inputNames) {
        if (name == "inputs_embeds")
          values.push_back(floats(current, {1, sequence, hidden}, impl_->memory));
        else if (name == "attention_mask")
          values.push_back(integers(mask, {1, static_cast<std::int64_t>(mask.size())}, impl_->memory));
        else if (!past.empty())
          values.push_back(std::move(past.at(cacheIndex++)));
        else
          values.push_back(floats(empty, {1, 16, 0, 64}, impl_->memory));
      }
      auto output = impl_->languageModel->Run(Ort::RunOptions{nullptr}, inputPointers.data(), values.data(),
                                              values.size(), outputPointers.data(), outputPointers.size());
      auto &logits = output.at(logitsIndex);
      const auto shape = logits.GetTensorTypeAndShapeInfo().GetShape();
      if (shape.size() != 3)
        throw std::runtime_error("language model logits shape mismatch");
      const auto vocabulary = shape[2];
      const auto *scores = logits.GetTensorData<float>() + (shape[1] - 1) * vocabulary;
      const std::int64_t next = sampleSpeechToken(scores, vocabulary, generated, random);
      generated.push_back(next);
      if (next == kStopSpeechToken)
        break;
      past.clear();
      for (std::size_t i = 0; i < output.size(); ++i)
        if (i != logitsIndex)
          past.push_back(std::move(output[i]));
      std::vector<std::int64_t> nextId{next}, nextPosition{step + 1};
      current = std::move(embed(nextId, nextPosition).first);
      sequence = 1;
      mask.push_back(1);
    }
    if (cancelled.load()) {
      if (error)
        *error = QStringLiteral("Speech generation was cancelled.");
      return result;
    }
    if (timedOut) {
      if (error)
        *error =
            QStringLiteral(
                "Speech generation exceeded two minutes on the %1 backend. GPU acceleration is required for practical "
                "Chatterbox generation on this system.")
                .arg(impl_->backend);
      return result;
    }
    const auto end = generated.back() == kStopSpeechToken ? generated.end() - 1 : generated.end();
    prompt.insert(prompt.end(), generated.begin() + 1, end);
    std::vector<Ort::Value> decoderInputs;
    decoderInputs.push_back(integers(prompt, {1, static_cast<std::int64_t>(prompt.size())}, impl_->memory));
    decoderInputs.push_back(floats(speaker, speakerShape, impl_->memory));
    decoderInputs.push_back(floats(features, featureShape, impl_->memory));
    auto decoded = runAll(*impl_->decoder, std::move(decoderInputs));
    result.samples = copyFloats(decoded[0]);
    return result;
  } catch (const std::exception &exception) {
    if (error)
      *error = QStringLiteral("Chatterbox inference failed: %1").arg(QString::fromUtf8(exception.what()));
    return result;
  }
#endif
}

} // namespace voxlocal
