#include "InjectHUD.h"

#include <algorithm>
#include <memory>
#include <string_view>

#include "../elemental_reactions/ElementalGauges.h"
#include "Offsets.h"
#include "Utils.h"

namespace InjectHUD {
    TRUEHUD_API::IVTrueHUD4* g_trueHUD = nullptr;
    SKSE::PluginHandle g_pluginHandle = static_cast<SKSE::PluginHandle>(-1);
    std::unordered_map<RE::FormID, HUDEntry> widgets{};
    std::unordered_map<RE::FormID, std::vector<ActiveReactionHUD>> combos;
    std::deque<PendingReaction> g_comboQueue;
    std::mutex g_comboMx;
}

namespace {
    using namespace InjectHUD;

    inline float NowHours() { return RE::Calendar::GetSingleton()->GetHoursPassed(); }

    // Drena a fila de "BeginReaction" para a UI e materializa ActiveReactionHUD.
    static void DrainComboQueueOnUI() {
        std::deque<PendingReaction> take;
        {
            std::scoped_lock lk(g_comboMx);
            take.swap(g_comboQueue);
        }
        if (take.empty()) return;

        const double nowRt = NowRtS();
        const float nowH = NowHours();

        auto& RR = ReactionRegistry::get();

        for (auto& pr : take) {
            RE::Actor* a = pr.handle ? pr.handle.get().get() : nullptr;
            if (a) {
                InjectHUD::AddFor(a);  // garante pelo menos slot 0
            }

            ActiveReactionHUD hud{};
            hud.reaction = pr.reaction;
            hud.realTime = pr.realTime;
            hud.durationS = pr.secs;

            if (pr.realTime) {
                hud.endRtS = nowRt + pr.secs;
            } else {
                hud.endH = nowH + (pr.secs / 3600.0f);
            }

            // HUD: puxa do registro da reação
            if (const auto* rd = RR.get(pr.reaction)) {
                hud.iconPath = rd->hud.iconPath;  // << novo
                hud.tint = rd->hud.iconTint;
            } else {
                // fallback defensivo (um dds qualquer)
                hud.iconPath = "img://textures/erf/icons/icon_fire.dds";
                hud.tint = 0xFFFFFF;
            }

            auto& vec = combos[pr.id];
            // remove expirados antes de inserir (evita “fantasmas”)
            std::erase_if(vec, [nowRt, nowH](const ActiveReactionHUD& c) {
                return c.realTime ? (nowRt >= c.endRtS) : (nowH >= c.endH);
            });
            vec.push_back(std::move(hud));

            spdlog::info("[InjectHUD] DrainComboQueueOnUI id={:08X} reaction={} secs={} rt={} total={}", pr.id,
                         static_cast<unsigned>(pr.reaction), pr.secs, pr.realTime, vec.size());
        }
    }

    // Filtra reações ainda vivas para o ator.
    static std::vector<ActiveReactionHUD> GetActiveReactions(RE::FormID id) {
        std::vector<ActiveReactionHUD> out;
        const auto it = combos.find(id);
        if (it == combos.end()) {
            spdlog::debug("[InjectHUD] GetActiveReactions id={:08X} -> none", id);
            return out;
        }

        const double nowRt = NowRtS();
        const float nowH = RE::Calendar::GetSingleton()->GetHoursPassed();

        for (const auto& c : it->second) {
            const bool alive = c.realTime ? (nowRt < c.endRtS) : (nowH < c.endH);
            if (alive) out.push_back(c);
        }
        if (out.size() > 3) out.resize(3);  // limite visual

        spdlog::info("[InjectHUD] GetActiveReactions id={:08X} -> {}", id, out.size());
        return out;
    }

    // ---------------- Position helpers ----------------
    inline bool IsPlayerActor(RE::Actor* a) {
        spdlog::info("entrou no isPlayer");
        auto* pc = RE::PlayerCharacter::GetSingleton();
        if (a && pc) spdlog::info("eh o player? {}", a == pc);
        return a && pc && (a == pc);
    }

    static int CountAlive(const std::vector<WidgetPtr>& v) {
        int n = 0;
        for (auto& p : v)
            if (p) ++n;
        return n;
    }

    static int LowestFreeSlot(const std::vector<InjectHUD::WidgetPtr>& v, int maxSlots) {
        for (int s = 0; s < maxSlots; ++s) {
            if (s >= (int)v.size() || !v[s]) return s;
        }
        return -1;
    }

