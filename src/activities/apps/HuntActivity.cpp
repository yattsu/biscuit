#include "HuntActivity.h"
#include "FireActivity.h"
#include "activities/ActivityManager.h"

#include <cstring>
#include <cstdio>

#include "components/UITheme.h"
#include "fontIds.h"

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static const char* authTypeName(uint8_t authType) {
    switch (authType) {
        case 0: return "Open";
        case 1: return "WEP";
        case 2: return "WPA";
        case 3: return "WPA2-PSK";
        case 4: return "WPA3";
        default: return "Unknown";
    }
}

static const char* targetTypeName(TargetType type) {
    switch (type) {
        case TargetType::AP:     return "WiFi AP";
        case TargetType::STA: return "WiFi Client";
        case TargetType::BLE:  return "BLE Device";
        default:                      return "Unknown";
    }
}

static const char* targetTypePrefix(TargetType type) {
    switch (type) {
        case TargetType::AP:     return "[AP] ";
        case TargetType::STA: return "[CL] ";
        case TargetType::BLE:  return "[BT] ";
        default:                      return "[ ] ";
    }
}

static void formatMac(char* buf, size_t bufLen, const uint8_t mac[6]) {
    snprintf(buf, bufLen, "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

// Format epoch seconds as HH:MM (no RTC available so just show raw seconds
// modulo a day for relative times).
static void formatTime(char* buf, size_t bufLen, uint32_t epoch) {
    if (epoch == 0) {
        snprintf(buf, bufLen, "--:--");
        return;
    }
    uint32_t sec = epoch % 86400u;
    unsigned h = sec / 3600u;
    unsigned m = (sec % 3600u) / 60u;
    snprintf(buf, bufLen, "%02u:%02u", h, m);
}

// ---------------------------------------------------------------------------
// setTarget — call before onEnter
// ---------------------------------------------------------------------------

void HuntActivity::setTarget(const uint8_t mac[6]) {
    memcpy(selectedMac, mac, 6);
    hasPreselected = true;
}

// ---------------------------------------------------------------------------
// loadTargetList — fills targetList[] from DB cache
// ---------------------------------------------------------------------------

void HuntActivity::loadTargetList() {
    targetCount = 0;
    int c1 = 0;
    TARGETS.getSorted(TargetType::AP, targetList, 50, c1, 0);
    targetCount = c1;

    Target* temp[50];
    int c2 = 0;
    int remaining = 50 - targetCount;
    if (remaining > 0) {
        TARGETS.getSorted(TargetType::STA, temp, remaining, c2, 0);
        for (int i = 0; i < c2 && targetCount < 50; i++) {
            targetList[targetCount++] = temp[i];
        }
    }

    int c3 = 0;
    remaining = 50 - targetCount;
    if (remaining > 0) {
        TARGETS.getSorted(TargetType::BLE, temp, remaining, c3, 0);
        for (int i = 0; i < c3 && targetCount < 50; i++) {
            targetList[targetCount++] = temp[i];
        }
    }
}

// ---------------------------------------------------------------------------
// selectTarget
// ---------------------------------------------------------------------------

void HuntActivity::selectTarget(int index) {
    if (index < 0 || index >= targetCount || !targetList[index]) return;
    current = targetList[index];
    memcpy(selectedMac, current->mac, 6);
    profileScroll = 0;
    state = PROFILE_VIEW;
    analyzeCapabilities();
    requestUpdate();
}

// ---------------------------------------------------------------------------
// analyzeCapabilities
// ---------------------------------------------------------------------------

void HuntActivity::analyzeCapabilities() {
    capCount = 0;
    capIndex = 0;
    if (!current) return;

    auto add = [this](const char* n, const char* r, bool a, CapAction act = CAP_NONE, int atkType = -1) {
        if (capCount < MAX_CAPABILITIES) {
            capabilities[capCount++] = {n, r, a, act, atkType};
        }
    };

    if (current->type == TargetType::AP) {
        add("Handshake capture",
            current->pmf ? "PMF enabled — may fail" : "No PMF — feasible",
            !current->pmf, CAP_FIRE, 3);
        add("PMKID harvest",
            "Passive — no client needed",
            current->authType >= 3, CAP_FIRE, 4);
        add("Portal clone",
            "Clone AP + serve portal page",
            true, CAP_FIRE, 8);
        add("Beacon flood",
            "Flood nearby device WiFi lists",
            true, CAP_FIRE, 5);
        if (current->wps) {
            add("WPS assessment",
                "No automated tool available",
                false, CAP_NONE, -1);
        }
    } else if (current->type == TargetType::STA) {
        add("Probe response",
            current->probeCount > 0 ? "Has probed SSIDs — feasible" : "No probes seen",
            current->probeCount > 0, CAP_FIRE, 7);
        add("Track movement",
            "Monitor RSSI over time",
            true, CAP_TRACK, -1);
        add("AP association spoof",
            "Respond as known AP to client",
            current->probeCount > 0, CAP_FIRE, 7);
    } else if (current->type == TargetType::BLE) {
        add("Clone advertisement",
            "Copy BLE broadcast data",
            true, CAP_FIRE, 9);
        add("Enumerate services",
            "Connect and list GATT services",
            true, CAP_FIRE, 11);
        add("Track proximity",
            "Monitor RSSI distance",
            true, CAP_TRACK, -1);
    }
}

// ---------------------------------------------------------------------------
// onEnter / onExit
// ---------------------------------------------------------------------------

void HuntActivity::onEnter() {
    Activity::onEnter();
    TARGETS.loadCache();

    if (hasPreselected) {
        current = TARGETS.findByMac(selectedMac);
        if (current) {
            state = PROFILE_VIEW;
            profileScroll = 0;
            analyzeCapabilities();
        } else {
            state = SELECT_TARGET;
            hasPreselected = false;
        }
    } else {
        state = SELECT_TARGET;
    }

    loadTargetList();
    listIndex = 0;

    buttonNavigator.onNext([this] {
        if (state == SELECT_TARGET) {
            listIndex = ButtonNavigator::nextIndex(listIndex, targetCount);
            requestUpdate();
        } else if (state == CAPABILITIES) {
            capIndex = ButtonNavigator::nextIndex(capIndex, capCount);
            requestUpdate();
        }
    });
    buttonNavigator.onPrevious([this] {
        if (state == SELECT_TARGET) {
            listIndex = ButtonNavigator::previousIndex(listIndex, targetCount);
            requestUpdate();
        } else if (state == CAPABILITIES) {
            capIndex = ButtonNavigator::previousIndex(capIndex, capCount);
            requestUpdate();
        }
    });

    requestUpdate();
}

void HuntActivity::onExit() {
    Activity::onExit();
}

// ---------------------------------------------------------------------------
// loop
// ---------------------------------------------------------------------------

void HuntActivity::loop() {
    switch (state) {

        case SELECT_TARGET: {
            if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
                finish();
                return;
            }
            if (mappedInput.wasReleased(MappedInputManager::Button::Down) ||
                mappedInput.wasReleased(MappedInputManager::Button::Right)) {
                if (targetCount > 0) {
                    listIndex = ButtonNavigator::nextIndex(listIndex, targetCount);
                    requestUpdate();
                }
            }
            if (mappedInput.wasReleased(MappedInputManager::Button::Up) ||
                mappedInput.wasReleased(MappedInputManager::Button::Left)) {
                if (targetCount > 0) {
                    listIndex = ButtonNavigator::previousIndex(listIndex, targetCount);
                    requestUpdate();
                }
            }
            if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
                if (targetCount > 0) {
                    selectTarget(listIndex);
                }
            }
            break;
        }

        case PROFILE_VIEW: {
            if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
                if (hasPreselected) {
                    finish();
                } else {
                    state = SELECT_TARGET;
                    requestUpdate();
                }
                return;
            }
            if (mappedInput.wasReleased(MappedInputManager::Button::Down)) {
                profileScroll++;
                requestUpdate();
            }
            if (mappedInput.wasReleased(MappedInputManager::Button::Up)) {
                if (profileScroll > 0) { profileScroll--; requestUpdate(); }
            }
            if (mappedInput.wasReleased(MappedInputManager::Button::Confirm) ||
                mappedInput.wasReleased(MappedInputManager::Button::Right)) {
                state = CAPABILITIES;
                capIndex = 0;
                requestUpdate();
            }
            if (mappedInput.wasReleased(MappedInputManager::Button::Left)) {
                state = MOVEMENT_LOG;
                requestUpdate();
            }
            if (mappedInput.wasReleased(MappedInputManager::Button::PageForward)) {
                if (current) {
                    char path[64];
                    char macBuf[18];
                    formatMac(macBuf, sizeof(macBuf), current->mac);
                    // Replace ':' with '-' for filename safety
                    for (int i = 0; i < 17; i++) {
                        if (macBuf[i] == ':') macBuf[i] = '-';
                    }
                    snprintf(path, sizeof(path), "/biscuit/targets/%s.txt", macBuf);
                    TARGETS.exportProfile(current->mac, path);
                }
            }
            break;
        }

        case CAPABILITIES: {
            if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
                state = PROFILE_VIEW;
                requestUpdate();
                return;
            }
            if (mappedInput.wasReleased(MappedInputManager::Button::Down)) {
                if (capCount > 0) {
                    capIndex = ButtonNavigator::nextIndex(capIndex, capCount);
                    requestUpdate();
                }
            }
            if (mappedInput.wasReleased(MappedInputManager::Button::Up)) {
                if (capCount > 0) {
                    capIndex = ButtonNavigator::previousIndex(capIndex, capCount);
                    requestUpdate();
                }
            }
            if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
                if (current && capIndex < capCount && capabilities[capIndex].available) {
                    const auto& cap = capabilities[capIndex];
                    if (cap.action == CAP_FIRE && cap.fireAttackType >= 0) {
                        auto fire = std::make_unique<FireActivity>(renderer, mappedInput);
                        fire->setTarget(current->mac);
                        fire->setAttack(cap.fireAttackType);
                        activityManager.pushActivity(std::move(fire));
                    } else if (cap.action == CAP_TRACK) {
                        state = MOVEMENT_LOG;
                        requestUpdate();
                    }
                }
            }
            break;
        }

        case MOVEMENT_LOG: {
            if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
                state = PROFILE_VIEW;
                requestUpdate();
            }
            break;
        }
    }
}

