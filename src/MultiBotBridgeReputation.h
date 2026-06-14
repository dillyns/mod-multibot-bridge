#pragma once

#include "Chat.h"
#include "Player.h"

#include <string>

void SendBotReputationPackets(Player* requester, ChatMsg replyType, std::string const& botName, std::string const& requestToken);