    static void CompactVisualPosAfterRemoval(std::vector<InjectHUD::WidgetPtr>& v, int removedPos) {
        for (auto& p : v) {
            if (p && p->_pos > removedPos) --p->_pos;
        }
    }

    static void RemoveAtSlot(std::vector<InjectHUD::WidgetPtr>& v, RE::FormID id, int slot) {
        if (slot < 0 || slot >= (int)v.size() || !v[slot]) return;
        const auto wid = InjectHUD::MakeWidgetID(id, slot);
        ::InjectHUD::g_trueHUD->RemoveWidget(::InjectHUD::g_pluginHandle, ERF_WIDGET_TYPE, wid,
                                             TRUEHUD_API::WidgetRemovalMode::Immediate);
        const int removedPos = v[slot]->_pos;
        v[slot].reset();
        spdlog::info("[InjectHUD] RemoveAtSlot id={:08X} slot{} removedPos={}", id, slot, removedPos);
        CompactVisualPosAfterRemoval(v, removedPos);
    }

    // ---------------- Player fixed layout ----------------
    void FollowPlayerFixed(InjectHUD::ERFWidget& w) {
        if (!w._view) {
            spdlog::warn("[InjectHUD] FollowPlayerFixed: _view=null (slot={}, pos={})", w._slot, w._pos);
            return;
        }

        RE::GRectF rect = w._view->GetVisibleFrameRect();
        const double baseX = rect.left + InjectHUD::ERFWidget::kPlayerMarginLeftPx;
        const double baseY = rect.bottom - InjectHUD::ERFWidget::kPlayerMarginBottomPx;
        const double targetX = baseX + double(w._pos) * InjectHUD::ERFWidget::kSlotSpacingPx;
        const double targetY = baseY;

        if (w._needsSnap || std::isnan(w._lastX) || std::isnan(w._lastY)) {
            spdlog::debug("[InjectHUD] FollowPlayerFixed: snapping slot={} pos={}", w._slot, w._pos);
            w._lastX = targetX;
            w._lastY = targetY;
            w._needsSnap = false;
        }

        // arredonda para pixel inteiro (evita blur)
        const double px = std::floor(targetX + 0.5);
        const double py = std::floor(targetY + 0.5);

        // COMMIT ATÔMICO: posição + escala de uma vez
        RE::GFxValue::DisplayInfo di;
        di.SetPosition(static_cast<float>(px), static_cast<float>(py));
        di.SetScale(100.0f * InjectHUD::ERFWidget::kPlayerScale, 100.0f * InjectHUD::ERFWidget::kPlayerScale);
        w._object.SetDisplayInfo(di);

        // visibilidade
        RE::GFxValue vis;
        vis.SetBoolean(true);
        w._object.SetMember("_visible", vis);

        // cache
        w._lastX = px;
        w._lastY = py;

        spdlog::debug("[InjectHUD] FollowPlayerFixed: final ({}, {}) scale={} slot={} pos={}", w._lastX, w._lastY,
                      InjectHUD::ERFWidget::kPlayerScale, w._slot, w._pos);
    }
}

// ====================== ERFWidget ======================
void InjectHUD::ERFWidget::Initialize() {
    spdlog::info("[InjectHUD] ERFWidget::Initialize slot={} this={}", _slot, fmt::ptr(this));
    if (!_view) {
        spdlog::warn("[InjectHUD] Initialize: _view=null slot={}", _slot);
        return;
    }
    RE::GFxValue vis, alpha;
    vis.SetBoolean(false);
    alpha.SetNumber(100.0);
    _object.SetMember("_visible", vis);
    _object.SetMember("_alpha", alpha);

    _needsSnap = true;
    ResetSmoothing();
    _hadContent = false;
    _lastGaugeRtS = std::numeric_limits<double>::quiet_NaN();
}

