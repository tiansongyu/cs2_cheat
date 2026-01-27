/**
 * CS2 核心数据结构关系图 - C++ 伪代码
 *
 * 本文件用 C++ 伪代码展示 Entity、Controller、Pawn、Handle 之间的关系
 * 所有结构体直接展开，不使用继承，提高可读性
 */

#pragma once
#include <cstdint>

namespace cs2 {

// ============================================================================
// 前向声明
// ============================================================================
struct C_CSPlayerPawn;
struct CCSPlayerController;

// ============================================================================
// 基础类型
// ============================================================================
struct Vector { float x, y, z; };

// ============================================================================
// Handle - 实体的安全引用（不是直接指针！）
// ============================================================================
struct CHandle_Pawn {
    uint32_t value;  // 低15位=索引, 高位=序列号

    uint32_t GetIndex() { return value & 0x7FFF; }
    bool IsValid() { return value != 0xFFFFFFFF && value != 0; }

    // 解析后得到: C_CSPlayerPawn*
};

struct CHandle_Controller {
    uint32_t value;

    uint32_t GetIndex() { return value & 0x7FFF; }
    bool IsValid() { return value != 0xFFFFFFFF && value != 0; }

    // 解析后得到: CCSPlayerController*
};

// ============================================================================
// EntityList - 全局实体列表（两级索引）
// ============================================================================
struct EntityChunk {
    void* entities[512];  // 每个Chunk存512个实体指针，步长0x70
};

struct EntityList {
    uint8_t _pad_0x00[0x10];        // +0x00: 填充
    EntityChunk* chunks[64];         // +0x10: 64个Chunk指针

    // 通过索引获取实体
    void* GetByIndex(uint32_t index) {
        uint32_t chunkIdx = index / 512;
        uint32_t inChunk = index % 512;
        if (!chunks[chunkIdx]) return nullptr;
        return chunks[chunkIdx]->entities[inChunk];
    }
};

// ============================================================================
// C_CSPlayerPawn - 玩家的物理身体（完全展开，无继承）
// ============================================================================
struct C_CSPlayerPawn {
    // -------- 来自 C_BaseEntity --------
    uint8_t _pad_0x00[0x354];
    int32_t m_iHealth;               // +0x354: 血量
    uint8_t _pad_0x358[0x4];
    uint8_t m_lifeState;             // +0x35C: 生存状态 (0=活着)
    uint8_t _pad_0x35D[0x96];
    uint8_t m_iTeamNum;              // +0x3F3: 队伍 (2=T, 3=CT)

    // -------- 来自 C_BasePlayerPawn --------
    uint8_t _pad_0x3F4[0x1194];
    Vector m_vOldOrigin;             // +0x1588: 3D世界坐标
    uint8_t _pad_0x1594[0xC];
    CHandle_Controller m_hController;// +0x15A0: ★ 指向控制此Pawn的Controller

    // -------- C_CSPlayerPawn 独有 --------
    uint8_t _pad_0x15A4[0x1154];
    bool m_bIsScoped;                // +0x26F8: 是否开镜
    uint8_t _pad_0x26F9[0x33];
    int32_t m_ArmorValue;            // +0x272C: 护甲值
};

// ============================================================================
// CCSPlayerController - 玩家的逻辑控制器（完全展开，无继承）
// ============================================================================
struct CCSPlayerController {
    uint8_t _pad_0x3F4[0x304];
    char m_iszPlayerName[128];       // +0x6F8: 玩家名称
    uint8_t _pad_0x778[0x8];
    uint64_t m_steamID;              // +0x780: Steam ID
    uint8_t _pad_0x788[0x184];
    CHandle_Pawn m_hPlayerPawn;      // +0x90C: ★ 指向此玩家的Pawn
    uint8_t _pad_0x910[0x4];
    bool m_bPawnIsAlive;             // +0x914: Pawn是否存活（从Pawn缓存）
    uint8_t _pad_0x915[0x3];
    int32_t m_iPawnHealth;           // +0x918: Pawn血量（从Pawn缓存）
    uint8_t _pad_0x91C[0x4];
    int32_t m_iPawnArmor;            // +0x920: Pawn护甲（从Pawn缓存）
};

// ============================================================================
// 关系图
// ============================================================================
/*
    EntityList (client.dll + dwEntityList)
         │
         ├── chunks[0] ──► EntityChunk
         │                      │
         │                      ├── entities[1]: CCSPlayerController* ─┐
         │                      ├── entities[2]: CCSPlayerController*  │
         │                      ├── ...                                │
         │                      └── entities[64]: CCSPlayerController* │
         │                                                             │
         ├── chunks[1] ──► EntityChunk                                 │
         │                      │                                      │
         │                      ├── entities[0]: C_CSPlayerPawn* ◄─────┘
         │                      ├── entities[1]: C_CSPlayerPawn*   m_hPlayerPawn
         │                      └── ...                            解析后指向这里
         └── ...


    ┌──────────────────────────────┐         ┌──────────────────────────────┐
    │    CCSPlayerController       │         │      C_CSPlayerPawn          │
    │    (EntityList 索引 1-64)    │         │    (EntityList 索引 512+)    │
    ├──────────────────────────────┤         ├──────────────────────────────┤
    │                              │         │ +0x354: m_iHealth  ◄─────┐   │
    │ +0x6F8: m_iszPlayerName[128] │         │ +0x35C: m_lifeState      │   │
    │ +0x780: m_steamID            │         │ +0x3F3: m_iTeamNum       │   │
    │                              │         │ +0x1588: m_vOldOrigin    │   │
    │ +0x914: m_bPawnIsAlive (缓存)│ ────────┼─────────────────────────►│   │
    │ +0x918: m_iPawnHealth (缓存) │ ────────┼─────────────────────────►│   │
    │ +0x920: m_iPawnArmor  (缓存) │         │ +0x26F8: m_bIsScoped         │
    │                              │         │ +0x272C: m_ArmorValue        │
    │                              │         │                              │
    │ +0x90C: m_hPlayerPawn ───────┼────────►│ (Handle解析后得到指针)       │
    │                              │         │                              │
    │         (Handle解析后) ◄─────┼─────────┼── +0x15A0: m_hController     │
    └──────────────────────────────┘         └──────────────────────────────┘
                    ▲                                       │
                    │              双向引用                 │
                    └───────────────────────────────────────┘
*/

// ============================================================================
// 遍历玩家伪代码
// ============================================================================
void TraverseAllPlayers(EntityList* entityList) {
    for (int i = 1; i <= 64; i++) {
        // 1. 从 EntityList 获取 Controller
        CCSPlayerController* controller = (CCSPlayerController*)entityList->GetByIndex(i);
        if (!controller) continue;

        // 2. 读取 Controller 上的 Handle
        CHandle_Pawn pawnHandle = controller->m_hPlayerPawn;
        if (!pawnHandle.IsValid()) continue;

        // 3. 通过 Handle 的索引，从 EntityList 获取 Pawn
        uint32_t pawnIndex = pawnHandle.GetIndex();
        C_CSPlayerPawn* pawn = (C_CSPlayerPawn*)entityList->GetByIndex(pawnIndex);
        if (!pawn) continue;

        // 4. 现在可以读取数据了
        // controller->m_iszPlayerName  = 玩家名
        // controller->m_steamID        = Steam ID
        // pawn->m_iHealth              = 血量
        // pawn->m_vOldOrigin           = 3D坐标
        // pawn->m_bIsScoped            = 是否开镜
    }
}

} // namespace cs2

