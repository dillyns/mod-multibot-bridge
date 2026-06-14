#pragma once

#include "Chat.h"
#include "Player.h"

#include <string>

void SendBankPackets(Player* requester, ChatMsg replyType, std::string const& botName, std::string const& requestToken);
void SendGuildBankPackets(Player* requester, ChatMsg replyType, std::string const& botName, std::string const& requestToken);
void RunInventoryItemActionCommand(Player* requester, ChatMsg replyType, std::string const& botName, std::string const& requestToken, std::string const& actionValue, std::string const& itemIdValue, std::string const& countValue);