void InjectHUD::ERFWidget::FollowActorHead(RE::Actor* actor) {
    if (!actor || !_view) return;
    if (IsPlayerActor(actor)) {
        FollowPlayerFixed(*this);
        return;
    }

    if (!g_viewPort || !g_worldToCamMatrix) {
        RE::GFxValue vis;
        vis.SetBoolean(false);
        _object.SetMember("_visible", vis);
        return;
    }

    // === TrueHUD-style anchor: torso + Z em MUNDO ===
    RE::NiPoint3 world{};
    // bGetTorsoPos = true  -> ancora no torso/body-part como o TrueHUD
    if (!Utils::GetTargetPos(actor->CreateRefHandle(), world, /*bGetTorsoPos=*/true)) {
        RE::GFxValue vis;
        vis.SetBoolean(false);
        _object.SetMember("_visible", vis);
        return;
    }

    // Ajuste de altura em MUNDO (como o TrueHUD faz com fInfoBarOffsetZ)
    // Diminua esse valor se o ícone estiver "alto" demais; valores típicos 10–25.
    static constexpr float kWorldOffsetZ = 70.0f;  // ajuste fino a gosto
    world.z += kWorldOffsetZ;

    // Projeção
    float nx = 0.f, ny = 0.f, depth = 0.f;
    RE::NiCamera::WorldPtToScreenPt3((float (*)[4])g_worldToCamMatrix, *g_viewPort, world, nx, ny, depth, 1e-5f);
    if (depth < 0.f || nx < 0.f || nx > 1.f || ny < 0.f || ny > 1.f) {
        RE::GFxValue vis;
        vis.SetBoolean(false);
        _object.SetMember("_visible", vis);
        return;
    }

    // Rect visível do SWF
    const RE::GRectF rect = _view->GetVisibleFrameRect();
    const float stageW = rect.right - rect.left;
    const float stageH = rect.bottom - rect.top;
    if (stageW <= 1.f || stageH <= 1.f) return;

    // Normalizado -> px (flip Y, igual TrueHUD)
    ny = 1.0f - ny;
    double px = rect.left + stageW * nx;
    double py = rect.top + stageH * ny;

    // Slots em X (ordem visual) – mantenha como você já faz
    px += double(_pos) * ERFWidget::kSlotSpacingPx - 40.0;

    // (Opcional) remova/zerar kTopOffsetPx: o ajuste agora é em MUNDO, não em tela
    // py += ERFWidget::kTopOffsetPx; // => comente/remova para seguir TrueHUD ao pé da letra

    // Escala por distância (TrueHUD-style: linearizar com near/far)
    float scalePct = 100.f;
    if (g_fNear && g_fFar) {
        const float fNear = *g_fNear, fFar = *g_fFar;
        const float lin = fNear * fFar / (fFar + depth * (fNear - fFar));
        const float clamped = std::clamp(lin, 500.f, 2000.f);
        scalePct = (((clamped - 500.f) * (50.f - 100.f)) / (2000.f - 500.f)) + 100.f;
    }

    // Snap inicial
    if (_needsSnap || std::isnan(_lastX) || std::isnan(_lastY)) {
        _lastX = px;
        _lastY = py;
        _needsSnap = false;
    }

    // Arredonda e commit atômico
    px = std::floor(px + 0.5);
    py = std::floor(py + 0.5);
    RE::GFxValue::DisplayInfo di;
    di.SetPosition(static_cast<float>(px), static_cast<float>(py));
    di.SetScale(scalePct, scalePct);
    _object.SetDisplayInfo(di);

    RE::GFxValue vis;
    vis.SetBoolean(true);
    _object.SetMember("_visible", vis);

    _lastX = px;
    _lastY = py;
}

void InjectHUD::ERFWidget::SetIconAndGauge(const std::string& iconPath, const std::vector<std::uint32_t>& values,
                                           const std::vector<std::uint32_t>& colors, std::uint32_t tintRGB) {
    if (!_view) {
        spdlog::warn("[InjectHUD] SetIconAndGauge: _view=null slot={}", _slot);
        return;
    }

    RE::GFxValue ready;
    if (!_object.Invoke("isReady", &ready, nullptr, 0) || !ready.IsBool() || !ready.GetBool()) {
        spdlog::debug("[InjectHUD] SetIconAndGauge: view not ready slot={}", _slot);
        return;
    }

    const std::size_t n = std::min(values.size(), colors.size());
    if (n == 0) {
        spdlog::info("[InjectHUD] SetIconAndGauge: empty arrays, hiding slot={}", _slot);
        RE::GFxValue vis;
        vis.SetBoolean(false);
        _object.SetMember("_visible", vis);
        _hadContent = false;
        return;
    }
    if (values.size() != colors.size()) {
        spdlog::warn("[InjectHUD] SetIconAndGauge: size mismatch v={} c={} (trunc to {})", values.size(), colors.size(),
                     n);
    }

    // 1) Ícone (path + linkage vazio)
    {
        RE::GFxValue args[2], ret;
        args[0].SetString(iconPath.c_str());
        args[1].SetNumber(static_cast<double>(tintRGB));
        const bool ok = _object.Invoke("setIcon", &ret, args, 2);
        spdlog::info("[InjectHUD] setIcon slot={} path='{}' tint={:#X} ok={} retBool?={}", _slot, iconPath, tintRGB, ok,
                     (ret.IsBool() ? ret.GetBool() : -1));
    }

    // 2) Arrays (valores/cores)
    RE::GFxValue valsArr, colsArr;
    _view->CreateArray(&valsArr);
    _view->CreateArray(&colsArr);

    bool any = false;
    for (std::size_t i = 0; i < n; ++i) {
        const double v = std::clamp<double>(values[i], 0.0, 100.0);
        RE::GFxValue vv, cc;
        vv.SetNumber(v);
        cc.SetNumber(static_cast<double>(colors[i]));
        valsArr.PushBack(vv);
        colsArr.PushBack(cc);
        any = any || (v > 0.0);
    }

    {
        RE::GFxValue args[2];
        args[0] = valsArr;
        args[1] = colsArr;
        const bool ok = _object.Invoke("setAccumulators", nullptr, args, 2);
        spdlog::info("[InjectHUD] setAccumulators slot={} count={} ok={}", _slot, n, ok);
    }

    // 3) Visibilidade
    RE::GFxValue vis;
    vis.SetBoolean(any);
    const bool okVis = _object.SetMember("_visible", vis);
    spdlog::info("[InjectHUD] _visible({}) ok={}", any, okVis);

    _hadContent = any;
}

