#pragma once
#ifndef EARTHVIEW3D_RUNTIME_H
#define EARTHVIEW3D_RUNTIME_H

#include "earthview3d/RuntimePaths.h"

namespace earthview3d {

/**
 * @brief Point OSG, GDAL and PROJ at their runtime files; call before the first GlobeView3D.
 *
 * Adds paths.osgPluginDir to osgDB's library path list (once) and applies the GDAL/PROJ
 * data environment (see RuntimePaths). Safe to call more than once; later calls add new
 * plugin folders only. If a host never calls it, the first GlobeView3D calls it with
 * RuntimePaths::buildTreeDefaults(). GUI thread only.
 */
void configureRuntime(const RuntimePaths& paths);

/// True once configureRuntime() has run (explicitly or from the first GlobeView3D).
bool isRuntimeConfigured();

} // namespace earthview3d

#endif // EARTHVIEW3D_RUNTIME_H
