--[[
    CS2 Entity/Controller/Pawn/Handle 关系探索脚本
    
    使用方法：
    1. 打开 Cheat Engine，附加到 cs2.exe
    2. 菜单: Table -> Show Cheat Table Lua Script
    3. 粘贴此脚本，点击 Execute
    4. 在游戏中进入一局比赛
    5. 在 CE 底部的 Lua Engine 窗口中调用函数，例如：
       - PrintAllPlayers()      -- 打印所有玩家
       - PrintLocalPlayer()     -- 打印本地玩家
       - ExploreEntity(index)   -- 探索指定索引的实体
]]

-- ============================================================================
-- 偏移量配置（需要根据游戏版本更新）
-- ============================================================================
local offsets = {
    -- client.dll 全局偏移
    dwEntityList = 0x24A7B48,
    dwLocalPlayerController = 0x22ECA18,
    
    -- EntityList 结构
    CHUNK_BASE = 0x10,        -- chunks 数组在 EntityList 中的偏移
    CHUNK_SIZE = 512,         -- 每个 chunk 的实体数
    ENTITY_STRIDE = 0x70,     -- 每个实体槽位的步长
    
    -- Handle 解析
    HANDLE_MASK = 0x7FFF,     -- 提取索引的掩码
    
    -- CCSPlayerController 偏移
    ctrl_m_iszPlayerName = 0x6F8,    -- 玩家名称
    ctrl_m_steamID = 0x780,          -- Steam ID
    ctrl_m_hPlayerPawn = 0x90C,      -- Pawn Handle
    ctrl_m_bPawnIsAlive = 0x914,     -- Pawn 存活状态（缓存）
    ctrl_m_iPawnHealth = 0x918,      -- Pawn 血量（缓存）
    
    -- C_CSPlayerPawn 偏移
    pawn_m_iHealth = 0x354,          -- 血量
    pawn_m_lifeState = 0x35C,        -- 生存状态
    pawn_m_iTeamNum = 0x3F3,         -- 队伍
    pawn_m_vOldOrigin = 0x1588,      -- 3D坐标 (Vector: x, y, z 各4字节)
    pawn_m_hController = 0x15A0,     -- Controller Handle
    pawn_m_bIsScoped = 0x26F8,       -- 是否开镜
    pawn_m_ArmorValue = 0x272C,      -- 护甲
}

-- ============================================================================
-- 辅助函数
-- ============================================================================

-- 获取 client.dll 基址
function GetClientBase()
    return getAddress("client.dll")
end

-- 从 Handle 提取实体索引
function HandleToIndex(handle)
    if handle == 0 or handle == 0xFFFFFFFF then
        return nil
    end
    return handle & offsets.HANDLE_MASK
end

-- 通过索引从 EntityList 获取实体指针
function GetEntityByIndex(entityList, index)
    if not entityList or entityList == 0 then return nil end
    if not index then return nil end
    
    local chunkIdx = math.floor(index / offsets.CHUNK_SIZE)
    local inChunk = index % offsets.CHUNK_SIZE
    
    -- 读取 chunk 指针
    local chunkPtrAddr = entityList + offsets.CHUNK_BASE + (chunkIdx * 8)
    local chunk = readQword(chunkPtrAddr)
    if not chunk or chunk == 0 then return nil end
    
    -- 读取实体指针
    local entityAddr = chunk + (inChunk * offsets.ENTITY_STRIDE)
    local entity = readQword(entityAddr)
    return entity
end

-- 通过 Handle 获取实体
function GetEntityByHandle(entityList, handle)
    local index = HandleToIndex(handle)
    return GetEntityByIndex(entityList, index)
end

-- 读取字符串
function ReadString(address, maxLen)
    if not address or address == 0 then return "" end
    maxLen = maxLen or 128
    local str = readString(address, maxLen)
    return str or ""
end

-- 读取 Vector (x, y, z)
function ReadVector(address)
    if not address or address == 0 then return {x=0, y=0, z=0} end
    return {
        x = readFloat(address),
        y = readFloat(address + 4),
        z = readFloat(address + 8)
    }
end

-- ============================================================================
-- 核心功能函数
-- ============================================================================