void InjectHUD::ERFWidget::SetCombo(const std::string& iconPath, float remaining01, std::uint32_t tintRGB) {
    if (!_view) {
        spdlog::warn("[InjectHUD] SetCombo: _view=null slot={}", _slot);
        return;
    }

    RE::GFxValue ready;
    if (!_object.Invoke("isReady", &ready, nullptr, 0) || !ready.IsBool() || !ready.GetBool()) {
        spdlog::debug("[InjectHUD] SetCombo: view not ready slot={}", _slot);
        return;
    }

    const double f = std::clamp<double>(remaining01, 0.0, 1.0);
    spdlog::info("[InjectHUD] SetCombo slot={} pos={} path='{}' remain01={} tint={:#X}", _slot, _pos, iconPath, f,
                 tintRGB);

    bool ok = true;

    // Ícone do combo (path + linkage vazio)
    {
        RE::GFxValue args[2], ret;
        args[0].SetString(iconPath.c_str());
        args[1].SetNumber(static_cast<double>(tintRGB));
        ok &= _object.Invoke("setIcon", &ret, args, 2);
        spdlog::info("[InjectHUD] setIcon(ok={}) retBool?={}", ok, (ret.IsBool() ? ret.GetBool() : -1));
    }

    // Preenchimento do anel de combo
    {
        RE::GFxValue args[2];
        args[0].SetNumber(f);
        args[1].SetNumber(static_cast<double>(tintRGB));
        ok &= _object.Invoke("setComboFill", nullptr, args, 2);
        spdlog::info("[InjectHUD] setComboFill ok={} f={}", ok, f);
    }

    RE::GFxValue vis;
    vis.SetBoolean(true);
    ok &= _object.SetMember("_visible", vis);
    spdlog::info("[InjectHUD] _visible(true) ok={}", ok);
}

// ====================== Top-level API ======================
void InjectHUD::AddFor(RE::Actor* actor) {
    if (!g_trueHUD || !actor) {
        spdlog::warn("[InjectHUD] AddFor: missing TrueHUD or actor");
        return;
    }

    const auto id = actor->GetFormID();
    auto& entry = widgets[id];

    // cachear o handle do ator (barato e persistente)
    entry.handle = actor->CreateRefHandle();

    auto& vec = entry.slots;
    if (!vec.empty()) {
        spdlog::info("[InjectHUD] AddFor id={:08X} already has {} widgets", id, vec.size());
        return;
    }

    const auto h = actor->GetHandle();
    g_trueHUD->AddActorInfoBar(h);
    if (!g_trueHUD->HasInfoBar(h, true) && !IsPlayerActor(actor)) {
        spdlog::warn("[InjectHUD] AddFor id={:08X} HasInfoBar=false", id);
        return;
    }

    auto w0 = std::make_shared<ERFWidget>(0);
    const auto wid0 = MakeWidgetID(id, 0);
    spdlog::info("[InjectHUD] AddFor id={:08X} -> AddWidget slot0 wid={:08X}", id, wid0);
    g_trueHUD->AddWidget(g_pluginHandle, ERF_WIDGET_TYPE, wid0, ERF_SYMBOL_NAME, w0);
    w0->ProcessDelegates();
    if (!w0->_view) {
        spdlog::error("[InjectHUD] AddFor id={:08X} slot0 view=null após ProcessDelegates()", id);
    }
    w0->SetPos(0);
    vec.clear();
    vec.push_back(std::move(w0));
}

