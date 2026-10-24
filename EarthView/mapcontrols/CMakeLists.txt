# On-map controls (QML side bar over MapWidget). Opt-in for hosts: link earthview::mapcontrols
# and create a MapControlsOverlay per MapWidget. earthview_core is unchanged and does not
# depend on Qt Quick.
add_library(earthview_mapcontrols STATIC
    BaseMapSources.cpp
    BaseMapSources.h
    FeatureDetails.cpp
    FeatureDetails.h
    GeoMeasure.cpp
    GeoMeasure.h
    LayerImages.cpp
    LayerImages.h
    MapControlsModel.cpp
    MapControlsModel.h
    MapControlsOverlay.cpp
    MapControlsOverlay.h
    MeasureModel.cpp
    MeasureModel.h
    SmoothNavigation.cpp
    SmoothNavigation.h
    SmoothNavigator.cpp
    SmoothNavigator.h
)
add_library(earthview::mapcontrols ALIAS earthview_mapcontrols)

target_include_directories(earthview_mapcontrols
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
)

target_link_libraries(earthview_mapcontrols
    PUBLIC
        earthview::core
        Qt6::Qml
        Qt6::Quick
        Qt6::QuickWidgets
    PRIVATE
        # Tile pictures (qml/icons) are drawn with QSvgRenderer for the layer chooser; linking it
        # also makes windeployqt ship the SVG image plugin.
        Qt6::Svg
)

# QML and icons are compiled into the binary (no files to deploy next to the exe).
qt_add_resources(earthview_mapcontrols "earthview_mapcontrols_qml"
    PREFIX "/earthview/mapcontrols"
    BASE "qml"
    FILES
        qml/MapSideBar.qml
        qml/LayersButton.qml
        qml/LayerTile.qml
        qml/FeatureCard.qml
        qml/icons/compass.svg
        qml/icons/globe3d.svg
        qml/icons/ruler.svg
        qml/icons/settings.svg
        qml/icons/proj_mercator.svg
        qml/icons/proj_orthographic.svg
        qml/icons/thumb_osm.svg
        qml/icons/thumb_imagery.svg
        qml/icons/thumb_hillshade.svg
        qml/icons/thumb_topo.svg
        qml/icons/thumb_map.svg
        qml/icons/overlay_grid.svg
)

# windeployqt scans this folder for QML imports (QtQuick).
set(EARTHVIEW_MAPCONTROLS_QML_DIR "${CMAKE_CURRENT_SOURCE_DIR}/qml" CACHE INTERNAL "Map controls QML directory")
