#pragma once

#include <QKeyEvent>
#include <QMap>
#include <QString>
#include <linux/input.h>

namespace InputKeys
{
inline const QMap<int, QString> &linuxNames()
{
    static const QMap<int, QString> names{{KEY_A, "A"},
                                          {KEY_B, "B"},
                                          {KEY_C, "C"},
                                          {KEY_D, "D"},
                                          {KEY_E, "E"},
                                          {KEY_F, "F"},
                                          {KEY_G, "G"},
                                          {KEY_H, "H"},
                                          {KEY_I, "I"},
                                          {KEY_J, "J"},
                                          {KEY_K, "K"},
                                          {KEY_L, "L"},
                                          {KEY_M, "M"},
                                          {KEY_N, "N"},
                                          {KEY_O, "O"},
                                          {KEY_P, "P"},
                                          {KEY_Q, "Q"},
                                          {KEY_R, "R"},
                                          {KEY_S, "S"},
                                          {KEY_T, "T"},
                                          {KEY_U, "U"},
                                          {KEY_V, "V"},
                                          {KEY_W, "W"},
                                          {KEY_X, "X"},
                                          {KEY_Y, "Y"},
                                          {KEY_Z, "Z"},
                                          {KEY_0, "N0"},
                                          {KEY_1, "N1"},
                                          {KEY_2, "N2"},
                                          {KEY_3, "N3"},
                                          {KEY_4, "N4"},
                                          {KEY_5, "N5"},
                                          {KEY_6, "N6"},
                                          {KEY_7, "N7"},
                                          {KEY_8, "N8"},
                                          {KEY_9, "N9"},
                                          {KEY_SPACE, "Space"},
                                          {KEY_ENTER, "Return"},
                                          {KEY_TAB, "Tab"},
                                          {KEY_ESC, "Escape"},
                                          {KEY_BACKSPACE, "Backspace"},
                                          {KEY_LEFTSHIFT, "LeftShift"},
                                          {KEY_RIGHTSHIFT, "RightShift"},
                                          {KEY_LEFTCTRL, "LeftControl"},
                                          {KEY_RIGHTCTRL, "RightControl"},
                                          {KEY_LEFTALT, "LeftAlt"},
                                          {KEY_RIGHTALT, "RightAlt"},
                                          {KEY_LEFT, "LeftArrow"},
                                          {KEY_RIGHT, "RightArrow"},
                                          {KEY_UP, "UpArrow"},
                                          {KEY_DOWN, "DownArrow"},
                                          {KEY_F1, "F1"},
                                          {KEY_F2, "F2"},
                                          {KEY_F3, "F3"},
                                          {KEY_F4, "F4"},
                                          {KEY_F5, "F5"},
                                          {KEY_F6, "F6"},
                                          {KEY_F7, "F7"},
                                          {KEY_F8, "F8"},
                                          {KEY_F9, "F9"},
                                          {KEY_F10, "F10"},
                                          {KEY_F11, "F11"},
                                          {KEY_F12, "F12"},
                                          {BTN_LEFT, "LeftMouseButton"},
                                          {BTN_RIGHT, "RightMouseButton"},
                                          {BTN_MIDDLE, "MiddleMouseButton"}};
    return names;
}

inline QString fromQt(const QKeyEvent *event)
{
    // Wayland/XKB native scan codes are evdev codes plus 8. Use physical keys
    // for letter bindings, even when a non-Latin keyboard layout is selected.
    if (event->nativeScanCode() >= 8) {
        const auto token = linuxNames().value(event->nativeScanCode() - 8);
        if (!token.isEmpty())
            return token;
    }
    const int key = event->key();
    if (key >= Qt::Key_A && key <= Qt::Key_Z)
        return QString(QChar('A' + key - Qt::Key_A));
    if (key >= Qt::Key_0 && key <= Qt::Key_9)
        return QStringLiteral("N%1").arg(key - Qt::Key_0);
    if (key >= Qt::Key_F1 && key <= Qt::Key_F12)
        return QStringLiteral("F%1").arg(key - Qt::Key_F1 + 1);
    static const QMap<int, QString> special{
        {Qt::Key_Space, "Space"},    {Qt::Key_Return, "Return"},    {Qt::Key_Enter, "Return"},
        {Qt::Key_Tab, "Tab"},        {Qt::Key_Escape, "Escape"},    {Qt::Key_Backspace, "Backspace"},
        {Qt::Key_Left, "LeftArrow"}, {Qt::Key_Right, "RightArrow"}, {Qt::Key_Up, "UpArrow"},
        {Qt::Key_Down, "DownArrow"}, {Qt::Key_Shift, "LeftShift"},  {Qt::Key_Control, "LeftControl"},
        {Qt::Key_Alt, "LeftAlt"}};
    return special.value(key);
}
} // namespace InputKeys
