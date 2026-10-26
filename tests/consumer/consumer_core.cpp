// earthview3d::core from an installed package: headers, link and a little logic.
#include <earthview3d/CameraControl.h>
#include <earthview3d/MapConfig.h>

#include <cstdio>

int main()
{
    earthview3d::CameraMode mode = earthview3d::CameraMode::Orbit;
    if (!earthview3d::parseCameraMode(QStringLiteral("stealth"), &mode)
        || mode != earthview3d::CameraMode::Stealth) {
        std::puts("consumer_core: parseCameraMode failed");
        return 1;
    }
    earthview3d::MapConfig config;
    QString error;
    if (config.validate(&error)) {
        std::puts("consumer_core: an empty MapConfig must not validate");
        return 1;
    }
    std::puts("consumer_core: OK");
    return 0;
}