// ---------------------------------------------------------------------------
// render
// ---------------------------------------------------------------------------

void HuntActivity::render(RenderLock&&) {
    renderer.clearScreen();

    switch (state) {
        case SELECT_TARGET:  renderSelectTarget();  break;
        case PROFILE_VIEW:   renderProfile();       break;
        case CAPABILITIES:   renderCapabilities();  break;
        case MOVEMENT_LOG:   renderMovementLog();   break;
    }

    renderer.displayBuffer();
}

// ---------------------------------------------------------------------------
// renderSelectTarget
// ---------------------------------------------------------------------------

void HuntActivity::renderSelectTarget() const {
    const auto& metrics = UITheme::getInstance().getMetrics();
    const auto pageWidth  = renderer.getScreenWidth();
    const auto pageHeight = renderer.getScreenHeight();

    GUI.drawHeader(renderer,
        Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight},
        "HUNT — Targets");

    const int headerBottom = metrics.topPadding + metrics.headerHeight;
    const int listHeight   = pageHeight - headerBottom - metrics.buttonHintsHeight;

    if (targetCount == 0) {
        renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 - 10, "No targets in database");
    } else {
        GUI.drawList(renderer,
            Rect{0, headerBottom, pageWidth, listHeight},
            targetCount,
            listIndex,
            [this](int i) -> std::string {
                if (!targetList[i]) return "---";
                char buf[64];
                char macBuf[18];
                formatMac(macBuf, sizeof(macBuf), targetList[i]->mac);
                const char* label = (targetList[i]->ssid[0] != '\0') ? targetList[i]->ssid
                                  : (targetList[i]->name[0] != '\0') ? targetList[i]->name
                                  : macBuf;
                snprintf(buf, sizeof(buf), "%s%s", targetTypePrefix(targetList[i]->type), label);
                return std::string(buf);
            },
            [this](int i) -> std::string {
                if (!targetList[i]) return "";
                bool stale = !TARGETS.isSeenThisSession(targetList[i]);
                char buf[40];
                char macBuf[18];
                formatMac(macBuf, sizeof(macBuf), targetList[i]->mac);
                snprintf(buf, sizeof(buf), "%s  %s%d", macBuf,
                         stale ? "OLD " : "RSSI ",
                         (int)targetList[i]->rssi);
                return std::string(buf);
            });
    }

    const auto labels = mappedInput.mapLabels("Back", "Select", "Up", "Down");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

