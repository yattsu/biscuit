#pragma once
#include <cstdint>
#include <string>
#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

class LootBoxActivity final : public Activity {
 public:
  explicit LootBoxActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("LootBox", renderer, mappedInput) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override { return true; }

 private:
  enum Screen { MAIN_MENU, PULLING, REVEAL_SINGLE, REVEAL_MULTI, COLLECTION, ITEM_DETAIL };
  Screen screen = MAIN_MENU;
  int menuIndex = 0;
  ButtonNavigator buttonNavigator;

  // Credits (shared with Casino)
  int32_t credits = 0;
  static constexpr const char* CASINO_SAVE_PATH = "/biscuit/casino.dat";
  static constexpr const char* COLLECTION_SAVE_PATH = "/biscuit/lootbox.dat";
  void loadCredits();
  void saveCredits();

  // Collection: 50 items, stored as bitfield (7 bytes = 56 bits, using first 50)
  uint8_t collected[7] = {};
  int totalCollected = 0;
  void loadCollection();
  void saveCollection();
  bool hasItem(int id) const;
  void setItem(int id);
  int countCollected() const;

  // Item database
  enum Rarity : uint8_t { COMMON, RARE, EPIC, LEGENDARY };
  struct Item {
    const char* name;
    Rarity rarity;
    uint8_t iconId;
  };
  static const Item ITEMS[50];
  static constexpr int ITEM_COUNT = 50;
  static constexpr int COMMON_COUNT = 20;
  static constexpr int RARE_COUNT = 15;
  static constexpr int EPIC_COUNT = 10;
  static constexpr int LEGENDARY_COUNT = 5;

  // Pull costs
  static constexpr int SINGLE_COST = 100;
  static constexpr int MULTI_COST = 450;
  static constexpr int MULTI_COUNT = 5;

  // Pull results
  int pullResults[5] = {};
  int pullCount = 0;
  bool pullIsNew[5] = {};
  int revealIndex = 0;

  // Animation state
  enum AnimState { ANIM_IDLE, ANIM_SHAKING, ANIM_OPENING, ANIM_REVEALED };
  AnimState animState = ANIM_IDLE;
  int animFrame = 0;
  unsigned long animStartMs = 0;
  static constexpr int SHAKE_FRAMES = 4;
  static constexpr unsigned long SHAKE_FRAME_MS = 150;
  static constexpr int OPEN_FRAMES = 3;
  static constexpr unsigned long OPEN_FRAME_MS = 250;

  // Collection browser
  int collectionPage = 0;
  int collectionCursor = 0;
  static constexpr int ITEMS_PER_PAGE = 15;
  static constexpr int GRID_COLS = 5;
  static constexpr int GRID_ROWS = 3;

  // Detail view
  int detailItemId = -1;

  // Gacha logic
  int rollItem(bool guaranteeRare = false);
  Rarity rollRarity(bool guaranteeRare = false);
  void performSinglePull();
  void performMultiPull();

  // Drawing helpers
  void renderMainMenu();
  void renderPulling();
  void renderRevealSingle();
  void renderRevealMulti();
  void renderCollection();
  void renderItemDetail();
  void drawItemIcon(int x, int y, int size, int iconId, bool locked) const;
  void drawLootBox(int cx, int cy, int size, int shakeOffset) const;
  void drawRarityBorder(int x, int y, int w, int h, Rarity rarity) const;
  void drawStars(int cx, int y, Rarity rarity) const;
  static const char* rarityName(Rarity r);

  // Dithering helpers
  static void fillDithered25(const GfxRenderer& r, int x, int y, int w, int h);
  static void fillDithered50(const GfxRenderer& r, int x, int y, int w, int h);
  static void fillDithered75(const GfxRenderer& r, int x, int y, int w, int h);
};
