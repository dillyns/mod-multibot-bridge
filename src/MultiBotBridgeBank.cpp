#include "MultiBotBridgeBank.h"
#include "MultiBotBridgeInternal.h"

#include "AiObjectContext.h"
#include "ChatHelper.h"
#include "Creature.h"
#include "DatabaseEnv.h"
#include "GameObject.h"
#include "GuildMgr.h"
#include "Item.h"
#include "ObjectMgr.h"
#include "PlayerbotAI.h"
#include "PlayerbotMgr.h"
#include "Playerbots.h"
#include "SharedDefines.h"
#include "StringFormat.h"
#include "Unit.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

using namespace MultiBotBridgeInternal;

namespace
{
std::string ToUpper(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return std::toupper(c); });
    return value;
}

PlayerbotAI* GetBotAI(Player* bot)
{
    if (!bot)
        return nullptr;

    return sPlayerbotsMgr.GetPlayerbotAI(bot);
}

Creature* FindNearbyNpcWithFlag(Player* bot, uint32 npcFlag)
{
    PlayerbotAI* const botAI = GetBotAI(bot);
    if (!botAI || !botAI->GetAiObjectContext())
        return nullptr;

    AiObjectContext* const context = botAI->GetAiObjectContext();
    GuidVector const npcs = *context->GetValue<GuidVector>("nearest npcs");
    for (ObjectGuid const guid : npcs)
    {
        Unit* const unit = botAI->GetUnit(guid);
        if (!unit || unit->IsHostileTo(bot) || !unit->HasNpcFlag(static_cast<NPCFlags>(npcFlag)))
            continue;

        if (Creature* const creature = unit->ToCreature())
            return creature;
    }

    return nullptr;
}

Creature* FindNearbyVendorSellingItem(Player* bot, uint32 itemId, uint32& vendorSlot, uint32& vendorExtendedCost, bool& sawVendor)
{
    vendorSlot = 0;
    vendorExtendedCost = 0;
    sawVendor = false;

    PlayerbotAI* const botAI = GetBotAI(bot);
    if (!botAI || !botAI->GetAiObjectContext() || !itemId)
        return nullptr;

    AiObjectContext* const context = botAI->GetAiObjectContext();
    GuidVector const npcs = *context->GetValue<GuidVector>("nearest npcs");
    for (ObjectGuid const guid : npcs)
    {
        Unit* const unit = botAI->GetUnit(guid);
        if (!unit || unit->IsHostileTo(bot) || !unit->HasNpcFlag(static_cast<NPCFlags>(UNIT_NPC_FLAG_VENDOR)))
            continue;

        Creature* const creature = unit->ToCreature();
        if (!creature)
            continue;

        sawVendor = true;
        VendorItemData const* const vendorItems = creature->GetVendorItems();
        if (!vendorItems)
            continue;

        for (uint32 slot = 0; slot < vendorItems->GetItemCount(); ++slot)
        {
            VendorItem const* const vendorItem = vendorItems->GetItem(slot);
            if (vendorItem && vendorItem->item == itemId)
            {
                vendorSlot = slot;
                vendorExtendedCost = vendorItem->ExtendedCost;
                return creature;
            }
        }
    }

    return nullptr;
}

GameObject* FindNearbyGuildBank(Player* bot)
{
    PlayerbotAI* const botAI = GetBotAI(bot);
    if (!botAI || !botAI->GetAiObjectContext())
        return nullptr;

    AiObjectContext* const context = botAI->GetAiObjectContext();
    GuidVector const objects = *context->GetValue<GuidVector>("nearest game objects");
    for (ObjectGuid const guid : objects)
        if (GameObject* const go = botAI->GetGameObject(guid))
            if (bot->GetGameObjectIfCanInteractWith(go->GetGUID(), GAMEOBJECT_TYPE_GUILD_BANK))
                return go;

    return nullptr;
}

