#include "skin_runtime.hpp"

#include "client_dll.hpp"
#include "offsets.hpp"
#include "core/memory/game_layout.hpp"
#include "core/memory/memory.hpp"
#include "core/diagnostics.hpp"

#include <chrono>
#include <mutex>

namespace skins
{
    namespace
    {
        class WindowsMemory final : public game::RemoteMemory
        {
        public:
            bool read(const std::uintptr_t address, void* value, const std::size_t size) override
            {
                return memory::ReadRaw(address, value, size);
            }
            bool write(const std::uintptr_t address, const void* value, const std::size_t size) override
            {
                return memory::TryWriteRaw(address, value, size);
            }
            [[nodiscard]] bool writesAllowed() const override { return memory::WritesAllowed(); }
        };

        game::EntityLayout entityLayout()
        {
            using namespace cs2_dumper::schemas::client_dll;
            game::EntityLayout result;
            result.entityList = cs2_dumper::offsets::client_dll::dwEntityList;
            result.localPawn = cs2_dumper::offsets::client_dll::dwLocalPlayerPawn;
            result.identity = CEntityInstance::m_pEntity;
            result.identityFlags = CEntityIdentity::m_flags;
            result.identityHandle = game_layout::ENTITY_IDENTITY_HANDLE;
            result.chunkArray = game_layout::ENTITY_LIST_CHUNK_ARRAY;
            result.identityStride = game_layout::ENTITY_IDENTITY_STRIDE;
            result.health = C_BaseEntity::m_iHealth;
            result.lifeState = C_BaseEntity::m_lifeState;
            result.team = C_BaseEntity::m_iTeamNum;
            result.weaponServices = C_BasePlayerPawn::m_pWeaponServices;
            result.weapons = CPlayer_WeaponServices::m_hMyWeapons;
            result.activeWeapon = CPlayer_WeaponServices::m_hActiveWeapon;
            result.owner = C_BaseEntity::m_hOwnerEntity;
            result.attributeManager = C_EconEntity::m_AttributeManager;
            result.item = C_AttributeContainer::m_Item;
            result.definition = C_EconItemView::m_iItemDefinitionIndex;
            return result;
        }

        SkinLayout skinLayout()
        {
            using namespace cs2_dumper::schemas::client_dll;
            SkinLayout result;
            result.paintKit = C_EconEntity::m_nFallbackPaintKit;
            result.seed = C_EconEntity::m_nFallbackSeed;
            result.wear = C_EconEntity::m_flFallbackWear;
            result.itemIdHigh = C_EconItemView::m_iItemIDHigh;
            result.initialized = C_EconItemView::m_bInitialized;
            result.disallowSoc = C_EconItemView::m_bDisallowSOC;
            result.visualsDataSet = C_CSWeaponBase::m_bVisualsDataSet;
            result.sceneNode = C_BaseEntity::m_pGameSceneNode;
            result.sceneOwner = CGameSceneNode::m_pOwner;
            result.sceneChild = CGameSceneNode::m_pChild;
            result.sceneSibling = CGameSceneNode::m_pNextSibling;
            result.modelState = CSkeletonInstance::m_modelState;
            result.meshMask = CModelState::m_MeshGroupMask;
            result.hudArms = C_CSPlayerPawn::m_hHudModelArms;
            result.attributeList = C_EconItemView::m_AttributeList;
            result.attributes = CAttributeList::m_Attributes;
            result.attributeDefinition = CEconItemAttribute::m_iAttributeDefinitionIndex;
            result.attributeValue = CEconItemAttribute::m_flValue;
            result.attributeInitialValue = CEconItemAttribute::m_flInitialValue;
            result.attributeStride = game_layout::ECON_ATTRIBUTE_STRIDE;
            return result;
        }
    }

    struct Runtime::Impl
    {
        explicit Impl(const std::uintptr_t module)
            : backend(memory, entityLayout(), skinLayout(), module), changer(backend) {}

        WindowsMemory memory;
        MemoryBackend backend;
        SkinChanger changer;
        std::chrono::steady_clock::time_point nextTick{};
        std::uint64_t lastRequest{};
        bool lastEnabled = false;
        mutable std::mutex mutex;
        Status published;

        void publish()
        {
            const auto& current = changer.status();
            if (current.failures != published.failures) diagnostics::log(L"Skin application failed; inspect the Skins page.");
            if (current.applications != published.applications) diagnostics::log(L"Skin parameters written and read back; visual refresh requires game verification.");
            std::lock_guard<std::mutex> lock(mutex);
            published = current;
        }
    };

    Runtime::Runtime(const std::uintptr_t clientModule) : impl_(std::make_unique<Impl>(clientModule)) {}
    Runtime::~Runtime() = default;

    void Runtime::tick(const Settings& settings, const bool enabled, const bool foreground,
                       const bool diagnostics, const std::uint64_t request)
    {
        const auto now = std::chrono::steady_clock::now();
        if (now < impl_->nextTick && enabled == impl_->lastEnabled && request == impl_->lastRequest) return;
        impl_->nextTick = now + std::chrono::milliseconds(enabled ? 50 : 250);
        impl_->lastEnabled = enabled;
        impl_->lastRequest = request;
        impl_->changer.tick(settings, enabled, foreground, diagnostics, request, now);
        impl_->publish();
    }

    void Runtime::shutdown()
    {
        impl_->changer.shutdown();
        impl_->publish();
    }

    Status Runtime::status() const
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        return impl_->published;
    }
}