// ---------------------------------------------------------------------------
// renderProfile
// ---------------------------------------------------------------------------

void HuntActivity::renderProfile() const {
    if (!current) return;

    const auto& metrics  = UITheme::getInstance().getMetrics();
    const auto pageWidth = renderer.getScreenWidth();
    (void)renderer.getScreenHeight(); // pageHeight unused in profile view

    GUI.drawHeader(renderer,
        Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight},
        "HUNT — Profile");

    const int x  = metrics.contentSidePadding;
    const int lh = 24;  // line height
    int y = metrics.topPadding + metrics.headerHeight + 8 - profileScroll * lh;

    char macBuf[18];
    formatMac(macBuf, sizeof(macBuf), current->mac);

    // --- MAC ---
    char line[80];
    snprintf(line, sizeof(line), "MAC:  %s", macBuf);
    renderer.drawText(UI_10_FONT_ID, x, y, line); y += lh;

    // --- Type ---
    snprintf(line, sizeof(line), "Type: %s", targetTypeName(current->type));
    renderer.drawText(UI_10_FONT_ID, x, y, line); y += lh;

    // --- Vendor ---
    if (current->vendor[0] != '\0') {
        snprintf(line, sizeof(line), "Vendor: %s", current->vendor);
        renderer.drawText(UI_10_FONT_ID, x, y, line); y += lh;
    }

    // --- Type-specific fields ---
    if (current->type == TargetType::AP) {
        if (current->ssid[0] != '\0') {
            snprintf(line, sizeof(line), "SSID: \"%s\"", current->ssid);
            renderer.drawText(UI_10_FONT_ID, x, y, line); y += lh;
        }
        snprintf(line, sizeof(line), "Channel: %d   Auth: %s",
                 (int)current->channel, authTypeName(current->authType));
        renderer.drawText(UI_10_FONT_ID, x, y, line); y += lh;

        snprintf(line, sizeof(line), "PMF: %s   WPS: %s",
                 current->pmf ? "Yes" : "No",
                 current->wps ? "Yes" : "No");
        renderer.drawText(UI_10_FONT_ID, x, y, line); y += lh;

        snprintf(line, sizeof(line), "Clients: %d", (int)current->clientCount);
        renderer.drawText(UI_10_FONT_ID, x, y, line); y += lh;

        if (current->hasHandshake || current->hasPmkid) {
            snprintf(line, sizeof(line), "Captured: %s%s",
                     current->hasHandshake ? "EAPOL " : "",
                     current->hasPmkid    ? "PMKID"  : "");
            renderer.drawText(UI_10_FONT_ID, x, y, line); y += lh;
        }

    } else if (current->type == TargetType::STA) {
        if (current->ssid[0] != '\0') {
            snprintf(line, sizeof(line), "Assoc AP: \"%s\"", current->ssid);
            renderer.drawText(UI_10_FONT_ID, x, y, line); y += lh;
        }
        snprintf(line, sizeof(line), "Probed SSIDs: %d", (int)current->probeCount);
        renderer.drawText(UI_10_FONT_ID, x, y, line); y += lh;

        for (int i = 0; i < current->probeCount && i < 5; i++) {
            snprintf(line, sizeof(line), "  [%d] \"%s\"", i + 1, current->probes[i]);
            renderer.drawText(UI_10_FONT_ID, x, y, line); y += lh;
        }

    } else if (current->type == TargetType::BLE) {
        if (current->name[0] != '\0') {
            snprintf(line, sizeof(line), "Name: \"%s\"", current->name);
            renderer.drawText(UI_10_FONT_ID, x, y, line); y += lh;
        }
    }

    // --- Common fields ---
    snprintf(line, sizeof(line), "RSSI: %d dBm", (int)current->rssi);
    renderer.drawText(UI_10_FONT_ID, x, y, line); y += lh;

    if (current->os[0] != '\0') {
        snprintf(line, sizeof(line), "OS: %s", current->os);
    } else {
        snprintf(line, sizeof(line), "OS: (unknown)");
    }
    renderer.drawText(UI_10_FONT_ID, x, y, line); y += lh;

    // --- Timestamps ---
    char tfirst[8], tlast[8];
    formatTime(tfirst, sizeof(tfirst), current->firstSeen);
    formatTime(tlast,  sizeof(tlast),  current->lastSeen);
    snprintf(line, sizeof(line), "First: %s   Last: %s", tfirst, tlast);
    renderer.drawText(UI_10_FONT_ID, x, y, line); y += lh;

    // --- Scroll hint ---
    if (profileScroll > 0) {
        renderer.drawText(SMALL_FONT_ID, x, metrics.topPadding + metrics.headerHeight + 2,
                          "^ scroll up");
    }

    // Button hints
    const auto labels = mappedInput.mapLabels("Back", "Assess", "Log", "Caps");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

