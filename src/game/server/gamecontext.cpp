/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "gamecontext.h"

#include <engine/storage.h>
#include <engine/map.h>
#include <engine/shared/config.h>

#include <game/gamecore.h>
#include <game/layers.h>

#include "gamemodes/dungeon.h"
#include "gamemodes/main.h"

#include "mmocore/CommandProcessor.h"
#include "mmocore/PathFinder.h"
#include "mmocore/GameEntities/loltext.h"
#include "mmocore/GameEntities/Items/drop_bonuses.h"
#include "mmocore/GameEntities/Items/drop_items.h"
#include "mmocore/GameEntities/Items/flying_experience.h"

#include "mmocore/Components/Accounts/AccountCore.h"
#include "mmocore/Components/Accounts/AccountMinerCore.h"
#include "mmocore/Components/Accounts/AccountPlantCore.h"
#include "mmocore/Components/Bots/BotCore.h"
#include "mmocore/Components/Dungeons/DungeonCore.h"
#include "mmocore/Components/Mails/MailBoxCore.h"
#include "mmocore/Components/Guilds/GuildCore.h"
#include "mmocore/Components/Houses/HouseCore.h"
#include "mmocore/Components/Quests/QuestCore.h"
#include "mmocore/Components/Skills/SkillsCore.h"

#include <cstdarg>

#include "mmocore/Components/Warehouse/WarehouseData.h"

// static data that have the same value in different objects
std::unordered_map < std::string, int > CGS::ms_aEffects[MAX_PLAYERS];
int CGS::m_MultiplierExp = 100;

CGS::CGS()
{
	for(auto& pBroadcastState : m_aBroadcastStates)
	{
		pBroadcastState.m_NoChangeTick = 0;
		pBroadcastState.m_LifeSpanTick = 0;
		pBroadcastState.m_aPrevMessage[0] = 0;
		pBroadcastState.m_aNextMessage[0] = 0;
		pBroadcastState.m_Priority = BroadcastPriority::LOWER;
	}

	for(auto& apPlayer : m_apPlayers)
		apPlayer = nullptr;

	m_pServer = nullptr;
	m_pController = nullptr;
	m_pMmoController = nullptr;
	m_pCommandProcessor = nullptr;
	m_pPathFinder = nullptr;
	m_pLayers = nullptr;
}

CGS::~CGS()
{
	m_Events.Clear();
	for(auto& pEffects : ms_aEffects)
		pEffects.clear();
	for(auto* apPlayer : m_apPlayers)
		delete apPlayer;

	delete m_pController;
	delete m_pMmoController;
	delete m_pCommandProcessor;
	delete m_pPathFinder;
	delete m_pLayers;
}

class CCharacter *CGS::GetPlayerChar(int ClientID)
{
	if(ClientID < 0 || ClientID >= MAX_CLIENTS || !m_apPlayers[ClientID])
		return nullptr;
	return m_apPlayers[ClientID]->GetCharacter();
}

CPlayer *CGS::GetPlayer(int ClientID, bool CheckAuthed, bool CheckCharacter)
{
	if(ClientID < 0 || ClientID >= MAX_CLIENTS || !m_apPlayers[ClientID])
		return nullptr;

	CPlayer *pPlayer = m_apPlayers[ClientID];
	if((CheckAuthed && pPlayer->IsAuthed()) || !CheckAuthed)
	{
		if(CheckCharacter && !pPlayer->GetCharacter())
			return nullptr;
		return pPlayer;
	}
	return nullptr;
}

CPlayer* CGS::GetPlayerFromUserID(int AccountID)
{
	for(int i = 0; i < MAX_PLAYERS; i++)
	{
		CPlayer* pPlayer = GetPlayer(i, true);
		if(pPlayer && pPlayer->Acc().m_UserID == AccountID)
			return pPlayer;
	}
	return nullptr;
}

// Level String by Matodor (Progress Bar) creates some sort of bar progress
std::unique_ptr<char[]> CGS::LevelString(int MaxValue, int CurrentValue, int Step, char toValue, char fromValue)
{
	CurrentValue = clamp(CurrentValue, 0, MaxValue);

	const int Size = 3 + MaxValue / Step;
	std::unique_ptr<char[]> Buf(new char[Size]);
	Buf[0] = '[';
	Buf[Size - 2] = ']';
	Buf[Size - 1] = '\0';

	const int a = CurrentValue / Step;
	const int b = (MaxValue - CurrentValue) / Step;
    int i = 1;

	for (int ai = 0; ai < a; ai++, i++)
		Buf[i] = toValue;
	for (int bi = 0; bi < b || i < Size - 2; bi++, i++)
		Buf[i] = fromValue;

	return Buf;
}

CItemDescription* CGS::GetItemInfo(ItemIdentifier ItemID) const
{
	dbg_assert(CItemDescription::Data().find(ItemID) != CItemDescription::Data().end(), "invalid referring to the CItemDescription");

	return &CItemDescription::Data()[ItemID];
}
CQuestDataInfo &CGS::GetQuestInfo(int QuestID) const { return CQuestDataInfo::ms_aDataQuests[QuestID]; }

CAttributeDescription* CGS::GetAttributeInfo(AttributeIdentifier ID) const
{
	dbg_assert(CAttributeDescription::Data().find(ID) != CAttributeDescription::Data().end(), "invalid referring to the CAttributeDescription");

	return &CAttributeDescription::Data()[ID];
}

CWarehouse* CGS::GetWarehouse(int ID) const
{
	dbg_assert(CWarehouse::Data().find(ID) != CWarehouse::Data().end(), "invalid referring to the CWarehouse");

	return &CWarehouse::Data()[ID];
}

/* #########################################################################
	EVENTS
######################################################################### */
void CGS::CreateDamage(vec2 Pos, int ClientID, int Amount, bool CritDamage)
{
	float a = 3 * 3.14159f / 2 /* + Angle */;
	//float a = get_angle(dir);
	float s = a - pi / 3;
	float e = a + pi / 3;
	for (int i = 0; i < Amount; i++)
	{
		float f = mix(s, e, float(i + 1) / float(Amount + 2));
		CNetEvent_DamageInd* pEvent = (CNetEvent_DamageInd*)m_Events.Create(NETEVENTTYPE_DAMAGEIND, sizeof(CNetEvent_DamageInd));
		if (pEvent)
		{
			pEvent->m_X = (int)Pos.x;
			pEvent->m_Y = (int)Pos.y;
			pEvent->m_Angle = (int)(f * 256.0f);
		}
	}
}

void CGS::CreateHammerHit(vec2 Pos)
{
	CNetEvent_HammerHit *pEvent = (CNetEvent_HammerHit *)m_Events.Create(NETEVENTTYPE_HAMMERHIT, sizeof(CNetEvent_HammerHit));
	if(pEvent)
	{
		pEvent->m_X = (int)Pos.x;
		pEvent->m_Y = (int)Pos.y;
	}
}

void CGS::CreateExplosion(vec2 Pos, int Owner, int Weapon, int MaxDamage)
{
	// create the event
	CNetEvent_Explosion *pEvent = (CNetEvent_Explosion *)m_Events.Create(NETEVENTTYPE_EXPLOSION, sizeof(CNetEvent_Explosion));
	if(pEvent)
	{
		pEvent->m_X = (int)Pos.x;
		pEvent->m_Y = (int)Pos.y;
	}

	// deal damage
	CCharacter *apEnts[MAX_CLIENTS];
	constexpr float Radius = 135.0f;
	constexpr float InnerRadius = 48.0f;
	const int Num = m_World.FindEntities(Pos, Radius, (CEntity **)apEnts, MAX_CLIENTS, CGameWorld::ENTTYPE_CHARACTER);
	for(int i = 0; i < Num; i++)
	{
		vec2 Diff = apEnts[i]->GetPos() - Pos;
		vec2 ForceDir(0, 1);
		const float Length = length(Diff);
		if(Length)
			ForceDir = normalize(Diff) * 1.0f;

		const float Factor = 1 - clamp((Length-InnerRadius)/(Radius-InnerRadius), 0.0f, 1.0f);
		if (const int Damage = (int)(Factor * MaxDamage))
		{
			float Strength;
			if(Owner == -1 || !m_apPlayers[Owner])
				Strength = 0.5f;
			else
				Strength = m_apPlayers[Owner]->m_NextTuningParams.m_ExplosionStrength;
			
			apEnts[i]->TakeDamage(ForceDir * (Strength * Length), Damage, Owner, Weapon);
		}
	}
}

void CGS::CreatePlayerSpawn(vec2 Pos, int64 Mask)
{
	CNetEvent_Spawn *ev = (CNetEvent_Spawn *)m_Events.Create(NETEVENTTYPE_SPAWN, sizeof(CNetEvent_Spawn), Mask);
	if(ev)
	{
		ev->m_X = (int)Pos.x;
		ev->m_Y = (int)Pos.y;
	}
}

void CGS::CreateDeath(vec2 Pos, int ClientID)
{
	CNetEvent_Death *pEvent = (CNetEvent_Death *)m_Events.Create(NETEVENTTYPE_DEATH, sizeof(CNetEvent_Death));
	if(pEvent)
	{
		pEvent->m_X = (int)Pos.x;
		pEvent->m_Y = (int)Pos.y;
		pEvent->m_ClientID = ClientID;
	}
}

void CGS::CreateSound(vec2 Pos, int Sound, int64 Mask)
{
	// fix for vanilla unterstand SoundID
	if(Sound < 0 || Sound > 40)
		return;

	CNetEvent_SoundWorld *pEvent = (CNetEvent_SoundWorld *)m_Events.Create(NETEVENTTYPE_SOUNDWORLD, sizeof(CNetEvent_SoundWorld), Mask);
	if(pEvent)
	{
		pEvent->m_X = (int)Pos.x;
		pEvent->m_Y = (int)Pos.y;
		pEvent->m_SoundID = Sound;
	}
}

void CGS::CreatePlayerSound(int ClientID, int Sound)
{
	// fix for vanilla unterstand SoundID
	if(!m_apPlayers[ClientID] || Sound < 0 || Sound > 40)
		return;

	CNetEvent_SoundWorld* pEvent = (CNetEvent_SoundWorld*)m_Events.Create(NETEVENTTYPE_SOUNDWORLD, sizeof(CNetEvent_SoundWorld), CmaskOne(ClientID));
	if(pEvent)
	{
		pEvent->m_X = (int)m_apPlayers[ClientID]->m_ViewPos.x;
		pEvent->m_Y = (int)m_apPlayers[ClientID]->m_ViewPos.y;
		pEvent->m_SoundID = Sound;
	}
}

