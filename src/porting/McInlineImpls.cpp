#include <Core/Resource/PackIdVersion.hpp>
#include <Core/SemVer/SemVersion.hpp>
#include <SharedTypes/legacy/ActorUniqueID.hpp>
#include <VanillaComponents/StateVectorComponent.hpp>
#include <SharedTypes/legacy/Facing.hpp>
#include <SharedTypes/world/level/BlockPos.hpp>
#include <network/NetworkPeer.hpp>
#include <network/Packet.hpp>
#include <raknet/PacketPriority.hpp>
#include <world/ContainerID.hpp>
#include <world/actor/Actor.hpp>
#include <world/actor/player/Inventory.hpp>
#include <world/actor/player/Player.hpp>
#include <world/actor/player/PlayerInventory.hpp>
#include <world/item/Item.hpp>
#include <world/item/ItemDescriptor.hpp>
#include <world/item/ItemStackBase.hpp>
#include <world/item/registry/ItemRegistryRef.hpp>
#include <world/level/Level.hpp>
#include <world/level/block/Block.hpp>
#include <world/level/block/BlockType.hpp>
#include <world/level/material/Material.hpp>

#include <cstdint>

template SemVersionBase<Bedrock::StaticOptimizedString>::SemVersionBase(
    std::uint16_t,
    std::uint16_t,
    std::uint16_t,
    Bedrock::StaticOptimizedString,
    Bedrock::StaticOptimizedString
);

Packet::Packet()
    : mPriority(PacketPriority::MEDIUM_PRIORITY)
    , mReliability(NetworkPeer::Reliability::ReliableOrdered)
    , mSenderSubId(SubClientId{})
    , mIsHandled(false)
    , mReceiveTimepoint()
    , mHandler(nullptr)
    , mCompressible(Compressibility::Compressible)
{}

Packet::~Packet() = default;

TypedServerNetId<ItemStackNetIdTag, int, 0>::~TypedServerNetId() = default;

TypedClientNetId<ItemStackRequestIdTag, int, 0>::~TypedClientNetId() = default;

TypedClientNetId<ItemStackLegacyRequestIdTag, int, 0>::~TypedClientNetId() = default;

ItemDescriptor::~ItemDescriptor() = default;

ItemRegistryRef::ItemRegistryRef() = default;

ItemRegistryRef::ItemRegistryRef(std::weak_ptr<ItemRegistry> registry)
    : mWeakRegistry(std::move(registry))
{}

ItemRegistryRef::~ItemRegistryRef() = default;

ActorRuntimeID::ActorRuntimeID()
    : rawID(0)
{}

ActorRuntimeID::ActorRuntimeID(const uint64_t id)
    : rawID(id)
{}

bool Actor::isClientSide() const
{
    return mLevel == nullptr || mLevel->isClientSide();
}

EntityContext& Actor::getEntity()
{
    return mEntityContext;
}

const EntityContext& Actor::getEntity() const
{
    return mEntityContext;
}

const Vec3& Actor::getPosition() const
{
    return mBuiltInComponents.mStateVectorComponent->mPos;
}

PlayerInventory& Player::getSupplies()
{
    return *mInventory;
}

const ItemStack& PlayerInventory::getSelectedItem() const
{
    if (mSelectedContainerId == ContainerID::CONTAINER_ID_INVENTORY)
        return mInventory->getItem(mSelected);

    return ItemStack::EMPTY_ITEM;
}

const Item* ItemStackBase::getItem() const
{
    return mItem.pc == nullptr ? nullptr : mItem.pc->ptr;
}

bool ItemStackBase::isBlock() const
{
    return !mItem.isNull() && !mItem->getBlockType().isNull();
}

bool ItemStackBase::matches(const ItemStackBase& other) const
{
    return mCount == other.mCount && matchesItem(other);
}

bool ItemStackBase::operator!=(const ItemStackBase& rhs) const
{
    return !matches(rhs);
}

bool ItemStackBase::isLiquidClipItem() const
{
    const Item* item = getItem();
    return item != nullptr && item->isLiquidClipItem();
}

BlockPos BlockPos::neighbor(uint8_t facing) const
{
    switch (static_cast<Facing::Name>(facing)) {
    case Facing::Name::DOWN:  return below();
    case Facing::Name::UP:    return above();
    case Facing::Name::NORTH: return north();
    case Facing::Name::SOUTH: return south();
    case Facing::Name::WEST:  return west();
    case Facing::Name::EAST:  return east();
    default:                  return *this;
    }
}

const BlockType& Block::getBlockType() const
{
    return *mBlockType;
}

const Material& Block::getMaterial() const
{
    return mBlockType->getMaterial();
}

const Block& BlockType::getDefaultState() const
{
    return *mDefaultState;
}

const Material& BlockType::getMaterial() const
{
    return mMaterial;
}

const WeakPtr<const BlockType>& Item::getBlockType() const
{
    return mBlockType;
}

bool Material::isLiquid() const
{
    return mLiquid;
}