// ---------------------------------------------------------------------------
// renderCapabilities
// ---------------------------------------------------------------------------

void HuntActivity::renderCapabilities() const {
    const auto& metrics  = UITheme::getInstance().getMetrics();
    const auto pageWidth = renderer.getScreenWidth();
    const auto pageHeight = renderer.getScreenHeight();

    GUI.drawHeader(renderer,
        Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight},
        "HUNT — Assessment");

    const int headerBottom = metrics.topPadding + metrics.headerHeight;
    const int listHeight   = pageHeight - headerBottom - metrics.buttonHintsHeight;

    if (capCount == 0) {
        renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 - 10, "No capabilities assessed");
    } else {
        GUI.drawList(renderer,
            Rect{0, headerBottom, pageWidth, listHeight},
            capCount,
            capIndex,
            [this](int i) -> std::string {
                char buf[64];
                snprintf(buf, sizeof(buf), "%s  [%s]",
                         capabilities[i].name,
                         capabilities[i].available ? "available" : "limited");
                return std::string(buf);
            },
            [this](int i) -> std::string {
                return std::string(capabilities[i].reason);
            });
    }

    const auto labels = mappedInput.mapLabels("Back", "Launch", "Up", "Down");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

// ---------------------------------------------------------------------------
// renderMovementLog
// ---------------------------------------------------------------------------