/* #########################################################################
	CHAT FUNCTIONS
######################################################################### */
void CGS::SendChat(int ChatterClientID, int Mode, const char *pText)
{
	char aBuf[256];
	if(ChatterClientID >= 0 && ChatterClientID < MAX_CLIENTS)
		str_format(aBuf, sizeof(aBuf), "%d:%d:%s: %s", ChatterClientID, Mode, Server()->ClientName(ChatterClientID), pText);
	else
		str_format(aBuf, sizeof(aBuf), "*** %s", pText);

	char aBufMode[32];
	if(Mode == CHAT_TEAM)
		str_copy(aBufMode, "teamchat", sizeof(aBufMode));
	else
		str_copy(aBufMode, "chat", sizeof(aBufMode));
	Console()->Print(IConsole::OUTPUT_LEVEL_ADDINFO, aBufMode, aBuf);

	CNetMsg_Sv_Chat Msg;
	Msg.m_Team = -1;
	Msg.m_ClientID = ChatterClientID;
	Msg.m_pMessage = pText;

	if(Mode == CHAT_ALL)
	{
		// send discord chat only from players
		if(ChatterClientID < MAX_PLAYERS)
			Server()->SendDiscordMessage(g_Config.m_SvDiscordServerChatChannel, DC_SERVER_CHAT, Server()->ClientName(ChatterClientID), pText);

		Server()->SendPackMsg(&Msg, MSGFLAG_VITAL | MSGFLAG_NORECORD, -1);
	}
	else if(Mode == CHAT_TEAM)
	{
		CPlayer* pChatterPlayer = GetPlayer(ChatterClientID, true);
		if(!pChatterPlayer || pChatterPlayer->Acc().m_GuildID <= 0)
		{
			Chat(ChatterClientID, "This chat is intended for team / guilds!");
			return;
		}

		// send discord chat only from players
		if(pChatterPlayer)
			Server()->SendDiscordMessage(g_Config.m_SvDiscordServerChatChannel, DC_SERVER_CHAT, Server()->ClientName(ChatterClientID), pText);

		// pack one for the recording only
		Server()->SendPackMsg(&Msg, MSGFLAG_VITAL|MSGFLAG_NOSEND, -1);

		// send chat to guild team
		const int GuildID = pChatterPlayer->Acc().m_GuildID;
		for(int i = 0; i < MAX_PLAYERS; i++)
		{
			CPlayer *pSearchPlayer = GetPlayer(i, true);
			if(pSearchPlayer && pSearchPlayer->Acc().m_GuildID == GuildID)
				Server()->SendPackMsg(&Msg, MSGFLAG_VITAL|MSGFLAG_NORECORD, i);
		}
	}
}

// Fake chat player in game
void CGS::FakeChat(const char *pName, const char *pText)
{
	const int FakeClientID = CreateBot(BotsTypes::TYPE_BOT_FAKE, 1, 1);
	if(FakeClientID < 0 || FakeClientID > MAX_CLIENTS || !m_apPlayers[FakeClientID])
		return;

	// send a chat and delete a player and throw a player away
	SendChat(FakeClientID, CHAT_ALL, pText);
	delete m_apPlayers[FakeClientID];
	m_apPlayers[FakeClientID] = nullptr;
}

// send a formatted message
void CGS::Chat(int ClientID, const char* pText, ...)
{
	const int Start = (ClientID < 0 ? 0 : ClientID);
	const int End = (ClientID < 0 ? MAX_PLAYERS : ClientID + 1);

	CNetMsg_Sv_Chat Msg;
	Msg.m_Team = -1;
	Msg.m_ClientID = -1;

	va_list VarArgs;
	va_start(VarArgs, pText);

	dynamic_string Buffer;
	for (int i = Start; i < End; i++)
	{
		if (m_apPlayers[i])
		{
			Server()->Localization()->Format_VL(Buffer, m_apPlayers[i]->GetLanguage(), pText, VarArgs);

			Msg.m_pMessage = Buffer.buffer();
			Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, i);
			Buffer.clear();
		}
	}
	va_end(VarArgs);
}

// send to an authorized player
bool CGS::ChatAccount(int AccountID, const char* pText, ...)
{
	CPlayer *pPlayer = GetPlayerFromUserID(AccountID);
	if(!pPlayer)
		return false;

	CNetMsg_Sv_Chat Msg;
	Msg.m_Team = -1;
	Msg.m_ClientID = -1;

	va_list VarArgs;
	va_start(VarArgs, pText);

	dynamic_string Buffer;
	Server()->Localization()->Format_VL(Buffer, pPlayer->GetLanguage(), pText, VarArgs);

	Msg.m_pMessage = Buffer.buffer();
	Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, pPlayer->GetCID());
	Buffer.clear();
	va_end(VarArgs);
	return true;
}

// Send a guild a message
void CGS::ChatGuild(int GuildID, const char* pText, ...)
{
	if(GuildID <= 0)
		return;

	CNetMsg_Sv_Chat Msg;
	Msg.m_Team = -1;
	Msg.m_ClientID = -1;

	va_list VarArgs;
	va_start(VarArgs, pText);

	dynamic_string Buffer;
	for(int i = 0 ; i < MAX_PLAYERS ; i ++)
	{
		CPlayer *pPlayer = GetPlayer(i, true);
		if(pPlayer && pPlayer->Acc().IsGuild() && pPlayer->Acc().m_GuildID == GuildID)
		{
			Buffer.append("[Guild]");
			Server()->Localization()->Format_VL(Buffer, m_apPlayers[i]->GetLanguage(), pText, VarArgs);

			Msg.m_pMessage = Buffer.buffer();

			Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, i);
			Buffer.clear();
		}
	}
	va_end(VarArgs);
}

// Send a message in world
void CGS::ChatWorldID(int WorldID, const char* Suffix, const char* pText, ...)
{
	CNetMsg_Sv_Chat Msg;
	Msg.m_Team = -1;
	Msg.m_ClientID = -1;

	va_list VarArgs;
	va_start(VarArgs, pText);

	dynamic_string Buffer;
	for (int i = 0; i < MAX_PLAYERS; i++)
	{
		CPlayer* pPlayer = GetPlayer(i, true);
		if(!pPlayer || !IsPlayerEqualWorldID(i, WorldID))
			continue;

		Buffer.append(Suffix);
		Server()->Localization()->Format_VL(Buffer, m_apPlayers[i]->GetLanguage(), pText, VarArgs);

		Msg.m_pMessage = Buffer.buffer();

		Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, i, WorldID);
		Buffer.clear();
	}
	va_end(VarArgs);
}

// Send discord message
void CGS::ChatDiscord(int Color, const char *Title, const char* pText, ...)
{
#ifdef CONF_DISCORD
	va_list VarArgs;
	va_start(VarArgs, pText);

	dynamic_string Buffer;
	Server()->Localization()->Format_VL(Buffer, "en", pText, VarArgs);
	Server()->SendDiscordMessage(g_Config.m_SvDiscordServerChatChannel, Color, Title, Buffer.buffer());
	Buffer.clear();

	va_end(VarArgs);
#endif
}

// Send a discord message to the channel
void CGS::ChatDiscordChannel(const char *pChanel, int Color, const char *Title, const char* pText, ...)
{
#ifdef CONF_DISCORD
	va_list VarArgs;
	va_start(VarArgs, pText);

	dynamic_string Buffer;
	Server()->Localization()->Format_VL(Buffer, "en", pText, VarArgs);
	Server()->SendDiscordMessage(pChanel, Color, Title, Buffer.buffer());
	Buffer.clear();

	va_end(VarArgs);
#endif
}

// Send Motd
void CGS::Motd(int ClientID, const char* Text, ...)
{
	if(ClientID < 0 || ClientID >= MAX_PLAYERS)
		return;

	CNetMsg_Sv_Motd Msg;

	va_list VarArgs;
	va_start(VarArgs, Text);

	if(m_apPlayers[ClientID])
	{
		dynamic_string Buffer;
		Server()->Localization()->Format_VL(Buffer, m_apPlayers[ClientID]->GetLanguage(), Text, VarArgs);

		Msg.m_pMessage = Buffer.buffer();

		Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, ClientID);
		Buffer.clear();
	}
	va_end(VarArgs);
}

/* #########################################################################
	BROADCAST FUNCTIONS
######################################################################### */
void CGS::AddBroadcast(int ClientID, const char* pText, BroadcastPriority Priority, int LifeSpan)
{
	if (ClientID < 0 || ClientID >= MAX_PLAYERS)
		return;

	if(m_aBroadcastStates[ClientID].m_TimedPriority > Priority)
		return;

	str_copy(m_aBroadcastStates[ClientID].m_aTimedMessage, pText, sizeof(m_aBroadcastStates[ClientID].m_aTimedMessage));
	m_aBroadcastStates[ClientID].m_LifeSpanTick = LifeSpan;
	m_aBroadcastStates[ClientID].m_TimedPriority = Priority;
}

// formatted broadcast
void CGS::Broadcast(int ClientID, BroadcastPriority Priority, int LifeSpan, const char *pText, ...)
{
	const int Start = (ClientID < 0 ? 0 : ClientID);
	const int End = (ClientID < 0 ? MAX_PLAYERS : ClientID+1);

	va_list VarArgs;
	va_start(VarArgs, pText);
	for(int i = Start; i < End; i++)
	{
		if(m_apPlayers[i])
		{
			dynamic_string Buffer;
			Server()->Localization()->Format_VL(Buffer, m_apPlayers[i]->GetLanguage(), pText, VarArgs);
			AddBroadcast(i, Buffer.buffer(), Priority, LifeSpan);
			Buffer.clear();
		}
	}
	va_end(VarArgs);
}

// formatted world broadcast
void CGS::BroadcastWorldID(int WorldID, BroadcastPriority Priority, int LifeSpan, const char *pText, ...)
{
	va_list VarArgs;
	va_start(VarArgs, pText);
	for(int i = 0; i < MAX_PLAYERS; i++)
	{
		if(m_apPlayers[i] && IsPlayerEqualWorldID(i, WorldID))
		{
			dynamic_string Buffer;
			Server()->Localization()->Format_VL(Buffer, m_apPlayers[i]->GetLanguage(), pText, VarArgs);
			AddBroadcast(i, Buffer.buffer(), Priority, LifeSpan);
			Buffer.clear();
		}
	}
	va_end(VarArgs);
}

