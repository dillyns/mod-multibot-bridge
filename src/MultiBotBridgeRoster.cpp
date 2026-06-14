#include "MultiBotBridgeInternal.h"
#include "MultiBotBridgeRoster.h"

#include "PlayerbotMgr.h"
#include "Playerbots.h"

#include <sstream>
#include <string>

using namespace MultiBotBridgeInternal;

namespace
{
char const* const kOpcodeRoster = "ROSTER";
char const* const kOpcodeAccountRoster = "ACCOUNT_ROSTER";
char const* const kOpcodeGuildRoster = "GUILD_ROSTER";
char const* const kOpcodeFriendRoster = "FRIEND_ROSTER";

} // namespace

void SendRosterPacket(Player* player, ChatMsg replyType)
{
    std::ostringstream out;
    bool first = true;

    for (Player* const bot : GetBridgeVisibleBots(player))
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
    PlayerbotMgr* mgr = GET_PLAYERBOT_MGR(player);
    if (!mgr)
        return;

    uint32 const masterAccountId = player->GetSession()->GetAccountId();

    std::ostringstream out;
    bool first = true;

    auto appendEntries = [&](std::vector<PlayerbotHolder::BotEntry> const& entries)
    {
        for (PlayerbotHolder::BotEntry const& entry : entries)
        {
            if (!first)
                out << ';';
            first = false;
            out << entry.name << ',' << static_cast<uint32>(entry.cls) << ',' << static_cast<uint32>(entry.level);
        }
    };

    appendEntries(mgr->ListAccountBots(masterAccountId));
    appendEntries(mgr->ListLinkedAccountBots(masterAccountId));

    SendAddonPacket(player, replyType, kOpcodeAccountRoster, out.str());
}

void SendGuildRosterPacket(Player* player, ChatMsg replyType)
{
    PlayerbotMgr* mgr = GET_PLAYERBOT_MGR(player);
    if (!mgr)
        return;

    uint32 const guildId = player->GetGuildId();
    if (!guildId)
    {
        SendAddonPacket(player, replyType, kOpcodeGuildRoster, "");
        return;
    }

    std::ostringstream out;
    bool first = true;

    for (PlayerbotHolder::BotEntry const& entry : mgr->ListGuildBots(guildId))
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
    PlayerbotMgr* mgr = GET_PLAYERBOT_MGR(player);
    if (!mgr)
        return;

    std::ostringstream out;
    bool first = true;

    for (PlayerbotHolder::BotEntry const& entry : mgr->ListFriendBots(player->GetGUID()))
    {
        if (!first)
            out << ';';
        first = false;
        out << entry.name << ',' << static_cast<uint32>(entry.cls) << ',' << static_cast<uint32>(entry.level);
    }

    SendAddonPacket(player, replyType, kOpcodeFriendRoster, out.str());
}
