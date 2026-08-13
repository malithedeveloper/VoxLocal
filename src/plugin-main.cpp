#include "voxlocal/runtime.hpp"
#include "voxlocal/voxlocal-dock.hpp"
#include "voxlocal/voxlocal-source.hpp"
#include "voxlocal/welcome-wizard.hpp"

#include <QAction>
#include <QCoreApplication>
#include <QDockWidget>
#include <QFileInfo>
#include <QKeySequence>
#include <QPointer>
#include <QSslSocket>
#include <QTimer>

#include <obs-frontend-api.h>
#include <obs-module.h>
#include <util/bmem.h>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("voxlocal", "en-US")

namespace {

voxlocal::VoxLocalRuntime *runtime = nullptr;
QPointer<voxlocal::VoxLocalDock> dock;
QPointer<voxlocal::WelcomeWizard> wizard;
QPointer<QAction> settingsAction;

void showVoxLocalDock()
{
  if (!dock)
    return;
  QWidget *container = dock;
  while (container && !qobject_cast<QDockWidget *>(container))
    container = container->parentWidget();
  if (container) {
    container->show();
    container->raise();
  }
  dock->show();
  dock->raise();
}

} // namespace

const char *obs_module_description(void)
{
  return "Local multilingual voice-cloned TTS for Kick chat with a native OBS overlay.";
}

bool obs_module_load(void)
{
#ifdef _WIN32
  const auto moduleBinaryPath = QString::fromUtf8(obs_get_module_binary_path(obs_current_module()));
  const QFileInfo moduleBinaryInfo(moduleBinaryPath);
  QCoreApplication::addLibraryPath(moduleBinaryInfo.isDir() ? moduleBinaryInfo.absoluteFilePath()
                                                            : moduleBinaryInfo.absolutePath());
  if (!QSslSocket::supportsSsl())
    blog(LOG_ERROR, "[VoxLocal] No functional Qt TLS backend is available");
#endif

  char *settingsPath = obs_module_config_path("settings.json");
  char *modelPath = obs_module_config_path("models");
  if (!settingsPath || !modelPath) {
    blog(LOG_ERROR, "[VoxLocal] Could not resolve the module configuration directory");
    bfree(settingsPath);
    bfree(modelPath);
    return false;
  }

  runtime = new voxlocal::VoxLocalRuntime(QString::fromUtf8(settingsPath), QString::fromUtf8(modelPath));
  bfree(settingsPath);
  bfree(modelPath);
  voxlocal::bindSourceRuntime(runtime);
  voxlocal::registerVoxLocalSource();

  auto *parent = static_cast<QWidget *>(obs_frontend_get_main_window());
  dock = new voxlocal::VoxLocalDock(runtime, parent);
  if (!obs_frontend_add_dock_by_id("voxlocal.controls", "VoxLocal", dock)) {
    blog(LOG_ERROR, "[VoxLocal] Could not add the VoxLocal dock");
    delete dock;
    dock = nullptr;
  }
  const bool turkish = runtime->settings().interfaceLanguage == voxlocal::InterfaceLanguage::Turkish;
  settingsAction =
      static_cast<QAction *>(obs_frontend_add_tools_menu_qaction(turkish ? "VoxLocal Ayarları" : "VoxLocal Settings"));
  if (settingsAction) {
    settingsAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+V")));
    settingsAction->setShortcutContext(Qt::ApplicationShortcut);
    parent->addAction(settingsAction);
    QObject::connect(settingsAction, &QAction::triggered, parent, [] { showVoxLocalDock(); });
    QObject::connect(runtime, &voxlocal::VoxLocalRuntime::settingsChanged, settingsAction, [] {
      if (settingsAction && runtime)
        settingsAction->setText(runtime->settings().interfaceLanguage == voxlocal::InterfaceLanguage::Turkish
                                    ? QStringLiteral("VoxLocal Ayarları")
                                    : QStringLiteral("VoxLocal Settings"));
    });
  }
  QTimer::singleShot(900, parent, [] { showVoxLocalDock(); });
  runtime->start();
  if (!runtime->settings().welcomeCompleted) {
    QTimer::singleShot(400, parent, [parent] {
      if (!runtime || runtime->settings().welcomeCompleted)
        return;
      wizard = new voxlocal::WelcomeWizard(runtime, parent);
      wizard->setAttribute(Qt::WA_DeleteOnClose);
      wizard->show();
      wizard->raise();
    });
  }
  blog(LOG_INFO, "[VoxLocal] plugin %s loaded", VOXLOCAL_VERSION);
  return true;
}

void obs_module_unload(void)
{
  if (wizard)
    wizard->close();
  obs_frontend_remove_dock("voxlocal.controls");
  if (dock)
    delete dock;
  dock = nullptr;
  if (settingsAction)
    delete settingsAction;
  settingsAction = nullptr;
  voxlocal::bindSourceRuntime(nullptr);
  delete runtime;
  runtime = nullptr;
  blog(LOG_INFO, "[VoxLocal] plugin unloaded");
}