Item* FindBagItemByEntry(Player* bot, uint32 itemEntry)
{
    if (!bot || !itemEntry)
        return nullptr;

    for (uint8 slot = INVENTORY_SLOT_ITEM_START; slot < INVENTORY_SLOT_ITEM_END; ++slot)
        if (Item* const item = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
            if (item->GetEntry() == itemEntry)
                return item;

    for (uint8 bag = INVENTORY_SLOT_BAG_START; bag < INVENTORY_SLOT_BAG_END; ++bag)
    {
        Bag const* const pBag = (Bag*)bot->GetItemByPos(INVENTORY_SLOT_BAG_0, bag);
        if (!pBag)
            continue;

        for (uint8 slot = 0; slot < pBag->GetBagSize(); ++slot)
            if (Item* const item = bot->GetItemByPos(bag, slot))
                if (item->GetEntry() == itemEntry)
                    return item;
    }

    return nullptr;
}

Item* FindBankItemByEntry(Player* bot, uint32 itemEntry)
{
    if (!bot || !itemEntry)
        return nullptr;

    for (uint8 slot = BANK_SLOT_ITEM_START; slot < BANK_SLOT_ITEM_END; ++slot)
        if (Item* const item = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
            if (item->GetEntry() == itemEntry)
                return item;

    for (uint8 bag = BANK_SLOT_BAG_START; bag < BANK_SLOT_BAG_END; ++bag)
    {
        Bag const* const pBag = (Bag*)bot->GetItemByPos(INVENTORY_SLOT_BAG_0, bag);
        if (!pBag)
            continue;

        for (uint8 slot = 0; slot < pBag->GetBagSize(); ++slot)
            if (Item* const item = bot->GetItemByPos(bag, slot))
                if (item->GetEntry() == itemEntry)
                    return item;
    }

    return nullptr;
}

void AddBankItemToSnapshot(std::map<uint32, uint32>& itemCounts, std::map<uint32, ItemTemplate const*>& itemTemplates, std::map<uint32, bool>& soulboundByEntry, Item* item)
{
    if (!item)
        return;

    ItemTemplate const* const proto = item->GetTemplate();
    if (!proto)
        return;

    uint32 const itemId = proto->ItemId;
    itemCounts[itemId] += item->GetCount();
    itemTemplates[itemId] = proto;
    if (item->IsSoulBound())
        soulboundByEntry[itemId] = true;
}

void AddItemEntryToSnapshot(std::map<uint32, uint32>& itemCounts, std::map<uint32, ItemTemplate const*>& itemTemplates, uint32 itemId, uint32 count)
{
    if (!itemId || !count)
        return;

    ItemTemplate const* const proto = sObjectMgr->GetItemTemplate(itemId);
    if (!proto)
        return;

    itemCounts[itemId] += count;
    itemTemplates[itemId] = proto;
}

int32 GetGuildBankTabWithdrawRemaining(Guild* guild, Player* bot, uint8 tabId)
{
    if (!guild || !bot)
        return 0;

    Guild::Member const* const member = guild->GetMember(bot->GetGUID());
    if (!member)
        return 0;

    if (member->IsRank(GR_GUILDMASTER) || guild->GetLeaderGUID() == bot->GetGUID())
        return std::numeric_limits<int32>::max();

    QueryResult result = CharacterDatabase.Query(
        "SELECT gbright, SlotPerDay FROM guild_bank_right "
        "WHERE guildid = {} AND TabId = {} AND rid = {}",
        guild->GetId(), uint32(tabId), uint32(member->GetRankId()));

    if (!result)
        return 0;

    Field* const fields = result->Fetch();
    uint32 const rights = fields[0].Get<uint32>();
    uint32 const slotsPerDay = fields[1].Get<uint32>();

    if ((rights & GUILD_BANK_RIGHT_VIEW_TAB) == 0 || slotsPerDay == 0)
        return 0;

    if (slotsPerDay == uint32(GUILD_WITHDRAW_SLOT_UNLIMITED))
        return std::numeric_limits<int32>::max();

    int32 const used = member->GetBankWithdrawValue(tabId);
    int64 const remaining = int64(slotsPerDay) - int64(used > 0 ? used : 0);
    return remaining > 0 ? int32(std::min<int64>(remaining, std::numeric_limits<int32>::max())) : 0;
}

int32 GetGuildBankWithdrawRemaining(Guild* guild, Player* bot)
{
    int32 bestRemaining = 0;
    for (uint8 tabId = 0; tabId < GUILD_BANK_MAX_TABS; ++tabId)
    {
        int32 const remaining = GetGuildBankTabWithdrawRemaining(guild, bot, tabId);
        if (remaining == std::numeric_limits<int32>::max())
            return remaining;

        if (remaining > bestRemaining)
            bestRemaining = remaining;
    }

    return bestRemaining;
}

uint32 MoveMatchingBagItemsToBank(Player* bot, uint32 itemId, uint32 requestedCount, std::string& reason)
{
    if (!bot || !itemId)
    {
        reason = "BAD_REQUEST";
        return 0;
    }

    if (!FindNearbyNpcWithFlag(bot, UNIT_NPC_FLAG_BANKER))
    {
        reason = "BANKER_NOT_FOUND";
        return 0;
    }

    uint32 moved = 0;
    while (Item* const item = FindBagItemByEntry(bot, itemId))
    {
        uint32 const stackCount = item->GetCount();
        ItemPosCountVec dest;
        InventoryResult const msg = bot->CanBankItem(NULL_BAG, NULL_SLOT, dest, item, false);
        if (msg != EQUIP_ERR_OK)
        {
            reason = "BANK_FULL";
            return moved;
        }

        bot->RemoveItem(item->GetBagSlot(), item->GetSlot(), true);
        bot->BankItem(dest, item, true);
        moved += stackCount;

        if (requestedCount > 0 && moved >= requestedCount)
            break;
    }

    if (!moved)
        reason = "ITEM_NOT_FOUND";

    return moved;
}

uint32 MoveMatchingBankItemsToBags(Player* bot, uint32 itemId, uint32 requestedCount, std::string& reason)
{
    if (!bot || !itemId)
    {
        reason = "BAD_REQUEST";
        return 0;
    }

    if (!FindNearbyNpcWithFlag(bot, UNIT_NPC_FLAG_BANKER))
    {
        reason = "BANKER_NOT_FOUND";
        return 0;
    }

    uint32 moved = 0;
    while (Item* const item = FindBankItemByEntry(bot, itemId))
    {
        uint32 const stackCount = item->GetCount();
        ItemPosCountVec dest;
        InventoryResult const msg = bot->CanStoreItem(NULL_BAG, NULL_SLOT, dest, item, false);
        if (msg != EQUIP_ERR_OK)
        {
            reason = "BAGS_FULL";
            return moved;
        }

        bot->RemoveItem(item->GetBagSlot(), item->GetSlot(), true);
        bot->StoreItem(dest, item, true);
        moved += stackCount;

        if (requestedCount > 0 && moved >= requestedCount)
            break;
    }

    if (!moved)
        reason = "ITEM_NOT_FOUND";

    return moved;
}

uint32 MoveMatchingBagItemsToGuildBank(Player* requester, Player* bot, uint32 itemId, uint32 requestedCount, std::string& reason)
{
    if (!bot || !itemId)
    {
        reason = "BAD_REQUEST";
        return 0;
    }

    if (!bot->GetGuildId())
    {
        reason = "BOT_NOT_IN_GUILD";
        return 0;
    }

    Guild* const guild = sGuildMgr->GetGuildById(bot->GetGuildId());
    if (!guild)
    {
        reason = "BOT_NOT_IN_GUILD";
        return 0;
    }

    if (!FindNearbyGuildBank(bot))
    {
        reason = "GUILD_BANK_NOT_FOUND";
        return 0;
    }

    if (!guild->MemberHasTabRights(bot->GetGUID(), 0, GUILD_BANK_RIGHT_DEPOSIT_ITEM))
    {
        reason = "NO_GUILD_BANK_RIGHTS";
        return 0;
    }

    uint32 moved = 0;
    while (Item* const item = FindBagItemByEntry(bot, itemId))
    {
        uint32 const stackCount = item->GetCount();
        uint32 const playerSlot = item->GetSlot();
        uint32 const playerBag = item->GetBagSlot();
        ObjectGuid const itemGuid = item->GetGUID();
        guild->SwapItemsWithInventory(bot, false, 0, 255, playerBag, playerSlot, 0);

        if (Item* const remaining = bot->GetItemByPos(playerBag, playerSlot))
            if (remaining->GetGUID() == itemGuid)
            {
                reason = "GUILD_BANK_FULL";
                return moved;
            }

        moved += stackCount;

        if (requestedCount > 0 && moved >= requestedCount)
            break;
    }

    if (!moved)
        reason = "ITEM_NOT_FOUND";

    return moved;
}

uint32 MoveMatchingGuildBankItemsToBags(Player* bot, uint32 itemId, uint32 requestedCount, std::string& reason)
{
    if (!bot || !itemId)
    {
        reason = "BAD_REQUEST";
        return 0;
    }

    if (!bot->GetGuildId())
    {
        reason = "BOT_NOT_IN_GUILD";
        return 0;
    }

    Guild* const guild = sGuildMgr->GetGuildById(bot->GetGuildId());
    if (!guild)
    {
        reason = "BOT_NOT_IN_GUILD";
        return 0;
    }

    if (!FindNearbyGuildBank(bot))
    {
        reason = "GUILD_BANK_NOT_FOUND";
        return 0;
    }

    if (GetGuildBankWithdrawRemaining(guild, bot) == 0)
    {
        reason = "NO_GUILD_BANK_RIGHTS";
        return 0;
    }

    QueryResult result = CharacterDatabase.Query(
        "SELECT gbi.TabId, gbi.SlotId, ii.count "
        "FROM guild_bank_item gbi "
        "INNER JOIN item_instance ii ON ii.guid = gbi.item_guid "
        "WHERE gbi.guildid = {} AND ii.itemEntry = {} "
        "ORDER BY gbi.TabId, gbi.SlotId",
        guild->GetId(), itemId);

    if (!result)
    {
        reason = "ITEM_NOT_FOUND";
        return 0;
    }

    bool foundAny = false;
    bool foundWithdrawable = false;
    uint32 moved = 0;

    do
    {
        Field* const fields = result->Fetch();
        uint8 const tabId = fields[0].Get<uint8>();
        uint8 const slotId = fields[1].Get<uint8>();
        uint32 const stackCount = fields[2].Get<uint32>();
        foundAny = true;

        if (GetGuildBankTabWithdrawRemaining(guild, bot, tabId) == 0)
            continue;

        foundWithdrawable = true;

        uint32 splitCount = 0;
        if (requestedCount > 0)
        {
            uint32 const remainingRequest = requestedCount > moved ? requestedCount - moved : 0;
            if (!remainingRequest)
                break;

            splitCount = std::min(stackCount, remainingRequest);
            if (splitCount >= stackCount)
                splitCount = 0;
        }

        uint32 const before = bot->GetItemCount(itemId, false);
        guild->SwapItemsWithInventory(bot, true, tabId, slotId, NULL_BAG, NULL_SLOT, splitCount);
        uint32 const after = bot->GetItemCount(itemId, false);

        if (after > before)
            moved += after - before;

        if (requestedCount > 0 && moved >= requestedCount)
            break;
    }
    while (result->NextRow());

    if (!moved)
    {
        if (!foundAny)
            reason = "ITEM_NOT_FOUND";
        else if (!foundWithdrawable)
            reason = "NO_GUILD_BANK_RIGHTS";
        else
            reason = "BAGS_FULL";
    }

    return moved;
}

uint32 BuyMatchingVendorItem(Player* bot, uint32 itemId, uint32 requestedCount, std::string& reason)
{
    if (!bot || !itemId)
    {
        reason = "BAD_REQUEST";
        return 0;
    }

    ItemTemplate const* const proto = sObjectMgr->GetItemTemplate(itemId);
    if (!proto)
    {
        reason = "ITEM_NOT_FOUND";
        return 0;
    }

    uint32 vendorSlot = 0;
    uint32 vendorExtendedCost = 0;
    bool sawVendor = false;
    Creature* const vendor = FindNearbyVendorSellingItem(bot, itemId, vendorSlot, vendorExtendedCost, sawVendor);
    if (!vendor)
    {
        reason = sawVendor ? "VENDOR_DOES_NOT_SELL_ITEM" : "VENDOR_NOT_FOUND";
        return 0;
    }

    uint32 const desired = requestedCount > 0 ? requestedCount : 1;
    uint32 bought = 0;
    for (uint32 i = 0; i < desired; ++i)
    {
        uint32 const price = uint32(std::floor(proto->BuyPrice * bot->GetReputationPriceDiscount(vendor)));
        if (price > 0 && bot->GetMoney() < price)
        {
            reason = "NOT_ENOUGH_MONEY";
            break;
        }

        uint32 const oldCount = bot->GetItemCount(itemId, false);
        bot->BuyItemFromVendorSlot(vendor->GetGUID(), vendorSlot, itemId, 1, NULL_BAG, NULL_SLOT);
        uint32 const newCount = bot->GetItemCount(itemId, false);
        if (newCount <= oldCount)
        {
            reason = vendorExtendedCost > 0 ? "VENDOR_REQUIRES_SPECIAL_CURRENCY" : "BUY_FAILED";
            break;
        }

        bought += newCount - oldCount;
    }

    return bought;
}
} // namespace

void SendBankPackets(Player* requester, ChatMsg replyType, std::string const& botName, std::string const& requestToken)
{
    std::string const trimmedBotName = Acore::String::Trim(botName);
    Player* const bot = FindBotByName(requester, trimmedBotName);
    std::string const effectiveBotName = bot ? bot->GetName() : trimmedBotName;
    std::string const prefixPayload = UrlEncodeField(effectiveBotName) + std::string(1, kFieldSeparator) + requestToken;

    SendAddonPacket(requester, replyType, "BANK_BEGIN", prefixPayload);

    if (!bot)
    {
        SendAddonPacket(requester, replyType, "BANK_ERROR", prefixPayload + std::string(1, kFieldSeparator) + "NO_BOT");
        SendAddonPacket(requester, replyType, "BANK_END", prefixPayload);
        return;
    }

    if (!FindNearbyNpcWithFlag(bot, UNIT_NPC_FLAG_BANKER))
    {
        SendAddonPacket(requester, replyType, "BANK_ERROR", prefixPayload + std::string(1, kFieldSeparator) + "BANKER_NOT_FOUND");
        SendAddonPacket(requester, replyType, "BANK_END", prefixPayload);
        return;
    }

    std::map<uint32, uint32> itemCounts;
    std::map<uint32, ItemTemplate const*> itemTemplates;
    std::map<uint32, bool> soulboundByEntry;

    for (uint8 slot = BANK_SLOT_ITEM_START; slot < BANK_SLOT_ITEM_END; ++slot)
        AddBankItemToSnapshot(itemCounts, itemTemplates, soulboundByEntry, bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot));

    for (uint8 bag = BANK_SLOT_BAG_START; bag < BANK_SLOT_BAG_END; ++bag)
    {
        Bag const* const pBag = (Bag*)bot->GetItemByPos(INVENTORY_SLOT_BAG_0, bag);
        if (!pBag)
            continue;

        for (uint8 slot = 0; slot < pBag->GetBagSize(); ++slot)
            AddBankItemToSnapshot(itemCounts, itemTemplates, soulboundByEntry, bot->GetItemByPos(bag, slot));
    }

    for (auto const& entry : itemCounts)
    {
        ItemTemplate const* const proto = itemTemplates[entry.first];
        if (!proto)
            continue;

        std::string line = ChatHelper::FormatItem(proto, entry.second);
        if (soulboundByEntry[entry.first])
            line += " (soulbound)";

        SendAddonPacket(requester, replyType, "BANK_ITEM", prefixPayload + std::string(1, kFieldSeparator) + UrlEncodeField(line));
    }

    SendAddonPacket(requester, replyType, "BANK_END", prefixPayload);
}

