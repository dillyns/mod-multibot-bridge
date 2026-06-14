#pragma once

#include "Chat.h"
#include "Player.h"

#include <string>
#include <vector>

// Shared internal utilities used by multiple translation units within this module.
namespace MultiBotBridgeInternal
{
    inline constexpr char kFieldSeparator = '~';

    inline uint32 GetPct(uint32 current, uint32 max)
    {
        if (!max)
            return 0;

        return static_cast<uint32>((current * 100u) / max);
    }

    std::string UrlEncodeField(std::string const& value);
    std::string UrlDecodeField(std::string const& value);
    void SendAddonPacket(Player* player, ChatMsg chatType, std::string const& opcode, std::string const& payload = "");
    Player* FindBotByName(Player* player, std::string const& botName);
    std::vector<Player*> GetBridgeVisibleBots(Player* player);
} // namespace MultiBotBridgeInternal
