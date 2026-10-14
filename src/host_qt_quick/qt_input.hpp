#pragma once

#include <QHoverEvent>
#include <QMouseEvent>
#include <QPointF>
#include <QWheelEvent>

namespace nekomote::qt_quick
{
// Item-local logical coordinates. Qt major differences stay at the host boundary.
inline QPointF input_position(const QMouseEvent& event)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    return event.position();
#else
    return event.localPos();
#endif
}
inline QPointF input_position(const QHoverEvent& event)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    return event.position();
#else
    return event.posF();
#endif
}
inline QPointF input_position(const QWheelEvent& event)
{
#if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)
    return event.position();
#else
    return event.posF();
#endif
}
} // namespace nekomote::qt_quick