void SendGuildBankPackets(Player* requester, ChatMsg replyType, std::string const& botName, std::string const& requestToken)
{
    std::string const trimmedBotName = Acore::String::Trim(botName);
    Player* const bot = FindBotByName(requester, trimmedBotName);
    std::string const effectiveBotName = bot ? bot->GetName() : trimmedBotName;
    std::string const prefixPayload = UrlEncodeField(effectiveBotName) + std::string(1, kFieldSeparator) + requestToken;

    SendAddonPacket(requester, replyType, "GBANK_BEGIN", prefixPayload);

    auto sendErrorAndEnd = [&](std::string const& reason)
    {
        SendAddonPacket(requester, replyType, "GBANK_ERROR", prefixPayload + std::string(1, kFieldSeparator) + reason);
        SendAddonPacket(requester, replyType, "GBANK_END", prefixPayload);
    };

    if (!bot)
    {
        sendErrorAndEnd("NO_BOT");
        return;
    }

    if (!bot->GetGuildId())
    {
        sendErrorAndEnd("BOT_NOT_IN_GUILD");
        return;
    }

    Guild* const guild = sGuildMgr->GetGuildById(bot->GetGuildId());
    if (!guild)
    {
        sendErrorAndEnd("BOT_NOT_IN_GUILD");
        return;
    }

    int32 const withdrawRemaining = GetGuildBankWithdrawRemaining(guild, bot);
    SendAddonPacket(
        requester,
        replyType,
        "GBANK_RIGHTS",
        prefixPayload + std::string(1, kFieldSeparator)
            + (withdrawRemaining != 0 ? "1" : "0")
            + std::string(1, kFieldSeparator)
            + std::to_string(withdrawRemaining));

    std::map<uint32, uint32> itemCounts;
    std::map<uint32, ItemTemplate const*> itemTemplates;

    for (uint8 tabId = 0; tabId < GUILD_BANK_MAX_TABS; ++tabId)
    {
        QueryResult result = CharacterDatabase.Query(
            "SELECT ii.itemEntry, ii.count "
            "FROM guild_bank_item gbi "
            "INNER JOIN item_instance ii ON ii.guid = gbi.item_guid "
            "WHERE gbi.guildid = {} AND gbi.TabId = {} "
            "ORDER BY gbi.SlotId",
            guild->GetId(), uint32(tabId));

        if (!result)
            continue;

        do
        {
            Field* const fields = result->Fetch();
            AddItemEntryToSnapshot(itemCounts, itemTemplates, fields[0].Get<uint32>(), fields[1].Get<uint32>());
        }
        while (result->NextRow());
    }

    for (auto const& entry : itemCounts)
    {
        ItemTemplate const* const proto = itemTemplates[entry.first];
        if (!proto)
            continue;

        SendAddonPacket(requester, replyType, "GBANK_ITEM", prefixPayload + std::string(1, kFieldSeparator) + UrlEncodeField(ChatHelper::FormatItem(proto, entry.second)));
    }

    SendAddonPacket(requester, replyType, "GBANK_END", prefixPayload);
}

