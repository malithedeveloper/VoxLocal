#include "voxlocal/welcome-wizard.hpp"

#include "voxlocal/config-store.hpp"
#include "voxlocal/voxlocal-source.hpp"

#include <QCloseEvent>
#include <QComboBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHash>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QVBoxLayout>
#include <QWizardPage>

#include <algorithm>

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

WelcomeWizard::WelcomeWizard(VoxLocalRuntime *runtime, QWidget *parent) : QWizard(parent), runtime_(runtime)
{
  voicePersonaId_ = QUuid::createUuid().toString(QUuid::WithoutBraces);
  setWizardStyle(QWizard::ClassicStyle);
  setOption(QWizard::NoBackButtonOnStartPage);
  setOption(QWizard::NoCancelButton);
  setOption(QWizard::HaveCustomButton1);
  setWindowModality(Qt::ApplicationModal);
  setMinimumSize(620, 460);
  buildPages();
  retranslateUi();
  connect(this, &QWizard::customButtonClicked, this, [this](int button) {
    if (button != QWizard::CustomButton1)
      return;
    if (currentId() == pageIds().last())
      accept();
    else
      next();
  });
}

QString WelcomeWizard::text(const char *english, const char *turkish) const
{
  const bool useTurkish = interfaceLanguage_ && interfaceLanguage_->currentData().toString() == QStringLiteral("tr");
  return useTurkish ? QString::fromUtf8(turkish) : QString::fromUtf8(english);
}

