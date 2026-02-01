//
// Created by keega on 9/24/2021.
//
#include <sstream>
#include <algorithm>

#include "sim/ConsoleSimulator.h"
#include "sim/search/ScumSearchAgent2.h"
#include "sim/SimHelpers.h"
#include "sim/PrintHelpers.h"
#include "game/Game.h"
#include "game/Map.h"
#include "combat/BattleContext.h"

#include "slaythespire.h"

namespace sts {

    NNInterface::NNInterface() :
            cardEncodeMap(createOneHotCardEncodingMap()),
            bossEncodeMap(createBossEncodingMap()) {}

    int NNInterface::getCardIdx(Card c) const {
        int idx = cardEncodeMap[static_cast<int>(c.id)] * 2;
        if (idx == -1) {
            std::cerr << "attemped to get encoding idx for invalid card" << std::endl;
            assert(false);
        }

        if (c.isUpgraded()) {
            idx += 1;
        }

        return idx;
    }

    std::array<int,NNInterface::observation_space_size> NNInterface::getObservation(const GameContext &gc) const {
        // 调用带 nullptr 的版本（无战斗信息）
        return getObservation(gc, nullptr);
    }

    std::array<int,NNInterface::observation_space_size> NNInterface::getObservation(const GameContext &gc, const BattleContext *bc) const {
        std::array<int,observation_space_size> ret {};

        int offset = 0;

        // ===== 基础信息 [0-3] =====
        ret[offset++] = std::min(gc.curHp, playerHpMax);
        ret[offset++] = std::min(gc.maxHp, playerHpMax);
        ret[offset++] = std::min(gc.gold, playerGoldMax);
        ret[offset++] = gc.floorNum;

        // ===== Boss One-Hot [4-13] =====
        int bossEncodeIdx = offset + bossEncodeMap.at(gc.boss);
        ret[bossEncodeIdx] = 1;
        offset += 10;

        // ===== 牌组卡牌数量 [14-233] =====
        for (auto c : gc.deck.cards) {
            int encodeIdx = offset + getCardIdx(c);
            ret[encodeIdx] = std::min(ret[encodeIdx]+1, cardCountMax);
        }
        offset += 220;

        // ===== 遗物 [234-411] =====
        for (auto r : gc.relics.relics) {
            int encodeIdx = offset + static_cast<int>(r.id);
            ret[encodeIdx] = 1;
        }
        offset += 178;

        // ===== 战斗信息（需要在战斗中才有意义） =====
        if (bc != nullptr && gc.screenState == ScreenState::BATTLE) {
            int battleOffset = 412;

            // ===== 玩家基础战斗状态 [412-415] =====
            ret[battleOffset++] = std::max(0, bc->player.energy);
            ret[battleOffset++] = std::max(0, bc->player.block);
            ret[battleOffset++] = bc->player.strength;
            ret[battleOffset++] = bc->player.dexterity;

            // ===== 敌人基础状态 [416-440] (5敌人 * 5属性) =====
            for (int i = 0; i < 5; ++i) {
                const auto &m = bc->monsters.arr[i];
                if (m.isAlive()) {
                    ret[battleOffset++] = std::max(0, m.curHp);
                    ret[battleOffset++] = std::max(0, m.block);
                    ret[battleOffset++] = m.strength;
                    ret[battleOffset++] = m.vulnerable;
                    ret[battleOffset++] = m.weak;
                } else {
                    battleOffset += 5;
                }
            }

            // ===== 手牌CardId [441-450] (兼容旧格式) =====
            for (int i = 0; i < 10; ++i) {
                if (i < bc->cards.cardsInHand) {
                    ret[battleOffset++] = static_cast<int>(bc->cards.hand[i].id);
                } else {
                    ret[battleOffset++] = 0;
                }
            }

            // ===== 敌人意图信息 [451-465] (5敌人 * 3属性) =====
            for (int i = 0; i < 5; ++i) {
                const auto &m = bc->monsters.arr[i];
                if (m.isAlive()) {
                    DamageInfo dInfo = m.getMoveBaseDamage(*bc);
                    int calculatedDamage = m.calculateDamageToPlayer(*bc, dInfo.damage);
                    ret[battleOffset++] = calculatedDamage;       // 计算后的单次伤害
                    ret[battleOffset++] = dInfo.attackCount;      // 攻击次数
                    ret[battleOffset++] = m.isAttacking() ? 1 : 0; // 是否攻击
                } else {
                    battleOffset += 3;
                }
            }

            // ===== 新增扩展信息 =====
            
            // ===== 手牌详细信息 [466-505] (10手牌 * 4属性) =====
            // 每张手牌: cost, costForTurn, type(0=攻击,1=技能,2=能力,3=状态,4=诅咒), upgraded
            for (int i = 0; i < 10; ++i) {
                if (i < bc->cards.cardsInHand) {
                    const auto &card = bc->cards.hand[i];
                    ret[battleOffset++] = std::max(0, static_cast<int>(card.cost));
                    ret[battleOffset++] = std::max(0, static_cast<int>(card.costForTurn));
                    ret[battleOffset++] = static_cast<int>(card.getType());
                    ret[battleOffset++] = card.upgraded ? 1 : 0;
                } else {
                    battleOffset += 4;
                }
            }

            // ===== 玩家状态/Buff/Debuff [506-545] (40维) =====
            const auto &p = bc->player;
            // Debuffs
            ret[battleOffset++] = p.hasStatusRuntime(PS::VULNERABLE) ? p.getStatusRuntime(PS::VULNERABLE) : 0;
            ret[battleOffset++] = p.hasStatusRuntime(PS::WEAK) ? p.getStatusRuntime(PS::WEAK) : 0;
            ret[battleOffset++] = p.hasStatusRuntime(PS::FRAIL) ? p.getStatusRuntime(PS::FRAIL) : 0;
            // 基础属性 (已在前面输出，这里是冗余但为了完整性)
            ret[battleOffset++] = p.strength;
            ret[battleOffset++] = p.dexterity;
            ret[battleOffset++] = p.artifact;
            ret[battleOffset++] = p.focus;
            // 防御相关
            ret[battleOffset++] = p.hasStatusRuntime(PS::METALLICIZE) ? p.getStatusRuntime(PS::METALLICIZE) : 0;
            ret[battleOffset++] = p.hasStatusRuntime(PS::PLATED_ARMOR) ? p.getStatusRuntime(PS::PLATED_ARMOR) : 0;
            ret[battleOffset++] = p.hasStatusRuntime(PS::THORNS) ? p.getStatusRuntime(PS::THORNS) : 0;
            ret[battleOffset++] = p.hasStatusRuntime(PS::REGEN) ? p.getStatusRuntime(PS::REGEN) : 0;
            ret[battleOffset++] = p.hasStatusRuntime(PS::INTANGIBLE) ? p.getStatusRuntime(PS::INTANGIBLE) : 0;
            ret[battleOffset++] = p.hasStatusRuntime(PS::BUFFER) ? p.getStatusRuntime(PS::BUFFER) : 0;
            ret[battleOffset++] = p.hasStatusRuntime(PS::BARRICADE) ? 1 : 0;
            // 进攻相关
            ret[battleOffset++] = p.hasStatusRuntime(PS::CORRUPTION) ? 1 : 0;
            ret[battleOffset++] = p.hasStatusRuntime(PS::DEMON_FORM) ? p.getStatusRuntime(PS::DEMON_FORM) : 0;
            ret[battleOffset++] = p.hasStatusRuntime(PS::NOXIOUS_FUMES) ? p.getStatusRuntime(PS::NOXIOUS_FUMES) : 0;
            ret[battleOffset++] = p.hasStatusRuntime(PS::AFTER_IMAGE) ? p.getStatusRuntime(PS::AFTER_IMAGE) : 0;
            ret[battleOffset++] = p.hasStatusRuntime(PS::COMBUST) ? p.getStatusRuntime(PS::COMBUST) : 0;
            ret[battleOffset++] = p.hasStatusRuntime(PS::DARK_EMBRACE) ? p.getStatusRuntime(PS::DARK_EMBRACE) : 0;
            ret[battleOffset++] = p.hasStatusRuntime(PS::EVOLVE) ? p.getStatusRuntime(PS::EVOLVE) : 0;
            ret[battleOffset++] = p.hasStatusRuntime(PS::FEEL_NO_PAIN) ? p.getStatusRuntime(PS::FEEL_NO_PAIN) : 0;
            ret[battleOffset++] = p.hasStatusRuntime(PS::FIRE_BREATHING) ? p.getStatusRuntime(PS::FIRE_BREATHING) : 0;
            ret[battleOffset++] = p.hasStatusRuntime(PS::INFINITE_BLADES) ? p.getStatusRuntime(PS::INFINITE_BLADES) : 0;
            ret[battleOffset++] = p.hasStatusRuntime(PS::RAGE) ? p.getStatusRuntime(PS::RAGE) : 0;
            ret[battleOffset++] = p.hasStatusRuntime(PS::RUPTURE) ? p.getStatusRuntime(PS::RUPTURE) : 0;
            ret[battleOffset++] = p.hasStatusRuntime(PS::VIGOR) ? p.getStatusRuntime(PS::VIGOR) : 0;
            ret[battleOffset++] = p.hasStatusRuntime(PS::DOUBLE_TAP) ? p.getStatusRuntime(PS::DOUBLE_TAP) : 0;
            ret[battleOffset++] = p.hasStatusRuntime(PS::BURST) ? p.getStatusRuntime(PS::BURST) : 0;
            // 限制类
            ret[battleOffset++] = p.hasStatusRuntime(PS::DRAW_REDUCTION) ? 1 : 0;
            ret[battleOffset++] = p.hasStatusRuntime(PS::ENTANGLED) ? 1 : 0;
            ret[battleOffset++] = p.hasStatusRuntime(PS::NO_DRAW) ? 1 : 0;
            ret[battleOffset++] = p.hasStatusRuntime(PS::CONFUSED) ? 1 : 0;
            ret[battleOffset++] = p.hasStatusRuntime(PS::HEX) ? 1 : 0;
            ret[battleOffset++] = p.hasStatusRuntime(PS::WRAITH_FORM) ? p.getStatusRuntime(PS::WRAITH_FORM) : 0;
            // Watcher相关
            ret[battleOffset++] = p.hasStatusRuntime(PS::MANTRA) ? p.getStatusRuntime(PS::MANTRA) : 0;
            ret[battleOffset++] = (p.stance == Stance::DIVINITY) ? 1 : 0;
            ret[battleOffset++] = (p.stance == Stance::CALM) ? 1 : 0;
            ret[battleOffset++] = (p.stance == Stance::WRATH) ? 1 : 0;
            // Defect相关
            ret[battleOffset++] = p.orbSlots;

            // ===== 敌人详细状态 [546-595] (5敌人 * 10状态) =====
            for (int i = 0; i < 5; ++i) {
                const auto &m = bc->monsters.arr[i];
                if (m.isAlive()) {
                    ret[battleOffset++] = m.poison;
                    ret[battleOffset++] = m.artifact;
                    ret[battleOffset++] = m.hasStatus<MS::INTANGIBLE>() ? m.getStatus<MS::INTANGIBLE>() : 0;
                    ret[battleOffset++] = m.hasStatus<MS::THORNS>() ? m.getStatus<MS::THORNS>() : 0;
                    ret[battleOffset++] = m.hasStatus<MS::CURL_UP>() ? m.getStatus<MS::CURL_UP>() : 0;
                    ret[battleOffset++] = m.hasStatus<MS::MODE_SHIFT>() ? m.getStatus<MS::MODE_SHIFT>() : 0;
                    ret[battleOffset++] = m.hasStatus<MS::RITUAL>() ? m.getStatus<MS::RITUAL>() : 0;
                    ret[battleOffset++] = m.regen;
                    ret[battleOffset++] = m.metallicize;
                    ret[battleOffset++] = m.shackled;
                } else {
                    battleOffset += 10;
                }
            }

            // ===== 牌堆信息 [596-599] =====
            ret[battleOffset++] = static_cast<int>(bc->cards.drawPile.size());
            ret[battleOffset++] = static_cast<int>(bc->cards.discardPile.size());
            ret[battleOffset++] = static_cast<int>(bc->cards.exhaustPile.size());
            ret[battleOffset++] = bc->turn;

            // ===== 药水信息 [600-604] =====
            for (int i = 0; i < 5; ++i) {
                if (i < bc->potionCount) {
                    ret[battleOffset++] = static_cast<int>(bc->potions[i]);
                } else {
                    ret[battleOffset++] = 0;
                }
            }
        }

        return ret;
    }

