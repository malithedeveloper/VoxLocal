#include "voxlocal/voxlocal-source.hpp"

#include "voxlocal/runtime.hpp"

#include <QJsonDocument>

#include <obs-frontend-api.h>
#include <obs-module.h>
#include <util/platform.h>

#include <algorithm>
#include <mutex>
#include <vector>

namespace voxlocal {
namespace {

struct SourceData {
  obs_source_t *context = nullptr;
  obs_source_t *browser = nullptr;
  uint32_t width = 960;
  uint32_t height = 260;
  float age = 0.0F;
  bool initialEventSent = false;
  bool active = false;
};

std::mutex sourcesMutex;
std::vector<SourceData *> sources;
VoxLocalRuntime *runtime = nullptr;

void dispatch(SourceData *source, const QJsonObject &event)
{
  if (!source || !source->browser)
    return;
  auto *handler = obs_source_get_proc_handler(source->browser);
  if (!handler)
    return;
  const auto json = QJsonDocument(event).toJson(QJsonDocument::Compact);
  calldata_t parameters;
  calldata_init(&parameters);
  calldata_set_string(&parameters, "eventName", "voxlocal");
  calldata_set_string(&parameters, "jsonString", json.constData());
  proc_handler_call(handler, "javascript_event", &parameters);
  calldata_free(&parameters);
}

QJsonObject configurationEvent()
{
  if (!runtime)
    return {{QStringLiteral("type"), QStringLiteral("configure")}};
  const auto &overlay = runtime->settings().overlay;
  return {{QStringLiteral("type"), QStringLiteral("configure")},
          {QStringLiteral("preset"), overlay.preset},
          {QStringLiteral("fontFamily"), overlay.fontFamily},
          {QStringLiteral("background"), overlay.background},
          {QStringLiteral("foreground"), overlay.foreground},
          {QStringLiteral("fallbackNameColor"), overlay.fallbackNameColor},
          {QStringLiteral("entranceAnimation"), overlay.entranceAnimation},
          {QStringLiteral("borderRadius"), overlay.borderRadius},
          {QStringLiteral("visibleMilliseconds"), overlay.visibleMilliseconds},
          {QStringLiteral("showName"), overlay.showName}};
}

void broadcast(const QJsonObject &event)
{
  std::scoped_lock lock(sourcesMutex);
  for (auto *source : sources)
    dispatch(source, event);
}

void outputAudio(const TtsAudio &audio)
{
  std::scoped_lock lock(sourcesMutex);
  auto found = std::ranges::find_if(sources, [](const SourceData *source) { return source->active; });
  if (found == sources.end() && !sources.empty())
    found = sources.begin();
  if (found == sources.end() || audio.samples.empty())
    return;
  const auto *source = *found;
  if (obs_source_get_monitoring_type(source->context) == OBS_MONITORING_TYPE_NONE)
    obs_source_set_monitoring_type(source->context, OBS_MONITORING_TYPE_MONITOR_AND_OUTPUT);
  constexpr std::size_t chunkFrames = 1024;
  const auto baseTimestamp = os_gettime_ns();
  for (std::size_t offset = 0; offset < audio.samples.size(); offset += chunkFrames) {
    const auto frames = std::min(chunkFrames, audio.samples.size() - offset);
    obs_source_audio output{};
    output.data[0] = reinterpret_cast<const uint8_t *>(audio.samples.data() + offset);
    output.frames = static_cast<uint32_t>(frames);
    output.speakers = SPEAKERS_MONO;
    output.format = AUDIO_FORMAT_FLOAT;
    output.samples_per_sec = static_cast<uint32_t>(audio.sampleRate);
    output.timestamp = baseTimestamp + offset * 1000000000ULL / static_cast<std::size_t>(audio.sampleRate);
    obs_source_output_audio(source->context, &output);
  }
}

const char *sourceName(void *) { return obs_module_text("VoxLocalOverlay"); }

void update(void *data, obs_data_t *settings)
{
  auto *source = static_cast<SourceData *>(data);
  source->width = static_cast<uint32_t>(obs_data_get_int(settings, "width"));
  source->height = static_cast<uint32_t>(obs_data_get_int(settings, "height"));
  source->width = std::clamp(source->width, 160U, 7680U);
  source->height = std::clamp(source->height, 80U, 4320U);
  if (!source->browser)
    return;
  obs_data_t *browserSettings = obs_source_get_settings(source->browser);
  obs_data_set_int(browserSettings, "width", source->width);
  obs_data_set_int(browserSettings, "height", source->height);
  obs_source_update(source->browser, browserSettings);
  obs_data_release(browserSettings);
  source->initialEventSent = false;
  source->age = 0.0F;
}

void *create(obs_data_t *settings, obs_source_t *context)
{
  auto *source = new SourceData;
  source->context = context;
  source->width = static_cast<uint32_t>(obs_data_get_int(settings, "width"));
  source->height = static_cast<uint32_t>(obs_data_get_int(settings, "height"));
  if (source->width < 160)
    source->width = 960;
  if (source->height < 80)
    source->height = 260;
  obs_source_set_monitoring_type(context, OBS_MONITORING_TYPE_MONITOR_AND_OUTPUT);

  char *file = obs_module_file("overlay/index.html");
  obs_data_t *browserSettings = obs_data_create();
  obs_data_set_bool(browserSettings, "is_local_file", true);
  obs_data_set_string(browserSettings, "local_file", file ? file : "");
  obs_data_set_int(browserSettings, "width", source->width);
  obs_data_set_int(browserSettings, "height", source->height);
  obs_data_set_bool(browserSettings, "shutdown", false);
  obs_data_set_bool(browserSettings, "restart_when_active", false);
  source->browser = obs_source_create_private("browser_source", "VoxLocal Internal Overlay", browserSettings);
  obs_data_release(browserSettings);
  bfree(file);
  if (!source->browser)
    blog(LOG_ERROR, "[VoxLocal] obs-browser is unavailable; install the official OBS Browser Source component");
  {
    std::scoped_lock lock(sourcesMutex);
    sources.push_back(source);
  }
  return source;
}

void destroy(void *data)
{
  auto *source = static_cast<SourceData *>(data);
  {
    std::scoped_lock lock(sourcesMutex);
    std::erase(sources, source);
  }
  if (source->active && source->browser)
    obs_source_remove_active_child(source->context, source->browser);
  if (source->browser)
    obs_source_release(source->browser);
  delete source;
}

void defaults(obs_data_t *settings)
{
  obs_data_set_default_int(settings, "width", runtime ? runtime->settings().overlay.width : 960);
  obs_data_set_default_int(settings, "height", runtime ? runtime->settings().overlay.height : 260);
}

obs_properties_t *properties(void *)
{
  auto *properties = obs_properties_create();
  obs_properties_add_int(properties, "width", obs_module_text("Width"), 160, 7680, 1);
  obs_properties_add_int(properties, "height", obs_module_text("Height"), 80, 4320, 1);
  return properties;
}

uint32_t width(void *data) { return static_cast<SourceData *>(data)->width; }
uint32_t height(void *data) { return static_cast<SourceData *>(data)->height; }

void activate(void *data)
{
  auto *source = static_cast<SourceData *>(data);
  source->active = true;
  if (source->browser)
    obs_source_add_active_child(source->context, source->browser);
}

void deactivate(void *data)
{
  auto *source = static_cast<SourceData *>(data);
  source->active = false;
  if (source->browser)
    obs_source_remove_active_child(source->context, source->browser);
}

void tick(void *data, float seconds)
{
  auto *source = static_cast<SourceData *>(data);
  source->age += seconds;
  if (!source->initialEventSent && source->age >= 1.0F) {
    dispatch(source, configurationEvent());
    source->initialEventSent = true;
  }
}

void render(void *data, gs_effect_t *)
{
  const auto *source = static_cast<SourceData *>(data);
  if (source->browser)
    obs_source_video_render(source->browser);
}

void enumerate(void *data, obs_source_enum_proc_t callback, void *parameter)
{
  const auto *source = static_cast<SourceData *>(data);
  if (source->browser)
    callback(source->context, source->browser, parameter);
}

} // namespace

void registerVoxLocalSource()
{
  obs_source_info info{};
  info.id = "voxlocal_overlay";
  info.type = OBS_SOURCE_TYPE_INPUT;
  info.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_AUDIO | OBS_SOURCE_CUSTOM_DRAW;
  info.get_name = sourceName;
  info.create = create;
  info.destroy = destroy;
  info.get_width = width;
  info.get_height = height;
  info.get_defaults = defaults;
  info.get_properties = properties;
  info.update = update;
  info.activate = activate;
  info.deactivate = deactivate;
  info.video_tick = tick;
  info.video_render = render;
  info.enum_active_sources = enumerate;
  obs_register_source(&info);
}

void bindSourceRuntime(VoxLocalRuntime *value)
{
  runtime = value;
  if (!runtime)
    return;
  QObject::connect(runtime, &VoxLocalRuntime::overlayEvent, runtime,
                   [](const QJsonObject &event) { broadcast(event); });
  QObject::connect(runtime, &VoxLocalRuntime::audioProduced, runtime,
                   [](const TtsAudio &audio) { outputAudio(audio); });
  QObject::connect(runtime, &VoxLocalRuntime::settingsChanged, runtime, [] { broadcast(configurationEvent()); });
}

bool addOverlayToCurrentScene(QString *error)
{
  obs_data_t *browserDefaults = obs_get_source_defaults("browser_source");
  if (!browserDefaults) {
    if (error)
      *error =
          QStringLiteral("OBS Browser Source is not installed. Install the official Browser Source component first.");
    return false;
  }
  obs_data_release(browserDefaults);
  obs_source_t *current = obs_frontend_get_current_scene();
  if (!current) {
    if (error)
      *error = QStringLiteral("No active OBS scene was found.");
    return false;
  }
  obs_scene_t *scene = obs_scene_from_source(current);
  if (!scene) {
    obs_source_release(current);
    if (error)
      *error = QStringLiteral("The active OBS source is not a scene.");
    return false;
  }
  obs_source_t *source = obs_source_create("voxlocal_overlay", "VoxLocal Overlay", nullptr, nullptr);
  if (!source) {
    obs_source_release(current);
    if (error)
      *error = QStringLiteral("Could not create the VoxLocal Overlay source.");
    return false;
  }
  obs_scene_add(scene, source);
  obs_source_release(source);
  obs_source_release(current);
  return true;
}

} // namespace voxlocal
