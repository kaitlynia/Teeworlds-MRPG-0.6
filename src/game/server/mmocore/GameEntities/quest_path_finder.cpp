/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include <game/server/mmocore/Components/Bots/BotData.h>
#include "quest_path_finder.h"

#include <game/server/mmocore/Components/Worlds/WorldSwapCore.h>
#include <game/server/gamecontext.h>

CQuestPathFinder::CQuestPathFinder(CGameWorld* pGameWorld, vec2 Pos, int ClientID, QuestBotInfo QuestBot)
: CEntity(pGameWorld, CGameWorld::ENTTYPE_FINDQUEST, Pos)
{
	m_ClientID = ClientID;
	m_SubBotID = QuestBot.m_SubBotID;
	m_TargetPos = GS()->Mmo()->WorldSwap()->GetPositionQuestBot(ClientID, QuestBot);
	m_MainScenario = str_startswith(GS()->GetQuestInfo(QuestBot.m_QuestID).GetStory(), "Main") != nullptr;

	GameWorld()->InsertEntity(this);
}

void CQuestPathFinder::Tick()
{
	CPlayer* pPlayer = GS()->GetPlayer(m_ClientID, true, true);
	if(!pPlayer || !length(m_TargetPos))
	{
		GS()->m_World.DestroyEntity(this);
		return;
	}

	const int QuestID = QuestBotInfo::ms_aQuestBot[m_SubBotID].m_QuestID;
	const int Step = QuestBotInfo::ms_aQuestBot[m_SubBotID].m_Step;
	if (pPlayer->GetQuest(QuestID).m_Step != Step || pPlayer->GetQuest(QuestID).GetState() != QuestState::QUEST_ACCEPT || pPlayer->GetQuest(QuestID).m_StepsQuestBot[m_SubBotID].m_StepComplete)
	{
		GS()->CreateDeath(m_Pos, m_ClientID);
		GS()->m_World.DestroyEntity(this);
		return;
	}

	const vec2 PlayerPosition = GS()->m_apPlayers[m_ClientID]->GetCharacter()->m_Core.m_Pos;
	const vec2 Direction = normalize(PlayerPosition - m_TargetPos);
	m_Pos = PlayerPosition - Direction * clamp(distance(m_Pos, m_TargetPos), 32.0f, 90.0f);
}

void CQuestPathFinder::Snap(int SnappingClient)
{
	if(NetworkClipped(SnappingClient) || SnappingClient != m_ClientID)
		return;

	CNetObj_Pickup *pP = static_cast<CNetObj_Pickup *>(Server()->SnapNewItem(NETOBJTYPE_PICKUP, GetID(), sizeof(CNetObj_Pickup)));
	if(!pP)
		return;

	pP->m_X = (int)m_Pos.x;
	pP->m_Y = (int)m_Pos.y;
	pP->m_Type = (m_MainScenario ? (int)POWERUP_HEALTH : (int)POWERUP_ARMOR);
	pP->m_Subtype = 0;
}