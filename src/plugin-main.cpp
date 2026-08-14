#include "voxlocal/runtime.hpp"
#include "voxlocal/voxlocal-dock.hpp"
#include "voxlocal/voxlocal-source.hpp"
#include "voxlocal/welcome-wizard.hpp"

#include <QAction>
#include <QCoreApplication>
#include <QDockWidget>
#include <QFileInfo>
#include <QKeySequence>
#include <QMessageBox>
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
bool frontendReady = false;
bool startupHandled = false;
bool modelDecisionHandled = false;
bool frontendShuttingDown = false;

QWidget *dockContainer()
{
  QWidget *container = dock;
  while (container && !qobject_cast<QDockWidget *>(container))
    container = container->parentWidget();
  return container;
}

void hideVoxLocalDock()
{
  if (auto *container = dockContainer())
    container->hide();
  if (dock)
    dock->hide();
}

void showVoxLocalDock()
{
  if (!dock)
    return;
  QWidget *container = dockContainer();
  if (container) {
    container->show();
    container->raise();
  }
  dock->show();
  dock->raise();
}

void handleModelStartup()
{
  if (!runtime || !frontendReady || !runtime->settings().welcomeCompleted || runtime->engineReady() ||
      runtime->engineLoading() || !runtime->modelManager()->isInstalled() || modelDecisionHandled)
    return;
  modelDecisionHandled = true;
  switch (runtime->settings().modelStartupBehavior) {
  case voxlocal::ModelStartupBehavior::AlwaysLoad:
    runtime->loadModel();
    return;
  case voxlocal::ModelStartupBehavior::NeverLoad:
    return;
  case voxlocal::ModelStartupBehavior::Ask:
    break;
  }

  const bool turkish = runtime->settings().interfaceLanguage == voxlocal::InterfaceLanguage::Turkish;
  auto *parent = static_cast<QWidget *>(obs_frontend_get_main_window());
  const auto answer = QMessageBox::question(
      parent, QStringLiteral("VoxLocal"),
      turkish ? QStringLiteral("TTS modeli bu OBS oturumunda belleğe açılsın mı? Model arka planda açılır.")
              : QStringLiteral("Load the TTS model into memory for this OBS session? It loads in the background."),
      QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
  if (answer == QMessageBox::Yes)
    runtime->loadModel();
}

void showWelcomeWizard()
{
  if (!runtime || wizard)
    return;
  auto *parent = static_cast<QWidget *>(obs_frontend_get_main_window());
  hideVoxLocalDock();
  wizard = new voxlocal::WelcomeWizard(runtime, parent);
  wizard->setAttribute(Qt::WA_DeleteOnClose);
  QObject::connect(wizard, &QWizard::accepted, parent, [] {
    if (!runtime)
      return;
    runtime->start();
    showVoxLocalDock();
    QTimer::singleShot(250, runtime, [] { handleModelStartup(); });
  });
  wizard->show();
  wizard->raise();
  wizard->activateWindow();
}

void handleFrontendReady()
{
  if (!runtime || startupHandled)
    return;
  frontendReady = true;
  startupHandled = true;
  runtime->start();
  if (!runtime->settings().welcomeCompleted) {
    showWelcomeWizard();
    return;
  }
  showVoxLocalDock();
  QTimer::singleShot(250, runtime, [] { handleModelStartup(); });
}

void frontendEvent(enum obs_frontend_event event, void *)
{
  if (event == OBS_FRONTEND_EVENT_EXIT) {
    frontendShuttingDown = true;
    return;
  }
  if (event != OBS_FRONTEND_EVENT_FINISHED_LOADING)
    return;
  auto *parent = static_cast<QWidget *>(obs_frontend_get_main_window());
  QTimer::singleShot(0, parent, [] { handleFrontendReady(); });
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
  QObject::connect(runtime, &voxlocal::VoxLocalRuntime::errorOccurred, parent, [](const QString &error) {
    const auto message = error.toUtf8();
    blog(LOG_ERROR, "[VoxLocal] %s", message.constData());
  });
  QObject::connect(runtime, &voxlocal::VoxLocalRuntime::statusChanged, parent, [](const QString &status) {
    if (status.startsWith(QStringLiteral("ready:"))) {
      const auto message = status.section(QLatin1Char(':'), 1).toUtf8();
      blog(LOG_INFO, "[VoxLocal] TTS model ready on %s", message.constData());
    }
  });
  dock = new voxlocal::VoxLocalDock(runtime, parent);
  if (!obs_frontend_add_dock_by_id("voxlocal.controls", "VoxLocal", dock)) {
    blog(LOG_ERROR, "[VoxLocal] Could not add the VoxLocal dock");
    delete dock;
    dock = nullptr;
  }
  hideVoxLocalDock();
  const bool turkish = runtime->settings().interfaceLanguage == voxlocal::InterfaceLanguage::Turkish;
  settingsAction =
      static_cast<QAction *>(obs_frontend_add_tools_menu_qaction(turkish ? "VoxLocal Ayarları" : "VoxLocal Settings"));
  if (settingsAction) {
    settingsAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+V")));
    settingsAction->setShortcutContext(Qt::ApplicationShortcut);
    parent->addAction(settingsAction);
    QObject::connect(settingsAction, &QAction::triggered, parent, [] {
      if (runtime && !runtime->settings().welcomeCompleted) {
        showWelcomeWizard();
        if (wizard) {
          wizard->raise();
          wizard->activateWindow();
        }
        return;
      }
      showVoxLocalDock();
    });
    QObject::connect(runtime, &voxlocal::VoxLocalRuntime::settingsChanged, settingsAction, [] {
      if (settingsAction && runtime)
        settingsAction->setText(runtime->settings().interfaceLanguage == voxlocal::InterfaceLanguage::Turkish
                                    ? QStringLiteral("VoxLocal Ayarları")
                                    : QStringLiteral("VoxLocal Settings"));
    });
  }
  QObject::connect(runtime->modelManager(), &voxlocal::ModelManager::ready, parent,
                   [] { QTimer::singleShot(0, runtime, [] { handleModelStartup(); }); });
  obs_frontend_add_event_callback(frontendEvent, nullptr);
  blog(LOG_INFO, "[VoxLocal] plugin %s loaded", VOXLOCAL_VERSION);
  return true;
}

void obs_module_unload(void)
{
  if (!frontendShuttingDown)
    obs_frontend_remove_event_callback(frontendEvent, nullptr);
  if (wizard)
    wizard->closeForShutdown();
  if (!frontendShuttingDown)
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
  frontendReady = false;
  startupHandled = false;
  modelDecisionHandled = false;
  frontendShuttingDown = false;
  blog(LOG_INFO, "[VoxLocal] plugin unloaded");
}
