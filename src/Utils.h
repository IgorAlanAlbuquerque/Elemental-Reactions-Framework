#pragma once
#include "RE/Skyrim.h"

namespace Utils {
    bool GetNodePosition(RE::ActorPtr a_actor, const char* a_nodeName, RE::NiPoint3& point);
    bool GetTorsoPos(RE::ActorPtr a_actor, RE::NiPoint3& point);
    bool GetBoundTopPos(RE::Actor* a, RE::NiPoint3& out);
    bool GetTargetPos(RE::ObjectRefHandle a_target, RE::NiPoint3& pos, bool bPreferBody);
}