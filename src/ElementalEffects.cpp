#include "ElementalEffects.h"

namespace ElementalEffects {
    static void EfeitoFogo(RE::Actor* a, ElementalGauges::Type, void*) { spdlog::info("reacao de fogo ativada"); }
    static void EfeitoGelo(RE::Actor* a, ElementalGauges::Type, void*) { spdlog::info("reacao de gelo ativada"); }
    static void EfeitoShock(RE::Actor* a, ElementalGauges::Type, void*) { spdlog::info("reacao de shock ativada"); }

    void ConfigurarGatilhos() {
        using Type = ElementalGauges::Type;

        ElementalGauges::FullTrigger fire{};
        fire.cb = &EfeitoFogo;
        fire.lockoutSeconds = 10.0f;
        fire.lockoutIsRealTime = false;
        fire.clearOnTrigger = true;
        fire.deferToTask = true;

        ElementalGauges::FullTrigger frost = fire;
        frost.cb = &EfeitoGelo;
        ElementalGauges::FullTrigger shock = fire;
        shock.cb = &EfeitoShock;

        ElementalGauges::SetOnFull(Type::Fire, fire);
        ElementalGauges::SetOnFull(Type::Frost, frost);
        ElementalGauges::SetOnFull(Type::Shock, shock);
    }
}