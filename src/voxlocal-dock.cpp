#include "voxlocal/voxlocal-dock.hpp"

#include "voxlocal/config-store.hpp"
#include "voxlocal/voxlocal-source.hpp"

#include <QCheckBox>
#include <QColor>
#include <QColorDialog>
#include <QComboBox>
#include <QFileDialog>
#include <QFontComboBox>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHash>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QVBoxLayout>

#include <algorithm>
#include <array>

namespace voxlocal {
namespace {

int progressValue(qint64 received, qint64 total)
{
  if (total <= 0)
    return 0;
  return static_cast<int>(std::clamp(received, qint64{0}, total) * 10000 / total);
}

QString percentage(qint64 received, qint64 total)
{
  const double value =
      total > 0 ? 100.0 * static_cast<double>(std::clamp(received, qint64{0}, total)) / static_cast<double>(total)
                : 0.0;
  return QString::number(value, 'f', 2) + QStringLiteral("%");
}

} // namespace

VoxLocalDock::VoxLocalDock(VoxLocalRuntime *runtime, QWidget *parent)
    : QWidget(parent), runtime_(runtime), working_(runtime->settings())
{
  setObjectName(QStringLiteral("VoxLocalDock"));
  buildUi();
  connect(runtime_, &VoxLocalRuntime::settingsChanged, this, &VoxLocalDock::refresh);
  connect(runtime_, &VoxLocalRuntime::statusChanged, this, [this](const QString &status) {
    status_->setText(localizedStatus(status));
    updateModelRuntimeState();
  });
  connect(runtime_, &VoxLocalRuntime::errorOccurred, this, [this](const QString &error) {
    status_->setText(error);
    status_->setToolTip(error);
  });
  connect(runtime_, &VoxLocalRuntime::modelProgress, this,
          [this](qint64 received, qint64 total, const QString &file) { updateModelProgress(received, total, file); });
  connect(runtime_->modelManager(), &ModelManager::installingChanged, this, &VoxLocalDock::updateModelDownloadState);
  connect(runtime_, &VoxLocalRuntime::engineLoadingChanged, this, [this] { updateModelRuntimeState(); });
  connect(runtime_->modelManager(), &ModelManager::failed, model_, &QLabel::setText);
  connect(runtime_->modelManager(), &ModelManager::ready, this, [this] {
    updateModelProgress(runtime_->modelManager()->totalBytes(), runtime_->modelManager()->totalBytes(), {});
    updateModelDownloadState(false);
  });
  connect(runtime_, &VoxLocalRuntime::voiceImportProgress, this, &VoxLocalDock::updateVoiceImportProgress);
  connect(runtime_, &VoxLocalRuntime::voiceImportFinished, this, &VoxLocalDock::finishVoiceImport);
  connect(runtime_, &VoxLocalRuntime::voiceImportFailed, this, &VoxLocalDock::failVoiceImport);
  connect(runtime_, &VoxLocalRuntime::speechStarted, this, [this](const QString &personaId) {
    updateSpeechState(personaId, text("Generating cloned speech locally…", "Klonlanmış ses yerelde üretiliyor…"), true);
  });
  connect(runtime_, &VoxLocalRuntime::speechFinished, this, [this](const QString &personaId) {
    updateSpeechState(personaId,
                      text("Speech generated and sent to OBS audio.", "Ses üretildi ve OBS sesine gönderildi."), false);
  });
  connect(runtime_, &VoxLocalRuntime::speechFailed, this,
          [this](const QString &personaId, const QString &error) { updateSpeechState(personaId, error, false, true); });
  refresh();
}

QString VoxLocalDock::text(const char *english, const char *turkish) const
{
  return working_.interfaceLanguage == InterfaceLanguage::Turkish ? QString::fromUtf8(turkish)
                                                                  : QString::fromUtf8(english);
}

QString VoxLocalDock::localizedOperation(const QString &operation) const
{
  if (working_.interfaceLanguage != InterfaceLanguage::Turkish)
    return operation;
  static const QHash<QString, QString> translations{
      {QStringLiteral("Checking the selected media file"), QStringLiteral("Seçilen medya dosyası denetleniyor")},
      {QStringLiteral("Locating the local FFmpeg converter"), QStringLiteral("Yerel FFmpeg dönüştürücüsü bulunuyor")},
      {QStringLiteral("Extracting audio and converting it to 24 kHz mono WAV"),
       QStringLiteral("Ses çıkarılıp 24 kHz mono WAV biçimine dönüştürülüyor")},
      {QStringLiteral("Validating converted speech audio"), QStringLiteral("Dönüştürülen konuşma sesi doğrulanıyor")},
      {QStringLiteral("Saving the prepared persona voice"), QStringLiteral("Hazırlanan persona sesi kaydediliyor")},
      {QStringLiteral("Voice preparation complete"), QStringLiteral("Ses hazırlığı tamamlandı")}};
  return translations.value(operation, operation);
}

QString VoxLocalDock::localizedStatus(const QString &status) const
{
  if (status.startsWith(QStringLiteral("ready:")))
    return text("Ready — %1 backend", "Hazır — %1 altyapısı").arg(status.section(QLatin1Char(':'), 1));
  if (status == QStringLiteral("loading-model"))
    return text("Loading the local model…", "Yerel model yükleniyor…");
  if (status == QStringLiteral("model-error"))
    return text("The model could not be loaded.", "Model yüklenemedi.");
  if (status == QStringLiteral("connected"))
    return text("Connected to Kick chat", "Kick sohbetine bağlandı");
  if (status == QStringLiteral("stopped"))
    return text("Stopped", "Durduruldu");
  if (status == QStringLiteral("Generating speech locally…"))
    return text("Generating speech locally…", "Ses yerelde üretiliyor…");
  if (status == QStringLiteral("Speech ready — playing in OBS"))
    return text("Speech ready — playing in OBS", "Ses hazır — OBS içinde oynatılıyor");
  if (status == QStringLiteral("Speech generation failed"))
    return text("Speech generation failed", "Ses üretimi başarısız oldu");
  return status;
}

void VoxLocalDock::localize(QWidget *widget, const char *english, const char *turkish)
{
  localizedWidgets_.push_back({widget, QString::fromUtf8(english), QString::fromUtf8(turkish)});
}

void VoxLocalDock::retranslateUi()
{
  const bool turkish = working_.interfaceLanguage == InterfaceLanguage::Turkish;
  for (const auto &entry : localizedWidgets_) {
    const QString value = turkish ? entry.turkish : entry.english;
    if (auto *label = qobject_cast<QLabel *>(entry.widget))
      label->setText(value);
    else if (auto *button = qobject_cast<QPushButton *>(entry.widget))
      button->setText(value);
    else if (auto *checkBox = qobject_cast<QCheckBox *>(entry.widget))
      checkBox->setText(value);
    else if (auto *group = qobject_cast<QGroupBox *>(entry.widget))
      group->setTitle(value);
  }
  languageMode_->setItemText(0, text("Fixed", "Sabit"));
  languageMode_->setItemText(1, text("Automatic", "Otomatik"));
  modelStartup_->setItemText(0, text("Ask at startup", "Açılışta sor"));
  modelStartup_->setItemText(1, text("Load automatically", "Açılsın"));
  modelStartup_->setItemText(2, text("Do not load", "Açılmasın"));
  preset_->setItemText(preset_->findData(QStringLiteral("minimal")), text("Minimal", "Minimal"));
  preset_->setItemText(preset_->findData(QStringLiteral("subtitle")), text("Subtitle", "Altyazı"));
  const std::array<std::pair<const char *, const char *>, 6> animations{{{"Fade", "Solma"},
                                                                         {"Slide from left", "Soldan kay"},
                                                                         {"Slide from right", "Sağdan kay"},
                                                                         {"Slide from top", "Yukarıdan kay"},
                                                                         {"Slide from bottom", "Aşağıdan kay"},
                                                                         {"None", "Yok"}}};
  for (int index = 0; index < static_cast<int>(animations.size()); ++index)
    animation_->setItemText(index, text(animations[static_cast<std::size_t>(index)].first,
                                        animations[static_cast<std::size_t>(index)].second));
  status_->setText(localizedStatus(runtime_->status()));
  modelInfo_->setText(text("Chatterbox is downloaded once and then runs offline. Download size: %1.",
                           "Chatterbox bir kez indirilir ve çevrimdışı çalışır. İndirme boyutu: %1.")
                          .arg(ModelManager::formatBytes(runtime_->modelManager()->totalBytes())));
  updateModelProgress(runtime_->modelManager()->downloadedBytes(), runtime_->modelManager()->totalBytes(), {});
  updateModelDownloadState(runtime_->modelManager()->isInstalling());
  updateModelRuntimeState();
}

void VoxLocalDock::buildUi()
{
  auto *content = new QWidget;
  auto *layout = new QVBoxLayout(content);
  auto *heading = new QLabel(QStringLiteral("VOXLOCAL"));
  QFont headingFont = heading->font();
  headingFont.setPointSize(18);
  headingFont.setBold(true);
  heading->setFont(headingFont);
  layout->addWidget(heading);
  status_ = new QLabel;
  status_->setWordWrap(true);
  layout->addWidget(status_);

  const auto addRow = [this](QFormLayout *form, const char *english, const char *turkish, QWidget *field) {
    auto *label = new QLabel;
    localize(label, english, turkish);
    form->addRow(label, field);
  };

  auto *general = new QGroupBox;
  localize(general, "General", "Genel");
  auto *generalForm = new QFormLayout(general);
  interfaceLanguage_ = new QComboBox;
  interfaceLanguage_->addItem(QStringLiteral("English"), QStringLiteral("en"));
  interfaceLanguage_->addItem(QStringLiteral("Türkçe"), QStringLiteral("tr"));
  channel_ = new QLineEdit;
  addRow(generalForm, "Interface language", "Arayüz dili", interfaceLanguage_);
  addRow(generalForm, "Kick channel", "Kick kanalı", channel_);
  auto *generalButtons = new QWidget;
  auto *generalButtonsLayout = new QHBoxLayout(generalButtons);
  generalButtonsLayout->setContentsMargins(0, 0, 0, 0);
  saveButton_ = new QPushButton;
  addOverlayButton_ = new QPushButton;
  localize(saveButton_, "Save", "Kaydet");
  localize(addOverlayButton_, "Add OBS overlay", "OBS katmanı ekle");
  generalButtonsLayout->addWidget(saveButton_);
  generalButtonsLayout->addWidget(addOverlayButton_);
  generalForm->addRow(generalButtons);
  connect(saveButton_, &QPushButton::clicked, this, &VoxLocalDock::save);
  connect(addOverlayButton_, &QPushButton::clicked, this, [this] {
    QString error;
    if (!addOverlayToCurrentScene(&error))
      QMessageBox::warning(this, QStringLiteral("VoxLocal"), error);
  });
  connect(interfaceLanguage_, &QComboBox::currentIndexChanged, this, [this] {
    working_.interfaceLanguage = interfaceLanguage_->currentData().toString() == QStringLiteral("tr")
                                     ? InterfaceLanguage::Turkish
                                     : InterfaceLanguage::English;
    retranslateUi();
  });
  layout->addWidget(general);

  auto *modelGroup = new QGroupBox;
  localize(modelGroup, "Local model", "Yerel model");
  auto *modelLayout = new QVBoxLayout(modelGroup);
  modelInfo_ = new QLabel;
  modelInfo_->setWordWrap(true);
  model_ = new QLabel;
  model_->setWordWrap(true);
  modelProgress_ = new QProgressBar;
  modelProgress_->setRange(0, 10000);
  modelInstall_ = new QPushButton;
  modelLoad_ = new QPushButton;
  localize(modelLoad_, "Load model now", "Modeli şimdi aç");
  modelRuntime_ = new QLabel;
  modelRuntime_->setWordWrap(true);
  modelStartup_ = new QComboBox;
  modelStartup_->addItem(QString{}, QVariant(QStringLiteral("ask")));
  modelStartup_->addItem(QString{}, QVariant(QStringLiteral("always")));
  modelStartup_->addItem(QString{}, QVariant(QStringLiteral("never")));
  auto *modelStartupLabel = new QLabel;
  localize(modelStartupLabel, "At OBS startup", "OBS açılışında");
  modelLayout->addWidget(modelInfo_);
  modelLayout->addWidget(model_);
  modelLayout->addWidget(modelProgress_);
  modelLayout->addWidget(modelInstall_);
  modelLayout->addWidget(modelRuntime_);
  modelLayout->addWidget(modelLoad_);
  modelLayout->addWidget(modelStartupLabel);
  modelLayout->addWidget(modelStartup_);
  connect(modelInstall_, &QPushButton::clicked, runtime_->modelManager(), &ModelManager::install);
  connect(modelLoad_, &QPushButton::clicked, runtime_, &VoxLocalRuntime::loadModel);
  layout->addWidget(modelGroup);

  auto *personaGroup = new QGroupBox;
  localize(personaGroup, "Personas", "Personalar");
  auto *personaLayout = new QVBoxLayout(personaGroup);
  personas_ = new QListWidget;
  personaLayout->addWidget(personas_);
  auto *personaButtons = new QHBoxLayout;
  personaNew_ = new QPushButton;
  personaDelete_ = new QPushButton;
  localize(personaNew_, "New", "Yeni");
  localize(personaDelete_, "Delete", "Sil");
  personaButtons->addWidget(personaNew_);
  personaButtons->addWidget(personaDelete_);
  personaLayout->addLayout(personaButtons);
  auto *form = new QFormLayout;
  personaName_ = new QLineEdit;
  command_ = new QLineEdit;
  voice_ = new QLineEdit;
  voice_->setReadOnly(true);
  auto *voiceField = new QWidget;
  auto *voiceRow = new QHBoxLayout(voiceField);
  voiceRow->setContentsMargins(0, 0, 0, 0);
  voiceRow->addWidget(voice_);
  voiceBrowse_ = new QPushButton;
  localize(voiceBrowse_, "Media…", "Medya…");
  voiceRow->addWidget(voiceBrowse_);
  languageMode_ = new QComboBox;
  languageMode_->addItem(QString{}, QVariant(QStringLiteral("fixed")));
  languageMode_->addItem(QString{}, QVariant(QStringLiteral("auto")));
  language_ = new QComboBox;
  for (const auto &language : ConfigStore::supportedLanguages())
    language_->addItem(language.toUpper(), language);
  addRow(form, "Name", "Ad", personaName_);
  addRow(form, "Command", "Komut", command_);
  addRow(form, "Audio or video sample", "Ses veya video örneği", voiceField);
  addRow(form, "Language mode", "Dil modu", languageMode_);
  addRow(form, "Language or fallback", "Dil veya yedek dil", language_);
  testText_ = new QLineEdit;
  testText_->setPlaceholderText(QStringLiteral("Merhaba"));
  addRow(form, "Test text", "Test metni", testText_);
  auto *roles = new QWidget;
  auto *rolesLayout = new QGridLayout(roles);
  rolesLayout->setContentsMargins(0, 0, 0, 0);
  everyone_ = new QCheckBox;
  subscribers_ = new QCheckBox;
  moderators_ = new QCheckBox;
  broadcaster_ = new QCheckBox;
  localize(everyone_, "Everyone", "Herkes");
  localize(subscribers_, "Subscribers", "Aboneler");
  localize(moderators_, "Moderators", "Moderatörler");
  localize(broadcaster_, "Broadcaster", "Yayıncı");
  rolesLayout->addWidget(everyone_, 0, 0);
  rolesLayout->addWidget(subscribers_, 0, 1);
  rolesLayout->addWidget(moderators_, 1, 0);
  rolesLayout->addWidget(broadcaster_, 1, 1);
  addRow(form, "Who can use it", "Kimler kullanabilir", roles);
  personaLayout->addLayout(form);
  voiceImportPanel_ = new QGroupBox;
  localize(voiceImportPanel_, "Voice preparation", "Ses hazırlığı");
  auto *voiceImportLayout = new QVBoxLayout(voiceImportPanel_);
  voiceImportStatus_ = new QLabel;
  voiceImportStatus_->setWordWrap(true);
  voiceImportProgress_ = new QProgressBar;
  voiceImportProgress_->setRange(0, 100);
  voiceImportSteps_ = new QListWidget;
  voiceImportSteps_->setMaximumHeight(125);
  voiceImportLayout->addWidget(voiceImportStatus_);
  voiceImportLayout->addWidget(voiceImportProgress_);
  voiceImportLayout->addWidget(voiceImportSteps_);
  voiceImportPanel_->hide();
  personaLayout->addWidget(voiceImportPanel_);
  personaPreview_ = new QPushButton;
  localize(personaPreview_, "Test current persona", "Seçili personayı test et");
  personaTestStatus_ = new QLabel;
  personaTestStatus_->setWordWrap(true);
  personaTestProgress_ = new QProgressBar;
  personaTestProgress_->setRange(0, 100);
  personaTestProgress_->setValue(0);
  personaTestProgress_->setTextVisible(false);
  personaLayout->addWidget(personaPreview_);
  personaLayout->addWidget(personaTestStatus_);
  personaLayout->addWidget(personaTestProgress_);
  connect(personas_, &QListWidget::currentRowChanged, this, &VoxLocalDock::loadPersona);
  connect(personaNew_, &QPushButton::clicked, this, [this] {
    if (personas_->currentRow() >= 0)
      storePersona(personas_->currentRow());
    working_.personas.emplace_back();
    personas_->addItem(working_.personas.back().name);
    personas_->setCurrentRow(static_cast<int>(working_.personas.size()) - 1);
  });
  connect(personaDelete_, &QPushButton::clicked, this, [this] {
    const int row = personas_->currentRow();
    if (row < 0)
      return;
    working_.personas.erase(working_.personas.begin() + row);
    delete personas_->takeItem(row);
    if (!working_.personas.empty())
      personas_->setCurrentRow(std::min(row, static_cast<int>(working_.personas.size()) - 1));
  });
  connect(voiceBrowse_, &QPushButton::clicked, this, [this] {
    const int row = personas_->currentRow();
    if (row < 0 || row >= static_cast<int>(working_.personas.size())) {
      QMessageBox::information(this, QStringLiteral("VoxLocal"),
                               text("Create or select a persona first.", "Önce bir persona oluştur veya seç."));
      return;
    }
    const auto path = QFileDialog::getOpenFileName(
        this, text("Voice sample", "Ses örneği"), {},
        text("Audio and video (*.wav *.mp3 *.m4a *.aac *.flac *.ogg *.opus *.wma *.aiff *.mp4 *.mkv *.mov *.webm "
             "*.avi);;All files (*)",
             "Ses ve video (*.wav *.mp3 *.m4a *.aac *.flac *.ogg *.opus *.wma *.aiff *.mp4 *.mkv *.mov *.webm "
             "*.avi);;Tüm dosyalar (*)"));
    if (path.isEmpty())
      return;
    voiceImportSteps_->clear();
    voiceImportPanel_->show();
    setVoiceImportControlsEnabled(false);
    runtime_->importVoiceAsync(path, working_.personas[static_cast<std::size_t>(row)].id);
  });
  connect(personaPreview_, &QPushButton::clicked, this, [this] {
    const auto selectedId = personas_->currentRow() >= 0 ? working_.personas[personas_->currentRow()].id : QString{};
    if (!save())
      return;
    QString error;
    const QString sample = testText_->text().trimmed();
    if (selectedId.isEmpty() || !runtime_->preview(selectedId, sample, &error)) {
      updateSpeechState(selectedId, error, false, true);
      QMessageBox::warning(this, QStringLiteral("VoxLocal"), error);
    }
  });
  layout->addWidget(personaGroup);

  auto *ttsGroup = new QGroupBox;
  localize(ttsGroup, "TTS controls", "TTS kontrolleri");
  auto *ttsForm = new QFormLayout(ttsGroup);
  ttsEnabled_ = new QCheckBox;
  localize(ttsEnabled_, "TTS enabled", "TTS açık");
  globalCooldown_ = new QSpinBox;
  globalCooldown_->setRange(0, 3600);
  globalCooldown_->setSuffix(QStringLiteral(" s"));
  maxCharacters_ = new QSpinBox;
  maxCharacters_->setRange(1, 1000);
  ttsForm->addRow(ttsEnabled_);
  addRow(ttsForm, "Global cooldown", "Global bekleme süresi", globalCooldown_);
  addRow(ttsForm, "Maximum characters", "En fazla karakter", maxCharacters_);
  auto *cooldownHint = new QLabel;
  cooldownHint->setWordWrap(true);
  localize(cooldownHint,
           "While a message is being generated, later TTS commands are skipped. The cooldown starts when audio "
           "begins playing.",
           "Bir mesaj üretilirken sonraki TTS komutları atlanır. Bekleme süresi ses oynatılmaya başlayınca başlar.");
  ttsForm->addRow(cooldownHint);
  layout->addWidget(ttsGroup);

  auto *overlay = new QGroupBox;
  localize(overlay, "Overlay", "Katman");
  auto *overlayForm = new QFormLayout(overlay);
  preset_ = new QComboBox;
  preset_->addItem(QString{}, QVariant(QStringLiteral("minimal")));
  preset_->addItem(QString{}, QVariant(QStringLiteral("subtitle")));
  animation_ = new QComboBox;
  animation_->addItem(QString{}, QVariant(QStringLiteral("fade")));
  animation_->addItem(QString{}, QVariant(QStringLiteral("slide-left")));
  animation_->addItem(QString{}, QVariant(QStringLiteral("slide-right")));
  animation_->addItem(QString{}, QVariant(QStringLiteral("slide-down")));
  animation_->addItem(QString{}, QVariant(QStringLiteral("slide-up")));
  animation_->addItem(QString{}, QVariant(QStringLiteral("none")));
  font_ = new QFontComboBox;
  background_ = new QPushButton;
  foreground_ = new QPushButton;
  fallbackNameColor_ = new QPushButton;
  localize(background_, "Choose color…", "Renk seç…");
  localize(foreground_, "Choose color…", "Renk seç…");
  localize(fallbackNameColor_, "Choose color…", "Renk seç…");
  showName_ = new QCheckBox;
  localize(showName_, "Show sender name", "Gönderen adını göster");
  addRow(overlayForm, "Preset", "Görünüm", preset_);
  addRow(overlayForm, "Entrance animation", "Geliş animasyonu", animation_);
  addRow(overlayForm, "Font", "Yazı tipi", font_);
  addRow(overlayForm, "Background color", "Arka plan rengi", background_);
  addRow(overlayForm, "Text color", "Metin rengi", foreground_);
  addRow(overlayForm, "Fallback name color", "Yedek isim rengi", fallbackNameColor_);
  overlayForm->addRow(showName_);
  connect(background_, &QPushButton::clicked, this, [this] { chooseColor(background_, &working_.overlay.background); });
  connect(foreground_, &QPushButton::clicked, this, [this] { chooseColor(foreground_, &working_.overlay.foreground); });
  connect(fallbackNameColor_, &QPushButton::clicked, this,
          [this] { chooseColor(fallbackNameColor_, &working_.overlay.fallbackNameColor); });
  layout->addWidget(overlay);
  layout->addStretch();

  auto *scroll = new QScrollArea(this);
  scroll->setWidgetResizable(true);
  scroll->setFrameShape(QFrame::NoFrame);
  scroll->setWidget(content);
  auto *outer = new QVBoxLayout(this);
  outer->setContentsMargins(0, 0, 0, 0);
  outer->addWidget(scroll);
  retranslateUi();
}

void VoxLocalDock::refresh()
{
  working_ = runtime_->settings();
  interfaceLanguage_->setCurrentIndex(working_.interfaceLanguage == InterfaceLanguage::Turkish ? 1 : 0);
  channel_->setText(working_.kick.channelSlug);
  personas_->clear();
  for (const auto &persona : working_.personas)
    personas_->addItem(QStringLiteral("!%1 — %2").arg(persona.command, persona.name));
  if (!working_.personas.empty())
    personas_->setCurrentRow(0);
  ttsEnabled_->setChecked(working_.ttsEnabled);
  globalCooldown_->setValue(working_.globalCooldownSeconds);
  maxCharacters_->setValue(working_.maxTextLength);
  switch (working_.modelStartupBehavior) {
  case ModelStartupBehavior::AlwaysLoad:
    modelStartup_->setCurrentIndex(1);
    break;
  case ModelStartupBehavior::NeverLoad:
    modelStartup_->setCurrentIndex(2);
    break;
  case ModelStartupBehavior::Ask:
    modelStartup_->setCurrentIndex(0);
    break;
  }
  preset_->setCurrentIndex(std::max(0, preset_->findData(working_.overlay.preset)));
  animation_->setCurrentIndex(std::max(0, animation_->findData(working_.overlay.entranceAnimation)));
  font_->setCurrentFont(QFont(working_.overlay.fontFamily));
  showName_->setChecked(working_.overlay.showName);
  refreshColorButtons();
  retranslateUi();
}

void VoxLocalDock::refreshColorButtons()
{
  const auto apply = [](QPushButton *button, const QString &color) {
    const QColor value(color);
    const QString textColor = value.lightness() < 128 ? QStringLiteral("#ffffff") : QStringLiteral("#000000");
    button->setStyleSheet(QStringLiteral("QPushButton { background: %1; color: %2; }").arg(color, textColor));
  };
  apply(background_, working_.overlay.background);
  apply(foreground_, working_.overlay.foreground);
  apply(fallbackNameColor_, working_.overlay.fallbackNameColor);
}

void VoxLocalDock::chooseColor(QPushButton *, QString *target)
{
  const QColor selected = QColorDialog::getColor(QColor(*target), this, text("Choose color", "Renk seç"));
  if (!selected.isValid())
    return;
  *target = selected.name(QColor::HexRgb);
  refreshColorButtons();
}

void VoxLocalDock::updateModelProgress(qint64 received, qint64 total, const QString &fileName)
{
  received = std::clamp(received, qint64{0}, std::max(total, qint64{0}));
  modelProgress_->setValue(progressValue(received, total));
  modelProgress_->setFormat(percentage(received, total));
  if (runtime_->modelManager()->isInstalled()) {
    model_->setText(text("Installed and verified", "Kuruldu ve doğrulandı") + QStringLiteral(" — ") +
                    ModelManager::formatBytes(total));
    return;
  }
  const QString detail = working_.interfaceLanguage == InterfaceLanguage::Turkish
                             ? QStringLiteral("%1 / %2 indirildi — %3 kaldı")
                                   .arg(ModelManager::formatBytes(received), ModelManager::formatBytes(total),
                                        ModelManager::formatBytes(std::max(total - received, qint64{0})))
                             : QStringLiteral("%1 / %2 downloaded — %3 remaining")
                                   .arg(ModelManager::formatBytes(received), ModelManager::formatBytes(total),
                                        ModelManager::formatBytes(std::max(total - received, qint64{0})));
  if (!fileName.isEmpty())
    model_->setText(text("Downloading ", "İndiriliyor: ") + fileName + QStringLiteral("\n") + detail);
  else if (received > 0)
    model_->setText(text("Partial download saved", "Yarım indirme kaydedildi") + QStringLiteral("\n") + detail);
  else
    model_->setText(text("Not downloaded", "İndirilmedi") + QStringLiteral("\n") + detail);
}

void VoxLocalDock::updateModelDownloadState(bool installing)
{
  modelInstall_->setEnabled(!installing && !runtime_->modelManager()->isInstalled());
  if (runtime_->modelManager()->isInstalled())
    modelInstall_->setText(text("Model installed", "Model kuruldu"));
  else if (installing)
    modelInstall_->setText(text("Downloading and verifying…", "İndiriliyor ve doğrulanıyor…"));
  else if (runtime_->modelManager()->downloadedBytes() > 0)
    modelInstall_->setText(text("Resume and verify model", "İndirmeye devam et ve doğrula"));
  else
    modelInstall_->setText(text("Download and verify model", "Modeli indir ve doğrula"));
  updateModelRuntimeState();
}

void VoxLocalDock::updateModelRuntimeState()
{
  if (runtime_->engineReady()) {
    modelRuntime_->setText(text("Loaded in memory — %1", "Bellekte açık — %1").arg(runtime_->engineBackend()));
  } else if (runtime_->engineLoading()) {
    modelRuntime_->setText(text("Loading in the background…", "Arka planda açılıyor…"));
  } else if (runtime_->modelManager()->isInstalled()) {
    modelRuntime_->setText(text("Installed on disk, not loaded in memory.", "Diskte kurulu, bellekte açık değil."));
  } else {
    modelRuntime_->setText(
        text("Download and verify the model before loading it.", "Modeli açmadan önce indirip doğrula."));
  }
  modelLoad_->setEnabled(runtime_->modelManager()->isInstalled() && !runtime_->engineReady() &&
                         !runtime_->engineLoading());
}

void VoxLocalDock::updateVoiceImportProgress(const QString &personaId, int percent, const QString &operation)
{
  const int row = personas_->currentRow();
  if (row < 0 || row >= static_cast<int>(working_.personas.size()) ||
      working_.personas[static_cast<std::size_t>(row)].id != personaId)
    return;
  const QString shownOperation = localizedOperation(operation);
  voiceImportPanel_->show();
  voiceImportProgress_->setValue(std::clamp(percent, 0, 100));
  voiceImportProgress_->setFormat(QStringLiteral("%1% — %2").arg(percent).arg(shownOperation));
  voiceImportStatus_->setText(shownOperation);
  if (voiceImportSteps_->count() > 0) {
    auto *previous = voiceImportSteps_->item(voiceImportSteps_->count() - 1);
    if (!previous->text().startsWith(QStringLiteral("✓")))
      previous->setText(QStringLiteral("✓ ") + previous->text().mid(2));
  }
  voiceImportSteps_->addItem(QStringLiteral("● ") + shownOperation);
  voiceImportSteps_->scrollToBottom();
}

void VoxLocalDock::finishVoiceImport(const QString &personaId, const QString &voicePath)
{
  const auto found =
      std::ranges::find_if(working_.personas, [&](const Persona &persona) { return persona.id == personaId; });
  if (found == working_.personas.end())
    return;
  found->referenceAudioPath = voicePath;
  const int row = personas_->currentRow();
  if (row >= 0 && row < static_cast<int>(working_.personas.size()) &&
      working_.personas[static_cast<std::size_t>(row)].id == personaId) {
    voice_->setText(voicePath);
    voiceImportProgress_->setValue(100);
    voiceImportProgress_->setFormat(text("100% — Ready", "100% — Hazır"));
    voiceImportStatus_->setText(text("Voice is ready for zero-shot cloning.", "Ses, zero-shot klonlama için hazır."));
    if (voiceImportSteps_->count() > 0) {
      auto *last = voiceImportSteps_->item(voiceImportSteps_->count() - 1);
      last->setText(QStringLiteral("✓ ") + last->text().mid(2));
    }
  }
  setVoiceImportControlsEnabled(true);
}

void VoxLocalDock::failVoiceImport(const QString &personaId, const QString &error)
{
  const int row = personas_->currentRow();
  if (row >= 0 && row < static_cast<int>(working_.personas.size()) &&
      working_.personas[static_cast<std::size_t>(row)].id == personaId) {
    voiceImportStatus_->setText(error);
    voiceImportProgress_->setFormat(text("Failed", "Başarısız"));
    voiceImportSteps_->addItem(QStringLiteral("✕ ") + error);
  }
  setVoiceImportControlsEnabled(true);
}

void VoxLocalDock::setVoiceImportControlsEnabled(bool enabled)
{
  personas_->setEnabled(enabled);
  personaNew_->setEnabled(enabled);
  personaDelete_->setEnabled(enabled);
  voiceBrowse_->setEnabled(enabled);
  personaPreview_->setEnabled(enabled);
  saveButton_->setEnabled(enabled);
}

void VoxLocalDock::updateSpeechState(const QString &personaId, const QString &message, bool running, bool failed)
{
  const int row = personas_->currentRow();
  if (!personaId.isEmpty() && (row < 0 || row >= static_cast<int>(working_.personas.size()) ||
                               working_.personas[static_cast<std::size_t>(row)].id != personaId))
    return;
  personaTestStatus_->setText(message);
  personaPreview_->setEnabled(!running && !runtime_->isVoiceImporting());
  if (running) {
    personaTestProgress_->setRange(0, 0);
    personaTestProgress_->setTextVisible(false);
  } else {
    personaTestProgress_->setRange(0, 100);
    personaTestProgress_->setValue(failed ? 0 : 100);
    personaTestProgress_->setTextVisible(true);
    personaTestProgress_->setFormat(failed ? text("Failed", "Başarısız") : text("Ready", "Hazır"));
  }
}

void VoxLocalDock::loadPersona(int index)
{
  if (index < 0 || index >= static_cast<int>(working_.personas.size()))
    return;
  const auto &persona = working_.personas[static_cast<std::size_t>(index)];
  personaName_->setText(persona.name);
  command_->setText(persona.command);
  voice_->setText(persona.referenceAudioPath);
  languageMode_->setCurrentIndex(persona.languageMode == LanguageMode::Automatic ? 1 : 0);
  language_->setCurrentIndex(std::max(0, language_->findData(persona.languageMode == LanguageMode::Automatic
                                                                 ? persona.automaticFallbackLanguage
                                                                 : persona.language)));
  everyone_->setChecked(hasRole(persona.access.allowed, UserRole::Everyone));
  subscribers_->setChecked(hasRole(persona.access.allowed, UserRole::Subscriber));
  moderators_->setChecked(hasRole(persona.access.allowed, UserRole::Moderator));
  broadcaster_->setChecked(hasRole(persona.access.allowed, UserRole::Broadcaster));
}

void VoxLocalDock::storePersona(int index)
{
  if (index < 0 || index >= static_cast<int>(working_.personas.size()))
    return;
  auto &persona = working_.personas[static_cast<std::size_t>(index)];
  persona.name = personaName_->text().trimmed().isEmpty() ? QStringLiteral("Voice") : personaName_->text().trimmed();
  persona.command = CommandRouter::normalizeCommand(command_->text());
  if (persona.command.isEmpty())
    persona.command = QStringLiteral("voice");
  persona.languageMode =
      languageMode_->currentData().toString() == QStringLiteral("auto") ? LanguageMode::Automatic : LanguageMode::Fixed;
  if (persona.languageMode == LanguageMode::Automatic)
    persona.automaticFallbackLanguage = language_->currentData().toString();
  else
    persona.language = language_->currentData().toString();
  persona.access.allowed = UserRole::None;
  if (everyone_->isChecked())
    persona.access.allowed = persona.access.allowed | UserRole::Everyone;
  if (subscribers_->isChecked())
    persona.access.allowed = persona.access.allowed | UserRole::Subscriber;
  if (moderators_->isChecked())
    persona.access.allowed = persona.access.allowed | UserRole::Moderator;
  if (broadcaster_->isChecked())
    persona.access.allowed = persona.access.allowed | UserRole::Broadcaster;
  if (auto *item = personas_->item(index))
    item->setText(QStringLiteral("!%1 — %2").arg(persona.command, persona.name));
}

bool VoxLocalDock::save()
{
  if (runtime_->isVoiceImporting()) {
    QMessageBox::information(this, QStringLiteral("VoxLocal"),
                             text("Wait for voice preparation to finish before saving.",
                                  "Kaydetmeden önce ses hazırlığının tamamlanmasını bekle."));
    return false;
  }
  storePersona(personas_->currentRow());
  working_.interfaceLanguage = interfaceLanguage_->currentData().toString() == QStringLiteral("tr")
                                   ? InterfaceLanguage::Turkish
                                   : InterfaceLanguage::English;
  working_.kick.channelSlug = channel_->text().trimmed().toLower();
  working_.ttsEnabled = ttsEnabled_->isChecked();
  const QString startup = modelStartup_->currentData().toString();
  working_.modelStartupBehavior = startup == QStringLiteral("always")  ? ModelStartupBehavior::AlwaysLoad
                                  : startup == QStringLiteral("never") ? ModelStartupBehavior::NeverLoad
                                                                       : ModelStartupBehavior::Ask;
  working_.globalCooldownSeconds = globalCooldown_->value();
  working_.maxTextLength = maxCharacters_->value();
  working_.overlay.preset = preset_->currentData().toString();
  working_.overlay.entranceAnimation = animation_->currentData().toString();
  working_.overlay.fontFamily = font_->currentFont().family();
  working_.overlay.showName = showName_->isChecked();
  QString error;
  if (!runtime_->applySettings(working_, &error)) {
    QMessageBox::warning(this, QStringLiteral("VoxLocal"), error);
    return false;
  }
  return true;
}

} // namespace voxlocal