    std::array<int,NNInterface::observation_space_size> NNInterface::getObservationMaximums() const {
        std::array<int,observation_space_size> ret {};
        int spaceOffset = 0;

        // ===== 基础信息 [0-3] =====
        ret[spaceOffset++] = playerHpMax;  // curHp
        ret[spaceOffset++] = playerHpMax;  // maxHp
        ret[spaceOffset++] = playerGoldMax; // gold
        ret[spaceOffset++] = 60;           // floorNum

        // ===== Boss One-Hot [4-13] =====
        std::fill(ret.begin()+spaceOffset, ret.begin()+spaceOffset+10, 1);
        spaceOffset += 10;

        // ===== 牌组卡牌数量 [14-233] =====
        std::fill(ret.begin()+spaceOffset, ret.begin()+spaceOffset+220, cardCountMax);
        spaceOffset += 220;

        // ===== 遗物 [234-411] =====
        std::fill(ret.begin()+spaceOffset, ret.begin()+spaceOffset+178, 1);
        spaceOffset += 178;

        // ===== 玩家基础战斗状态 [412-415] =====
        ret[spaceOffset++] = 10;   // 能量
        ret[spaceOffset++] = 999;  // 护甲
        ret[spaceOffset++] = 50;   // 力量
        ret[spaceOffset++] = 50;   // 敏捷

        // ===== 敌人基础状态 [416-440] (5敌人 * 5属性) =====
        for (int i = 0; i < 5; ++i) {
            ret[spaceOffset++] = 500;  // 敌人 HP (boss可能更高)
            ret[spaceOffset++] = 999;  // 敌人护甲
            ret[spaceOffset++] = 50;   // 敌人力量
            ret[spaceOffset++] = 50;   // 敌人易伤
            ret[spaceOffset++] = 50;   // 敌人虚弱
        }

        // ===== 手牌CardId [441-450] =====
        for (int i = 0; i < 10; ++i) {
            ret[spaceOffset++] = 400;  // CardId 上限
        }

        // ===== 敌人意图 [451-465] (5敌人 * 3属性) =====
        for (int i = 0; i < 5; ++i) {
            ret[spaceOffset++] = 200;  // intentDamage (计算后单次伤害)
            ret[spaceOffset++] = 20;   // intentHits (攻击次数)
            ret[spaceOffset++] = 1;    // isAttacking (0/1)
        }

        // ===== 新增扩展信息 =====

        // ===== 手牌详细信息 [466-505] (10手牌 * 4属性) =====
        for (int i = 0; i < 10; ++i) {
            ret[spaceOffset++] = 10;   // cost
            ret[spaceOffset++] = 10;   // costForTurn
            ret[spaceOffset++] = 4;    // type (0-4)
            ret[spaceOffset++] = 1;    // upgraded (0/1)
        }

        // ===== 玩家状态/Buff/Debuff [506-545] (40维) =====
        ret[spaceOffset++] = 20;   // VULNERABLE
        ret[spaceOffset++] = 20;   // WEAK
        ret[spaceOffset++] = 20;   // FRAIL
        ret[spaceOffset++] = 50;   // strength (冗余)
        ret[spaceOffset++] = 50;   // dexterity (冗余)
        ret[spaceOffset++] = 20;   // artifact
        ret[spaceOffset++] = 20;   // focus
        ret[spaceOffset++] = 20;   // METALLICIZE
        ret[spaceOffset++] = 30;   // PLATED_ARMOR
        ret[spaceOffset++] = 20;   // THORNS
        ret[spaceOffset++] = 20;   // REGEN
        ret[spaceOffset++] = 10;   // INTANGIBLE
        ret[spaceOffset++] = 10;   // BUFFER
        ret[spaceOffset++] = 1;    // BARRICADE
        ret[spaceOffset++] = 1;    // CORRUPTION
        ret[spaceOffset++] = 20;   // DEMON_FORM
        ret[spaceOffset++] = 20;   // NOXIOUS_FUMES
        ret[spaceOffset++] = 20;   // AFTER_IMAGE
        ret[spaceOffset++] = 20;   // COMBUST
        ret[spaceOffset++] = 10;   // DARK_EMBRACE
        ret[spaceOffset++] = 10;   // EVOLVE
        ret[spaceOffset++] = 20;   // FEEL_NO_PAIN
        ret[spaceOffset++] = 10;   // FIRE_BREATHING
        ret[spaceOffset++] = 10;   // INFINITE_BLADES
        ret[spaceOffset++] = 20;   // RAGE
        ret[spaceOffset++] = 10;   // RUPTURE
        ret[spaceOffset++] = 50;   // VIGOR
        ret[spaceOffset++] = 5;    // DOUBLE_TAP
        ret[spaceOffset++] = 5;    // BURST
        ret[spaceOffset++] = 1;    // DRAW_REDUCTION
        ret[spaceOffset++] = 1;    // ENTANGLED
        ret[spaceOffset++] = 1;    // NO_DRAW
        ret[spaceOffset++] = 1;    // CONFUSED
        ret[spaceOffset++] = 1;    // HEX
        ret[spaceOffset++] = 10;   // WRAITH_FORM
        ret[spaceOffset++] = 10;   // MANTRA
        ret[spaceOffset++] = 1;    // DIVINITY stance
        ret[spaceOffset++] = 1;    // CALM stance
        ret[spaceOffset++] = 1;    // WRATH stance
        ret[spaceOffset++] = 10;   // orbSlots

        // ===== 敌人详细状态 [546-595] (5敌人 * 10状态) =====
        for (int i = 0; i < 5; ++i) {
            ret[spaceOffset++] = 99;   // poison
            ret[spaceOffset++] = 10;   // artifact
            ret[spaceOffset++] = 10;   // intangible
            ret[spaceOffset++] = 20;   // thorns
            ret[spaceOffset++] = 20;   // curl_up
            ret[spaceOffset++] = 100;  // mode_shift
            ret[spaceOffset++] = 10;   // ritual
            ret[spaceOffset++] = 30;   // regen
            ret[spaceOffset++] = 30;   // metallicize
            ret[spaceOffset++] = 20;   // shackled
        }

        // ===== 牌堆信息 [596-599] =====
        ret[spaceOffset++] = 50;   // drawPile size
        ret[spaceOffset++] = 50;   // discardPile size
        ret[spaceOffset++] = 50;   // exhaustPile size
        ret[spaceOffset++] = 20;   // turn

        // ===== 药水信息 [600-604] =====
        for (int i = 0; i < 5; ++i) {
            ret[spaceOffset++] = 100;  // potion ID
        }

        // Note: Total should be 605 dimensions (indices 0-604)
        // We declared 609 to leave small buffer, unused slots are 0

        return ret;
    }