// the tick of the broadcast and his life
void CGS::BroadcastTick(int ClientID)
{
	if (ClientID < 0 || ClientID >= MAX_PLAYERS)
		return;

	if(m_apPlayers[ClientID] && IsPlayerEqualWorldID(ClientID))
	{
		CBroadcastState& Broadcast = m_aBroadcastStates[ClientID];
		if(Broadcast.m_LifeSpanTick > 0 && Broadcast.m_TimedPriority > Broadcast.m_Priority)
		{
			// combine game information with game priority
			if(Broadcast.m_TimedPriority < BroadcastPriority::MAIN_INFORMATION)
			{
				char aAppendBuf[1024]{ };
				str_copy(aAppendBuf, Broadcast.m_aTimedMessage, sizeof(aAppendBuf));
				m_apPlayers[ClientID]->FormatBroadcastBasicStats(Broadcast.m_aNextMessage, sizeof(Broadcast.m_aNextMessage), aAppendBuf);
				Broadcast.m_LifeSpanTick = 1000;
			}
			else
			{
				str_copy(Broadcast.m_aNextMessage, Broadcast.m_aTimedMessage, sizeof(Broadcast.m_aNextMessage));
			}
		}

		// send broadcast only if the message is different, or to fight auto-fading
		if(Broadcast.m_NoChangeTick > Server()->TickSpeed() || str_comp(m_aBroadcastStates[ClientID].m_aPrevMessage, m_aBroadcastStates[ClientID].m_aNextMessage) != 0)
		{
			CNetMsg_Sv_Broadcast Msg;
			Msg.m_pMessage = Broadcast.m_aNextMessage;
			Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, ClientID);
			str_copy(Broadcast.m_aPrevMessage, Broadcast.m_aNextMessage, sizeof(Broadcast.m_aPrevMessage));
			Broadcast.m_NoChangeTick = 0;
		}
		else
		{
			Broadcast.m_NoChangeTick++;
		}

		// update broadcast state
		if(Broadcast.m_LifeSpanTick > 0)
			Broadcast.m_LifeSpanTick--;

		if(Broadcast.m_LifeSpanTick <= 0)
		{
			Broadcast.m_aTimedMessage[0] = 0;
			Broadcast.m_TimedPriority = BroadcastPriority::LOWER;
		}
		Broadcast.m_aNextMessage[0] = 0;
		Broadcast.m_Priority = BroadcastPriority::LOWER;
	}
	else
	{
		m_aBroadcastStates[ClientID].m_NoChangeTick = 0;
		m_aBroadcastStates[ClientID].m_LifeSpanTick = 0;
		m_aBroadcastStates[ClientID].m_aPrevMessage[0] = 0;
		m_aBroadcastStates[ClientID].m_aNextMessage[0] = 0;
		m_aBroadcastStates[ClientID].m_aTimedMessage[0] = 0;
		m_aBroadcastStates[ClientID].m_Priority = BroadcastPriority::LOWER;
		m_aBroadcastStates[ClientID].m_TimedPriority = BroadcastPriority::LOWER;
	}
}

/* #########################################################################
	PACKET MESSAGE FUNCTIONS
######################################################################### */
void CGS::SendEmoticon(int ClientID, int Emoticon, bool SenderClient)
{
	CPlayer* pPlayer = GetPlayer(ClientID, true, true);
	if (pPlayer && SenderClient)
		Mmo()->Skills()->ParseEmoticionSkill(pPlayer, Emoticon);

	CNetMsg_Sv_Emoticon Msg;
	Msg.m_ClientID = ClientID;
	Msg.m_Emoticon = Emoticon;
	Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, -1, m_WorldID);
}

void CGS::SendWeaponPickup(int ClientID, int Weapon)
{
	CNetMsg_Sv_WeaponPickup Msg;
	Msg.m_Weapon = Weapon;
	Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, ClientID);
}

void CGS::SendMotd(int ClientID)
{
	CNetMsg_Sv_Motd Msg;
	Msg.m_pMessage = g_Config.m_SvMotd;
	Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, ClientID);
}

// Send a change of skin
void CGS::SendSkinChange(int ClientID, int TargetID)
{
	CPlayer *pPlayer = GetPlayer(ClientID, true);
	if(!pPlayer)
		return;

/*	CNetMsg_Sv_SkinChange Msg;
	Msg.m_ClientID = ClientID;
	for(int p = 0; p < NUM_SKINPARTS; p++)
	{
		Msg.m_apSkinPartNames[p] = pPlayer->Acc().m_aaSkinPartNames[p];
		Msg.m_aUseCustomColors[p] = pPlayer->Acc().m_aUseCustomColors[p];
		Msg.m_aSkinPartColors[p] = pPlayer->Acc().m_aSkinPartColors[p];
	}
	Server()->SendPackMsg(&Msg, MSGFLAG_VITAL|MSGFLAG_NORECORD, TargetID);*/
}

void CGS::SendGameMsg(int GameMsgID, int ClientID)
{
//	CMsgPacker Msg(NETMSGTYPE_SV_GAMEMSG);
//	Msg.AddInt(GameMsgID);
//	Server()->SendMsg(&Msg, MSGFLAG_VITAL, ClientID);
}

void CGS::SendGameMsg(int GameMsgID, int ParaI1, int ClientID)
{
//	CMsgPacker Msg(NETMSGTYPE_SV_GAMEMSG);
//	Msg.AddInt(GameMsgID);
//	Msg.AddInt(ParaI1);
//	Server()->SendMsg(&Msg, MSGFLAG_VITAL, ClientID);
}

void CGS::SendGameMsg(int GameMsgID, int ParaI1, int ParaI2, int ParaI3, int ClientID)
{
//	CMsgPacker Msg(NETMSGTYPE_SV_GAMEMSG);
//	Msg.AddInt(GameMsgID);
//	Msg.AddInt(ParaI1);
//	Msg.AddInt(ParaI2);
//	Msg.AddInt(ParaI3);
//	Server()->SendMsg(&Msg, MSGFLAG_VITAL, ClientID);
}

void CGS::SendTuningParams(int ClientID)
{
	CMsgPacker Msg(NETMSGTYPE_SV_TUNEPARAMS);
	int *pParams = (int *)&m_Tuning;
	for(unsigned i = 0; i < sizeof(m_Tuning)/sizeof(int); i++)
		Msg.AddInt(pParams[i]);
	Server()->SendMsg(&Msg, MSGFLAG_VITAL, ClientID);
}

/* #########################################################################
	ENGINE GAMECONTEXT
######################################################################### */
void CGS::UpdateDiscordStatus()
{
#ifdef CONF_DISCORD
	if(Server()->Tick() % (Server()->TickSpeed() * 10) != 0 || m_WorldID != MAIN_WORLD_ID)
		return;

	int Players = 0;
	for(int i = 0; i < MAX_PLAYERS; i++)
	{
		if(Server()->ClientIngame(i))
			Players++;
	}

	if(Players > 0)
	{
		char aBuf[64];
		str_format(aBuf, sizeof(aBuf), "%d player's play MRPG!", Players);
		Server()->UpdateDiscordStatus(aBuf);
		return;
	}
	Server()->UpdateDiscordStatus("and expects players.");
#endif
}

void CGS::OnInit(int WorldID)
{
	m_pServer = Kernel()->RequestInterface<IServer>();
	m_pConsole = Kernel()->RequestInterface<IConsole>();
	m_pStorage = Kernel()->RequestInterface<IStorageEngine>();
	m_World.SetGameServer(this);
	m_Events.SetGameServer(this);
	m_WorldID = WorldID;
	m_RespawnWorldID = -1;

	for(int i = 0; i < NUM_NETOBJTYPES; i++)
		Server()->SnapSetStaticsize(i, m_NetObjHandler.GetObjSize(i));

	// create controller
	m_pLayers = new CLayers();
	m_pLayers->Init(Kernel(), WorldID);
	m_Collision.Init(m_pLayers);
	m_pMmoController = new MmoController(this);
	m_pMmoController->LoadLogicWorld();
	UpdateZoneDungeon();
	UpdateZonePVP();

	// create all game objects for the server
	if(IsDungeon())
		m_pController = new CGameControllerDungeon(this);
	else
		m_pController = new CGameControllerMain(this);

	m_pCommandProcessor = new CCommandProcessor(this);

	// initialize cores
	CMapItemLayerTilemap *pTileMap = m_pLayers->GameLayer();
	CTile *pTiles = (CTile *)Kernel()->RequestInterface<IMap>(WorldID)->GetData(pTileMap->m_Data);
	for(int y = 0; y < pTileMap->m_Height; y++)
	{
		for(int x = 0; x < pTileMap->m_Width; x++)
		{
			const int Index = pTiles[y*pTileMap->m_Width+x].m_Index;
			if(Index >= ENTITY_OFFSET)
			{
				const vec2 Pos(x*32.0f+16.0f, y*32.0f+16.0f);
				m_pController->OnEntity(Index-ENTITY_OFFSET, Pos);
			}
		}
	}

	// initialize pathfinder
	m_pPathFinder = new CPathfinder(m_pLayers, &m_Collision);
	Console()->Chain("sv_motd", ConchainSpecialMotdupdate, this);
}

void CGS::OnConsoleInit()
{
	m_pServer = Kernel()->RequestInterface<IServer>();
	m_pConsole = Kernel()->RequestInterface<IConsole>();

	Console()->Register("set_world_time", "i[hour]", CFGFLAG_SERVER, ConSetWorldTime, m_pServer, "Set worlds time.");
	Console()->Register("parseskin", "i[cid]", CFGFLAG_SERVER, ConParseSkin, m_pServer, "Parse skin on console. Easy for devlop bots.");
	Console()->Register("giveitem", "i[cid]i[itemid]i[count]i[enchant]i[mail]", CFGFLAG_SERVER, ConGiveItem, m_pServer, "Give item <clientid> <itemid> <count> <enchant> <mail 1=yes 0=no>");
	Console()->Register("removeitem", "i[cid]i[itemid]i[count]", CFGFLAG_SERVER, ConRemItem, m_pServer, "Remove item <clientid> <itemid> <count>");
	Console()->Register("disband_guild", "r[guildname]", CFGFLAG_SERVER, ConDisbandGuild, m_pServer, "Disband the guild with the name");
	Console()->Register("say", "r[text]", CFGFLAG_SERVER, ConSay, m_pServer, "Say in chat");
	Console()->Register("addcharacter", "i[cid]r[botname]", CFGFLAG_SERVER, ConAddCharacter, m_pServer, "(Warning) Add new bot on database or update if finding <clientid> <bot name>");
	Console()->Register("sync_lines_for_translate", "", CFGFLAG_SERVER, ConSyncLinesForTranslate, m_pServer, "Perform sync lines in translated files. Order non updated translated to up");
}

void CGS::OnTick()
{
	m_World.m_Core.m_Tuning = m_Tuning;
	m_World.Tick();
	m_pController->Tick();


	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(!Server()->ClientIngame(i) || !m_apPlayers[i] || m_apPlayers[i]->GetPlayerWorldID() != m_WorldID)
			continue;

		m_apPlayers[i]->Tick();
		m_apPlayers[i]->PostTick();
		if(i < MAX_PLAYERS)
		{
			BroadcastTick(i);
		}
	}

	Mmo()->OnTick();
}

