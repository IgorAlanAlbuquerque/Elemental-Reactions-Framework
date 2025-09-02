#pragma once

#include "RE/Skyrim.h"

namespace ElemStates {
    enum class ElementalState { Wet, Rubber, Fur, Fat };

    class ExtraElementalState : public RE::BSExtraData {
    public:
        inline static constexpr auto EXTRADATA_TYPE = static_cast<RE::ExtraDataType>(0x8000);

        bool wet = false;
        bool rubber = false;
        bool fur = false;
        bool fat = false;

        static ExtraElementalState* GetOrCreate(RE::Actor* actor) {
            if (!actor) return nullptr;

            auto xList = actor->extraList;
            auto existing = static_cast<ExtraElementalState*>(xList.GetByType(EXTRADATA_TYPE));
            if (existing) return existing;

            auto* extra = new ExtraElementalState();
            xList.Add(extra);
            return extra;
        }

        bool Get(ElementalState state) {
            switch (state) {
                case ElementalState::Wet:
                    return wet;
                case ElementalState::Rubber:
                    return rubber;
                case ElementalState::Fur:
                    return fur;
                case ElementalState::Fat:
                    return fat;
                default:
                    return false;
            }
        }

        void Set(ElementalState state, bool value) {
            switch (state) {
                case ElementalState::Wet:
                    wet = value;
                    break;
                case ElementalState::Rubber:
                    rubber = value;
                    break;
                case ElementalState::Fur:
                    fur = value;
                    break;
                case ElementalState::Fat:
                    fat = value;
                    break;
                default:
                    break;
            }
        }

        virtual RE::ExtraDataType GetType() const override;
    };

    inline bool GetState(RE::Actor* actor, ElementalState state) {
        if (auto* data = ExtraElementalState::GetOrCreate(actor)) {
            return data->Get(state);
        }
        return false;
    }

    inline void SetState(RE::Actor* actor, ElementalState state, bool value) {
        if (auto* data = ExtraElementalState::GetOrCreate(actor)) {
            data->Set(state, value);
        }
    }
}
