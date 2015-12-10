/****************************************************************************
** Meta object code from reading C++ file 'application.h'
**
** Created by: The Qt Meta Object Compiler version 63 (Qt 4.8.6)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "application.h"
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'application.h' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 63
#error "This file was generated using the moc from 4.8.6. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

QT_BEGIN_MOC_NAMESPACE
static const uint qt_meta_data_cw__Application[] = {

 // content:
       6,       // revision
       0,       // classname
       0,    0, // classinfo
      18,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       0,       // signalCount

 // slots: signature, parameters, type, tag, flags
      17,   16,   16,   16, 0x08,
      25,   16,   16,   16, 0x08,
      37,   16,   16,   16, 0x08,
      45,   16,   16,   16, 0x08,
      52,   16,   16,   16, 0x08,
      67,   16,   16,   16, 0x08,
      75,   16,   16,   16, 0x08,
      88,   16,   16,   16, 0x08,
     103,   16,   16,   16, 0x08,
     122,   16,   16,   16, 0x08,
     138,   16,   16,   16, 0x08,
     151,   16,   16,   16, 0x08,
     165,   16,   16,   16, 0x08,
     188,   16,   16,   16, 0x08,
     214,   16,   16,   16, 0x08,
     222,   16,   16,   16, 0x08,
     231,   16,   16,   16, 0x08,
     248,   16,   16,   16, 0x08,

       0        // eod
};

static const char qt_meta_stringdata_cw__Application[] = {
    "cw::Application\0\0about()\0startstop()\0"
    "start()\0stop()\0new_instance()\0clear()\0"
    "sync_speed()\0speed_change()\0"
    "frequency_change()\0volume_change()\0"
    "gap_change()\0mode_change()\0"
    "curtis_mode_b_change()\0adaptive_receive_change()\0"
    "fonts()\0colors()\0toggle_toolbar()\0"
    "poll_timer_event()\0"
};

void cw::Application::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    if (_c == QMetaObject::InvokeMetaMethod) {
        Q_ASSERT(staticMetaObject.cast(_o));
        Application *_t = static_cast<Application *>(_o);
        switch (_id) {
        case 0: _t->about(); break;
        case 1: _t->startstop(); break;
        case 2: _t->start(); break;
        case 3: _t->stop(); break;
        case 4: _t->new_instance(); break;
        case 5: _t->clear(); break;
        case 6: _t->sync_speed(); break;
        case 7: _t->speed_change(); break;
        case 8: _t->frequency_change(); break;
        case 9: _t->volume_change(); break;
        case 10: _t->gap_change(); break;
        case 11: _t->mode_change(); break;
        case 12: _t->curtis_mode_b_change(); break;
        case 13: _t->adaptive_receive_change(); break;
        case 14: _t->fonts(); break;
        case 15: _t->colors(); break;
        case 16: _t->toggle_toolbar(); break;
        case 17: _t->poll_timer_event(); break;
        default: ;
        }
    }
    Q_UNUSED(_a);
}

const QMetaObjectExtraData cw::Application::staticMetaObjectExtraData = {
    0,  qt_static_metacall 
};

const QMetaObject cw::Application::staticMetaObject = {
    { &QMainWindow::staticMetaObject, qt_meta_stringdata_cw__Application,
      qt_meta_data_cw__Application, &staticMetaObjectExtraData }
};

#ifdef Q_NO_DATA_RELOCATION
const QMetaObject &cw::Application::getStaticMetaObject() { return staticMetaObject; }
#endif //Q_NO_DATA_RELOCATION

const QMetaObject *cw::Application::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->metaObject : &staticMetaObject;
}

void *cw::Application::qt_metacast(const char *_clname)
{
    if (!_clname) return 0;
    if (!strcmp(_clname, qt_meta_stringdata_cw__Application))
        return static_cast<void*>(const_cast< Application*>(this));
    return QMainWindow::qt_metacast(_clname);
}

int cw::Application::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QMainWindow::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 18)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 18;
    }
    return _id;
}
QT_END_MOC_NAMESPACE
