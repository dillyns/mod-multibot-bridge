#pragma once

#include "Chat.h"
#include "Player.h"

void SendRosterPacket(Player* player, ChatMsg replyType);
void SendAccountRosterPacket(Player* player, ChatMsg replyType);
void SendGuildRosterPacket(Player* player, ChatMsg replyType);
void SendFriendRosterPacket(Player* player, ChatMsg replyType);
