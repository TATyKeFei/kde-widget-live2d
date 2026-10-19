#pragma once

#include <QJsonArray>
#include <QPointF>
#include <QSize>
#include <QStringList>
#include <QUrl>
#include <memory>

// Cubism Framework state belongs to one renderer helper process.
void acquireCubism(void *shareGroup, bool compatibleContext);
void releaseCubism();

class CubismModel
{
  public:
    CubismModel(const QUrl &source, const QSize &size);
    ~CubismModel();
    void draw(float deltaSeconds, const QSize &size, float zoom, const QPointF &gaze, const QPointF &pointer,
              bool followCursor, const QString &idleGroup);
    void tap(const QPointF &position);
    void setExpression(const QString &name);
    void setExpressionActive(const QString &name, bool active);
    void playMotion(const QString &group, int index = -1);
    QStringList motionGroups() const;
    const QJsonArray &hotkeys() const;
    const QStringList &expressions() const;

  private:
    class Impl;
    std::unique_ptr<Impl> d;
};
