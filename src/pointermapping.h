#pragma once

#include <QJsonObject>
#include <QPointF>
#include <QString>
#include <algorithm>
#include <cmath>
#include <optional>

// The mouse-input subset of VTube Studio's ParameterSettings. Face/camera
// inputs deliberately stay out of this importer.
struct PointerMapping {
    QString parameter;
    bool vertical = false;
    double inputLower = -1, inputUpper = 1, outputLower = -30, outputUpper = 30;
    bool clampInput = false, clampOutput = false;
    double smoothing = 0;

    static std::optional<PointerMapping> fromJson(const QJsonObject &json)
    {
        const auto input = json["Input"].toString();
        if (input != QLatin1String("MousePositionX") && input != QLatin1String("MousePositionY"))
            return std::nullopt;
        PointerMapping mapping;
        mapping.parameter = json["OutputLive2D"].toString();
        mapping.vertical = input == QLatin1String("MousePositionY");
        if (mapping.parameter.isEmpty())
            return std::nullopt;
        const char *keys[] = {"InputRangeLower", "InputRangeUpper", "OutputRangeLower", "OutputRangeUpper"};
        double *values[] = {&mapping.inputLower, &mapping.inputUpper, &mapping.outputLower,
                            &mapping.outputUpper};
        for (int i = 0; i < 4; ++i) {
            const auto value = json[keys[i]];
            if (!value.isDouble() || !std::isfinite(value.toDouble()))
                return std::nullopt;
            *values[i] = value.toDouble();
        }
        const double inputSpan = mapping.inputUpper - mapping.inputLower;
        if (!std::isfinite(inputSpan) || std::abs(inputSpan) < 1e-6 ||
            !std::isfinite(mapping.outputUpper - mapping.outputLower))
            return std::nullopt;
        mapping.clampInput = json["ClampInput"].toBool();
        mapping.clampOutput = json["ClampOutput"].toBool();
        mapping.smoothing = std::clamp(json["Smoothing"].toDouble(), 0.0, 100.0);
        if (!std::isfinite(mapping.target({-1, -1})) || !std::isfinite(mapping.target({1, 1})))
            return std::nullopt;
        return mapping;
    }

    double target(const QPointF &pointer) const
    {
        double input = vertical ? pointer.y() : pointer.x();
        if (clampInput)
            input = std::clamp(input, std::min(inputLower, inputUpper), std::max(inputLower, inputUpper));
        double output =
            outputLower + (input - inputLower) / (inputUpper - inputLower) * (outputUpper - outputLower);
        if (clampOutput)
            output =
                std::clamp(output, std::min(outputLower, outputUpper), std::max(outputLower, outputUpper));
        return output;
    }

    double smooth(double current, double target, double delta) const
    {
        // Interpret smoothing as a response time in 60 Hz frames. Exponential
        // interpolation gives the same response at capped and unlimited FPS;
        // this approximates VTS smoothing, rather than its proprietary filter.
        const double weight = smoothing == 0 ? 1 : -std::expm1(-std::max(0.0, delta) * 60 / smoothing);
        return current + (target - current) * weight;
    }
};
