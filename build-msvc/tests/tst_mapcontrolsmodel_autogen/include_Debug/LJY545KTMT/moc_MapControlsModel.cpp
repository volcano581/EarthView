/****************************************************************************
** Meta object code from reading C++ file 'MapControlsModel.h'
**
** Created by: The Qt Meta Object Compiler version 69 (Qt 6.11.1)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../../../../../mapcontrols/MapControlsModel.h"
#include <QtCore/qmetatype.h>

#include <QtCore/qtmochelpers.h>

#include <memory>


#include <QtCore/qxptype_traits.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'MapControlsModel.h' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 69
#error "This file was generated using the moc from 6.11.1. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

#ifndef Q_CONSTINIT
#define Q_CONSTINIT
#endif

QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
QT_WARNING_DISABLE_GCC("-Wuseless-cast")
namespace {
struct qt_meta_tag_ZN16MapControlsModelE_t {};
} // unnamed namespace

template <> constexpr inline auto MapControlsModel::qt_create_metaobjectdata<qt_meta_tag_ZN16MapControlsModelE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "MapControlsModel",
        "orthographicChanged",
        "",
        "orthographic",
        "bearingDegreesChanged",
        "degrees",
        "globe3DAvailableChanged",
        "available",
        "measureAvailableChanged",
        "settingsOpenChanged",
        "open",
        "layersOpenChanged",
        "measureOpenChanged",
        "baseMapsChanged",
        "currentBaseMapChanged",
        "index",
        "hillshadeVisibleChanged",
        "visible",
        "citiesVisibleChanged",
        "bordersVisibleChanged",
        "gridVisibleChanged",
        "featureLayersChanged",
        "hiddenFeatureLayersChanged",
        "layers",
        "currentThemeChanged",
        "id",
        "thumbnailRevisionChanged",
        "panelMaxHeightChanged",
        "selectedFeatureChanged",
        "northResetRequested",
        "toolTipRequested",
        "text",
        "QPointF",
        "globalPos",
        "toolTipHideRequested",
        "closePanels",
        "toggleFeatureLayer",
        "isFeatureLayerVisible",
        "clearSelectedFeature",
        "resetNorth",
        "toggleSettings",
        "toggleLayers",
        "toggleMeasure",
        "showToolTip",
        "globalX",
        "globalY",
        "hideToolTip",
        "bearingDegrees",
        "globe3DAvailable",
        "measureAvailable",
        "settingsOpen",
        "layersOpen",
        "measureOpen",
        "baseMaps",
        "QVariantList",
        "currentBaseMap",
        "hillshadeVisible",
        "citiesVisible",
        "bordersVisible",
        "gridVisible",
        "featureLayers",
        "featureLayersAvailable",
        "hiddenFeatureLayers",
        "themes",
        "currentTheme",
        "thumbnailRevision",
        "panelMaxHeight",
        "selectedFeature",
        "QVariantMap",
        "featureSelected"
    };

    QtMocHelpers::UintData qt_methods {
        // Signal 'orthographicChanged'
        QtMocHelpers::SignalData<void(bool)>(1, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Bool, 3 },
        }}),
        // Signal 'bearingDegreesChanged'
        QtMocHelpers::SignalData<void(double)>(4, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Double, 5 },
        }}),
        // Signal 'globe3DAvailableChanged'
        QtMocHelpers::SignalData<void(bool)>(6, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Bool, 7 },
        }}),
        // Signal 'measureAvailableChanged'
        QtMocHelpers::SignalData<void(bool)>(8, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Bool, 7 },
        }}),
        // Signal 'settingsOpenChanged'
        QtMocHelpers::SignalData<void(bool)>(9, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Bool, 10 },
        }}),
        // Signal 'layersOpenChanged'
        QtMocHelpers::SignalData<void(bool)>(11, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Bool, 10 },
        }}),
        // Signal 'measureOpenChanged'
        QtMocHelpers::SignalData<void(bool)>(12, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Bool, 10 },
        }}),
        // Signal 'baseMapsChanged'
        QtMocHelpers::SignalData<void()>(13, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'currentBaseMapChanged'
        QtMocHelpers::SignalData<void(int)>(14, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Int, 15 },
        }}),
        // Signal 'hillshadeVisibleChanged'
        QtMocHelpers::SignalData<void(bool)>(16, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Bool, 17 },
        }}),
        // Signal 'citiesVisibleChanged'
        QtMocHelpers::SignalData<void(bool)>(18, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Bool, 17 },
        }}),
        // Signal 'bordersVisibleChanged'
        QtMocHelpers::SignalData<void(bool)>(19, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Bool, 17 },
        }}),
        // Signal 'gridVisibleChanged'
        QtMocHelpers::SignalData<void(bool)>(20, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Bool, 17 },
        }}),
        // Signal 'featureLayersChanged'
        QtMocHelpers::SignalData<void()>(21, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'hiddenFeatureLayersChanged'
        QtMocHelpers::SignalData<void(const QStringList &)>(22, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QStringList, 23 },
        }}),
        // Signal 'currentThemeChanged'
        QtMocHelpers::SignalData<void(const QString &)>(24, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 25 },
        }}),
        // Signal 'thumbnailRevisionChanged'
        QtMocHelpers::SignalData<void()>(26, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'panelMaxHeightChanged'
        QtMocHelpers::SignalData<void()>(27, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'selectedFeatureChanged'
        QtMocHelpers::SignalData<void()>(28, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'northResetRequested'
        QtMocHelpers::SignalData<void()>(29, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'toolTipRequested'
        QtMocHelpers::SignalData<void(const QString &, const QPointF &)>(30, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 31 }, { 0x80000000 | 32, 33 },
        }}),
        // Signal 'toolTipHideRequested'
        QtMocHelpers::SignalData<void()>(34, 2, QMC::AccessPublic, QMetaType::Void),
        // Method 'closePanels'
        QtMocHelpers::MethodData<void()>(35, 2, QMC::AccessPublic, QMetaType::Void),
        // Method 'toggleFeatureLayer'
        QtMocHelpers::MethodData<void(const QString &)>(36, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 25 },
        }}),
        // Method 'isFeatureLayerVisible'
        QtMocHelpers::MethodData<bool(const QString &) const>(37, 2, QMC::AccessPublic, QMetaType::Bool, {{
            { QMetaType::QString, 25 },
        }}),
        // Method 'clearSelectedFeature'
        QtMocHelpers::MethodData<void()>(38, 2, QMC::AccessPublic, QMetaType::Void),
        // Method 'resetNorth'
        QtMocHelpers::MethodData<void()>(39, 2, QMC::AccessPublic, QMetaType::Void),
        // Method 'toggleSettings'
        QtMocHelpers::MethodData<void()>(40, 2, QMC::AccessPublic, QMetaType::Void),
        // Method 'toggleLayers'
        QtMocHelpers::MethodData<void()>(41, 2, QMC::AccessPublic, QMetaType::Void),
        // Method 'toggleMeasure'
        QtMocHelpers::MethodData<void()>(42, 2, QMC::AccessPublic, QMetaType::Void),
        // Method 'showToolTip'
        QtMocHelpers::MethodData<void(const QString &, double, double)>(43, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 31 }, { QMetaType::Double, 44 }, { QMetaType::Double, 45 },
        }}),
        // Method 'hideToolTip'
        QtMocHelpers::MethodData<void()>(46, 2, QMC::AccessPublic, QMetaType::Void),
    };
    QtMocHelpers::UintData qt_properties {
        // property 'orthographic'
        QtMocHelpers::PropertyData<bool>(3, QMetaType::Bool, QMC::DefaultPropertyFlags | QMC::Writable | QMC::StdCppSet, 0),
        // property 'bearingDegrees'
        QtMocHelpers::PropertyData<double>(47, QMetaType::Double, QMC::DefaultPropertyFlags | QMC::Writable | QMC::StdCppSet, 1),
        // property 'globe3DAvailable'
        QtMocHelpers::PropertyData<bool>(48, QMetaType::Bool, QMC::DefaultPropertyFlags | QMC::Writable | QMC::StdCppSet, 2),
        // property 'measureAvailable'
        QtMocHelpers::PropertyData<bool>(49, QMetaType::Bool, QMC::DefaultPropertyFlags | QMC::Writable | QMC::StdCppSet, 3),
        // property 'settingsOpen'
        QtMocHelpers::PropertyData<bool>(50, QMetaType::Bool, QMC::DefaultPropertyFlags | QMC::Writable | QMC::StdCppSet, 4),
        // property 'layersOpen'
        QtMocHelpers::PropertyData<bool>(51, QMetaType::Bool, QMC::DefaultPropertyFlags | QMC::Writable | QMC::StdCppSet, 5),
        // property 'measureOpen'
        QtMocHelpers::PropertyData<bool>(52, QMetaType::Bool, QMC::DefaultPropertyFlags | QMC::Writable | QMC::StdCppSet, 6),
        // property 'baseMaps'
        QtMocHelpers::PropertyData<QVariantList>(53, 0x80000000 | 54, QMC::DefaultPropertyFlags | QMC::Writable | QMC::EnumOrFlag | QMC::StdCppSet, 7),
        // property 'currentBaseMap'
        QtMocHelpers::PropertyData<int>(55, QMetaType::Int, QMC::DefaultPropertyFlags | QMC::Writable | QMC::StdCppSet, 8),
        // property 'hillshadeVisible'
        QtMocHelpers::PropertyData<bool>(56, QMetaType::Bool, QMC::DefaultPropertyFlags | QMC::Writable | QMC::StdCppSet, 9),
        // property 'citiesVisible'
        QtMocHelpers::PropertyData<bool>(57, QMetaType::Bool, QMC::DefaultPropertyFlags | QMC::Writable | QMC::StdCppSet, 10),
        // property 'bordersVisible'
        QtMocHelpers::PropertyData<bool>(58, QMetaType::Bool, QMC::DefaultPropertyFlags | QMC::Writable | QMC::StdCppSet, 11),
        // property 'gridVisible'
        QtMocHelpers::PropertyData<bool>(59, QMetaType::Bool, QMC::DefaultPropertyFlags | QMC::Writable | QMC::StdCppSet, 12),
        // property 'featureLayers'
        QtMocHelpers::PropertyData<QVariantList>(60, 0x80000000 | 54, QMC::DefaultPropertyFlags | QMC::EnumOrFlag, 13),
        // property 'featureLayersAvailable'
        QtMocHelpers::PropertyData<bool>(61, QMetaType::Bool, QMC::DefaultPropertyFlags, 13),
        // property 'hiddenFeatureLayers'
        QtMocHelpers::PropertyData<QStringList>(62, QMetaType::QStringList, QMC::DefaultPropertyFlags | QMC::Writable | QMC::StdCppSet, 14),
        // property 'themes'
        QtMocHelpers::PropertyData<QVariantList>(63, 0x80000000 | 54, QMC::DefaultPropertyFlags | QMC::EnumOrFlag | QMC::Constant),
        // property 'currentTheme'
        QtMocHelpers::PropertyData<QString>(64, QMetaType::QString, QMC::DefaultPropertyFlags | QMC::Writable | QMC::StdCppSet, 15),
        // property 'thumbnailRevision'
        QtMocHelpers::PropertyData<int>(65, QMetaType::Int, QMC::DefaultPropertyFlags, 16),
        // property 'panelMaxHeight'
        QtMocHelpers::PropertyData<int>(66, QMetaType::Int, QMC::DefaultPropertyFlags | QMC::Writable | QMC::StdCppSet, 17),
        // property 'selectedFeature'
        QtMocHelpers::PropertyData<QVariantMap>(67, 0x80000000 | 68, QMC::DefaultPropertyFlags | QMC::EnumOrFlag, 18),
        // property 'featureSelected'
        QtMocHelpers::PropertyData<bool>(69, QMetaType::Bool, QMC::DefaultPropertyFlags, 18),
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<MapControlsModel, qt_meta_tag_ZN16MapControlsModelE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject MapControlsModel::staticMetaObject = { {
    QMetaObject::SuperData::link<QObject::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN16MapControlsModelE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN16MapControlsModelE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN16MapControlsModelE_t>.metaTypes,
    nullptr
} };

void MapControlsModel::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<MapControlsModel *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->orthographicChanged((*reinterpret_cast<std::add_pointer_t<bool>>(_a[1]))); break;
        case 1: _t->bearingDegreesChanged((*reinterpret_cast<std::add_pointer_t<double>>(_a[1]))); break;
        case 2: _t->globe3DAvailableChanged((*reinterpret_cast<std::add_pointer_t<bool>>(_a[1]))); break;
        case 3: _t->measureAvailableChanged((*reinterpret_cast<std::add_pointer_t<bool>>(_a[1]))); break;
        case 4: _t->settingsOpenChanged((*reinterpret_cast<std::add_pointer_t<bool>>(_a[1]))); break;
        case 5: _t->layersOpenChanged((*reinterpret_cast<std::add_pointer_t<bool>>(_a[1]))); break;
        case 6: _t->measureOpenChanged((*reinterpret_cast<std::add_pointer_t<bool>>(_a[1]))); break;
        case 7: _t->baseMapsChanged(); break;
        case 8: _t->currentBaseMapChanged((*reinterpret_cast<std::add_pointer_t<int>>(_a[1]))); break;
        case 9: _t->hillshadeVisibleChanged((*reinterpret_cast<std::add_pointer_t<bool>>(_a[1]))); break;
        case 10: _t->citiesVisibleChanged((*reinterpret_cast<std::add_pointer_t<bool>>(_a[1]))); break;
        case 11: _t->bordersVisibleChanged((*reinterpret_cast<std::add_pointer_t<bool>>(_a[1]))); break;
        case 12: _t->gridVisibleChanged((*reinterpret_cast<std::add_pointer_t<bool>>(_a[1]))); break;
        case 13: _t->featureLayersChanged(); break;
        case 14: _t->hiddenFeatureLayersChanged((*reinterpret_cast<std::add_pointer_t<QStringList>>(_a[1]))); break;
        case 15: _t->currentThemeChanged((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1]))); break;
        case 16: _t->thumbnailRevisionChanged(); break;
        case 17: _t->panelMaxHeightChanged(); break;
        case 18: _t->selectedFeatureChanged(); break;
        case 19: _t->northResetRequested(); break;
        case 20: _t->toolTipRequested((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QPointF>>(_a[2]))); break;
        case 21: _t->toolTipHideRequested(); break;
        case 22: _t->closePanels(); break;
        case 23: _t->toggleFeatureLayer((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1]))); break;
        case 24: { bool _r = _t->isFeatureLayerVisible((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])));
            if (_a[0]) *reinterpret_cast<bool*>(_a[0]) = std::move(_r); }  break;
        case 25: _t->clearSelectedFeature(); break;
        case 26: _t->resetNorth(); break;
        case 27: _t->toggleSettings(); break;
        case 28: _t->toggleLayers(); break;
        case 29: _t->toggleMeasure(); break;
        case 30: _t->showToolTip((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<double>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<double>>(_a[3]))); break;
        case 31: _t->hideToolTip(); break;
        default: ;
        }
    }
    if (_c == QMetaObject::IndexOfMethod) {
        if (QtMocHelpers::indexOfMethod<void (MapControlsModel::*)(bool )>(_a, &MapControlsModel::orthographicChanged, 0))
            return;
        if (QtMocHelpers::indexOfMethod<void (MapControlsModel::*)(double )>(_a, &MapControlsModel::bearingDegreesChanged, 1))
            return;
        if (QtMocHelpers::indexOfMethod<void (MapControlsModel::*)(bool )>(_a, &MapControlsModel::globe3DAvailableChanged, 2))
            return;
        if (QtMocHelpers::indexOfMethod<void (MapControlsModel::*)(bool )>(_a, &MapControlsModel::measureAvailableChanged, 3))
            return;
        if (QtMocHelpers::indexOfMethod<void (MapControlsModel::*)(bool )>(_a, &MapControlsModel::settingsOpenChanged, 4))
            return;
        if (QtMocHelpers::indexOfMethod<void (MapControlsModel::*)(bool )>(_a, &MapControlsModel::layersOpenChanged, 5))
            return;
        if (QtMocHelpers::indexOfMethod<void (MapControlsModel::*)(bool )>(_a, &MapControlsModel::measureOpenChanged, 6))
            return;
        if (QtMocHelpers::indexOfMethod<void (MapControlsModel::*)()>(_a, &MapControlsModel::baseMapsChanged, 7))
            return;
        if (QtMocHelpers::indexOfMethod<void (MapControlsModel::*)(int )>(_a, &MapControlsModel::currentBaseMapChanged, 8))
            return;
        if (QtMocHelpers::indexOfMethod<void (MapControlsModel::*)(bool )>(_a, &MapControlsModel::hillshadeVisibleChanged, 9))
            return;
        if (QtMocHelpers::indexOfMethod<void (MapControlsModel::*)(bool )>(_a, &MapControlsModel::citiesVisibleChanged, 10))
            return;
        if (QtMocHelpers::indexOfMethod<void (MapControlsModel::*)(bool )>(_a, &MapControlsModel::bordersVisibleChanged, 11))
            return;
        if (QtMocHelpers::indexOfMethod<void (MapControlsModel::*)(bool )>(_a, &MapControlsModel::gridVisibleChanged, 12))
            return;
        if (QtMocHelpers::indexOfMethod<void (MapControlsModel::*)()>(_a, &MapControlsModel::featureLayersChanged, 13))
            return;
        if (QtMocHelpers::indexOfMethod<void (MapControlsModel::*)(const QStringList & )>(_a, &MapControlsModel::hiddenFeatureLayersChanged, 14))
            return;
        if (QtMocHelpers::indexOfMethod<void (MapControlsModel::*)(const QString & )>(_a, &MapControlsModel::currentThemeChanged, 15))
            return;
        if (QtMocHelpers::indexOfMethod<void (MapControlsModel::*)()>(_a, &MapControlsModel::thumbnailRevisionChanged, 16))
            return;
        if (QtMocHelpers::indexOfMethod<void (MapControlsModel::*)()>(_a, &MapControlsModel::panelMaxHeightChanged, 17))
            return;
        if (QtMocHelpers::indexOfMethod<void (MapControlsModel::*)()>(_a, &MapControlsModel::selectedFeatureChanged, 18))
            return;
        if (QtMocHelpers::indexOfMethod<void (MapControlsModel::*)()>(_a, &MapControlsModel::northResetRequested, 19))
            return;
        if (QtMocHelpers::indexOfMethod<void (MapControlsModel::*)(const QString & , const QPointF & )>(_a, &MapControlsModel::toolTipRequested, 20))
            return;
        if (QtMocHelpers::indexOfMethod<void (MapControlsModel::*)()>(_a, &MapControlsModel::toolTipHideRequested, 21))
            return;
    }
    if (_c == QMetaObject::ReadProperty) {
        void *_v = _a[0];
        switch (_id) {
        case 0: *reinterpret_cast<bool*>(_v) = _t->isOrthographic(); break;
        case 1: *reinterpret_cast<double*>(_v) = _t->bearingDegrees(); break;
        case 2: *reinterpret_cast<bool*>(_v) = _t->isGlobe3DAvailable(); break;
        case 3: *reinterpret_cast<bool*>(_v) = _t->isMeasureAvailable(); break;
        case 4: *reinterpret_cast<bool*>(_v) = _t->isSettingsOpen(); break;
        case 5: *reinterpret_cast<bool*>(_v) = _t->isLayersOpen(); break;
        case 6: *reinterpret_cast<bool*>(_v) = _t->isMeasureOpen(); break;
        case 7: *reinterpret_cast<QVariantList*>(_v) = _t->baseMaps(); break;
        case 8: *reinterpret_cast<int*>(_v) = _t->currentBaseMap(); break;
        case 9: *reinterpret_cast<bool*>(_v) = _t->isHillshadeVisible(); break;
        case 10: *reinterpret_cast<bool*>(_v) = _t->areCitiesVisible(); break;
        case 11: *reinterpret_cast<bool*>(_v) = _t->areBordersVisible(); break;
        case 12: *reinterpret_cast<bool*>(_v) = _t->isGridVisible(); break;
        case 13: *reinterpret_cast<QVariantList*>(_v) = _t->featureLayers(); break;
        case 14: *reinterpret_cast<bool*>(_v) = _t->areFeatureLayersAvailable(); break;
        case 15: *reinterpret_cast<QStringList*>(_v) = _t->hiddenFeatureLayers(); break;
        case 16: *reinterpret_cast<QVariantList*>(_v) = _t->themes(); break;
        case 17: *reinterpret_cast<QString*>(_v) = _t->currentTheme(); break;
        case 18: *reinterpret_cast<int*>(_v) = _t->thumbnailRevision(); break;
        case 19: *reinterpret_cast<int*>(_v) = _t->panelMaxHeight(); break;
        case 20: *reinterpret_cast<QVariantMap*>(_v) = _t->selectedFeature(); break;
        case 21: *reinterpret_cast<bool*>(_v) = _t->isFeatureSelected(); break;
        default: break;
        }
    }
    if (_c == QMetaObject::WriteProperty) {
        void *_v = _a[0];
        switch (_id) {
        case 0: _t->setOrthographic(*reinterpret_cast<bool*>(_v)); break;
        case 1: _t->setBearingDegrees(*reinterpret_cast<double*>(_v)); break;
        case 2: _t->setGlobe3DAvailable(*reinterpret_cast<bool*>(_v)); break;
        case 3: _t->setMeasureAvailable(*reinterpret_cast<bool*>(_v)); break;
        case 4: _t->setSettingsOpen(*reinterpret_cast<bool*>(_v)); break;
        case 5: _t->setLayersOpen(*reinterpret_cast<bool*>(_v)); break;
        case 6: _t->setMeasureOpen(*reinterpret_cast<bool*>(_v)); break;
        case 7: _t->setBaseMaps(*reinterpret_cast<QVariantList*>(_v)); break;
        case 8: _t->setCurrentBaseMap(*reinterpret_cast<int*>(_v)); break;
        case 9: _t->setHillshadeVisible(*reinterpret_cast<bool*>(_v)); break;
        case 10: _t->setCitiesVisible(*reinterpret_cast<bool*>(_v)); break;
        case 11: _t->setBordersVisible(*reinterpret_cast<bool*>(_v)); break;
        case 12: _t->setGridVisible(*reinterpret_cast<bool*>(_v)); break;
        case 15: _t->setHiddenFeatureLayers(*reinterpret_cast<QStringList*>(_v)); break;
        case 17: _t->setCurrentTheme(*reinterpret_cast<QString*>(_v)); break;
        case 19: _t->setPanelMaxHeight(*reinterpret_cast<int*>(_v)); break;
        default: break;
        }
    }
}

const QMetaObject *MapControlsModel::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *MapControlsModel::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN16MapControlsModelE_t>.strings))
        return static_cast<void*>(this);
    return QObject::qt_metacast(_clname);
}

int MapControlsModel::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QObject::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 32)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 32;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 32)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 32;
    }
    if (_c == QMetaObject::ReadProperty || _c == QMetaObject::WriteProperty
            || _c == QMetaObject::ResetProperty || _c == QMetaObject::BindableProperty
            || _c == QMetaObject::RegisterPropertyMetaType) {
        qt_static_metacall(this, _c, _id, _a);
        _id -= 22;
    }
    return _id;
}

// SIGNAL 0
void MapControlsModel::orthographicChanged(bool _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 0, nullptr, _t1);
}

// SIGNAL 1
void MapControlsModel::bearingDegreesChanged(double _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 1, nullptr, _t1);
}

// SIGNAL 2
void MapControlsModel::globe3DAvailableChanged(bool _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 2, nullptr, _t1);
}

// SIGNAL 3
void MapControlsModel::measureAvailableChanged(bool _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 3, nullptr, _t1);
}

// SIGNAL 4
void MapControlsModel::settingsOpenChanged(bool _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 4, nullptr, _t1);
}

// SIGNAL 5
void MapControlsModel::layersOpenChanged(bool _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 5, nullptr, _t1);
}

// SIGNAL 6
void MapControlsModel::measureOpenChanged(bool _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 6, nullptr, _t1);
}

// SIGNAL 7
void MapControlsModel::baseMapsChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 7, nullptr);
}

// SIGNAL 8
void MapControlsModel::currentBaseMapChanged(int _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 8, nullptr, _t1);
}

// SIGNAL 9
void MapControlsModel::hillshadeVisibleChanged(bool _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 9, nullptr, _t1);
}

// SIGNAL 10
void MapControlsModel::citiesVisibleChanged(bool _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 10, nullptr, _t1);
}

// SIGNAL 11
void MapControlsModel::bordersVisibleChanged(bool _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 11, nullptr, _t1);
}

// SIGNAL 12
void MapControlsModel::gridVisibleChanged(bool _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 12, nullptr, _t1);
}

// SIGNAL 13
void MapControlsModel::featureLayersChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 13, nullptr);
}

// SIGNAL 14
void MapControlsModel::hiddenFeatureLayersChanged(const QStringList & _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 14, nullptr, _t1);
}

// SIGNAL 15
void MapControlsModel::currentThemeChanged(const QString & _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 15, nullptr, _t1);
}

// SIGNAL 16
void MapControlsModel::thumbnailRevisionChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 16, nullptr);
}

// SIGNAL 17
void MapControlsModel::panelMaxHeightChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 17, nullptr);
}

// SIGNAL 18
void MapControlsModel::selectedFeatureChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 18, nullptr);
}

// SIGNAL 19
void MapControlsModel::northResetRequested()
{
    QMetaObject::activate(this, &staticMetaObject, 19, nullptr);
}

// SIGNAL 20
void MapControlsModel::toolTipRequested(const QString & _t1, const QPointF & _t2)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 20, nullptr, _t1, _t2);
}

// SIGNAL 21
void MapControlsModel::toolTipHideRequested()
{
    QMetaObject::activate(this, &staticMetaObject, 21, nullptr);
}
QT_WARNING_POP
