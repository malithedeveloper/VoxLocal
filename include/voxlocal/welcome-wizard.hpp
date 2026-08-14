#pragma once

#include "voxlocal/runtime.hpp"

#include <QVector>
#include <QWizard>

class QComboBox;
class QCloseEvent;
class QLabel;
class QLineEdit;
class QListWidget;
class QProgressBar;
class QPushButton;

namespace voxlocal {

class WelcomeWizard final : public QWizard
{
  Q_OBJECT

public:
  explicit WelcomeWizard(VoxLocalRuntime *runtime, QWidget *parent = nullptr);
  void closeForShutdown();

protected:
  void accept() override;
  void reject() override;
  void closeEvent(QCloseEvent *event) override;

private:
  struct LocalizedWidget {
    QWidget *widget = nullptr;
    QString english;
    QString turkish;
  };

  QString text(const char *english, const char *turkish) const;
  QString localizedOperation(const QString &operation) const;
  void localize(QWidget *widget, const char *english, const char *turkish);
  void retranslateUi();
  void buildPages();
  void updateModelProgress(qint64 received, qint64 total, const QString &fileName);
  void updateModelDownloadState(bool installing);

  VoxLocalRuntime *runtime_;
  QVector<LocalizedWidget> localizedWidgets_;
  QComboBox *interfaceLanguage_ = nullptr;
  QComboBox *modelStartup_ = nullptr;
  QLabel *modelInfo_ = nullptr;
  QLabel *modelStatus_ = nullptr;
  QProgressBar *modelProgress_ = nullptr;
  QPushButton *modelInstall_ = nullptr;
  QWidget *voiceImportPanel_ = nullptr;
  QLabel *voiceImportStatus_ = nullptr;
  QProgressBar *voiceImportProgress_ = nullptr;
  QListWidget *voiceImportSteps_ = nullptr;
  QPushButton *voiceBrowse_ = nullptr;
  QString voicePersonaId_;
  QString preparedVoicePath_;
  QLineEdit *channel_ = nullptr;
  QLineEdit *personaName_ = nullptr;
  QLineEdit *command_ = nullptr;
  QLineEdit *voice_ = nullptr;
  QComboBox *language_ = nullptr;
  QComboBox *access_ = nullptr;
  bool allowClose_ = false;
};

} // namespace voxlocal
