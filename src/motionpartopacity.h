#pragma once

#include <Id/CubismIdManager.hpp>
#include <Model/CubismModel.hpp>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>

// Cubism motions write PartOpacity into parameter slots for CubismPose to
// consume. Models without a pose3.json need those values applied to their
// actual parts instead. Register only parts explicitly controlled by a motion.
class MotionPartOpacity
{
  public:
    void addMotion(Csm::CubismModel *model, const QByteArray &json)
    {
        const auto curves = QJsonDocument::fromJson(json).object()["Curves"].toArray();
        for (const auto &value : curves) {
            const auto curve = value.toObject();
            if (curve["Target"].toString() != QLatin1String("PartOpacity"))
                continue;
            const auto name = curve["Id"].toString().toUtf8();
            if (name.isEmpty())
                continue;
            const auto id = Csm::CubismFramework::GetIdManager()->GetId(name.constData());
            const auto part = model->GetPartIndex(id);
            if (part >= 0 && part < model->GetPartCount())
                m_parts.insert(part);
        }
    }

    void apply(Csm::CubismModel *model) const
    {
        for (const auto part : m_parts)
            model->SetPartOpacity(part, model->GetParameterValue(model->GetPartId(part)));
    }

  private:
    QSet<int> m_parts;
};