// Here we use functions that can have static data or functions that don't need to be called in all worlds
void CGS::OnTickMainWorld()
{
	if(m_DayEnumType != Server()->GetEnumTypeDay())
	{
		m_DayEnumType = Server()->GetEnumTypeDay();
		if(m_DayEnumType == DayType::NIGHT_TYPE)
			m_MultiplierExp = 100 + random_int() % 200;
		else if(m_DayEnumType == DayType::MORNING_TYPE)
			m_MultiplierExp = 100;

		SendDayInfo(-1);
	}

	UpdateDiscordStatus();
}

// output of all objects
void CGS::OnSnap(int ClientID)
{
	CPlayer* pPlayer = m_apPlayers[ClientID];
	if(!pPlayer || pPlayer->GetPlayerWorldID() != GetWorldID())
		return;

	m_World.Snap(ClientID);
	m_pController->Snap();
	m_Events.Snap(ClientID);
	for(auto& arpPlayer : m_apPlayers)
	{
		if(arpPlayer)
			arpPlayer->Snap(ClientID);
	}
}

void CGS::OnPreSnap() {}
void CGS::OnPostSnap()
{
	m_World.PostSnap();
	m_Events.Clear();
}

void CGS::OnMessage(int MsgID, CUnpacker *pUnpacker, int ClientID)
{
	void *pRawMsg = m_NetObjHandler.SecureUnpackMsg(MsgID, pUnpacker);
	CPlayer *pPlayer = m_apPlayers[ClientID];

	if(!pRawMsg)
	{
		if(g_Config.m_Debug)
		{
			char aBuf[256];
			str_format(aBuf, sizeof(aBuf), "dropped weird message '%s' (%d), failed on '%s'", m_NetObjHandler.GetMsgName(MsgID), MsgID, m_NetObjHandler.FailedMsgOn());
			Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "server", aBuf);
		}
		return;
	}

	if(Server()->ClientIngame(ClientID))
	{
		if(MsgID == NETMSGTYPE_CL_SAY)
		{
			if(g_Config.m_SvSpamprotection && pPlayer->m_aPlayerTick[TickState::LastChat] && pPlayer->m_aPlayerTick[TickState::LastChat]+Server()->TickSpeed() > Server()->Tick())
				return;

			CNetMsg_Cl_Say* pMsg = (CNetMsg_Cl_Say*)pRawMsg;
			if (!str_utf8_check(pMsg->m_pMessage))
			{
				return;
			}

			// trim right and set maximum length to 128 utf8-characters
			int Length = 0;
			const char *p = pMsg->m_pMessage;
			const char *pEnd = nullptr;
			while(*p)
			{
				const char *pStrOld = p;
				const int Code = str_utf8_decode(&p);

				// check if unicode is not empty
				if(Code > 0x20 && Code != 0xA0 && Code != 0x034F && (Code < 0x2000 || Code > 0x200F) && (Code < 0x2028 || Code > 0x202F) &&
					(Code < 0x205F || Code > 0x2064) && (Code < 0x206A || Code > 0x206F) && (Code < 0xFE00 || Code > 0xFE0F) &&
					Code != 0xFEFF && (Code < 0xFFF9 || Code > 0xFFFC))
				{
					pEnd = nullptr;
				}
				else if(pEnd == nullptr)
					pEnd = pStrOld;

				if(++Length >= 127)
				{
					*(const_cast<char *>(p)) = 0;
					break;
				}
			}
			if(pEnd != nullptr)
				*(const_cast<char *>(pEnd)) = 0;

			pPlayer->m_aPlayerTick[TickState::LastChat] = Server()->Tick();

			if(pMsg->m_pMessage[0] == '/')
			{
				CommandProcessor()->ChatCmd(pMsg->m_pMessage, pPlayer);
				return;
			}
			SendChat(ClientID, CHAT_ALL, pMsg->m_pMessage);
		}

		else if(MsgID == NETMSGTYPE_CL_CALLVOTE)
		{
			CNetMsg_Cl_CallVote *pMsg = (CNetMsg_Cl_CallVote *)pRawMsg;
			if (str_comp_nocase(pMsg->m_pType, "option") != 0 || Server()->Tick() < (pPlayer->m_aPlayerTick[TickState::LastVoteTry] + (Server()->TickSpeed() / 2)))
				return;

			pPlayer->m_aPlayerTick[TickState::LastVoteTry] = Server()->Tick();
			const auto& iter = std::find_if(m_aPlayerVotes[ClientID].begin(), m_aPlayerVotes[ClientID].end(), [pMsg](const CVoteOptions& vote)
			{
				const int length = str_length(pMsg->m_pValue);
				return (str_comp_nocase_num(pMsg->m_pValue, vote.m_aDescription, length) == 0);
			});

			if(iter != m_aPlayerVotes[ClientID].end())
			{
				const int InteractiveValue = string_to_number(pMsg->m_pReason, 1, 10000000);
				ParsingVoteCommands(ClientID, iter->m_aCommand, iter->m_TempID, iter->m_TempID2, InteractiveValue, pMsg->m_pReason, iter->m_Callback);
				return;
			}
			ResetVotes(ClientID, pPlayer->m_OpenVoteMenu);
		}

		else if(MsgID == NETMSGTYPE_CL_VOTE)
		{
			CNetMsg_Cl_Vote *pMsg = (CNetMsg_Cl_Vote *)pRawMsg;
			if(pPlayer->ParseItemsF3F4(pMsg->m_Vote))
				return;
		}

		else if(MsgID == NETMSGTYPE_CL_SETTEAM)
		{
			if(!pPlayer->IsAuthed())
			{
				Broadcast(pPlayer->GetCID(), BroadcastPriority::MAIN_INFORMATION, 100, "Use /register <name> <pass>.");
				return;
			}

			Broadcast(ClientID, BroadcastPriority::MAIN_INFORMATION, 100, "Team change is not allowed.");
		}

		else if (MsgID == NETMSGTYPE_CL_SETSPECTATORMODE)
		{
			return;
		}

		else if (MsgID == NETMSGTYPE_CL_EMOTICON)
		{
			CNetMsg_Cl_Emoticon *pMsg = (CNetMsg_Cl_Emoticon *)pRawMsg;

			if(g_Config.m_SvSpamprotection && pPlayer->m_aPlayerTick[TickState::LastEmote] && pPlayer->m_aPlayerTick[TickState::LastEmote]+(Server()->TickSpeed() / 2) > Server()->Tick())
				return;

			pPlayer->m_aPlayerTick[TickState::LastEmote] = Server()->Tick();
			SendEmoticon(ClientID, pMsg->m_Emoticon, true);
		}

		else if (MsgID == NETMSGTYPE_CL_KILL)
		{
			if(pPlayer->m_aPlayerTick[TickState::LastKill] && pPlayer->m_aPlayerTick[TickState::LastKill]+Server()->TickSpeed()*3 > Server()->Tick())
				return;

			Broadcast(ClientID, BroadcastPriority::MAIN_INFORMATION, 100, "Self kill is not allowed.");
			//pPlayer->m_PlayerTick[TickState::LastKill] = Server()->Tick();
			//pPlayer->KillCharacter(WEAPON_SELF);
		}

		else if(MsgID == NETMSGTYPE_CL_ISDDNETLEGACY)
		{
			IServer::CClientInfo Info;
			Server()->GetClientInfo(ClientID, &Info);
			if(Info.m_GotDDNetVersion)
			{
				return;
			}
			int DDNetVersion = pUnpacker->GetInt();
			if(pUnpacker->Error() || DDNetVersion < 0)
			{
				DDNetVersion = VERSION_DDRACE;
			}
			Server()->SetClientDDNetVersion(ClientID, DDNetVersion);
			//OnClientDDNetVersionKnown(ClientID);
		}
		else if(MsgID == NETMSGTYPE_CL_SHOWOTHERSLEGACY)
		{
			/*if(g_Config.m_SvShowOthers && !g_Config.m_SvShowOthersDefault)
			{
				CNetMsg_Cl_ShowOthersLegacy *pMsg = (CNetMsg_Cl_ShowOthersLegacy *)pRawMsg;
				pPlayer->m_ShowOthers = pMsg->m_Show;
			}*/
		}
		else if(MsgID == NETMSGTYPE_CL_SHOWOTHERS)
		{
			/*if(g_Config.m_SvShowOthers && !g_Config.m_SvShowOthersDefault)
			{
				CNetMsg_Cl_ShowOthers *pMsg = (CNetMsg_Cl_ShowOthers *)pRawMsg;
				pPlayer->m_ShowOthers = pMsg->m_Show;
			}*/
		}
		else if(MsgID == NETMSGTYPE_CL_SHOWDISTANCE)
		{
			//CNetMsg_Cl_ShowDistance *pMsg = (CNetMsg_Cl_ShowDistance *)pRawMsg;
			//pPlayer->m_ShowDistance = vec2(pMsg->m_X, pMsg->m_Y);
		}
		/*else if (MsgID == NETMSGTYPE_CL_SKINCHANGE)
		{
			if(pPlayer->m_aPlayerTick[TickState::LastChangeInfo] && pPlayer->m_aPlayerTick[TickState::LastChangeInfo]+Server()->TickSpeed()*5 > Server()->Tick())
				return;

			pPlayer->m_aPlayerTick[TickState::LastChangeInfo] = Server()->Tick();
			CNetMsg_Cl_SkinChange *pMsg = (CNetMsg_Cl_SkinChange *)pRawMsg;

			for(int p = 0; p < NUM_SKINPARTS; p++)
			{
				str_utf8_copy_num(pPlayer->Acc().m_aaSkinPartNames[p], pMsg->m_apSkinPartNames[p], sizeof(pPlayer->Acc().m_aaSkinPartNames[p]), MAX_SKIN_LENGTH);
				pPlayer->Acc().m_aUseCustomColors[p] = pMsg->m_aUseCustomColors[p];
				pPlayer->Acc().m_aSkinPartColors[p] = pMsg->m_aSkinPartColors[p];
			}

			// update all clients
			for(int i = 0; i < MAX_CLIENTS; ++i)
			{
				if(!m_apPlayers[i] || Server()->GetClientProtocolVersion(i) < MIN_SKINCHANGE_CLIENTVERSION)
					continue;

				SendSkinChange(pPlayer->GetCID(), i);
			}
		}
		else
			Mmo()->OnMessage(MsgID, pRawMsg, ClientID);*/
	}
	else
	{
		if (MsgID == NETMSGTYPE_CL_STARTINFO)
		{
			if(pPlayer->m_aPlayerTick[TickState::LastChangeInfo] != 0)
				return;

			pPlayer->m_aPlayerTick[TickState::LastChangeInfo] = Server()->Tick();

			// set start infos
			if(!pPlayer->IsAuthed())
			{
				CNetMsg_Cl_StartInfo* pMsg = (CNetMsg_Cl_StartInfo*)pRawMsg;
				if (!str_utf8_check(pMsg->m_pName))
				{
					Server()->Kick(ClientID, "name is not valid utf8");
					return;
				}
				if (!str_utf8_check(pMsg->m_pClan))
				{
					Server()->Kick(ClientID, "clan is not valid utf8");
					return;
				}
				if (!str_utf8_check(pMsg->m_pSkin))
				{
					Server()->Kick(ClientID, "skin is not valid utf8");
					return;
				}

				Server()->SetClientName(ClientID, pMsg->m_pName);
				Server()->SetClientClan(ClientID, pMsg->m_pClan);
				Server()->SetClientCountry(ClientID, pMsg->m_Country);

				str_copy(pPlayer->Acc().m_TeeInfos.m_aSkinName, pMsg->m_pSkin, sizeof(pPlayer->Acc().m_TeeInfos.m_aSkinName));
				pPlayer->Acc().m_TeeInfos.m_UseCustomColor = pMsg->m_UseCustomColor;
				pPlayer->Acc().m_TeeInfos.m_ColorBody = pMsg->m_ColorBody;
				pPlayer->Acc().m_TeeInfos.m_ColorFeet = pMsg->m_ColorFeet;
			}

			// send clear vote options
			CNetMsg_Sv_VoteClearOptions ClearMsg;
			Server()->SendPackMsg(&ClearMsg, MSGFLAG_VITAL, ClientID);

			// client is ready to enter
			CNetMsg_Sv_ReadyToEnter m;
			Server()->SendPackMsg(&m, MSGFLAG_VITAL | MSGFLAG_FLUSH, ClientID);
			
			Server()->ExpireServerInfo();
		}
	}
}

