#pragma once

#include "Chat.h"
#include "Player.h"

#include <string>

struct InventorySummaryData
{
    uint32 gold = 0;
    uint32 silver = 0;
    uint32 copper = 0;
    uint32 bagUsed = 0;
    uint32 bagTotal = 16;
};

InventorySummaryData BuildInventorySummary(Player* bot);

void SendInventorySnapshot(Player* requester, ChatMsg replyType, std::string const& botName, std::string const& requestToken);
