/****************************************************************************
** Meta object code from reading C++ file 'GlobeView3D.h'
**
** Created by: The Qt Meta Object Compiler version 69 (Qt 6.11.0)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../../../../../EarthView/earthview3d/include/earthview3d/GlobeView3D.h"
#include <QtCore/qmetatype.h>

#include <QtCore/qtmochelpers.h>

#include <memory>


#include <QtCore/qxptype_traits.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'GlobeView3D.h' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 69
#error "This file was generated using the moc from 6.11.0. It"
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
struct qt_meta_tag_ZN11earthview3d11GlobeView3DE_t {};
} // unnamed namespace

template <> constexpr inline auto earthview3d::GlobeView3D::qt_create_metaobjectdata<qt_meta_tag_ZN11earthview3d11GlobeView3DE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "earthview3d::GlobeView3D",
        "selectionChanged",
        "",
        "label",
        "entityPicked",
        "id",
        "cameraModeChanged",
        "earthview3d::CameraMode",
        "mode",
        "loadFailed",
        "reason"
    };

    QtMocHelpers::UintData qt_methods {
        // Signal 'selectionChanged'
        QtMocHelpers::SignalData<void(QString)>(1, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 3 },
        }}),
        // Signal 'entityPicked'
        QtMocHelpers::SignalData<void(quint64)>(4, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::ULongLong, 5 },
        }}),
        // Signal 'cameraModeChanged'
        QtMocHelpers::SignalData<void(earthview3d::CameraMode)>(6, 2, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 7, 8 },
        }}),
        // Signal 'loadFailed'
        QtMocHelpers::SignalData<void(QString)>(9, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 10 },
        }}),
    };
    QtMocHelpers::UintData qt_properties {
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<GlobeView3D, qt_meta_tag_ZN11earthview3d11GlobeView3DE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject earthview3d::GlobeView3D::staticMetaObject = { {
    QMetaObject::SuperData::link<QOpenGLWidget::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN11earthview3d11GlobeView3DE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN11earthview3d11GlobeView3DE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN11earthview3d11GlobeView3DE_t>.metaTypes,
    nullptr
} };

void earthview3d::GlobeView3D::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<GlobeView3D *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->selectionChanged((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1]))); break;
        case 1: _t->entityPicked((*reinterpret_cast<std::add_pointer_t<quint64>>(_a[1]))); break;
        case 2: _t->cameraModeChanged((*reinterpret_cast<std::add_pointer_t<earthview3d::CameraMode>>(_a[1]))); break;
        case 3: _t->loadFailed((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1]))); break;
        default: ;
        }
    }
    if (_c == QMetaObject::IndexOfMethod) {
        if (QtMocHelpers::indexOfMethod<void (GlobeView3D::*)(QString )>(_a, &GlobeView3D::selectionChanged, 0))
            return;
        if (QtMocHelpers::indexOfMethod<void (GlobeView3D::*)(quint64 )>(_a, &GlobeView3D::entityPicked, 1))
            return;
        if (QtMocHelpers::indexOfMethod<void (GlobeView3D::*)(earthview3d::CameraMode )>(_a, &GlobeView3D::cameraModeChanged, 2))
            return;
        if (QtMocHelpers::indexOfMethod<void (GlobeView3D::*)(QString )>(_a, &GlobeView3D::loadFailed, 3))
            return;
    }
}

const QMetaObject *earthview3d::GlobeView3D::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *earthview3d::GlobeView3D::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN11earthview3d11GlobeView3DE_t>.strings))
        return static_cast<void*>(this);
    return QOpenGLWidget::qt_metacast(_clname);
}

int earthview3d::GlobeView3D::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QOpenGLWidget::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 4)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 4;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 4)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 4;
    }
    return _id;
}

// SIGNAL 0
void earthview3d::GlobeView3D::selectionChanged(QString _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 0, nullptr, _t1);
}

// SIGNAL 1
void earthview3d::GlobeView3D::entityPicked(quint64 _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 1, nullptr, _t1);
}

// SIGNAL 2
void earthview3d::GlobeView3D::cameraModeChanged(earthview3d::CameraMode _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 2, nullptr, _t1);
}

// SIGNAL 3
void earthview3d::GlobeView3D::loadFailed(QString _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 3, nullptr, _t1);
}
QT_WARNING_POP
