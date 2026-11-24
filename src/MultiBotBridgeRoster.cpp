#include "MultiBotBridgeInternal.h"
#include "MultiBotBridgeRoster.h"

#include "PlayerbotAIConfig.h"
#include "Playerbots.h"
#include "PlayerbotsDatabase.h"

#include <sstream>
#include <string>

using namespace MultiBotBridgeInternal;

namespace
{
char const* const kOpcodeRoster = "ROSTER";
char const* const kOpcodeAccountRoster = "ACCOUNT_ROSTER";
char const* const kOpcodeGuildRoster = "GUILD_ROSTER";
char const* const kOpcodeFriendRoster = "FRIEND_ROSTER";

struct BotEntry
{
    std::string name;
    uint8 cls;
    uint8 level;
};

std::vector<BotEntry> ListAccountBots(uint32 accountId)
{
    std::vector<BotEntry> entries;
    QueryResult results = CharacterDatabase.Query("SELECT class, name, level FROM characters WHERE account = {}", accountId);
    if (results)
    {
        do
        {
            Field* fields = results->Fetch();
            uint8 cls = fields[0].Get<uint8>();
            std::string name = fields[1].Get<std::string>();
            uint8 level = fields[2].Get<uint8>();
            entries.push_back({name, cls, level});
        } while (results->NextRow());
    }
    return entries;
}

std::vector<BotEntry> ListLinkedAccountBots(uint32 masterAccountId)
{
    std::vector<BotEntry> entries;
    if (!sPlayerbotAIConfig.AllowTrustedAccountBots)
        return entries;
    QueryResult result = PlayerbotsDatabase.Query(
        "SELECT linked_account_id FROM playerbots_account_links WHERE account_id = {}", masterAccountId);
    if (!result)
        return entries;
    do
    {
        uint32 linkedAccountId = result->Fetch()[0].Get<uint32>();
        std::vector<BotEntry> linked = ListAccountBots(linkedAccountId);
        entries.insert(entries.end(), linked.begin(), linked.end());
    } while (result->NextRow());
    return entries;
}

std::vector<BotEntry> ListGuildBots(uint32 guildId)
{
    std::vector<BotEntry> entries;
    if (!sPlayerbotAIConfig.AllowGuildBots)
        return entries;
    QueryResult results = CharacterDatabase.Query(
        "SELECT c.name, c.class, c.level FROM characters c"
        " INNER JOIN guild_member gm ON gm.guid = c.guid"
        " WHERE gm.guildid = {}", guildId);
    if (results)
    {
        do
        {
            Field* fields = results->Fetch();
            std::string name = fields[0].Get<std::string>();
            uint8 cls = fields[1].Get<uint8>();
            uint8 level = fields[2].Get<uint8>();
            entries.push_back({name, cls, level});
        } while (results->NextRow());
    }
    return entries;
}

std::vector<BotEntry> ListFriendBots(ObjectGuid playerGuid)
{
    std::vector<BotEntry> entries;
    QueryResult results = CharacterDatabase.Query(
        "SELECT c.name, c.class, c.level FROM characters c"
        " INNER JOIN character_social cs ON cs.friend = c.guid"
        " WHERE cs.guid = {} AND cs.flags = 1", playerGuid.GetCounter());
    if (results)
    {
        do
        {
            Field* fields = results->Fetch();
            std::string name = fields[0].Get<std::string>();
            uint8 cls = fields[1].Get<uint8>();
            uint8 level = fields[2].Get<uint8>();
            entries.push_back({name, cls, level});
        } while (results->NextRow());
    }
    return entries;
}

} // namespace

void SendRosterPacket(Player* player, ChatMsg replyType)
{
    std::ostringstream out;
    bool first = true;

    std::vector<Player*> bots = GetBridgeVisibleBots(player);
    if (IsSelfBot(player))
        bots.insert(bots.begin(), player);

    for (Player* const bot : bots)
    {
        if (!first)
            out << ';';
        first = false;

        out << bot->GetName() << ',' << static_cast<uint32>(bot->getClass()) << ',' << static_cast<uint32>(bot->GetLevel())
            << ',' << static_cast<uint32>(bot->GetMapId()) << ',' << (bot->IsAlive() ? '1' : '0') << ','
            << GetPct(bot->GetHealth(), bot->GetMaxHealth()) << ',' << GetPct(bot->GetPower(POWER_MANA), bot->GetMaxPower(POWER_MANA));
    }

    SendAddonPacket(player, replyType, kOpcodeRoster, out.str());
}

void SendAccountRosterPacket(Player* player, ChatMsg replyType)
{
    uint32 const masterAccountId = player->GetSession()->GetAccountId();

    std::ostringstream out;
    bool first = true;

    auto appendEntries = [&](std::vector<BotEntry> const& entries)
    {
        for (BotEntry const& entry : entries)
        {
            if (!first)
                out << ';';
            first = false;
            out << entry.name << ',' << static_cast<uint32>(entry.cls) << ',' << static_cast<uint32>(entry.level);
        }
    };

    appendEntries(ListAccountBots(masterAccountId));
    appendEntries(ListLinkedAccountBots(masterAccountId));

    SendAddonPacket(player, replyType, kOpcodeAccountRoster, out.str());
}

void SendGuildRosterPacket(Player* player, ChatMsg replyType)
{
    uint32 const guildId = player->GetGuildId();
    if (!guildId)
    {
        SendAddonPacket(player, replyType, kOpcodeGuildRoster, "");
        return;
    }

    std::ostringstream out;
    bool first = true;

    for (BotEntry const& entry : ListGuildBots(guildId))
    {
        if (!first)
            out << ';';
        first = false;
        out << entry.name << ',' << static_cast<uint32>(entry.cls) << ',' << static_cast<uint32>(entry.level);
    }

    SendAddonPacket(player, replyType, kOpcodeGuildRoster, out.str());
}

void SendFriendRosterPacket(Player* player, ChatMsg replyType)
{
    std::ostringstream out;
    bool first = true;

    for (BotEntry const& entry : ListFriendBots(player->GetGUID()))
    {
        if (!first)
            out << ';';
        first = false;
        out << entry.name << ',' << static_cast<uint32>(entry.cls) << ',' << static_cast<uint32>(entry.level);
    }

    SendAddonPacket(player, replyType, kOpcodeFriendRoster, out.str());
}