void InjectHUD::UpdateFor(RE::Actor* actor) {
    if (!g_trueHUD || !actor) {
        spdlog::warn("[InjectHUD] UpdateFor: missing TrueHUD or actor");
        return;
    }

    const auto id = actor->GetFormID();
    auto it = widgets.find(id);
    if (it == widgets.end()) {
        spdlog::debug("[InjectHUD] UpdateFor id={:08X} -> no widgets entry", id);
        return;
    }
    auto& list = it->second.slots;
    if (list.empty()) {
        spdlog::debug("[InjectHUD] UpdateFor id={:08X} -> empty list", id);
        return;
    }
    // 1) Reações ativas (N)
    const auto actives = GetActiveReactions(id);

    // 2) Bundle do acumulador (N elementos) + sugestão de ícone (por reação “mais adequada”)
    auto bundleOpt = ElementalGauges::PickHudIconDecayed(id);
    const bool haveTotals =
        bundleOpt.has_value() && !bundleOpt->values.empty() &&
        std::any_of(bundleOpt->values.begin(), bundleOpt->values.end(), [](std::uint32_t v) { return v > 0; });

    int needed = static_cast<int>(actives.size()) + (haveTotals ? 1 : 0);
    needed = std::clamp(needed, 0, 3);

    spdlog::info("[InjectHUD] UpdateFor id={:08X} actives={} haveTotals={} needed={} listSize={}", id, actives.size(),
                 haveTotals, needed, list.size());

    if (needed == 0) {
        for (auto& w : list) {
            if (w && w->_view) {
                RE::GFxValue vis;
                vis.SetBoolean(false);
                w->_object.SetMember("_visible", vis);
            }
        }
        return;
    }

    // Garantir InfoBar
    const auto h = actor->GetHandle();
    g_trueHUD->AddActorInfoBar(h);
    if (!g_trueHUD->HasInfoBar(h, true) && !IsPlayerActor(actor)) {
        spdlog::warn("[InjectHUD] UpdateFor id={:08X} HasInfoBar=false", id);
        return;
    }

    // Crescer
    const int kMaxSlots = 3;
    while (CountAlive(list) < needed) {
        const int slot = LowestFreeSlot(list, kMaxSlots);
        if (slot < 0) {
            spdlog::warn("[InjectHUD] UpdateFor id={:08X} no free slot but still below needed?", id);
            break;
        }
        if (slot >= (int)list.size()) list.resize(slot + 1);

        auto w = std::make_shared<ERFWidget>(slot);
        const auto wid = MakeWidgetID(id, slot);
        g_trueHUD->AddWidget(g_pluginHandle, ERF_WIDGET_TYPE, wid, ERF_SYMBOL_NAME, w);
        w->ProcessDelegates();
        if (!w->_view) spdlog::error("[InjectHUD] slot{} view=null after ProcessDelegates()", slot);

        w->SetPos(CountAlive(list));
        list[slot] = std::move(w);
        spdlog::info("[InjectHUD] AddWidget slot{}: actor {:08X} pos={}", slot, id, list[slot]->_pos);
    }

    // Encolher
    while (CountAlive(list) > needed) {
        int victim = -1;
        for (int s = (int)list.size() - 1; s >= 0; --s) {
            if (list[s]) {
                victim = s;
                break;
            }
        }
        RemoveAtSlot(list, id, victim);
    }

    // Pintura por slot
    std::array<bool, 3> used{};
    used.fill(false);

    // 1) Reações primeiro
    int painted = 0;
    for (int s = 0; s < (int)list.size() && painted < (int)actives.size(); ++s) {
        if (!list[s] || !list[s]->_view) continue;
        auto& w = *list[s];
        w.SetPos(painted);
        w.FollowActorHead(actor);

        const auto& r = actives[painted];
        const double remain = r.realTime ? (r.endRtS - NowRtS()) : (double(r.endH - NowHours()) * 3600.0);
        const double denom = std::max(0.001, (double)r.durationS);
        const double frac = std::clamp(remain / denom, 0.0, 1.0);

        spdlog::info("[InjectHUD] PaintReaction id={:08X} slot{} pos={} path='{}' frac={:.3f} tint=#{:06X}", id, s,
                     w._pos, r.iconPath, frac, r.tint);

        // nova assinatura: SetCombo(iconPath, remaining01, tint)
        w.SetCombo(r.iconPath, (float)frac, r.tint);
        used[s] = true;
        ++painted;
    }

    // 2) Acumulador no primeiro slot livre
    if (haveTotals) {
        int accumPos = painted;
        bool done = false;

        for (int s = 0; s < (int)list.size(); ++s) {
            if (!list[s] || !list[s]->_view || used[s]) continue;

            auto& w = *list[s];
            w.SetPos(accumPos);
            w.FollowActorHead(actor);

            const auto& b = *bundleOpt;
            spdlog::info("[InjectHUD] PaintAccum id={:08X} slot{} pos={} nVals={} nCols={} path='{}' tint=#{:06X}", id,
                         s, w._pos, b.values.size(), b.colors.size(), b.iconPath, b.iconTint);

            // nova assinatura: SetIconAndGauge(iconPath, values, colors, tint)
            w.SetIconAndGauge(b.iconPath, b.values, b.colors, b.iconTint);

            used[s] = true;
            done = true;
            break;
        }

        if (!done) spdlog::debug("[InjectHUD] No free slot to paint accumulator (all used?)");
    }
}

