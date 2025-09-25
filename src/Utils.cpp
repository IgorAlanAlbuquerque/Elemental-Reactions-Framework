// THIS CODE IS INSPIRED BY THE CODE MADE BY ERSHin IN THE TRUEHUD REPOSITORY

#include "Utils.h"

static inline float kBoundFallbackRadius = 64.0f;

static bool GetHeadWorld(RE::Actor* a, RE::NiPoint3& out) {
    if (!a) return false;
    if (auto* root = a->Get3D2()) {
        const RE::NiAVObject* head = root->GetObjectByName(RE::BSFixedString("NPC Head [Head]"));
        if (head) {
            out = head->world.translate;
            return true;
        }
        const auto& b = root->worldBound;
        if (b.radius > 1e-4f) {
            out = b.center;
            return true;
        }
    }
    return false;
}

bool Utils::GetNodePosition(RE::ActorPtr a_actor, const char* a_nodeName, RE::NiPoint3& point) {
    bool ok = false;
    if (a_actor && a_nodeName && a_nodeName[0]) {
        if (RE::NiAVObject* root = a_actor->Get3D2()) {
            if (RE::NiAVObject* node = root->GetObjectByName(RE::BSFixedString(a_nodeName))) {
                point = node->world.translate;
                ok = true;
            }
        }
    }
    return ok;
}

bool Utils::GetTorsoPos(RE::ActorPtr a_actor, RE::NiPoint3& point) {
    if (!a_actor) return false;
    RE::TESRace* race = a_actor->GetRace();
    if (!race) return false;
    RE::BGSBodyPartData* bodyPartData = race->bodyPartData;
    if (!bodyPartData) return false;
    RE::BGSBodyPart* bodyPart = bodyPartData->parts[0];
    if (!bodyPart) return false;
    return GetNodePosition(a_actor, bodyPart->targetName.c_str(), point);
}

bool Utils::GetBoundTopPos(RE::Actor* a, RE::NiPoint3& out) {
    if (!a) return false;
    if (auto* root = a->Get3D(false)) {
        const auto& b = root->worldBound;
        const float r = (b.radius > 1e-4f) ? b.radius : kBoundFallbackRadius;
        out = {b.center.x, b.center.y, b.center.z + r};
        return true;
    }
    return false;
}

bool Utils::GetTargetPos(RE::ObjectRefHandle a_target, RE::NiPoint3& pos, bool bPreferBody) {
    auto target = a_target.get();
    if (!target) return false;
    if (target->Get3D2() == nullptr) return false;

    if (target->formType == RE::FormType::ActorCharacter) {
        RE::Actor* actorRaw = target->As<RE::Actor>();
        RE::ActorPtr actorPtr(actorRaw);

        if (bPreferBody) {
            if (GetTorsoPos(actorPtr, pos)) return true;
        }
        if (GetHeadWorld(actorRaw, pos)) return true;

        pos = target->GetLookingAtLocation();
        return true;
    }

    pos = target->GetPosition();
    return true;
}