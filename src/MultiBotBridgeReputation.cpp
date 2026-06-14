#include "MultiBotBridgeInternal.h"
#include "MultiBotBridgeReputation.h"
#include "DBCStores.h"
#include "ReputationMgr.h"
#include "StringFormat.h"
#include "Util.h"

#include <algorithm>
#include <sstream>
#include <string>
#include <vector>

using namespace MultiBotBridgeInternal;

namespace
{
char const* const kOpcodeReputationsBegin = "BOT_REPUTATIONS_BEGIN";
char const* const kOpcodeReputationsEnd = "BOT_REPUTATIONS_END";
char const* const kOpcodeReputationItem = "BOT_REPUTATION_ITEM";

struct BotReputationEntryData
{
    uint32 factionId = 0;
    std::string name;
    uint32 rank = 0;
    int32 value = 0;
    int32 maxValue = 0;
};

int32 GetReputationRankBase(ReputationRank rank)
{
    int32 base = ReputationMgr::Reputation_Cap + 1;
    for (int32 i = MAX_REPUTATION_RANK - 1; i >= static_cast<int32>(rank); --i)
        base -= ReputationMgr::PointsInRank[i];

    return base;
}

BotReputationEntryData BuildBotReputationEntry(Player* bot, FactionEntry const* entry)
{
    BotReputationEntryData data;
    if (!bot || !entry)
        return data;

    ReputationMgr& reputationMgr = bot->GetReputationMgr();
    ReputationRank const rank = reputationMgr.GetRank(entry);
    int32 const reputation = reputationMgr.GetReputation(entry->ID);
    int32 const maxValue = ReputationMgr::PointsInRank[rank];
    int32 value = reputation - GetReputationRankBase(rank);

    if (value < 0)
        value = 0;
    if (value > maxValue)
        value = maxValue;

    data.factionId = entry->ID;
    data.name = entry->name[0];
    data.rank = static_cast<uint32>(rank);
    data.value = value;
    data.maxValue = maxValue;
    return data;
}

std::vector<BotReputationEntryData> BuildBotReputationEntries(Player* bot)
{
    std::vector<BotReputationEntryData> entries;
    if (!bot)
        return entries;

    ReputationMgr& reputationMgr = bot->GetReputationMgr();
    FactionStateList const& stateList = reputationMgr.GetStateList();
    entries.reserve(stateList.size());

    for (auto const& itr : stateList)
    {
        FactionState const& faction = itr.second;
        if (!(faction.Flags & FACTION_FLAG_VISIBLE))
            continue;

        if (faction.Flags & (FACTION_FLAG_HIDDEN | FACTION_FLAG_INVISIBLE_FORCED) &&
            !(faction.Flags & FACTION_FLAG_SPECIAL))
            continue;

        FactionEntry const* const entry = sFactionStore.LookupEntry(faction.ID);
        if (!entry)
            continue;

        entries.push_back(BuildBotReputationEntry(bot, entry));
    }

    std::sort(entries.begin(), entries.end(), [](BotReputationEntryData const& left, BotReputationEntryData const& right)
    {
        return left.name < right.name;
    });

    return entries;
}

std::string BuildBotReputationEntryPayload(Player* bot, std::string const& token, BotReputationEntryData const& entry)
{
    std::ostringstream out;
    out << UrlEncodeField(bot->GetName())
        << kFieldSeparator << token
        << kFieldSeparator << entry.factionId
        << kFieldSeparator << UrlEncodeField(entry.name)
        << kFieldSeparator << entry.rank
        << kFieldSeparator << entry.value
        << kFieldSeparator << entry.maxValue;
    return out.str();
}
} // namespace

void SendBotReputationPackets(Player* requester, ChatMsg replyType, std::string const& botName, std::string const& requestToken)
{
    std::string const trimmedBotName = Acore::String::Trim(botName);
    Player* const bot = FindBotByName(requester, trimmedBotName);

    std::string const prefixPayload = UrlEncodeField(trimmedBotName) + std::string(1, kFieldSeparator) + requestToken;
    SendAddonPacket(requester, replyType, kOpcodeReputationsBegin, prefixPayload);

    if (!bot)
    {
        SendAddonPacket(requester, replyType, kOpcodeReputationsEnd, prefixPayload);
        return;
    }

    for (BotReputationEntryData const& entry : BuildBotReputationEntries(bot))
        SendAddonPacket(requester, replyType, kOpcodeReputationItem, BuildBotReputationEntryPayload(bot, requestToken, entry));

    SendAddonPacket(requester, replyType, kOpcodeReputationsEnd, UrlEncodeField(bot->GetName()) + std::string(1, kFieldSeparator) + requestToken);
}