-- 打印单个玩家的完整信息
function PrintPlayerInfo(entityList, controllerIndex)
    local controller = GetEntityByIndex(entityList, controllerIndex)
    if not controller or controller == 0 then return false end
    
    -- 读取 Controller 数据
    local playerName = ReadString(controller + offsets.ctrl_m_iszPlayerName)
    if playerName == "" then return false end  -- 空槽位
    
    local steamID = readQword(controller + offsets.ctrl_m_steamID)
    local pawnHandle = readInteger(controller + offsets.ctrl_m_hPlayerPawn)
    local pawnIsAlive = readBytes(controller + offsets.ctrl_m_bPawnIsAlive, 1)
    local pawnHealthCache = readInteger(controller + offsets.ctrl_m_iPawnHealth)
    
    print(string.format("\n===== Controller [索引 %d] =====", controllerIndex))
    print(string.format("  地址: %X", controller))
    print(string.format("  玩家名: %s", playerName))
    print(string.format("  Steam ID: %d", steamID or 0))
    print(string.format("  m_hPlayerPawn: %X (索引: %d)", pawnHandle, HandleToIndex(pawnHandle) or -1))
    print(string.format("  m_bPawnIsAlive (缓存): %s", pawnIsAlive == 1 and "true" or "false"))
    print(string.format("  m_iPawnHealth (缓存): %d", pawnHealthCache or 0))
    
    -- 通过 Handle 获取 Pawn
    local pawn = GetEntityByHandle(entityList, pawnHandle)
    if pawn and pawn ~= 0 then
        local health = readInteger(pawn + offsets.pawn_m_iHealth)
        local lifeState = readBytes(pawn + offsets.pawn_m_lifeState, 1)
        local team = readBytes(pawn + offsets.pawn_m_iTeamNum, 1)
        local pos = ReadVector(pawn + offsets.pawn_m_vOldOrigin)
        local isScoped = readBytes(pawn + offsets.pawn_m_bIsScoped, 1)
        local armor = readInteger(pawn + offsets.pawn_m_ArmorValue)
        local ctrlHandle = readInteger(pawn + offsets.pawn_m_hController)
        
        print(string.format("\n  ----- Pawn [索引 %d] -----", HandleToIndex(pawnHandle)))
        print(string.format("    地址: %X", pawn))
        print(string.format("    m_iHealth: %d", health or 0))
        print(string.format("    m_lifeState: %d (0=活着)", lifeState or -1))
        print(string.format("    m_iTeamNum: %d (2=T, 3=CT)", team or 0))
        print(string.format("    m_vOldOrigin: (%.1f, %.1f, %.1f)", pos.x, pos.y, pos.z))
        print(string.format("    m_bIsScoped: %s", isScoped == 1 and "true" or "false"))
        print(string.format("    m_ArmorValue: %d", armor or 0))
        print(string.format("    m_hController: %X (索引: %d) ◄── 反向引用", ctrlHandle, HandleToIndex(ctrlHandle) or -1))
        
        -- 验证双向引用
        if HandleToIndex(ctrlHandle) == controllerIndex then
            print("    ✓ 双向引用验证通过！")
        else
            print("    ✗ 双向引用不匹配！")
        end
    else
        print("\n  Pawn: (无效或玩家已死亡)")
    end

    return true
end

-- ============================================================================
-- 主要调用函数
-- ============================================================================

-- 打印所有玩家
function PrintAllPlayers()
    local clientBase = GetClientBase()
    if not clientBase then
        print("错误: 无法获取 client.dll 基址，请确保已附加到 cs2.exe")
        return
    end
    print(string.format("client.dll 基址: %X", clientBase))

    local entityList = readQword(clientBase + offsets.dwEntityList)
    if not entityList or entityList == 0 then
        print("错误: 无法读取 EntityList")
        return
    end
    print(string.format("EntityList 地址: %X", entityList))

    print("\n========== 遍历所有玩家 (索引 1-64) ==========")
    local count = 0
    for i = 1, 64 do
        if PrintPlayerInfo(entityList, i) then
            count = count + 1
        end
    end
    print(string.format("\n共找到 %d 个玩家", count))
end

-- 打印本地玩家
function PrintLocalPlayer()
    local clientBase = GetClientBase()
    if not clientBase then
        print("错误: 无法获取 client.dll 基址")
        return
    end

    local localController = readQword(clientBase + offsets.dwLocalPlayerController)
    if not localController or localController == 0 then
        print("错误: 无法读取本地玩家 Controller (可能不在游戏中)")
        return
    end

    print(string.format("本地玩家 Controller 地址: %X", localController))

    local entityList = readQword(clientBase + offsets.dwEntityList)

    -- 找到本地玩家的索引
    for i = 1, 64 do
        local ctrl = GetEntityByIndex(entityList, i)
        if ctrl == localController then
            print(string.format("本地玩家索引: %d", i))
            PrintPlayerInfo(entityList, i)
            return
        end
    end
    print("未找到本地玩家索引")
end

-- 探索指定索引的实体
function ExploreEntity(index)
    local clientBase = GetClientBase()
    if not clientBase then
        print("错误: 无法获取 client.dll 基址")
        return
    end

    local entityList = readQword(clientBase + offsets.dwEntityList)
    local entity = GetEntityByIndex(entityList, index)

    if not entity or entity == 0 then
        print(string.format("索引 %d: 空槽位", index))
        return
    end

    print(string.format("\n===== 实体 [索引 %d] =====", index))
    print(string.format("地址: %X", entity))

    -- 尝试作为 Controller 读取
    local playerName = ReadString(entity + offsets.ctrl_m_iszPlayerName)
    if playerName ~= "" then
        print(string.format("可能是 Controller, 玩家名: %s", playerName))
    end

    -- 尝试作为 Pawn 读取
    local health = readInteger(entity + offsets.pawn_m_iHealth)
    local team = readBytes(entity + offsets.pawn_m_iTeamNum, 1)
    print(string.format("作为 Pawn 读取: health=%d, team=%d", health or 0, team or 0))
end

-- ============================================================================
-- 使用说明
-- ============================================================================
print([[
========================================
CS2 Entity 关系探索脚本已加载！

可用命令：
  PrintAllPlayers()    -- 打印所有玩家的 Controller 和 Pawn 信息
  PrintLocalPlayer()   -- 打印本地玩家信息
  ExploreEntity(123)   -- 探索指定索引的实体

示例：
  在下方输入框输入 PrintAllPlayers() 然后回车
========================================
]])


