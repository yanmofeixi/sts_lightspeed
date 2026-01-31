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

        ret[offset++] = std::min(gc.curHp, playerHpMax);
        ret[offset++] = std::min(gc.maxHp, playerHpMax);
        ret[offset++] = std::min(gc.gold, playerGoldMax);
        ret[offset++] = gc.floorNum;

        int bossEncodeIdx = offset + bossEncodeMap.at(gc.boss);
        ret[bossEncodeIdx] = 1;
        offset += 10;

        for (auto c : gc.deck.cards) {
            int encodeIdx = offset + getCardIdx(c);
            ret[encodeIdx] = std::min(ret[encodeIdx]+1, cardCountMax);
        }
        offset += 220;

        for (auto r : gc.relics.relics) {
            int encodeIdx = offset + static_cast<int>(r.id);
            ret[encodeIdx] = 1;
        }
        offset += 178;

        // ===== 战斗信息（可选扩展） =====
        // 需要在战斗中才有意义
        // 维度规划:
        // [412-415] 玩家战斗状态：能量、护甲、力量、敏捷
        // [416-420] 敌人 0: HP, Block, Strength, Vulnerable, Weak
        // [421-425] 敌人 1: HP, Block, Strength, Vulnerable, Weak
        // [426-430] 敌人 2: HP, Block, Strength, Vulnerable, Weak
        // [431-435] 敌人 3: HP, Block, Strength, Vulnerable, Weak
        // [436-440] 敌人 4: HP, Block, Strength, Vulnerable, Weak
        // [441-450] 手牌：10 张，每张编码为 CardId（无牌为 0）
        // 总计新增 39 维 -> 451
        if (bc != nullptr && gc.screenState == ScreenState::BATTLE) {
            int battleOffset = 412;

            ret[battleOffset++] = std::max(0, bc->player.energy);
            ret[battleOffset++] = std::max(0, bc->player.block);
            ret[battleOffset++] = bc->player.strength;
            ret[battleOffset++] = bc->player.dexterity;

            for (int i = 0; i < 5; ++i) {
                const auto &m = bc->monsters.arr[i];
                if (m.isAlive()) {
                    ret[battleOffset++] = std::max(0, m.curHp);
                    ret[battleOffset++] = std::max(0, m.block);
                    ret[battleOffset++] = m.strength;
                    ret[battleOffset++] = m.vulnerable;
                    ret[battleOffset++] = m.weak;
                } else {
                    ret[battleOffset++] = 0;
                    ret[battleOffset++] = 0;
                    ret[battleOffset++] = 0;
                    ret[battleOffset++] = 0;
                    ret[battleOffset++] = 0;
                }
            }

            for (int i = 0; i < 10; ++i) {
                if (i < bc->cards.cardsInHand) {
                    ret[battleOffset++] = static_cast<int>(bc->cards.hand[i].id);
                } else {
                    ret[battleOffset++] = 0;
                }
            }

            // ===== 敌人意图信息 (15维) =====
            // [451-465] 每个敌人 3 维：intentDamage, intentHits, isAttacking
            for (int i = 0; i < 5; ++i) {
                const auto &m = bc->monsters.arr[i];
                if (m.isAlive()) {
                    DamageInfo dInfo = m.getMoveBaseDamage(*bc);
                    int calculatedDamage = m.calculateDamageToPlayer(*bc, dInfo.damage);
                    ret[battleOffset++] = calculatedDamage;       // 计算后的单次伤害
                    ret[battleOffset++] = dInfo.attackCount;      // 攻击次数
                    ret[battleOffset++] = m.isAttacking() ? 1 : 0; // 是否攻击
                } else {
                    ret[battleOffset++] = 0;
                    ret[battleOffset++] = 0;
                    ret[battleOffset++] = 0;
                }
            }
        }

        return ret;
    }

    std::array<int,NNInterface::observation_space_size> NNInterface::getObservationMaximums() const {
        std::array<int,observation_space_size> ret {};
        int spaceOffset = 0;

        ret[0] = playerHpMax;
        ret[1] = playerHpMax;
        ret[2] = playerGoldMax;
        ret[3] = 60;
        spaceOffset += 3;

        std::fill(ret.begin()+spaceOffset, ret.end(), 1);
        spaceOffset += 10;

        std::fill(ret.begin()+spaceOffset, ret.end(), cardCountMax);
        spaceOffset += 220;

        std::fill(ret.begin()+spaceOffset, ret.end(), 1);
        spaceOffset += 178;

        // ===== 战斗扩展部分最大值 =====
        // 注意: 这些最大值用于归一化，不需要非常精确
        ret[spaceOffset++] = 10;  // 能量
        ret[spaceOffset++] = 999; // 护甲
        ret[spaceOffset++] = 50;  // 力量
        ret[spaceOffset++] = 50;  // 敏捷

        for (int i = 0; i < 5; ++i) {
            ret[spaceOffset++] = playerHpMax; // 敌人 HP
            ret[spaceOffset++] = 999;         // 敌人护甲
            ret[spaceOffset++] = 50;          // 敌人力量
            ret[spaceOffset++] = 50;          // 敌人易伤
            ret[spaceOffset++] = 50;          // 敌人虚弱
        }

        for (int i = 0; i < 10; ++i) {
            ret[spaceOffset++] = 400; // CardId 上限粗略估计
        }

        // ===== 敌人意图部分最大值 =====
        for (int i = 0; i < 5; ++i) {
            ret[spaceOffset++] = 200;  // intentDamage (计算后单次伤害)
            ret[spaceOffset++] = 20;   // intentHits (攻击次数)
            ret[spaceOffset++] = 1;    // isAttacking (0/1)
        }

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