void CGS::OnClientConnected(int ClientID)
{
	if(!m_apPlayers[ClientID])
	{
		const int AllocMemoryCell = ClientID+m_WorldID*MAX_CLIENTS;
		m_apPlayers[ClientID] = new(AllocMemoryCell) CPlayer(this, ClientID);
	}

	SendMotd(ClientID);
	m_aBroadcastStates[ClientID] = {};
}

void CGS::OnClientEnter(int ClientID)
{
	CPlayer* pPlayer = m_apPlayers[ClientID];
	if(!pPlayer || pPlayer->IsBot())
		return;

	m_pController->OnPlayerConnect(pPlayer);

	// another
	if(!pPlayer->IsAuthed())
	{
		Chat(-1, "{STR} entered and joined the MRPG", Server()->ClientName(ClientID));
		ChatDiscord(DC_JOIN_LEAVE, Server()->ClientName(ClientID), "connected and enter in MRPG");

		Chat(ClientID, "- - - - - - - [Welcome to MRPG] - - - - - - -");
		Chat(ClientID, "You need to create an account, or log in if you already have.");
		Chat(ClientID, "Register: \"/register <login> <pass>\"");
		Chat(ClientID, "Log in: \"/login <login> <pass>\"");
		SendDayInfo(ClientID);

		ShowVotesNewbieInformation(ClientID);
		return;
	}

	Mmo()->Account()->LoadAccount(pPlayer, false);
	ResetVotes(ClientID, MenuList::MAIN_MENU);
}

void CGS::OnClientDrop(int ClientID, const char *pReason)
{
	if(!m_apPlayers[ClientID] || m_apPlayers[ClientID]->IsBot())
		return;

	// update clients on drop
	m_pController->OnPlayerDisconnect(m_apPlayers[ClientID]);

	if ((Server()->ClientIngame(ClientID) || Server()->IsClientChangesWorld(ClientID)) && IsPlayerEqualWorldID(ClientID))
	{
		char aBuf[128];
		str_format(aBuf, sizeof(aBuf), "leave player='%d:%s'", ClientID, Server()->ClientName(ClientID));
		Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "game", aBuf);

		Chat(-1, "{STR} has left the MRPG", Server()->ClientName(ClientID));
		ChatDiscord(DC_JOIN_LEAVE, Server()->ClientName(ClientID), "leave game MRPG");
		Mmo()->SaveAccount(m_apPlayers[ClientID], SaveType::SAVE_POSITION);
	}

	delete m_apPlayers[ClientID];
	m_apPlayers[ClientID] = nullptr;
}

void CGS::OnClientDirectInput(int ClientID, void *pInput)
{
	m_apPlayers[ClientID]->OnDirectInput((CNetObj_PlayerInput *)pInput);

	int Flags = ((CNetObj_PlayerInput *)pInput)->m_PlayerFlags;
	if((Flags & 256) || (Flags & 512))
	{
		Server()->Kick(ClientID, "please update your client or use DDNet client");
	}
}

void CGS::OnClientPredictedInput(int ClientID, void *pInput)
{
	m_apPlayers[ClientID]->OnPredictedInput((CNetObj_PlayerInput *)pInput);
}

// change the world
void CGS::PrepareClientChangeWorld(int ClientID)
{
	if (m_apPlayers[ClientID])
	{
		m_apPlayers[ClientID]->KillCharacter(WEAPON_WORLD);
		delete m_apPlayers[ClientID];
		m_apPlayers[ClientID] = nullptr;
	}
	const int AllocMemoryCell = ClientID+m_WorldID*MAX_CLIENTS;
	m_apPlayers[ClientID] = new(AllocMemoryCell) CPlayer(this, ClientID);
}

bool CGS::IsClientReady(int ClientID) const
{
	return m_apPlayers[ClientID] && m_apPlayers[ClientID]->m_aPlayerTick[TickState::LastChangeInfo] > 0;
}

bool CGS::IsClientPlayer(int ClientID) const
{
	return m_apPlayers[ClientID] && m_apPlayers[ClientID]->GetTeam() == TEAM_SPECTATORS ? false : true;
}

int CGS::GetClientVersion(int ClientID) const
{
	IServer::CClientInfo Info = {0};
	Server()->GetClientInfo(ClientID, &Info);
	return Info.m_DDNetVersion;
}

const char *CGS::Version() const { return GAME_VERSION; }
const char *CGS::NetVersion() const { return GAME_NETVERSION; }

// clearing all data at the exit of the client necessarily call once enough
void CGS::ClearClientData(int ClientID)
{
	Mmo()->ResetClientData(ClientID);
	m_aPlayerVotes[ClientID].clear();
	ms_aEffects[ClientID].clear();

	// clear active snap bots for player
	for(auto& pActiveSnap : DataBotInfo::ms_aDataBot)
		pActiveSnap.second.m_aVisibleActive[ClientID] = false;
}

int CGS::GetRank(int AccountID)
{
	return Mmo()->Account()->GetRank(AccountID);
}

/* #########################################################################
	CONSOLE GAMECONTEXT
######################################################################### */
void CGS::ConSetWorldTime(IConsole::IResult* pResult, void* pUserData)
{
	const int Hour = pResult->GetInteger(0);
	IServer* pServer = (IServer*)pUserData;
	pServer->SetOffsetGameTime(Hour);
}

void CGS::ConParseSkin(IConsole::IResult *pResult, void *pUserData)
{
	/*const int ClientID = clamp(pResult->GetInteger(0), 0, MAX_PLAYERS-1);
	IServer* pServer = (IServer*)pUserData;
	CGS* pSelf = (CGS*)pServer->GameServer(pServer->GetClientWorldID(ClientID));

	CPlayer *pPlayer = pSelf->GetPlayer(ClientID, true);
	if(pPlayer)
	{
		char aBuf[256];
		str_format(aBuf, sizeof(aBuf), "%s %s %s %s %s %s", pPlayer->Acc().m_aaSkinPartNames[0],
			pPlayer->Acc().m_aaSkinPartNames[1], pPlayer->Acc().m_aaSkinPartNames[2],
			pPlayer->Acc().m_aaSkinPartNames[3], pPlayer->Acc().m_aaSkinPartNames[4],
			pPlayer->Acc().m_aaSkinPartNames[5]);
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "parseskin", aBuf);

		str_format(aBuf, sizeof(aBuf), "%d %d %d %d %d %d", pPlayer->Acc().m_aSkinPartColors[0],
			pPlayer->Acc().m_aSkinPartColors[1], pPlayer->Acc().m_aSkinPartColors[2],
			pPlayer->Acc().m_aSkinPartColors[3], pPlayer->Acc().m_aSkinPartColors[4],
			pPlayer->Acc().m_aSkinPartColors[5]);
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "parseskin", aBuf);
	}*/
}

// give the item to the player
void CGS::ConGiveItem(IConsole::IResult *pResult, void *pUserData)
{
	const int ClientID = clamp(pResult->GetInteger(0), 0, MAX_PLAYERS - 1);
	const ItemIdentifier ItemID = pResult->GetInteger(1);
	const int Value = pResult->GetInteger(2);
	const int Enchant = pResult->GetInteger(3);
	const int Mail = pResult->GetInteger(4);

	IServer* pServer = (IServer*)pUserData;
	CGS* pSelf = (CGS*)pServer->GameServer(pServer->GetClientWorldID(ClientID));

	CPlayer *pPlayer = pSelf->GetPlayer(ClientID, true);
	if(pPlayer)
	{
		if (Mail == 0)
		{
			pPlayer->GetItem(ItemID)->Add(Value, 0, Enchant);
			return;
		}
		pSelf->SendInbox("Console", pPlayer, "The sender heavens", "Sent from console", ItemID, Value, Enchant);
	}
}

void CGS::ConDisbandGuild(IConsole::IResult* pResult, void* pUserData)
{
	IServer* pServer = (IServer*)pUserData;
	CGS* pSelf = (CGS*)pServer->GameServer(MAIN_WORLD_ID);
	const char* pGuildName = pResult->GetString(0);
	const int GuildID = pSelf->Mmo()->Member()->SearchGuildByName(pGuildName);

	char aBuf[256];
	if(GuildID <= 0)
	{
		str_format(aBuf, sizeof(aBuf), "\"%s\", no such guild has been found.", pGuildName);
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "disbandguild", aBuf);
		return;
	}

	str_format(aBuf, sizeof(aBuf), "Guild with identifier %d and by the name of %s has been disbanded.", GuildID, pGuildName);
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "disbandguild", aBuf);
	pSelf->Mmo()->Member()->DisbandGuild(GuildID);
}

