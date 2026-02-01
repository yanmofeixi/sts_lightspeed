//
// Created by keega on 9/24/2021.
//

#ifndef STS_LIGHTSPEED_SLAYTHESPIRE_H
#define STS_LIGHTSPEED_SLAYTHESPIRE_H

#include <vector>
#include <unordered_map>
#include <array>

#include "constants/Rooms.h"

namespace sts {

    struct NNInterface {
        // =====================================================================
        // 观察空间维度规划 (Observation Space Layout)
        // =====================================================================
        // 基础信息 (Basic Info):
        //   [0-3]     curHp, maxHp, gold, floorNum (4维)
        //   [4-13]    boss one-hot encoding (10维)
        //   [14-233]  deck cards count (220维, 110卡牌 * 2升级状态)
        //   [234-411] relics (178维)
        // 
        // 战斗信息 (Combat Info) - 仅战斗中有效:
        //   [412-415] 玩家基础: 能量, 护甲, 力量, 敏捷 (4维)
        //   [416-440] 敌人基础 x5: HP, Block, Strength, Vulnerable, Weak (25维)
        //   [441-450] 手牌CardId (10维) - 保留兼容
        //   [451-465] 敌人意图 x5: damage, hits, isAttacking (15维)
        //
        // === 新增扩展信息 ===
        // 手牌详细信息 (Hand Details):
        //   [466-505] 手牌 x10: cost, costForTurn, type, upgraded (40维)
        //
        // 玩家Buff/Debuff (Player Status):
        //   [506-545] 关键状态 x40: vulnerable, weak, frail, strength, dexterity,
        //             artifact, metallicize, platedArmor, thorns, regen,
        //             intangible, buffer, barricade, corruption, demonForm,
        //             noxiousFumes, afterImage, combust, darkEmbrace, evolve,
        //             feelNoPain, fireBreathing, infiniteBlades, rage, rupture,
        //             vigor, doubleTap, burst, draw_reduction, entangled,
        //             no_draw, confused, hex, wraith_form, mantra, divinity,
        //             calm, wrath, focus, orbSlots (40维)
        //
        // 敌人详细状态 (Enemy Status):
        //   [546-595] 敌人 x5 x10: poison, artifact, intangible, thorns, 
        //             curl_up, mode_shift, ritual, regen, metallicize, shackled (50维)
        //
        // 牌堆信息 (Pile Info):
        //   [596-599] drawPileSize, discardPileSize, exhaustPileSize, turn (4维)
        //
        // 药水信息 (Potions):
        //   [600-604] potion x5: potionId (5维)
        //
        // 总计: 609 维
        // =====================================================================
        
        static constexpr int observation_space_size = 609;
        static constexpr int playerHpMax = 200;
        static constexpr int playerGoldMax = 1800;
        static constexpr int cardCountMax = 7;
        
        // 扩展部分的维度常量
        static constexpr int HAND_DETAIL_SIZE = 40;      // 10张手牌 * 4属性
        static constexpr int PLAYER_STATUS_SIZE = 40;    // 玩家状态
        static constexpr int ENEMY_STATUS_SIZE = 50;     // 5敌人 * 10状态
        static constexpr int PILE_INFO_SIZE = 4;         // 牌堆信息
        static constexpr int POTION_SIZE = 5;            // 药水信息

        const std::vector<int> cardEncodeMap;
        const std::unordered_map<MonsterEncounter, int> bossEncodeMap;

        static inline NNInterface *theInstance = nullptr;

        NNInterface();

        int getCardIdx(Card c) const;
        std::array<int,observation_space_size> getObservationMaximums() const;
        std::array<int,observation_space_size> getObservation(const GameContext &gc) const;
        std::array<int,observation_space_size> getObservation(const GameContext &gc, const BattleContext *bc) const;


        static std::vector<int> createOneHotCardEncodingMap();
        static std::unordered_map<MonsterEncounter, int> createBossEncodingMap();
        static NNInterface* getInstance();

    };

    namespace search {
        class ScumSearchAgent2;
    }


    class GameContext;
    class BattleContext;
    class Map;

    namespace py {

        void play();

        search::ScumSearchAgent2* getAgent();
        void setGc(const GameContext &gc);
        GameContext* getGc();

        void playout();
        std::vector<Card> getCardReward(GameContext &gc);
        void pickRewardCard(GameContext &gc, Card card);
        void skipRewardCards(GameContext &gc);

        std::vector<int> getNNMapRepresentation(const Map &map);
        Room getRoomType(const Map &map, int x, int y);
        bool hasEdge(const Map &map, int x, int y, int x2);
    }


}


#endif //STS_LIGHTSPEED_SLAYTHESPIRE_H