    std::vector<int> NNInterface::createOneHotCardEncodingMap() {
        std::vector<CardId> redCards;
        for (int i = static_cast<int>(CardId::INVALID); i <= static_cast<int>(CardId::ZAP); ++i) {
            auto cid = static_cast<CardId>(i);
            auto color = getCardColor(cid);
            if (color == CardColor::RED) {
                redCards.push_back(cid);
            }
        }

        std::vector<CardId> colorlessCards;
        for (int i = 0; i < srcColorlessCardPoolSize; ++i) {
            colorlessCards.push_back(srcColorlessCardPool[i]);
        }
        std::sort(colorlessCards.begin(), colorlessCards.end(), [](auto a, auto b) {
            return std::string(getCardEnumName(a)) < std::string(getCardEnumName(b));
        });

        std::vector<int> encodingMap(372);
        std::fill(encodingMap.begin(), encodingMap.end(), 0);

        int hotEncodingIdx = 0;
        for (auto x : redCards) {
            encodingMap[static_cast<int>(x)] = hotEncodingIdx++;
        }
        for (auto x : colorlessCards) {
            encodingMap[static_cast<int>(x)] = hotEncodingIdx++;
        }

        return encodingMap;
    }

    std::unordered_map<MonsterEncounter, int> NNInterface::createBossEncodingMap() {
        std::unordered_map<MonsterEncounter, int> bossMap;
        bossMap[ME::SLIME_BOSS] = 0;
        bossMap[ME::HEXAGHOST] = 1;
        bossMap[ME::THE_GUARDIAN] = 2;
        bossMap[ME::CHAMP] = 3;
        bossMap[ME::AUTOMATON] = 4;
        bossMap[ME::COLLECTOR] = 5;
        bossMap[ME::TIME_EATER] = 6;
        bossMap[ME::DONU_AND_DECA] = 7;
        bossMap[ME::AWAKENED_ONE] = 8;
        bossMap[ME::THE_HEART] = 9;
        return bossMap;
    }