void CGS::ConRemItem(IConsole::IResult* pResult, void* pUserData)
{
	const int ClientID = clamp(pResult->GetInteger(0), 0, MAX_PLAYERS - 1);
	const ItemIdentifier ItemID = pResult->GetInteger(1);
	const int Value = pResult->GetInteger(2);

	IServer* pServer = (IServer*)pUserData;
	CGS* pSelf = (CGS*)pServer->GameServer(pServer->GetClientWorldID(ClientID));

	CPlayer* pPlayer = pSelf->GetPlayer(ClientID, true);
	if (pPlayer)
	{
		pPlayer->GetItem(ItemID)->Remove(Value, 0);
	}
}

void CGS::ConSay(IConsole::IResult *pResult, void *pUserData)
{
	IServer* pServer = (IServer*)pUserData;
	CGS* pSelf = (CGS*)pServer->GameServer(MAIN_WORLD_ID);
	pSelf->SendChat(-1, CHAT_ALL, pResult->GetString(0));
}

// add a new bot player to the database
void CGS::ConAddCharacter(IConsole::IResult *pResult, void *pUserData)
{
	const int ClientID = clamp(pResult->GetInteger(0), 0, MAX_PLAYERS - 1);
	IServer* pServer = (IServer*)pUserData;
	CGS* pSelf = (CGS*)pServer->GameServer(pServer->GetClientWorldID(ClientID));

	// we check if there is a player
	if(ClientID < 0 || ClientID >= MAX_PLAYERS || !pSelf->m_apPlayers[ClientID])
		return;

	// add a new kind of bot
	pSelf->Mmo()->BotsData()->ConAddCharacterBot(ClientID, pResult->GetString(1));
}

// dump dialogs for translate
void CGS::ConSyncLinesForTranslate(IConsole::IResult* pResult, void* pUserData)
{
	IServer* pServer = (IServer*)pUserData;
	CGS* pSelf = (CGS*)pServer->GameServer();

	// dump
	std::thread(&MmoController::ConSyncLinesForTranslate, pSelf->m_pMmoController).detach();
}

void CGS::ConchainSpecialMotdupdate(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	pfnCallback(pResult, pCallbackUserData);
	if(pResult->NumArguments())
	{
		CGS *pSelf = (CGS *)pUserData;
		pSelf->SendMotd(-1);
	}
}

void CGS::ConchainGameinfoUpdate(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	pfnCallback(pResult, pCallbackUserData);
	if(pResult->NumArguments())
	{
		return;
	}
}

/* #########################################################################
	VOTING MMO GAMECONTEXT
######################################################################### */
void CGS::ClearVotes(int ClientID)
{
	m_aPlayerVotes[ClientID].clear();

	// send vote options
	CNetMsg_Sv_VoteClearOptions ClearMsg;
	Server()->SendPackMsg(&ClearMsg, MSGFLAG_VITAL, ClientID);
}

// add a vote
void CGS::AV(int ClientID, const char *pCmd, const char *pDesc, const int TempInt, const int TempInt2, VoteCallBack Callback)
{
	if(ClientID < 0 || ClientID >= MAX_PLAYERS || !m_apPlayers[ClientID])
		return;

	char aBufDesc[VOTE_DESC_LENGTH]; // buffer x2 with unicode
	str_copy(aBufDesc, pDesc, sizeof(aBufDesc));
	if(str_comp(m_apPlayers[ClientID]->GetLanguage(), "ru") == 0 || str_comp(m_apPlayers[ClientID]->GetLanguage(), "uk") == 0)
		str_translation_utf8_to_cp(aBufDesc);

	CVoteOptions Vote;
	str_copy(Vote.m_aDescription, aBufDesc, sizeof(Vote.m_aDescription));
	str_copy(Vote.m_aCommand, pCmd, sizeof(Vote.m_aCommand));
	Vote.m_TempID = TempInt;
	Vote.m_TempID2 = TempInt2;
	Vote.m_Callback = Callback;

	m_aPlayerVotes[ClientID].push_back(Vote);
	
	if(Vote.m_aDescription[0] == '\0')
		str_copy(Vote.m_aDescription, "························", sizeof(Vote.m_aDescription));

	// send to vanilla clients
	CNetMsg_Sv_VoteOptionAdd OptionMsg;
	OptionMsg.m_pDescription = Vote.m_aDescription;
	Server()->SendPackMsg(&OptionMsg, MSGFLAG_VITAL, ClientID);
}

// add formatted vote
void CGS::AVL(int ClientID, const char *pCmd, const char *pText, ...)
{
	if(ClientID >= 0 && ClientID < MAX_PLAYERS && m_apPlayers[ClientID])
	{
		va_list VarArgs;
		va_start(VarArgs, pText);

		dynamic_string Buffer;
		if(str_comp(pCmd, "null") != 0)
			Buffer.append("- ");

		Server()->Localization()->Format_VL(Buffer, m_apPlayers[ClientID]->GetLanguage(), pText, VarArgs);
		AV(ClientID, pCmd, Buffer.buffer());
		Buffer.clear();

		va_end(VarArgs);
	}
}

// add formatted vote with color
void CGS::AVH(int ClientID, const int HiddenID, const char *pText, ...)
{
	if(ClientID >= 0 && ClientID < MAX_PLAYERS && m_apPlayers[ClientID])
	{
		va_list VarArgs;
		va_start(VarArgs, pText);

		const bool HiddenTab = (HiddenID >= TAB_STAT) ? m_apPlayers[ClientID]->GetHiddenMenu(HiddenID) : false;
		auto Symbols = [](int ID, const char* pValue, const char* pValue2) -> const char* {	return ID >= NUM_TAB_MENU ? (pValue) : (ID < NUM_TAB_MENU_INTERACTIVES ? (pValue2) : (pValue));	};
		const char* pSymbols = Symbols(HiddenID, HiddenTab ? "► " : "▼ ", HiddenTab ?  "▼ " : "► ");
		
		dynamic_string Buffer;

		Buffer.append(pSymbols);
		Server()->Localization()->Format_VL(Buffer, m_apPlayers[ClientID]->GetLanguage(), pText, VarArgs);
		if(HiddenID > TAB_SETTINGS_MODULES && HiddenID < NUM_TAB_MENU) { Buffer.append(" (Press me for help)"); }

		AV(ClientID, "HIDDEN", Buffer.buffer(), HiddenID, -1);

		Buffer.clear();
		va_end(VarArgs);
	}
}

// add formatted vote as menu
void CGS::AVM(int ClientID, const char *pCmd, const int TempInt, const int HiddenID, const char* pText, ...)
{
	if(ClientID >= 0 && ClientID < MAX_PLAYERS && m_apPlayers[ClientID])
	{
		if((!m_apPlayers[ClientID]->GetHiddenMenu(HiddenID) && HiddenID > TAB_SETTINGS_MODULES) ||
			(m_apPlayers[ClientID]->GetHiddenMenu(HiddenID) && HiddenID <= TAB_SETTINGS_MODULES))
			return;

		va_list VarArgs;
		va_start(VarArgs, pText);

		dynamic_string Buffer;
		if(TempInt != NOPE) { Buffer.append("- "); }

		Server()->Localization()->Format_VL(Buffer, m_apPlayers[ClientID]->GetLanguage(), pText, VarArgs);
		AV(ClientID, pCmd, Buffer.buffer(), TempInt);
		Buffer.clear();
		va_end(VarArgs);
	}
}

// add formatted vote with multiple id's
void CGS::AVD(int ClientID, const char *pCmd, const int TempInt, const int TempInt2, const int HiddenID, const char *pText, ...)
{
	if(ClientID >= 0 && ClientID < MAX_PLAYERS && m_apPlayers[ClientID])
	{
		if((!m_apPlayers[ClientID]->GetHiddenMenu(HiddenID) && HiddenID > TAB_SETTINGS_MODULES) ||
			(m_apPlayers[ClientID]->GetHiddenMenu(HiddenID) && HiddenID <= TAB_SETTINGS_MODULES))
			return;

		va_list VarArgs;
		va_start(VarArgs, pText);

		dynamic_string Buffer;
		if(TempInt != NOPE) { Buffer.append("- "); }

		Server()->Localization()->Format_VL(Buffer, m_apPlayers[ClientID]->GetLanguage(), pText, VarArgs);
		AV(ClientID, pCmd, Buffer.buffer(), TempInt, TempInt2);
		Buffer.clear();
		va_end(VarArgs);
	}
}

// add formatted callback vote with multiple id's (need disallow used it for menu)
void CGS::AVCALLBACK(int ClientID, const char *pCmd, const int TempInt, const int TempInt2, const int HiddenID, VoteCallBack Callback, const char* pText, ...)
{
	if(ClientID >= 0 && ClientID < MAX_PLAYERS && m_apPlayers[ClientID])
	{
		if((!m_apPlayers[ClientID]->GetHiddenMenu(HiddenID) && HiddenID > TAB_SETTINGS_MODULES) ||
			(m_apPlayers[ClientID]->GetHiddenMenu(HiddenID) && HiddenID <= TAB_SETTINGS_MODULES))
			return;

		va_list VarArgs;
		va_start(VarArgs, pText);

		dynamic_string Buffer;
		Buffer.append("- ");

		Server()->Localization()->Format_VL(Buffer, m_apPlayers[ClientID]->GetLanguage(), pText, VarArgs);
		AV(ClientID, pCmd, Buffer.buffer(), TempInt, TempInt2, Callback);

		Buffer.clear();
		va_end(VarArgs);
	}
}