QString WelcomeWizard::localizedOperation(const QString &operation) const
{
  if (!interfaceLanguage_ || interfaceLanguage_->currentData().toString() != QStringLiteral("tr"))
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

void WelcomeWizard::localize(QWidget *widget, const char *english, const char *turkish)
{
  localizedWidgets_.push_back({widget, QString::fromUtf8(english), QString::fromUtf8(turkish)});
}

void WelcomeWizard::retranslateUi()
{
  const bool turkish = interfaceLanguage_->currentData().toString() == QStringLiteral("tr");
  setWindowTitle(text("Welcome to VoxLocal", "VoxLocal'e hoş geldin"));
  for (const auto &entry : localizedWidgets_) {
    const QString value = turkish ? entry.turkish : entry.english;
    if (auto *page = qobject_cast<QWizardPage *>(entry.widget))
      page->setTitle(value);
    else if (auto *label = qobject_cast<QLabel *>(entry.widget))
      label->setText(value);
    else if (auto *button = qobject_cast<QPushButton *>(entry.widget))
      button->setText(value);
    else if (auto *group = qobject_cast<QGroupBox *>(entry.widget))
      group->setTitle(value);
  }
  setButtonText(QWizard::BackButton, text("Back", "Geri"));
  setButtonText(QWizard::NextButton, text("Next", "İleri"));
  setButtonText(QWizard::FinishButton, text("Finish", "Bitir"));
  setButtonText(QWizard::CustomButton1, text("Skip", "Geç"));
  modelStartup_->setItemText(0, text("Ask at startup", "Açılışta sor"));
  modelStartup_->setItemText(1, text("Load automatically", "Açılsın"));
  modelStartup_->setItemText(2, text("Do not load", "Açılmasın"));
  access_->setItemText(0, text("Everyone", "Herkes"));
  access_->setItemText(1, text("Subscribers, moderators and broadcaster", "Aboneler, moderatörler ve yayıncı"));
  access_->setItemText(2, text("Moderators and broadcaster", "Moderatörler ve yayıncı"));
  modelInfo_->setText(text("Chatterbox Multilingual is downloaded once and then runs offline. Download size: %1.",
                           "Chatterbox Multilingual bir kez indirilir ve çevrimdışı çalışır. İndirme boyutu: %1.")
                          .arg(ModelManager::formatBytes(runtime_->modelManager()->totalBytes())));
  updateModelProgress(runtime_->modelManager()->downloadedBytes(), runtime_->modelManager()->totalBytes(), {});
  updateModelDownloadState(runtime_->modelManager()->isInstalling());
}

void WelcomeWizard::buildPages()
{
  const auto addRow = [this](QFormLayout *form, const char *english, const char *turkish, QWidget *field) {
    auto *label = new QLabel;
    localize(label, english, turkish);
    form->addRow(label, field);
  };

  auto *languagePage = new QWizardPage;
  localize(languagePage, "Welcome", "Hoş geldin");
  auto *languageLayout = new QVBoxLayout(languagePage);
  auto *intro = new QLabel;
  localize(intro,
           "VoxLocal turns Kick chat commands into local, cloned speech inside OBS. No cloud TTS service or "
           "separate application is used.",
           "VoxLocal, Kick sohbet komutlarını OBS içinde yerel ve klonlanmış sese dönüştürür. Bulut TTS hizmeti "
           "veya ayrı bir uygulama kullanmaz.");
  intro->setWordWrap(true);
  interfaceLanguage_ = new QComboBox;
  interfaceLanguage_->addItem(QStringLiteral("English"), QStringLiteral("en"));
  interfaceLanguage_->addItem(QStringLiteral("Türkçe"), QStringLiteral("tr"));
  interfaceLanguage_->setCurrentIndex(runtime_->settings().interfaceLanguage == InterfaceLanguage::Turkish ? 1 : 0);
  auto *languageLabel = new QLabel;
  localize(languageLabel, "Interface language", "Arayüz dili");
  languageLayout->addWidget(intro);
  languageLayout->addWidget(languageLabel);
  languageLayout->addWidget(interfaceLanguage_);
  languageLayout->addStretch();
  connect(interfaceLanguage_, &QComboBox::currentIndexChanged, this, &WelcomeWizard::retranslateUi);
  addPage(languagePage);

  auto *modelPage = new QWizardPage;
  localize(modelPage, "Local voice model", "Yerel ses modeli");
  auto *modelLayout = new QVBoxLayout(modelPage);
  modelInfo_ = new QLabel;
  modelInfo_->setWordWrap(true);
  modelStatus_ = new QLabel;
  modelStatus_->setWordWrap(true);
  modelProgress_ = new QProgressBar;
  modelProgress_->setRange(0, 10000);
  modelInstall_ = new QPushButton;
  modelStartup_ = new QComboBox;
  modelStartup_->addItem(QString{}, QVariant(QStringLiteral("ask")));
  modelStartup_->addItem(QString{}, QVariant(QStringLiteral("always")));
  modelStartup_->addItem(QString{}, QVariant(QStringLiteral("never")));
  switch (runtime_->settings().modelStartupBehavior) {
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
  auto *modelStartupLabel = new QLabel;
  localize(modelStartupLabel, "At OBS startup", "OBS açılışında");
  modelLayout->addWidget(modelInfo_);
  modelLayout->addWidget(modelStatus_);
  modelLayout->addWidget(modelProgress_);
  modelLayout->addWidget(modelInstall_);
  modelLayout->addWidget(modelStartupLabel);
  modelLayout->addWidget(modelStartup_);
  modelLayout->addStretch();
  connect(modelInstall_, &QPushButton::clicked, runtime_->modelManager(), &ModelManager::install);
  connect(runtime_, &VoxLocalRuntime::modelProgress, this,
          [this](qint64 value, qint64 total, const QString &file) { updateModelProgress(value, total, file); });
  connect(runtime_->modelManager(), &ModelManager::installingChanged, this, &WelcomeWizard::updateModelDownloadState);
  connect(runtime_->modelManager(), &ModelManager::ready, this, [this] {
    updateModelProgress(runtime_->modelManager()->totalBytes(), runtime_->modelManager()->totalBytes(), {});
  });
  connect(runtime_, &VoxLocalRuntime::errorOccurred, modelStatus_, &QLabel::setText);
  addPage(modelPage);

  auto *kickPage = new QWizardPage;
  localize(kickPage, "Kick channel", "Kick kanalı");
  auto *kickLayout = new QFormLayout(kickPage);
  auto *kickInfo = new QLabel;
  localize(kickInfo, "Enter the public channel name shown in kick.com/channel-name.",
           "kick.com/kanal-adı adresinde görünen herkese açık kanal adını gir.");
  kickInfo->setWordWrap(true);
  channel_ = new QLineEdit;
  channel_->setPlaceholderText(QStringLiteral("channel-name"));
  channel_->setText(runtime_->settings().kick.channelSlug);
  kickLayout->addRow(kickInfo);
  addRow(kickLayout, "Channel", "Kanal", channel_);
  addPage(kickPage);

  auto *personaPage = new QWizardPage;
  localize(personaPage, "First persona", "İlk persona");
  auto *personaLayout = new QFormLayout(personaPage);
  personaName_ = new QLineEdit(QStringLiteral("Voice"));
  command_ = new QLineEdit(QStringLiteral("voice"));
  voice_ = new QLineEdit;
  voice_->setReadOnly(true);
  auto *voiceField = new QWidget;
  auto *voiceLayout = new QHBoxLayout(voiceField);
  voiceLayout->setContentsMargins(0, 0, 0, 0);
  voiceLayout->addWidget(voice_);
  voiceBrowse_ = new QPushButton;
  localize(voiceBrowse_, "Choose media…", "Medya seç…");
  voiceLayout->addWidget(voiceBrowse_);
  language_ = new QComboBox;
  for (const auto &code : ConfigStore::supportedLanguages())
    language_->addItem(code.toUpper(), code);
  language_->setCurrentIndex(language_->findData(QStringLiteral("en")));
  access_ = new QComboBox;
  access_->addItem(QString{}, QVariant::fromValue(static_cast<uint>(UserRole::Everyone)));
  access_->addItem(QString{}, QVariant::fromValue(static_cast<uint>(UserRole::Subscriber | UserRole::Moderator |
                                                                    UserRole::Broadcaster)));
  access_->addItem(QString{}, QVariant::fromValue(static_cast<uint>(UserRole::Moderator | UserRole::Broadcaster)));
  if (!runtime_->settings().personas.empty()) {
    const auto &persona = runtime_->settings().personas.front();
    voicePersonaId_ = persona.id;
    personaName_->setText(persona.name);
    command_->setText(persona.command);
    preparedVoicePath_ = persona.referenceAudioPath;
    voice_->setText(preparedVoicePath_);
    language_->setCurrentIndex(std::max(0, language_->findData(persona.language)));
    const auto allowed = static_cast<uint>(persona.access.allowed);
    for (int index = 0; index < access_->count(); ++index) {
      if (access_->itemData(index).toUInt() == allowed) {
        access_->setCurrentIndex(index);
        break;
      }
    }
  }
  addRow(personaLayout, "Persona name", "Persona adı", personaName_);
  addRow(personaLayout, "Chat command", "Sohbet komutu", command_);
  addRow(personaLayout, "Short clean audio or video sample", "Kısa ve temiz ses veya video örneği", voiceField);
  addRow(personaLayout, "Speech language", "Konuşma dili", language_);
  addRow(personaLayout, "Who can use it", "Kimler kullanabilir", access_);
  voiceImportPanel_ = new QGroupBox;
  localize(voiceImportPanel_, "Voice preparation", "Ses hazırlığı");
  auto *voiceImportLayout = new QVBoxLayout(voiceImportPanel_);
  voiceImportStatus_ = new QLabel;
  voiceImportStatus_->setWordWrap(true);
  voiceImportProgress_ = new QProgressBar;
  voiceImportProgress_->setRange(0, 100);
  voiceImportSteps_ = new QListWidget;
  voiceImportSteps_->setMaximumHeight(115);
  voiceImportLayout->addWidget(voiceImportStatus_);
  voiceImportLayout->addWidget(voiceImportProgress_);
  voiceImportLayout->addWidget(voiceImportSteps_);
  voiceImportPanel_->hide();
  personaLayout->addRow(voiceImportPanel_);
  connect(voiceBrowse_, &QPushButton::clicked, this, [this] {
    const auto file = QFileDialog::getOpenFileName(
        this, text("Voice sample", "Ses örneği"), {},
        text("Audio and video (*.wav *.mp3 *.m4a *.aac *.flac *.ogg *.opus *.wma *.aiff *.mp4 *.mkv *.mov *.webm "
             "*.avi);;All files (*)",
             "Ses ve video (*.wav *.mp3 *.m4a *.aac *.flac *.ogg *.opus *.wma *.aiff *.mp4 *.mkv *.mov *.webm "
             "*.avi);;Tüm dosyalar (*)"));
    if (file.isEmpty())
      return;
    preparedVoicePath_.clear();
    voice_->clear();
    voiceImportSteps_->clear();
    voiceImportPanel_->show();
    voiceBrowse_->setEnabled(false);
    runtime_->importVoiceAsync(file, voicePersonaId_);
  });
  connect(runtime_, &VoxLocalRuntime::voiceImportProgress, this,
          [this](const QString &personaId, int percent, const QString &operation) {
            if (personaId != voicePersonaId_)
              return;
            const QString shown = localizedOperation(operation);
            voiceImportProgress_->setValue(std::clamp(percent, 0, 100));
            voiceImportProgress_->setFormat(QStringLiteral("%1% — %2").arg(percent).arg(shown));
            voiceImportStatus_->setText(shown);
            if (voiceImportSteps_->count() > 0) {
              auto *previous = voiceImportSteps_->item(voiceImportSteps_->count() - 1);
              if (!previous->text().startsWith(QStringLiteral("✓")))
                previous->setText(QStringLiteral("✓ ") + previous->text().mid(2));
            }
            voiceImportSteps_->addItem(QStringLiteral("● ") + shown);
            voiceImportSteps_->scrollToBottom();
          });
  connect(runtime_, &VoxLocalRuntime::voiceImportFinished, this,
          [this](const QString &personaId, const QString &voicePath) {
            if (personaId != voicePersonaId_)
              return;
            preparedVoicePath_ = voicePath;
            voice_->setText(voicePath);
            voiceImportProgress_->setValue(100);
            voiceImportProgress_->setFormat(text("100% — Ready", "100% — Hazır"));
            voiceImportStatus_->setText(
                text("Voice is ready for zero-shot cloning.", "Ses, zero-shot klonlama için hazır."));
            if (voiceImportSteps_->count() > 0) {
              auto *last = voiceImportSteps_->item(voiceImportSteps_->count() - 1);
              last->setText(QStringLiteral("✓ ") + last->text().mid(2));
            }
            voiceBrowse_->setEnabled(true);
          });
  connect(runtime_, &VoxLocalRuntime::voiceImportFailed, this, [this](const QString &personaId, const QString &error) {
    if (personaId != voicePersonaId_)
      return;
    voiceImportStatus_->setText(error);
    voiceImportProgress_->setFormat(text("Failed", "Başarısız"));
    voiceImportSteps_->addItem(QStringLiteral("✕ ") + error);
    voiceBrowse_->setEnabled(true);
  });
  addPage(personaPage);

  auto *done = new QWizardPage;
  localize(done, "Ready", "Hazır");
  auto *doneLayout = new QVBoxLayout(done);
  auto *doneText = new QLabel;
  localize(doneText,
           "VoxLocal will add its overlay to the current scene, connect to Kick, and use the verified local model. "
           "You can change personas, TTS controls, and appearance from the VoxLocal dock.",
           "VoxLocal katmanını geçerli sahneye ekleyecek, Kick'e bağlanacak ve doğrulanmış yerel modeli "
           "kullanacak. Personaları, TTS kontrollerini ve görünümü VoxLocal panelinden değiştirebilirsin.");
  doneText->setWordWrap(true);
  doneLayout->addWidget(doneText);
  doneLayout->addStretch();
  addPage(done);
}

void WelcomeWizard::accept()
{
  Settings settings = runtime_->settings();
  settings.interfaceLanguage = interfaceLanguage_->currentData().toString() == QStringLiteral("tr")
                                   ? InterfaceLanguage::Turkish
                                   : InterfaceLanguage::English;
  const QString startup = modelStartup_->currentData().toString();
  settings.modelStartupBehavior = startup == QStringLiteral("always")  ? ModelStartupBehavior::AlwaysLoad
                                  : startup == QStringLiteral("never") ? ModelStartupBehavior::NeverLoad
                                                                       : ModelStartupBehavior::Ask;
  settings.kick.channelSlug = channel_->text().trimmed().toLower();
  settings.kick.enabled = !settings.kick.channelSlug.isEmpty();
  const QString normalizedCommand = CommandRouter::normalizeCommand(command_->text());
  if (!personaName_->text().trimmed().isEmpty() && !normalizedCommand.isEmpty() && !preparedVoicePath_.isEmpty()) {
    Persona persona;
    persona.id = voicePersonaId_;
    persona.name = personaName_->text().trimmed();
    persona.command = normalizedCommand;
    persona.language = language_->currentData().toString();
    persona.automaticFallbackLanguage = persona.language;
    persona.access.allowed = static_cast<UserRole>(access_->currentData().toUInt());
    persona.referenceAudioPath = preparedVoicePath_;
    settings.personas = {persona};
  }
  settings.welcomeCompleted = true;
  QString error;
  if (!runtime_->applySettings(settings, &error)) {
    QMessageBox::warning(this, QStringLiteral("VoxLocal"), error);
    return;
  }
  if (!addOverlayToCurrentScene(&error))
    QMessageBox::information(this, QStringLiteral("VoxLocal"),
                             error +
                                 text(" You can add it later from the dock.", " Daha sonra panelden ekleyebilirsin."));
  QWizard::accept();
}

void WelcomeWizard::reject()
{
  if (allowClose_)
    QWizard::reject();
}

void WelcomeWizard::closeEvent(QCloseEvent *event)
{
  if (allowClose_)
    QWizard::closeEvent(event);
  else
    event->ignore();
}

void WelcomeWizard::closeForShutdown()
{
  allowClose_ = true;
  QWizard::reject();
}

void WelcomeWizard::updateModelProgress(qint64 received, qint64 total, const QString &fileName)
{
  received = std::clamp(received, qint64{0}, std::max(total, qint64{0}));
  modelProgress_->setValue(progressValue(received, total));
  modelProgress_->setFormat(percentage(received, total));
  if (runtime_->modelManager()->isInstalled()) {
    modelStatus_->setText(
        text("Installed and verified — %1", "Kuruldu ve doğrulandı — %1").arg(ModelManager::formatBytes(total)));
    return;
  }
  const QString detail = text("%1 / %2 downloaded — %3 remaining", "%1 / %2 indirildi — %3 kaldı")
                             .arg(ModelManager::formatBytes(received), ModelManager::formatBytes(total),
                                  ModelManager::formatBytes(std::max(total - received, qint64{0})));
  if (!fileName.isEmpty())
    modelStatus_->setText(text("Downloading %1\n%2", "%1 indiriliyor\n%2").arg(fileName, detail));
  else if (received > 0)
    modelStatus_->setText(text("Partial download saved\n%1", "Yarım indirme kaydedildi\n%1").arg(detail));
  else
    modelStatus_->setText(text("Not downloaded\n%1", "İndirilmedi\n%1").arg(detail));
}

void WelcomeWizard::updateModelDownloadState(bool installing)
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
}

} // namespace voxlocal
