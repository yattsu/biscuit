#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

class CasinoActivity final : public Activity {
 public:
  explicit CasinoActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Casino", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override { return true; }

 private:
  // ---- top-level state ----
  enum Screen { LOBBY, SLOTS, BLACKJACK, COINFLIP, HIGHLOW, ROULETTE };
  Screen screen = LOBBY;
  int lobbyIndex = 0;
  int resetConfirmCount = 0;
  static constexpr int LOBBY_COUNT = 6;  // Slots, BJ, Coin, H/L, Roulette, Reset
  ButtonNavigator buttonNavigator;

  // ---- credits ----
  int32_t credits = 1000;
  static constexpr const char* SAVE_PATH = "/biscuit/casino.dat";
  void loadCredits();
  void saveCredits();

  // ---- shared ----
  static constexpr int BET_OPTIONS[] = {10, 25, 50, 100, 250, 500, 1000, 2500, 5000, 10000, 25000, 50000, 100000};
  static constexpr int NUM_BETS = 13;
  int betIndex = 1;  // default 25
  int currentBet() const { return BET_OPTIONS[betIndex]; }
  std::string resultMessage;
  int resultAmount = 0;  // positive=win, negative=loss
  bool showingResult = false;

  // ============ SLOTS (overhauled) ============
  struct SlotSymbolSet {
    uint8_t ids[8];
    int8_t payouts[8];  // 3-match multiplier per symbol
  };

  struct SlotMachineType {
    const char* name;
    const char* description;
    uint8_t numSymbols;
    SlotSymbolSet symbols;
    int8_t twoMatchMult;   // payout for 2-of-a-kind (0 = none)
    uint8_t wildSymbolIdx; // 0xFF = no wild
    bool hasFreeSpin;
    bool hasHoldReel;
    int16_t minBet;
  };

  static constexpr int NUM_MACHINES = 5;
  static const SlotMachineType MACHINES[NUM_MACHINES];

  enum SlotsScreen { SLOTS_MENU, SLOTS_PLAY, SLOTS_PAYOUTS, SLOTS_POWERUPS };
  enum SlotsPlayState { SP_BET, SP_HOLD_SELECT, SP_SPIN, SP_RESULT };

  SlotsScreen slotsScreen = SLOTS_MENU;
  SlotsPlayState slotsPlayState = SP_BET;
  int slotsMachineIndex = 0;
  int slotsMachineMenuIndex = 0;

  int reels[3] = {0, 0, 0};
  bool reelHold[3] = {false, false, false};
  int holdCursor = 0;

  int animFrame = 0;
  unsigned long animStartMs = 0;
  static constexpr int SPIN_FRAMES = 12;
  static constexpr unsigned long SPIN_FRAME_MS = 120;
  int reelStopFrame[3] = {8, 10, 12};  // when each reel locks to final value
  int finalReels[3] = {0, 0, 0};
  bool slotsShowLastResult = false;

  struct SlotsPowerups {
    uint8_t freeSpins = 0;
    uint8_t multiplier = 1;
    bool wildActive = false;
  };
  SlotsPowerups slotsPowerups;
  int powerupMenuIndex = 0;

  void slotsLoop();
  void slotsMenuLoop();
  void slotsPlayLoop();
  void slotsPayoutsLoop();
  void slotsPowerupsLoop();
  void slotsSpin();
  void slotsEvaluate();

  void slotsRender();
  void slotsRenderMenu();
  void slotsRenderPlay();
  void slotsRenderPayouts();
  void slotsRenderPowerups();
  void drawSlotReel(int x, int y, int symbolId, bool held, bool blur, int blurFrame);
  void drawReelBlur(int x, int y, int w, int h, int frameOffset);
  void drawSlotIcon(int cx, int cy, uint8_t symbolId);

  // ============ BLACKJACK ============
  enum BJState { BJ_BET, BJ_PLAYING, BJ_DEALER, BJ_RESULT };
  BJState bjState = BJ_BET;

  struct Card { uint8_t rank; uint8_t suit; };  // rank 1-13, suit 0-3
  static constexpr const char* SUIT_CHARS[] = {"S", "H", "D", "C"};
  static constexpr const char* RANK_CHARS[] = {"", "A", "2", "3", "4", "5", "6", "7", "8", "9", "10", "J", "Q", "K"};

  Card deck[52];
  int deckPos = 0;
  std::vector<Card> playerHand;
  std::vector<Card> dealerHand;
  bool dealerRevealed = false;

  void bjShuffle();
  Card bjDraw();
  int bjHandValue(const std::vector<Card>& hand) const;
  bool bjIsBlackjack(const std::vector<Card>& hand) const;
  std::string bjCardStr(const Card& c) const;
  void bjDeal();
  void bjDealerPlay();
  void bjEvaluate();
  void bjLoop();
  void bjRender();

  // ============ COIN FLIP ============
  enum CoinState { COIN_BET, COIN_PICK, COIN_FLIPPING, COIN_RESULT };
  CoinState coinState = COIN_BET;
  int coinPick = 0;  // 0=heads, 1=tails
  int coinResult = 0;
  int coinAnimFrame = 0;
  unsigned long coinAnimStartMs = 0;
  static constexpr int COIN_ANIM_FRAMES = 5;
  static constexpr unsigned long COIN_FRAME_MS = 200;

  void coinLoop();
  void coinFlip();
  void coinRender();

  // ============ HIGHER / LOWER ============
  enum HLState { HL_BET, HL_PLAYING, HL_RESULT };
  HLState hlState = HL_BET;
  Card hlCurrentCard;
  Card hlNextCard;
  int hlStreak = 0;
  int hlPot = 0;

  void hlLoop();
  void hlDraw();
  void hlGuess(bool higher);
  void hlCashOut();
  void hlRender();

  // ============ ROULETTE ============
  enum RLState { RL_BET, RL_PICK, RL_RESULT };
  RLState rlState = RL_BET;

  enum RLBetType { RL_RED, RL_BLACK, RL_ODD, RL_EVEN, RL_LOW, RL_HIGH, RL_DOZ1, RL_DOZ2, RL_DOZ3, RL_NUMBER };
  static constexpr int RL_NUM_BET_TYPES = 10;
  RLBetType rlBetType = RL_RED;
  int rlNumber = 1;  // 0-36 for straight bet
  int rlResult = 0;  // winning number 0-36

  static bool isRed(int n);
  static const char* rlBetName(RLBetType t, int num, char* buf, int bufsz);
  bool rlCellHighlighted(int n) const;
  void rlLoop();
  void rlSpin();
  void rlRender();

  void resetCredits() { credits = 1000; saveCredits(); }

  // ---- rendering helpers ----
  void renderLobby();
  void renderCreditsBar();
  void renderBetSelector(int y);
  void drawCard(int x, int y, const Card& c, bool faceDown = false);
};