void CGS::ResetVotes(int ClientID, int MenuList)
{
	CPlayer *pPlayer = GetPlayer(ClientID, true);
	if(!pPlayer)
		return;

	if(pPlayer && pPlayer->m_ActiveMenuRegisteredCallback)
	{
		pPlayer->m_ActiveMenuRegisteredCallback = nullptr;
		pPlayer->m_ActiveMenuOptionCallback = { nullptr };
	}

	sleep_pause(3); // TODO: fix need it
	pPlayer->m_OpenVoteMenu = MenuList;
	ClearVotes(ClientID);

	if (Mmo()->OnPlayerHandleMainMenu(ClientID, MenuList, true))
	{
		AVL(ClientID, "null", "The main menu will return as soon as you leave this zone!");
		return;
	}

	if(MenuList == MenuList::MAIN_MENU)
	{
		pPlayer->m_LastVoteMenu = MenuList::MAIN_MENU;

		// statistics menu
		const int ExpForLevel = pPlayer->ExpNeed(pPlayer->Acc().m_Level);
		AVH(ClientID, TAB_STAT, "Hi, {STR} Last log in {STR}", Server()->ClientName(ClientID), pPlayer->Acc().m_aLastLogin);
		AVM(ClientID, "null", NOPE, TAB_STAT, "Discord: \"{STR}\"", g_Config.m_SvDiscordInviteLink);
		AVM(ClientID, "null", NOPE, TAB_STAT, "Level {INT} : Exp {INT}/{INT}", pPlayer->Acc().m_Level, pPlayer->Acc().m_Exp, ExpForLevel);
		AVM(ClientID, "null", NOPE, TAB_STAT, "Skill Point {INT}SP", pPlayer->GetItem(itSkillPoint)->GetValue());
		AVM(ClientID, "null", NOPE, TAB_STAT, "Gold: {VAL}", pPlayer->GetItem(itGold)->GetValue());
		AV(ClientID, "null");

		// personal menu
		AVH(ClientID, TAB_PERSONAL, "☪ SUB MENU PERSONAL");
		AVM(ClientID, "MENU", MenuList::MENU_INVENTORY, TAB_PERSONAL, "Inventory");
		AVM(ClientID, "MENU", MenuList::MENU_EQUIPMENT, TAB_PERSONAL, "Equipment");
		AVM(ClientID, "MENU", MenuList::MENU_UPGRADES, TAB_PERSONAL, "Upgrades({INT}p)", pPlayer->Acc().m_Upgrade);
		AVM(ClientID, "MENU", MenuList::MENU_DUNGEONS, TAB_PERSONAL, "Dungeons");
		AVM(ClientID, "MENU", MenuList::MENU_SETTINGS, TAB_PERSONAL, "Settings");
		AVM(ClientID, "MENU", MenuList::MENU_INBOX, TAB_PERSONAL, "Mailbox");
		AVM(ClientID, "MENU", MenuList::MENU_JOURNAL_MAIN, TAB_PERSONAL, "Journal");
		if(Mmo()->House()->PlayerHouseID(pPlayer) > 0)
			AVM(ClientID, "MENU", MenuList::MENU_HOUSE, TAB_PERSONAL, "House");
		AVM(ClientID, "MENU", MenuList::MENU_GUILD_FINDER, TAB_PERSONAL, "Guild finder");
		if(pPlayer->Acc().IsGuild())
			AVM(ClientID, "MENU", MenuList::MENU_GUILD, TAB_PERSONAL, "Guild");
		AV(ClientID, "null");

		// info menu
		AVH(ClientID, TAB_INFORMATION, "√ SUB MENU INFORMATION");
		AVM(ClientID, "MENU", MenuList::MENU_GUIDE_GRINDING, TAB_INFORMATION, "Wiki / Grinding Guide ");
		AVM(ClientID, "MENU", MenuList::MENU_TOP_LIST, TAB_INFORMATION, "Ranking guilds and players");
	}
	else if(MenuList == MenuList::MENU_JOURNAL_MAIN)
	{
		pPlayer->m_LastVoteMenu = MenuList::MAIN_MENU;

		Mmo()->Quest()->ShowQuestsMainList(pPlayer);
		AddVotesBackpage(ClientID);
	}
	else if(MenuList == MenuList::MENU_INBOX)
	{
		pPlayer->m_LastVoteMenu = MenuList::MAIN_MENU;

		AddVotesBackpage(ClientID);
		AV(ClientID, "null");
		Mmo()->Inbox()->GetInformationInbox(pPlayer);
	}
	else if(MenuList == MenuList::MENU_UPGRADES)
	{
		pPlayer->m_LastVoteMenu = MenuList::MAIN_MENU;

		AVH(ClientID, TAB_INFO_UPGR, "Upgrades Information");
		AVM(ClientID, "null", NOPE, TAB_INFO_UPGR, "Select upgrades type in Reason, write count.");
		AV(ClientID, "null");

		ShowVotesPlayerStats(pPlayer);

		auto ShowAttributeVote = [&](int HiddenID, AttributeType Type, std::function<void(int HiddenID)> pFunc)
		{
			pFunc(HiddenID);
			for (const auto& [ID, Attribute] : CAttributeDescription::Data())
			{
				if(Attribute.IsType(Type) && Attribute.HasField())
					AVD(ClientID, "UPGRADE", (int)ID, Attribute.GetUpgradePrice(), HiddenID, "{STR} {INT}P (Price {INT}P)", Attribute.GetName(), pPlayer->Acc().m_aStats[ID], Attribute.GetUpgradePrice());
			}
		};

		// Disciple of War
		ShowAttributeVote(TAB_UPGR_DPS, AttributeType::Dps, [&](int HiddenID)
		{
			const int Range = pPlayer->GetTypeAttributesSize(AttributeType::Dps);
			AVH(ClientID, HiddenID, "Disciple of War. Level Power {INT}", Range);
		});
		AV(ClientID, "null");

		// Disciple of Tank
		ShowAttributeVote(TAB_UPGR_TANK, AttributeType::Tank, [&](int HiddenID)
		{
			const int Range = pPlayer->GetTypeAttributesSize(AttributeType::Tank);
			AVH(ClientID, HiddenID, "Disciple of Tank. Level Power {INT}", Range);
		});
		AV(ClientID, "null");

		// Disciple of Healer
		ShowAttributeVote(TAB_UPGR_HEALER, AttributeType::Healer, [&](int HiddenID)
		{
			const int Range = pPlayer->GetTypeAttributesSize(AttributeType::Healer);
			AVH(ClientID, HiddenID, "Disciple of Healer. Level Power {INT}", Range);
		});
		AV(ClientID, "null");

		// Upgrades Weapons and ammo
		ShowAttributeVote(TAB_UPGR_WEAPON, AttributeType::Weapon, [&](int HiddenID)
		{
			AVH(ClientID, HiddenID, "Upgrades Weapons / Ammo");
		});

		AV(ClientID, "null");
		AVH(ClientID, TAB_UPGR_JOB, "Disciple of Jobs");
		Mmo()->PlantsAcc()->ShowMenu(pPlayer);
		Mmo()->MinerAcc()->ShowMenu(pPlayer);
		AddVotesBackpage(ClientID);
	}
	else if (MenuList == MenuList::MENU_TOP_LIST)
	{
		pPlayer->m_LastVoteMenu = MenuList::MAIN_MENU;
		AVH(ClientID, TAB_INFO_TOP, "Ranking Information");
		AVM(ClientID, "null", NOPE, TAB_INFO_TOP, "Here you can see top server Guilds, Players.");
		AV(ClientID, "null");

		AVM(ClientID, "SORTEDTOP", ToplistTypes::GUILDS_LEVELING, NOPE, "Top 10 guilds leveling");
		AVM(ClientID, "SORTEDTOP", ToplistTypes::GUILDS_WEALTHY, NOPE, "Top 10 guilds wealthy");
		AVM(ClientID, "SORTEDTOP", ToplistTypes::PLAYERS_LEVELING, NOPE, "Top 10 players leveling");
		AVM(ClientID, "SORTEDTOP", ToplistTypes::PLAYERS_WEALTHY, NOPE, "Top 10 players wealthy");
		AddVotesBackpage(ClientID);
	}
	else if(MenuList == MenuList::MENU_GUIDE_GRINDING)
	{
		pPlayer->m_LastVoteMenu = MenuList::MAIN_MENU;
		AVH(ClientID, TAB_INFO_LOOT, "Grinding Information");
		AVM(ClientID, "null", NOPE, TAB_INFO_LOOT, "You can look mobs, plants, and ores.");
		AV(ClientID, "null");

		for(int ID = MAIN_WORLD_ID; ID < Server()->GetWorldsSize(); ID++)
		{
			AVM(ClientID, "SORTEDWIKIWORLD", ID, NOPE, Server()->GetWorldName(ID));
		}
		AV(ClientID, "null");

		if(pPlayer->m_aSortTabs[SORT_GUIDE_WORLD] < 0)
		{
			AVL(ClientID, "null", "Select a zone to view information");
		}
		else
		{
			const int WorldID = pPlayer->m_aSortTabs[SORT_GUIDE_WORLD];
			const bool ActiveMob = Mmo()->BotsData()->ShowGuideDropByWorld(WorldID, pPlayer);
			const bool ActivePlant = Mmo()->PlantsAcc()->ShowGuideDropByWorld(WorldID, pPlayer);
			const bool ActiveOre = Mmo()->MinerAcc()->ShowGuideDropByWorld(WorldID, pPlayer);
			if(!ActiveMob && !ActivePlant && !ActiveOre)
				AVL(ClientID, "null", "There are no drops in the selected area.");
		}
		AddVotesBackpage(ClientID);
	}

	Mmo()->OnPlayerHandleMainMenu(ClientID, MenuList, false);
}

// information for unauthorized players
void CGS::ShowVotesNewbieInformation(int ClientID)
{
	CPlayer *pPlayer = GetPlayer(ClientID);
	if(!pPlayer)
		return;

#define TI(_text) AVL(ClientID, "null", _text)
	TI("#### Hi, new adventurer! ####");
	TI("This server is a mmo server. You'll have to finish");
	TI("quests to continue the game. In these quests,");
	TI("you'll have to get items to give to quest npcs.");
	TI("To get a quest, you need to talk to NPCs.");
	TI("You talk to them by hammering them.");
	TI("You give these items by talking them again. ");
	TI("Hearts and Shields around you show the position");
	TI("quests' npcs. Hearts show Main quest, Shields show Others.");
	TI("Don't ask other people to give you the items,");
	TI("but you can ask for some help. Keep in mind that");
	TI("it is hard for everyone too. You can see that your shield");
	TI("(below your health bar) doesn't protect you,");
	TI("it's because it's not shield, it's mana.");
	TI("It is used for active skills, which you will need to buy");
	TI("in the future. Active skills use mana, but they use %% of mana.");
	AV(ClientID, "null");
	TI("#### Some Informations ####");
	TI("Brown Slimes drop Glue and Gel (lower chance to drop Gel)");
	TI("Green Slimes drop Glue");
	TI("Blue Slimes drop Gel");
	TI("Pink Slime drop 3 Gels and 3 Glue");
	TI("Goblins drop Goblin Ingots");
	TI("Orc Warrior drop Goblin Ingot and Torn Cloth Clothes of Orc");
	TI("- A more accurate drop can be found in the menu");
	AV(ClientID, "null");
	TI("#### The upgrades now ####");
	TI("- Strength : Damage");
	TI("- Dexterity : Shooting speed");
	TI("- Crit Dmg : Damage dealt by critical hits");
	TI("- Direct Crit Dmg : Critical chance");
	TI("- Hardness : Health");
	TI("- Lucky : Chance to avoid damage");
	TI("- Piety : Mana");
	TI("- Vampirism : Damage dealt converted into health");
	TI("All things you need is in Vote.");
	TI("Good game !");
#undef TI
}

// strong update votes variability of the data
void CGS::StrongUpdateVotes(int ClientID, int MenuList)
{
	if(m_apPlayers[ClientID] && m_apPlayers[ClientID]->m_OpenVoteMenu == MenuList)
		ResetVotes(ClientID, MenuList);
}

