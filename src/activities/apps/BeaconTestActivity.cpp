#include "BeaconTestActivity.h"
void BeaconTestActivity::onEnter() {}
void BeaconTestActivity::onExit() { stopAP(); }
void BeaconTestActivity::loop() {}
void BeaconTestActivity::render(RenderLock&&) {}
void BeaconTestActivity::loadSsidsForMode() {}
void BeaconTestActivity::loadCustomSsids() {}
std::string BeaconTestActivity::generateRandomSsid() { return "biscuit"; }
void BeaconTestActivity::startAP(const std::string&) {}
void BeaconTestActivity::stopAP() { apActive = false; }
void BeaconTestActivity::cycleNext() {}
