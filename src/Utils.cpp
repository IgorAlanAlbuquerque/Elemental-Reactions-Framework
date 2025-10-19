// THIS CODE IS INSPIRED BY THE CODE MADE BY ERSHin IN THE TRUEHUD REPOSITORY

#include "Utils.h"

#include <unordered_map>

namespace {
    // BSFixedString estático: evita recriar "NPC Head [Head]" a cada frame
    static const RE::BSFixedString kHeadName{"NPC Head [Head]"};
    // Raio padrão para bounds “magros”
    inline constexpr float kBoundFallbackRadius = 64.0f;

    // Cache fraco do nó de cabeça por ator (key = FormID).
    // Sem locks: Utils é usado pelo thread de UI no seu HUD; se usar noutro contexto, podemos migrar p/ StripedMap.
    static std::unordered_map<RE::FormID, RE::NiAVObject*> g_headCache;

    inline RE::NiAVObject* FindHeadNode(RE::NiAVObject* root) noexcept {
        return root ? root->GetObjectByName(kHeadName) : nullptr;
    }

    // Retorna o nó de cabeça cacheado (se ainda válido) ou busca e atualiza o cache.
    inline RE::NiAVObject* GetHeadNodeFast_(RE::Actor* a) {
        if (!a) return nullptr;
        const RE::FormID key = a->GetFormID();
        if (auto it = g_headCache.find(key); it != g_headCache.end()) {
            // validação fraca: ainda anexado na árvore?
            if (auto* n = it->second; n && n->parent) return n;
            g_headCache.erase(it);
        }
        RE::NiAVObject* root = a->Get3D2();  // não força carregamento
        if (!root) return nullptr;
        if (RE::NiAVObject* head = FindHeadNode(root)) {
            g_headCache.emplace(key, head);
            return head;
        }
        return nullptr;
    }
}

// Posição da cabeça com cache; cai p/ bound center quando não achar
bool Utils::GetHeadPosFast(RE::Actor* a, RE::NiPoint3& out) {
    if (!a) return false;
    if (RE::NiAVObject* head = GetHeadNodeFast_(a)) {
        out = head->world.translate;
        return true;
    }
    if (RE::NiAVObject* root = a->Get3D2()) {
        const auto& b = root->worldBound;
        if (b.radius > 1e-4f) {
            out = b.center;
            return true;
        }
    }
    return false;
}

bool Utils::GetNodePosition(RE::ActorPtr a_actor, const char* a_nodeName, RE::NiPoint3& point) {
    if (!a_actor) return false;
    if (!a_nodeName || a_nodeName[0] == '\0') return false;
    RE::NiAVObject* root = a_actor->Get3D2();
    if (!root) return false;
    // evita churn: instância única por nome em escopo local (função)
    static thread_local RE::BSFixedString s_name;
    s_name = a_nodeName;  // BSFixedString mantém interning; atribuição é barata
    if (RE::NiAVObject* node = root->GetObjectByName(s_name)) {
        point = node->world.translate;
        return true;
    }
    return false;
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
        if (Utils::GetHeadPosFast(actorRaw, pos)) return true;
        if (Utils::GetBoundTopPos(actorRaw, pos)) return true;

        pos = target->GetLookingAtLocation();
        return true;
    }

    pos = target->GetPosition();
    return true;
}