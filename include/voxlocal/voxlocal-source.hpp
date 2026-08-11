#pragma once

#include <QString>

namespace voxlocal {

class VoxLocalRuntime;

void registerVoxLocalSource();
void bindSourceRuntime(VoxLocalRuntime *runtime);
bool addOverlayToCurrentScene(QString *error = nullptr);

} // namespace voxlocal
