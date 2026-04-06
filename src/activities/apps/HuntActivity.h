#pragma once
#include "activities/Activity.h"
#include "util/ButtonNavigator.h"
#include "util/TargetDB.h"

class HuntActivity final : public Activity {
public:
    explicit HuntActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
        : Activity("Hunt", renderer, mappedInput) {}

    // Pre-select a target (called before onEnter by the launching activity)
    void setTarget(const uint8_t mac[6]);

    void onEnter() override;
    void onExit() override;
    void loop() override;
    void render(RenderLock&&) override;

private:
    enum State { SELECT_TARGET, PROFILE_VIEW, CAPABILITIES, MOVEMENT_LOG };

    State state = SELECT_TARGET;
    ButtonNavigator buttonNavigator;

    // Target list
    int listIndex = 0;
    Target* targetList[50] = {};
    int targetCount = 0;

    // Selected target
    uint8_t selectedMac[6] = {};
    bool hasPreselected = false;
    Target* current = nullptr;

    // Capability analysis
    enum CapAction : uint8_t { CAP_FIRE, CAP_TRACK, CAP_NONE };
    struct Capability {
        const char* name;
        const char* reason;
        bool available;
        CapAction action;
        int fireAttackType;  // FireActivity::AttackType cast to int, -1 if N/A
    };
    static constexpr int MAX_CAPABILITIES = 10;
    Capability capabilities[MAX_CAPABILITIES] = {};
    int capCount = 0;
    int capIndex = 0;

    // Profile scroll
    int profileScroll = 0;

    void analyzeCapabilities();
    void selectTarget(int index);
    void loadTargetList();

    void renderSelectTarget() const;
    void renderProfile() const;
    void renderCapabilities() const;
    void renderMovementLog() const;
};
