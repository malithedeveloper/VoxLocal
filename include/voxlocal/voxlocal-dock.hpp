#pragma once

#include "voxlocal/runtime.hpp"

#include <QVector>
#include <QWidget>

class QCheckBox;
class QComboBox;
class QFontComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QProgressBar;
class QPushButton;
class QSpinBox;

namespace voxlocal {

class VoxLocalDock final : public QWidget
{
  Q_OBJECT

public:
  explicit VoxLocalDock(VoxLocalRuntime *runtime, QWidget *parent = nullptr);

private:
  struct LocalizedWidget {
    QWidget *widget = nullptr;
    QString english;
    QString turkish;
  };

  QString text(const char *english, const char *turkish) const;
  QString localizedOperation(const QString &operation) const;
  QString localizedStatus(const QString &status) const;
  void localize(QWidget *widget, const char *english, const char *turkish);
  void retranslateUi();
  void buildUi();
  void refresh();
  void refreshColorButtons();
  void chooseColor(QPushButton *button, QString *target);
  void updateModelProgress(qint64 received, qint64 total, const QString &fileName);
  void updateModelDownloadState(bool installing);
  void updateModelRuntimeState();
  void updateVoiceImportProgress(const QString &personaId, int percent, const QString &operation);
  void finishVoiceImport(const QString &personaId, const QString &voicePath);
  void failVoiceImport(const QString &personaId, const QString &error);
  void setVoiceImportControlsEnabled(bool enabled);
  void updateSpeechState(const QString &personaId, const QString &message, bool running, bool failed = false);
  void loadPersona(int index);
  void storePersona(int index);
  bool save();

  VoxLocalRuntime *runtime_;
  Settings working_;
  QVector<LocalizedWidget> localizedWidgets_;
  QLabel *status_ = nullptr;
  QLabel *modelInfo_ = nullptr;
  QLabel *model_ = nullptr;
  QProgressBar *modelProgress_ = nullptr;
  QPushButton *modelInstall_ = nullptr;
  QPushButton *modelLoad_ = nullptr;
  QLabel *modelRuntime_ = nullptr;
  QComboBox *modelStartup_ = nullptr;
  QComboBox *interfaceLanguage_ = nullptr;
  QLineEdit *channel_ = nullptr;
  QListWidget *personas_ = nullptr;
  QLineEdit *personaName_ = nullptr;
  QLineEdit *command_ = nullptr;
  QLineEdit *voice_ = nullptr;
  QLineEdit *testText_ = nullptr;
  QWidget *voiceImportPanel_ = nullptr;
  QLabel *voiceImportStatus_ = nullptr;
  QProgressBar *voiceImportProgress_ = nullptr;
  QListWidget *voiceImportSteps_ = nullptr;
  QPushButton *voiceBrowse_ = nullptr;
  QPushButton *personaPreview_ = nullptr;
  QPushButton *personaNew_ = nullptr;
  QPushButton *personaDelete_ = nullptr;
  QPushButton *saveButton_ = nullptr;
  QPushButton *addOverlayButton_ = nullptr;
  QLabel *personaTestStatus_ = nullptr;
  QProgressBar *personaTestProgress_ = nullptr;
  QComboBox *languageMode_ = nullptr;
  QComboBox *language_ = nullptr;
  QCheckBox *everyone_ = nullptr;
  QCheckBox *subscribers_ = nullptr;
  QCheckBox *moderators_ = nullptr;
  QCheckBox *broadcaster_ = nullptr;
  QCheckBox *ttsEnabled_ = nullptr;
  QSpinBox *globalCooldown_ = nullptr;
  QSpinBox *maxCharacters_ = nullptr;
  QComboBox *preset_ = nullptr;
  QComboBox *animation_ = nullptr;
  QFontComboBox *font_ = nullptr;
  QPushButton *background_ = nullptr;
  QPushButton *foreground_ = nullptr;
  QPushButton *fallbackNameColor_ = nullptr;
  QCheckBox *showName_ = nullptr;
};

} // namespace voxlocal