void InjectHUD::BeginReaction(RE::Actor* a, ERF_ReactionHandle handle, float seconds, bool realTime) {
    if (!a || seconds <= 0.f) return;

    const auto id = a->GetFormID();
    spdlog::info("[InjectHUD] BeginReaction enqueue id={:08X} reaction={} seconds={} realTime={}", id,
                 static_cast<unsigned>(handle), seconds, realTime);

    PendingReaction pr{};
    pr.id = id;
    pr.handle = a->CreateRefHandle();
    pr.reaction = handle;
    pr.secs = seconds;
    pr.realTime = realTime;

    {
        std::scoped_lock lk(g_comboMx);
        g_comboQueue.push_back(std::move(pr));
    }
}

bool InjectHUD::RemoveFor(RE::FormID id) {
    if (!g_trueHUD || !id) return false;
    auto it = widgets.find(id);
    if (it == widgets.end()) return false;

    auto& vec = it->second.slots;  // <-- novo
    spdlog::info("[InjectHUD] RemoveFor id={:08X} slots={}", id, vec.size());
    for (int slot = 0; slot < (int)vec.size(); ++slot) {
        if (!vec[slot]) continue;
        const auto wid = MakeWidgetID(id, slot);
        g_trueHUD->RemoveWidget(g_pluginHandle, ERF_WIDGET_TYPE, wid, TRUEHUD_API::WidgetRemovalMode::Immediate);
    }
    widgets.erase(it);
    combos.erase(id);
    return true;
}

void InjectHUD::RemoveAllWidgets() {
    spdlog::info("[InjectHUD] RemoveAllWidgets count={} (has TrueHUD?{})", widgets.size(), (g_trueHUD != nullptr));
    if (!g_trueHUD) {
        widgets.clear();
        combos.clear();
        return;
    }

    for (const auto& [id, entry] : widgets) {
        const auto& vec = entry.slots;
        for (int slot = 0; slot < (int)vec.size(); ++slot) {
            if (!vec[slot]) continue;
            const auto wid = MakeWidgetID(id, slot);
            g_trueHUD->RemoveWidget(g_pluginHandle, ERF_WIDGET_TYPE, wid, TRUEHUD_API::WidgetRemovalMode::Immediate);
        }
    }
    widgets.clear();
    combos.clear();
}

void InjectHUD::OnTrueHUDClose() {
    spdlog::info("[InjectHUD] OnTrueHUDClose");
    RemoveAllWidgets();
    HUD::ResetTracking();
}

void InjectHUD::OnUIFrameBegin() { DrainComboQueueOnUI(); }

double InjectHUD::NowRtS() {
    using clock = std::chrono::steady_clock;
    static const auto t0 = clock::now();
    return std::chrono::duration<double>(clock::now() - t0).count();
}

std::uint32_t InjectHUD::MakeWidgetID(RE::FormID id, int slot) {
    std::uint32_t x = static_cast<std::uint32_t>(id);
    x ^= 0x9E3779B9u + (x << 6) + (x >> 2);
    x ^= (static_cast<std::uint32_t>(slot) + 1u) * 0x85EBCA6Bu;
    x ^= (x >> 16);
    return x ? x : 0xA5A5A5A5u;
}