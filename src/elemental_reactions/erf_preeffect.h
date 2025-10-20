#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "RE/Skyrim.h"
#include "erf_element.h"

using ERF_PreEffectHandle = std::uint16_t;

struct ERF_PreEffectDesc {
    std::string name;
    ERF_ElementHandle element = 0;
    std::uint8_t minGauge = 1;

    float durationSeconds = 2.0f;
    float cooldownSeconds = 0.5f;
    float baseIntensity = 0.0f;
    float scalePerPoint = 0.0f;
    float minIntensity = 0.0f;
    float maxIntensity = 1.0f;

    bool durationIsRealTime = true;
    bool cooldownIsRealTime = true;

    using Callback = void (*)(RE::Actor*, ERF_ElementHandle, std::uint8_t, float, void*);
    Callback cb = nullptr;
    void* user = nullptr;
};

class PreEffectRegistry {
public:
    static PreEffectRegistry& get();

    ERF_PreEffectHandle registerPreEffect(const ERF_PreEffectDesc& d);
    const ERF_PreEffectDesc* get(ERF_PreEffectHandle h) const;

    std::span<const ERF_PreEffectHandle> listByElement(ERF_ElementHandle h) const;

private:
    PreEffectRegistry() = default;
    std::vector<ERF_PreEffectDesc> _effects;
    std::vector<std::vector<ERF_PreEffectHandle>> _byElem;
};