void HuntActivity::renderMovementLog() const {
    if (!current) return;

    const auto& metrics  = UITheme::getInstance().getMetrics();
    const auto pageWidth = renderer.getScreenWidth();
    const auto pageHeight = renderer.getScreenHeight();

    GUI.drawHeader(renderer,
        Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight},
        "HUNT — Movement Log");

    const int x  = metrics.contentSidePadding;
    int y = metrics.topPadding + metrics.headerHeight + 16;
    const int lh = 28;

    char macBuf[18];
    formatMac(macBuf, sizeof(macBuf), current->mac);
    renderer.drawText(UI_10_FONT_ID, x, y, macBuf); y += lh;

    char tfirst[8], tlast[8];
    formatTime(tfirst, sizeof(tfirst), current->firstSeen);
    formatTime(tlast,  sizeof(tlast),  current->lastSeen);

    char line[64];
    snprintf(line, sizeof(line), "First seen: %s", tfirst);
    renderer.drawText(UI_10_FONT_ID, x, y, line); y += lh;

    snprintf(line, sizeof(line), "Last seen:  %s", tlast);
    renderer.drawText(UI_10_FONT_ID, x, y, line); y += lh;

    snprintf(line, sizeof(line), "Last RSSI:  %d dBm", (int)current->rssi);
    renderer.drawText(UI_10_FONT_ID, x, y, line); y += lh;

    y += 8;
    renderer.drawText(SMALL_FONT_ID, x, y,
                      "Continuous RSSI logging: not yet available");

    (void)pageHeight; // suppress unused warning

    const auto labels = mappedInput.mapLabels("Back", "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}
