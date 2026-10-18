#pragma once

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
    void draw(float deltaSeconds, const QSize &size, float zoom, const QPointF &gaze, bool followCursor,
              const QString &idleGroup);
    void tap(const QPointF &position);
    void setExpression(const QString &name);
    const QStringList &expressions() const;

  private:
    class Impl;
    std::unique_ptr<Impl> d;
};
