#pragma once

#include <QPointer>
#include <QQuickItem>
#include <QQuickWindow>
#include <QtQml/qqmlregistration.h>

// A separate surface: desktop containments must never be raised with a model.
class Live2DWindow : public QQuickWindow
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QQuickItem *anchorItem READ anchorItem WRITE setAnchorItem NOTIFY anchorItemChanged)
    Q_PROPERTY(bool clickThrough READ clickThrough WRITE setClickThrough NOTIFY clickThroughChanged)

  public:
    explicit Live2DWindow(QWindow *parent = nullptr);
    QQuickItem *anchorItem() const { return m_anchor; }
    void setAnchorItem(QQuickItem *item);
    bool clickThrough() const { return m_clickThrough; }
    void setClickThrough(bool enabled);
    Q_INVOKABLE void showAtAnchor();
    Q_INVOKABLE void beginMove();
    Q_INVOKABLE void moveFromStart(const QPointF &translation);

  Q_SIGNALS:
    void anchorItemChanged();
    void clickThroughChanged();
    void returnToDesktop();

  protected:
    bool event(QEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

  private:
    void placeAtAnchor();
    void applyPosition();
    void watchScreen(QScreen *screen);
    QPointer<QQuickItem> m_anchor;
    QPoint m_offset, m_moveStart;
    bool m_placed = false;
    bool m_clickThrough = false;
    QMetaObject::Connection m_screenGeometry;
};
