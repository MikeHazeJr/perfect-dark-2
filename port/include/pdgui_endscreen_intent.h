#ifndef PDGUI_ENDSCREEN_INTENT_H
#define PDGUI_ENDSCREEN_INTENT_H

enum class PdguiEndscreenIntent {
    None,
    Continue,
    Retry,
    MainMenu,
    Disconnect,
    Quit,
    ConfirmMainMenu,
    ConfirmDisconnect,
    ConfirmQuit
};

// Each results renderer seeds owner-filtered Back before submitting its
// native buttons. Subsequent button requests cannot replace that decision.
// Popup requests are consumed in the owning window; graph operations are
// dispatched only after all rendering and palette restoration are balanced.
class PdguiEndscreenDecision {
public:
    void request(PdguiEndscreenIntent intent)
    {
        if (intent_ == PdguiEndscreenIntent::None) intent_ = intent;
    }

    bool consume(PdguiEndscreenIntent intent)
    {
        if (intent_ != intent || intent == PdguiEndscreenIntent::None) return false;
        intent_ = PdguiEndscreenIntent::None;
        return true;
    }

    template <typename Callback>
    bool dispatch(Callback callback)
    {
        if (intent_ == PdguiEndscreenIntent::None) return false;
        const auto intent = intent_;
        intent_ = PdguiEndscreenIntent::None; // retire before callback/re-entry
        callback(intent);
        return true;
    }

private:
    PdguiEndscreenIntent intent_ = PdguiEndscreenIntent::None;
};

#endif