// strong update votes variability of the data
void CGS::StrongUpdateVotesForAll(int MenuList)
{
	for(int i = 0; i < MAX_PLAYERS; i++)
	{
		if(m_apPlayers[i] && m_apPlayers[i]->m_OpenVoteMenu == MenuList)
			ResetVotes(i, MenuList);
	}
}

// the back button adds a back button to the menu (But remember to specify the last menu ID).
void CGS::AddVotesBackpage(int ClientID)
{
	if(!m_apPlayers[ClientID])
		return;

	AV(ClientID, "null");
	AVL(ClientID, "BACK", "Backpage");
}

// print player statistics
void CGS::ShowVotesPlayerStats(CPlayer *pPlayer)
{
	const int ClientID = pPlayer->GetCID();
	AVH(ClientID, TAB_INFO_STAT, "Player Stats {STR}", IsDungeon() ? "(Sync)" : "\0");
	for(const auto& [ID, Attribute] : CAttributeDescription::Data())
	{
		if(!Attribute.HasField())
			continue;

		// if upgrades are cheap, they have a division of statistics
		const int Size = pPlayer->GetAttributeSize(ID);
		if(Attribute.GetDividing() <= 1)
		{
			const int WorkedSize = pPlayer->GetAttributeSize(ID, true);
			AVM(ClientID, "null", NOPE, TAB_INFO_STAT, "{INT} (+{INT}) - {STR}", Size, WorkedSize, Attribute.GetName());
			continue;
		}
		AVM(ClientID, "null", NOPE, TAB_INFO_STAT, "+{INT} - {STR}", Size, Attribute.GetName());
	}

	AVM(ClientID, "null", NOPE, NOPE, "Player Upgrade Point: {INT}P", pPlayer->Acc().m_Upgrade);
	AV(ClientID, "null");
}

// display information by currency
void CGS::ShowVotesItemValueInformation(CPlayer *pPlayer, ItemIdentifier ItemID)
{
	const int ClientID = pPlayer->GetCID();
	AVM(ClientID, "null", NOPE, NOPE, "You have {VAL} {STR}", pPlayer->GetItem(ItemID)->GetValue(), GetItemInfo(ItemID)->GetName());
}

// vote parsing of all functions of action methods
bool CGS::ParsingVoteCommands(int ClientID, const char *CMD, const int VoteID, const int VoteID2, int Get, const char *Text, VoteCallBack Callback)
{
	CPlayer *pPlayer = GetPlayer(ClientID, false, true);
	if(!pPlayer)
	{
		Chat(ClientID, "Use it when you're not dead!");
		return true;
	}

	//pPlayer->GetItem(itGold)->GetEnchantStats()

	const sqlstr::CSqlString<64> FormatText = sqlstr::CSqlString<64>(Text);
	if(Callback)
	{
		CVoteOptionsCallback InstanceCallback;
		InstanceCallback.pPlayer = pPlayer;
		InstanceCallback.Get = Get;
		InstanceCallback.VoteID = VoteID;
		InstanceCallback.VoteID2 = VoteID2;
		str_copy(InstanceCallback.Text, FormatText.cstr(), sizeof(InstanceCallback.Text));
		str_copy(InstanceCallback.Command, CMD, sizeof(InstanceCallback.Command));

		// TODO: fixme. improve the system using the ID method, as well as the ability to implement Backpage
		if(PPSTR(CMD, "MENU") == 0)
		{
			pPlayer->m_ActiveMenuOptionCallback = InstanceCallback;
			pPlayer->m_ActiveMenuRegisteredCallback = Callback;
		}
		Callback(InstanceCallback);
		return true;
	}

	if(PPSTR(CMD, "null") == 0)
		return true;

	CreatePlayerSound(ClientID, SOUND_BODY_LAND);

	if(PPSTR(CMD, "MENU") == 0)
	{
		ResetVotes(ClientID, VoteID);
		return true;
	}
	if (PPSTR(CMD, "SORTEDTOP") == 0)
	{
		ResetVotes(ClientID, MenuList::MENU_TOP_LIST);
		AV(ClientID, "null", "\0");
		Mmo()->ShowTopList(pPlayer, VoteID);
		return true;
	}
	if (PPSTR(CMD, "SORTEDWIKIWORLD") == 0)
	{
		pPlayer->m_aSortTabs[SORT_GUIDE_WORLD] = VoteID;
		StrongUpdateVotes(ClientID, MenuList::MENU_GUIDE_GRINDING);
		return true;
	}
	if(pPlayer->ParseVoteUpgrades(CMD, VoteID, VoteID2, Get))
		return true;

	// parsing everything else
	return Mmo()->OnParsingVoteCommands(pPlayer, CMD, VoteID, VoteID2, Get, FormatText.cstr());
}

/* #########################################################################
	MMO GAMECONTEXT
######################################################################### */
int CGS::CreateBot(short BotType, int BotID, int SubID)
{
	int BotClientID = MAX_PLAYERS;
	while(m_apPlayers[BotClientID])
	{
		BotClientID++;
		if(BotClientID >= MAX_CLIENTS)
			return -1;
	}

	Server()->InitClientBot(BotClientID);
	const int AllocMemoryCell = BotClientID+m_WorldID*MAX_CLIENTS;
	m_apPlayers[BotClientID] = new(AllocMemoryCell) CPlayerBot(this, BotClientID, BotID, SubID, BotType);
	return BotClientID;
}

// create lol text in the world
void CGS::CreateText(CEntity* pParent, bool Follow, vec2 Pos, vec2 Vel, int Lifespan, const char* pText)
{
	if(!CheckingPlayersDistance(Pos, 800))
		return;

	CLoltext Text;
	Text.Create(&m_World, pParent, Pos, Vel, Lifespan, pText, true, Follow);
}

// creates a particle of experience that follows the player
void CGS::CreateParticleExperience(vec2 Pos, int ClientID, int Experience, vec2 Force)
{
	new CFlyingExperience(&m_World, Pos, ClientID, Experience, Force);
}

// gives a bonus in the position type and quantity and the number of them.
void CGS::CreateDropBonuses(vec2 Pos, int Type, int Value, int NumDrop, vec2 Force)
{
	for(int i = 0; i < NumDrop; i++)
	{
		const vec2 Vel = Force + vec2(frandom() * 15.0f, frandom() * 15.0f);
		const float Angle = Force.x * (0.15f + frandom() * 0.1f);
		new CDropBonuses(&m_World, Pos, Vel, Angle, Type, Value);
	}
}

// lands items in the position type and quantity and their number themselves
void CGS::CreateDropItem(vec2 Pos, int ClientID, CItem DropItem, vec2 Force)
{
	if(DropItem.GetID() <= 0 || DropItem.GetValue() <= 0)
		return;

	const float Angle = angle(normalize(Force));
	new CDropItem(&m_World, Pos, Force, Angle, DropItem, ClientID);
}

// random drop of the item with percentage
void CGS::CreateRandomDropItem(vec2 Pos, int ClientID, float Chance, CItem DropItem, vec2 Force)
{
	const float RandomDrop = frandom() * 100.0f;
	if(RandomDrop < Chance)
		CreateDropItem(Pos, ClientID, DropItem, Force);
}

bool CGS::TakeItemCharacter(int ClientID)
{
	CPlayer *pPlayer = GetPlayer(ClientID, true, true);
	if(!pPlayer)
		return false;

	CDropItem *pDrop = (CDropItem*)m_World.ClosestEntity(pPlayer->GetCharacter()->m_Core.m_Pos, 64, CGameWorld::ENTTYPE_DROPITEM, nullptr);
	if(pDrop) { return pDrop->TakeItem(ClientID);}
	return false;
}

// send a message with or without the object using ClientID
void CGS::SendInbox(const char* pFrom, CPlayer* pPlayer, const char* Name, const char* Desc, ItemIdentifier ItemID, int Value, int Enchant)
{
	if(!pPlayer || !pPlayer->IsAuthed())
		return;

	SendInbox(pFrom, pPlayer->Acc().m_UserID, Name, Desc, ItemID, Value, Enchant);
}

// send a message with or without the object using AccountID
void CGS::SendInbox(const char* pFrom, int AccountID, const char* Name, const char* Desc, ItemIdentifier ItemID, int Value, int Enchant)
{
	Mmo()->Inbox()->SendInbox(pFrom, AccountID, Name, Desc, ItemID, Value, Enchant);
}

// send day information
void CGS::SendDayInfo(int ClientID)
{
	if(ClientID == -1)
		Chat(-1, "{STR} came! Good {STR}!", Server()->GetStringTypeDay(), Server()->GetStringTypeDay());
	if(m_DayEnumType == DayType::NIGHT_TYPE)
		Chat(ClientID, "Nighttime experience was increase to {INT}%", m_MultiplierExp);
	else if(m_DayEnumType == DayType::MORNING_TYPE)
		Chat(ClientID, "Daytime experience was downgraded to 100%");
}

int CGS::GetExperienceMultiplier(int Experience) const
{
	if(IsDungeon())
		return translate_to_percent_rest(Experience, g_Config.m_SvMultiplierExpRaidDungeon);
	return translate_to_percent_rest(Experience, m_MultiplierExp);
}

void CGS::UpdateZonePVP()
{
	m_AllowedPVP = false;
	if(IsDungeon())
		return;

	for(int i = MAX_PLAYERS; i < MAX_CLIENTS; i++)
	{
		if(m_apPlayers[i] && m_apPlayers[i]->GetBotType() == BotsTypes::TYPE_BOT_MOB && m_apPlayers[i]->GetPlayerWorldID() == m_WorldID)
		{
			m_AllowedPVP = true;
			return;
		}
	}
}

void CGS::UpdateZoneDungeon()
{
	m_DungeonID = 0;
	for(const auto& dd : CDungeonData::ms_aDungeon)
	{
		if(m_WorldID != dd.second.m_WorldID)
			continue;

		m_DungeonID = dd.first;
		return;
	}
}

bool CGS::IsPlayerEqualWorldID(int ClientID, int WorldID) const
{
	if(ClientID < 0 || ClientID >= MAX_CLIENTS || !m_apPlayers[ClientID])
		return false;

	if (WorldID <= -1)
		return m_apPlayers[ClientID]->GetPlayerWorldID() == m_WorldID;
	return m_apPlayers[ClientID]->GetPlayerWorldID() == WorldID;
}

bool CGS::CheckingPlayersDistance(vec2 Pos, float Distance) const
{
	for(int i = 0; i < MAX_PLAYERS; i++)
	{
		if(m_apPlayers[i] && IsPlayerEqualWorldID(i) && distance(Pos, m_apPlayers[i]->m_ViewPos) <= Distance)
			return true;
	}
	return false;
}

IGameServer *CreateGameServer() { return new CGS; }