    NNInterface* NNInterface::getInstance() {
        if (theInstance == nullptr) {
            theInstance = new NNInterface;
        }
        return theInstance;
    }

}

namespace sts::py {

    void play() {
        sts::SimulatorContext ctx;
        sts::ConsoleSimulator sim;
        sim.play(std::cin, std::cout, ctx);
    }

    search::ScumSearchAgent2* getAgent() {
        static search::ScumSearchAgent2 *agent = nullptr;
        if (agent == nullptr) {
            agent = new search::ScumSearchAgent2();
            agent->pauseOnCardReward = true;
        }
        return agent;
    }

    void playout(GameContext &gc) {
        auto agent = getAgent();
        agent->playout(gc);
    }

    std::vector<Card> getCardReward(GameContext &gc) {
        const bool inValidState = gc.outcome == GameOutcome::UNDECIDED &&
                                  gc.screenState == ScreenState::REWARDS &&
                                  gc.info.rewardsContainer.cardRewardCount > 0;

        if (!inValidState) {
            std::cerr << "GameContext was not in a state with card rewards, check that the game has not completed first." << std::endl;
            return {};
        }

        const auto &r = gc.info.rewardsContainer;
        const auto &cardList = r.cardRewards[r.cardRewardCount-1];
        return std::vector<Card>(cardList.begin(), cardList.end());
    }

