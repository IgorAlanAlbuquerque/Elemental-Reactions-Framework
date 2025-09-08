#include "InjectHUD.h"

#include <array>

using namespace std::literals;

namespace {

    void Eval(RE::GFxMovieView* v, const std::string& code) {
        if (!v) return;
        RE::GFxValue a{code.c_str()};
        v->Invoke("eval", nullptr, &a, 1);
    }

    void InstallHelpers(RE::GFxMovieView* v) {
        static bool done = false;
        if (done || !v) return;
        done = true;

        const char* script =
            "if(!_global.__smso){ _global.__smso={}; }"
            "_global.__smso.anchor='_root.TrueHUD';"
            "_global.__smso.findAnchor=function(){"
            "  var P=['_root.TrueHUD','_root.TrueHUD.TrueHUD','_root.widgets','_root.Root','_root'];"
            "  for(var i=0;i<P.length;i++){ var o=eval(P[i]); if(o){ _global.__smso.anchor=P[i]; return; } }"
            "};"
            "_global.__smso.spawn=function(fid){"
            "  var a=eval(_global.__smso.anchor); if(!a) return;"
            "  var n='smso_'+fid; if(a[n]) return;"
            "  var d=a.getNextHighestDepth(); a.createEmptyMovieClip(n,d);"
            "  a[n].loadMovie('Interface/SMSO/smsogauge.swf');"
            "  a[n]._xscale=a[n]._yscale=100; a[n]._visible=true;"
            "};"
            "_global.__smso.update=function(fid,f,fr,s,icon,tint){"
            "  var o=eval(_global.__smso.anchor); if(!o) return;"
            "  var m=o['smso_'+fid]; if(!m || !m.__impl) return;"
            "  m.__impl.setTotals(f,fr,s); m.__impl.setIcon(icon,tint); m.__impl.setVisible(true);"
            "};"
            "_global.__smso.remove=function(fid){"
            "  var o=eval(_global.__smso.anchor); if(!o) return;"
            "  var n='smso_'+fid; if(o[n]){ o[n].removeMovieClip(); }"
            "};";
        Eval(v, script);

        Eval(v, "_global.__smso.findAnchor();");
    }

    std::string Anchor() { return "_global.__smso.anchor"; }

    std::string NameFor(RE::FormID id) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "smso_%08X", id);
        return buf;
    }

    RE::GFxMovieView* GetTrueHUDView() {
        RE::GFxMovieView* view = nullptr;
        if (auto* ui = RE::UI::GetSingleton(); ui && ui->IsMenuOpen(RE::BSFixedString("TrueHUD"))) {
            if (auto menu = ui->GetMenu(RE::BSFixedString("TrueHUD")); menu) {
                view = menu->uiMovie.get();
            }
        }
        return view;
    }

    struct TrueHUDSink : RE::BSTEventSink<RE::MenuOpenCloseEvent> {
        RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent* ev,
                                              RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override {
            if (!ev || !ev->opening) return RE::BSEventNotifyControl::kContinue;
            if (ev->menuName != "TrueHUD"sv) return RE::BSEventNotifyControl::kContinue;
            if (auto* v = GetTrueHUDView()) {
                InstallHelpers(v);
                spdlog::info("[SMSO] TrueHUD aberto; helpers instalados.");
            }
            return RE::BSEventNotifyControl::kContinue;
        }
    } g_sink;

}  // anon

namespace HUD {

    void InitTrueHUDInjection() {
        if (auto* ui = RE::UI::GetSingleton()) {
            ui->AddEventSink<RE::MenuOpenCloseEvent>(&g_sink);
            if (ui->IsMenuOpen("TrueHUD"sv)) {
                if (auto* v = GetTrueHUDView()) {
                    InstallHelpers(v);
                }
            }
        }
    }

    void EnsureGaugeFor(RE::GFxMovieView* v, RE::Actor* a) {
        if (!v || !a) return;
        InstallHelpers(v);
        RE::GFxValue args[1]{RE::GFxValue(double(a->GetFormID()))};
        v->Invoke("_global.__smso.spawn", nullptr, args, 1);
    }

    void UpdateGaugeFor(RE::GFxMovieView* v, RE::Actor* a, std::uint8_t fire, std::uint8_t frost, std::uint8_t shock,
                        int iconId, std::uint32_t tintRGB) {
        if (!v || !a) return;
        InstallHelpers(v);
        RE::GFxValue args[6]{RE::GFxValue(double(a->GetFormID())), RE::GFxValue(double(fire)),
                             RE::GFxValue(double(frost)),          RE::GFxValue(double(shock)),
                             RE::GFxValue(double(iconId)),         RE::GFxValue(double(tintRGB))};
        v->Invoke("_global.__smso.update", nullptr, args, 6);
    }

    void RemoveGaugeFor(RE::GFxMovieView* v, RE::Actor* a) {
        if (!v || !a) return;
        InstallHelpers(v);
        RE::GFxValue args[1]{RE::GFxValue(double(a->GetFormID()))};
        v->Invoke("_global.__smso.remove", nullptr, args, 1);
    }

}