void RunInventoryItemActionCommand(Player* requester, ChatMsg replyType, std::string const& botName, std::string const& requestToken, std::string const& actionValue, std::string const& itemIdValue, std::string const& countValue)
{
    std::string const trimmedBotName = Acore::String::Trim(botName);
    std::string const token = Acore::String::Trim(requestToken);
    std::string const action = ToUpper(Acore::String::Trim(actionValue));
    uint32 const itemId = static_cast<uint32>(std::strtoul(Acore::String::Trim(itemIdValue).c_str(), nullptr, 10));
    uint32 const requestedCount = static_cast<uint32>(std::strtoul(Acore::String::Trim(countValue).c_str(), nullptr, 10));

    Player* const bot = FindBotByName(requester, trimmedBotName);
    std::string const effectiveBotName = bot ? bot->GetName() : trimmedBotName;

    std::string reason;
    uint32 moved = 0;
    if (!bot)
        reason = "NO_BOT";
    else if (action == "BANK_DEPOSIT")
        moved = MoveMatchingBagItemsToBank(bot, itemId, requestedCount, reason);
    else if (action == "BANK_WITHDRAW")
        moved = MoveMatchingBankItemsToBags(bot, itemId, requestedCount, reason);
    else if (action == "GBANK_DEPOSIT")
        moved = MoveMatchingBagItemsToGuildBank(requester, bot, itemId, requestedCount, reason);
    else if (action == "GBANK_WITHDRAW")
        moved = MoveMatchingGuildBankItemsToBags(bot, itemId, requestedCount, reason);
    else if (action == "BUY_ITEM")
        moved = BuyMatchingVendorItem(bot, itemId, requestedCount, reason);
    else
        reason = "BAD_ACTION";

    bool const ok = moved > 0;
    if (ok)
        reason = "OK";
    else if (reason.empty())
        reason = "FAILED";

    std::ostringstream payload;
    payload << UrlEncodeField(effectiveBotName)
        << kFieldSeparator << token
        << kFieldSeparator << action
        << kFieldSeparator << itemId
        << kFieldSeparator << (ok ? "OK" : "ERR")
        << kFieldSeparator << UrlEncodeField(reason)
        << kFieldSeparator << moved;

    SendAddonPacket(requester, replyType, "INVENTORY_ITEM_ACTION", payload.str());
}