    void pickRewardCard(GameContext &gc, Card card) {
        const bool inValidState = gc.outcome == GameOutcome::UNDECIDED &&
                                  gc.screenState == ScreenState::REWARDS &&
                                  gc.info.rewardsContainer.cardRewardCount > 0;
        if (!inValidState) {
            std::cerr << "GameContext was not in a state with card rewards, check that the game has not completed first." << std::endl;
            return;
        }
        auto &r = gc.info.rewardsContainer;
        gc.deck.obtain(gc, card);
        r.removeCardReward(r.cardRewardCount-1);
    }

    void skipRewardCards(GameContext &gc) {
        const bool inValidState = gc.outcome == GameOutcome::UNDECIDED &&
                                  gc.screenState == ScreenState::REWARDS &&
                                  gc.info.rewardsContainer.cardRewardCount > 0;
        if (!inValidState) {
            std::cerr << "GameContext was not in a state with card rewards, check that the game has not completed first." << std::endl;
            return;
        }

        if (gc.hasRelic(RelicId::SINGING_BOWL)) {
            gc.playerIncreaseMaxHp(2);
        }

        auto &r = gc.info.rewardsContainer;
        r.removeCardReward(r.cardRewardCount-1);
    }



    // BEGIN MAP THINGS ****************************

    std::vector<int> getNNMapRepresentation(const Map &map) {
        std::vector<int> ret;

        // 7 bits
        // push edges to first row
        for (int x = 0; x < 7; ++x) {
            if (map.getNode(x,0).edgeCount > 0) {
                ret.push_back(true);
            } else {
                ret.push_back(false);
            }
        }

        // for each node in a row, push valid edges to next row, 3 bits per node, 21 bits per row
        // skip 14th row because it is invariant
        // 21 * 13 == 273 bits
        for (int y = 0; y < 14; ++y) {
            for (int x = 0; x < 7; ++x) {

                bool localEdgeValues[3] {false, false, false};
                auto node = map.getNode(x,y);
                for (int i = 0; i < node.edgeCount; ++i) {
                    auto edge = node.edges[i];
                    if (edge < x) {
                        localEdgeValues[0] = true;
                    } else if (edge == x) {
                        localEdgeValues[1] = true;
                    } else {
                        localEdgeValues[2] = true;
                    }
                }
                ret.insert(ret.end(), localEdgeValues, localEdgeValues+3);
            }
        }

        // room types - for each node there are 6 possible rooms,
        // the first row is always monster, the 8th row is always treasure, 14th is always rest
        // this gives 14-3 valid rows == 11
        // 11 * 6 * 7 = 462 bits
        for (int y = 1; y < 14; ++y) {
            if (y == 8) {
                continue;
            }
            for (int x = 0; x < 7; ++x) {
                auto roomType = map.getNode(x,y).room;
                for (int i = 0; i < 6; ++i) {
                    ret.push_back(static_cast<int>(roomType) == i);
                }
            }
        }

        return ret;
    };

    Room getRoomType(const Map &map, int x, int y) {
        if (x < 0 || x > 6 || y < 0 || y > 14) {
            return Room::INVALID;
        }

        return map.getNode(x,y).room;
    }

    bool hasEdge(const Map &map, int x, int y, int x2) {
        if (x == -1) {
            return map.getNode(x2,0).edgeCount > 0;
        }

        if (x < 0 || x > 6 || y < 0 || y > 14) {
            return false;
        }


        auto node = map.getNode(x,y);
        for (int i = 0; i < node.edgeCount; ++i) {
            if (node.edges[i] == x2) {
                return true;
            }
        }
        return false;
    }

}
