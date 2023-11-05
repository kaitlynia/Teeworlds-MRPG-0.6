/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "QuestManager.h"

#include <game/server/gamecontext.h>

constexpr auto TW_QUESTS_DAILY_BOARD = "tw_quests_daily_board";

// This function is called during the initialization of the Quest Manager object.
void CQuestManager::OnInit()
{
	// Execute a SELECT query to retrieve all columns from the "tw_quests_list" table
	ResultPtr pRes = Database->Execute<DB::SELECT>("*", "tw_quests_list");
	while(pRes->next())
	{
		// Retrieve the values for each column from the current row of the result set
		QuestIdentifier ID = pRes->getInt("ID");
		std::string Name = pRes->getString("Name").c_str();
		std::string Story = pRes->getString("StoryLine").c_str();
		std::string Types = pRes->getString("Types").c_str();
		int Gold = pRes->getInt("Money");
		int Exp = pRes->getInt("Exp");

		// Initialize a CQuestDescription object with the retrieved values
		CQuestDescription(ID).Init(Name, Story, Types, Gold, Exp);
	}

	// Execute a SELECT query on the database and store the result pointer in pResDaily
	ResultPtr pResDaily = Database->Execute<DB::SELECT>("*", TW_QUESTS_DAILY_BOARD);
	while(pResDaily->next())
	{
		// Retrieve the values for each column from the current row of the result set
		QuestDailyBoardIdentifier ID = pResDaily->getInt("ID");
		std::string Name = pResDaily->getString("Name").c_str();
		vec2 Pos = vec2((float)pResDaily->getInt("PosX"), (float)pResDaily->getInt("PosY"));
		int WorldID = pResDaily->getInt("WorldID");

		// Initialize the daily quest with the server parameters
		CQuestsDailyBoard(ID).Init(Name, Pos, WorldID);
	}
}

// This method is called when a player's account is initialized.
void CQuestManager::OnInitAccount(CPlayer* pPlayer)
{
	// Get the client ID of the player
	const int ClientID = pPlayer->GetCID();

	// Execute a select query to fetch all rows from the "tw_accounts_quests" table where UserID is equal to the ID of the player's account
	ResultPtr pRes = Database->Execute<DB::SELECT>("*", "tw_accounts_quests", "WHERE UserID = '%d'", pPlayer->Acc().m_ID);
	while(pRes->next())
	{
		// Get the QuestID and Type values from the current row
		QuestIdentifier ID = pRes->getInt("QuestID");
		QuestState State = (QuestState)pRes->getInt("Type");

		// Initialize a new instance of CPlayerQuest with the QuestID and ClientID, and set its state to the retrieved Type value
		CPlayerQuest(ID, ClientID).Init(State);
	}
}

void CQuestManager::OnResetClient(int ClientID)
{
	CPlayerQuest::Data().erase(ClientID);
}

bool CQuestManager::OnHandleTile(CCharacter* pChr, int IndexCollision)
{
	CPlayer* pPlayer = pChr->GetPlayer();
	const int ClientID = pPlayer->GetCID();

	// shop zone
	if(pChr->GetHelper()->TileEnter(IndexCollision, TILE_QUEST_DAILY_BOARD))
	{
		_DEF_TILE_ENTER_ZONE_SEND_MSG_INFO(pPlayer);
		GS()->UpdateVotes(ClientID, pPlayer->m_CurrentVoteMenu);
		return true;
	}
	else if(pChr->GetHelper()->TileExit(IndexCollision, TILE_QUEST_DAILY_BOARD))
	{
		_DEF_TILE_EXIT_ZONE_SEND_MSG_INFO(pPlayer);
		GS()->UpdateVotes(ClientID, pPlayer->m_CurrentVoteMenu);
		return true;
	}

	return false;
}

bool CQuestManager::OnHandleMenulist(CPlayer* pPlayer, int Menulist, bool ReplaceMenu)
{
	const int ClientID = pPlayer->GetCID();
	CCharacter* pChr = pPlayer->GetCharacter();
	if(ReplaceMenu && pChr && pChr->IsAlive())
	{
		if(pChr->GetHelper()->BoolIndex(TILE_QUEST_DAILY_BOARD))
		{
			if(CQuestsDailyBoard* pDailyBoard = GetDailyBoard(pChr->m_Core.m_Pos))
			{
				ShowDailyQuests(pChr->GetPlayer(), pDailyBoard);
			}
			else
			{
				GS()->AV(ClientID, "null", "Daily board don't work");
			}

			return true;
		}

		return false;
	}

	if(Menulist == MENU_JOURNAL_MAIN)
	{
		pPlayer->m_LastVoteMenu = MenuList::MENU_MAIN;

		ShowQuestsMainList(pPlayer);

		GS()->AddVotesBackpage(ClientID);
		return true;
	}

	if(Menulist == MENU_JOURNAL_FINISHED)
	{
		pPlayer->m_LastVoteMenu = MENU_JOURNAL_MAIN;

		ShowQuestsTabList(pPlayer, QuestState::FINISHED);

		GS()->AddVotesBackpage(ClientID);
		return true;
	}

	if(Menulist == MENU_JOURNAL_QUEST_INFORMATION)
	{
		pPlayer->m_LastVoteMenu = MENU_JOURNAL_MAIN;

		const int QuestID = pPlayer->m_TempMenuValue;
		CQuestDescription* pQuestInfo = pPlayer->GetQuest(QuestID)->Info();

		pPlayer->GS()->Mmo()->Quest()->ShowQuestActivesNPC(pPlayer, QuestID);
		pPlayer->GS()->AV(ClientID, "null");
		pPlayer->GS()->AVL(ClientID, "null", "{STR} : Reward", pQuestInfo->GetName());
		pPlayer->GS()->AVL(ClientID, "null", "Gold: {VAL} Exp: {INT}", pQuestInfo->GetRewardGold(), pQuestInfo->GetRewardExp());

		pPlayer->GS()->AddVotesBackpage(ClientID);
	}

	return false;
}

bool CQuestManager::OnHandleVoteCommands(CPlayer* pPlayer, const char* CMD, const int VoteID, const int VoteID2, int Get, const char* GetText)
{
	return false;
}

void CQuestManager::OnPlayerHandleTimePeriod(CPlayer* pPlayer, TIME_PERIOD Period)
{
	if(Period == TIME_PERIOD::DAILY_STAMP)
	{
		dbg_msg("test", "update daily quest");
	}
}

static const char* GetStateName(QuestState State)
{
	switch(State)
	{
		case QuestState::ACCEPT: return "Active";
		case QuestState::FINISHED: return "Finished";
		default: return "Not active";
	}
}

void CQuestManager::ShowQuestsMainList(CPlayer* pPlayer)
{
	int ClientID = pPlayer->GetCID();

	// information
	const int TotalQuests = (int)CQuestDescription::Data().size();
	const int TotalComplectedQuests = GetClientComplectedQuestsSize(ClientID);
	const int TotalIncomplectedQuests = TotalQuests - TotalComplectedQuests;
	GS()->AVH(ClientID, TAB_INFO_STATISTIC_QUESTS, "Quests statistic");
	GS()->AVM(ClientID, "null", NOPE, TAB_INFO_STATISTIC_QUESTS, "Total quests: {INT}", TotalQuests);
	GS()->AVM(ClientID, "null", NOPE, TAB_INFO_STATISTIC_QUESTS, "Total complected quests: {INT}", TotalComplectedQuests);
	GS()->AVM(ClientID, "null", NOPE, TAB_INFO_STATISTIC_QUESTS, "Total incomplete quests: {INT}", TotalIncomplectedQuests);
	GS()->AV(ClientID, "null");

	// tabs with quests
	ShowQuestsTabList(pPlayer, QuestState::ACCEPT);
	ShowQuestsTabList(pPlayer, QuestState::NO_ACCEPT);

	// show the completed menu
	GS()->AVM(ClientID, "MENU", MENU_JOURNAL_FINISHED, NOPE, "List of completed quests");
}

void CQuestManager::ShowQuestsTabList(CPlayer* pPlayer, QuestState State)
{
	const int ClientID = pPlayer->GetCID();
	GS()->AVL(ClientID, "null", "{STR} quests", GetStateName(State));

	// check first quest story step
	bool IsEmptyList = true;
	std::list < std::string /*stories was checked*/ > StoriesChecked;
	for(const auto& [ID, pQuestInfo] : CQuestDescription::Data())
	{
		if(pPlayer->GetQuest(ID)->GetState() != State)
			continue;

		if(State == QuestState::FINISHED)
		{
			ShowQuestID(pPlayer, ID);
			IsEmptyList = false;
			continue;
		}

		const auto& IsAlreadyChecked = std::find_if(StoriesChecked.begin(), StoriesChecked.end(), [pStory = pQuestInfo.GetStory()](const std::string& stories)
		{
			return (str_comp_nocase(pStory, stories.c_str()) == 0);
		});
		if(IsAlreadyChecked == StoriesChecked.end())
		{
			StoriesChecked.emplace_back(CQuestDescription::Data()[ID].GetStory());
			ShowQuestID(pPlayer, ID);
			IsEmptyList = false;
		}
	}

	// if the quest list is empty
	if(IsEmptyList)
	{
		GS()->AV(ClientID, "null", "List of quests is empty");
	}
	GS()->AV(ClientID, "null");
}

void CQuestManager::ShowQuestID(CPlayer* pPlayer, int QuestID) const
{
	CQuestDescription* pQuestInfo = pPlayer->GetQuest(QuestID)->Info();
	const int QuestsSize = pQuestInfo->GetQuestStorySize();
	const int QuestPosition = pQuestInfo->GetQuestStoryPosition();

	GS()->AVD(pPlayer->GetCID(), "MENU", MENU_JOURNAL_QUEST_INFORMATION, QuestID, NOPE, "{INT}/{INT} {STR}: {STR}",
		QuestPosition, QuestsSize, pQuestInfo->GetStory(), pQuestInfo->GetName());
}

// active npc information display
void CQuestManager::ShowQuestActivesNPC(CPlayer* pPlayer, int QuestID) const
{
	CPlayerQuest* pPlayerQuest = pPlayer->GetQuest(QuestID);
	const int ClientID = pPlayer->GetCID();
	GS()->AVM(ClientID, "null", NOPE, NOPE, "Active NPC for current quests");

	for(auto& pStepBot : CQuestDescription::Data()[QuestID].m_StepsQuestBot)
	{
		const QuestBotInfo& BotInfo = pStepBot.second.m_Bot;
		if(!BotInfo.m_HasAction)
			continue;

		const int HideID = (NUM_TAB_MENU + BotInfo.m_SubBotID);
		const vec2 Pos = BotInfo.m_Position / 32.0f;
		CPlayerQuestStep& rQuestStepDataInfo = pPlayerQuest->m_aPlayerSteps[pStepBot.first];
		const char* pSymbol = (((pPlayerQuest->GetState() == QuestState::ACCEPT && rQuestStepDataInfo.m_StepComplete) || pPlayerQuest->GetState() == QuestState::FINISHED) ? "✔ " : "\0");

		GS()->AVH(ClientID, HideID, "{STR}Step {INT}. {STR} {STR}(x{INT} y{INT})", pSymbol, BotInfo.m_Step, BotInfo.GetName(), Server()->GetWorldName(BotInfo.m_WorldID), (int)Pos.x, (int)Pos.y);

		// skipped non accepted task list
		if(pPlayerQuest->GetState() != QuestState::ACCEPT)
		{
			GS()->AVM(ClientID, "null", NOPE, HideID, "Quest been completed, or not accepted!");
			continue;
		}

		// show required defeat
		bool NoTasks = true;
		if(!BotInfo.m_RequiredDefeat.empty())
		{
			for(auto& p : BotInfo.m_RequiredDefeat)
			{
				if(DataBotInfo::ms_aDataBot.find(p.m_BotID) != DataBotInfo::ms_aDataBot.end())
				{
					GS()->AVM(ClientID, "null", NOPE, HideID, "- Defeat {STR} [{INT}/{INT}]",
						DataBotInfo::ms_aDataBot[p.m_BotID].m_aNameBot, rQuestStepDataInfo.m_aMobProgress[p.m_BotID].m_Count, p.m_Value);
					NoTasks = false;
				}
			}
		}

		// show required item's
		if(!BotInfo.m_RequiredItems.empty())
		{
			for(auto& pRequired : BotInfo.m_RequiredItems)
			{
				CPlayerItem* pPlayerItem = pPlayer->GetItem(pRequired.m_Item);
				int ClapmItem = clamp(pPlayerItem->GetValue(), 0, pRequired.m_Item.GetValue());

				GS()->AVM(ClientID, "null", NOPE, HideID, "- Item {STR} [{VAL}/{VAL}]", pPlayerItem->Info()->GetName(), ClapmItem, pRequired.m_Item.GetValue());
				NoTasks = false;
			}
		}

		// show reward item's
		if(!BotInfo.m_RewardItems.empty())
		{
			for(auto& pRewardItem : BotInfo.m_RewardItems)
			{
				GS()->AVM(ClientID, "null", NOPE, HideID, "- Receive {STR}x{VAL}", pRewardItem.Info()->GetName(), pRewardItem.GetValue());
			}
		}

		// show move to
		if(!BotInfo.m_RequiredMoveTo.empty())
		{
			GS()->AVM(ClientID, "null", NOPE, HideID, "- Some action is required");
		}

		if(NoTasks)
		{
			GS()->AVM(ClientID, "null", NOPE, HideID, "You just need to talk.");
		}
	}
}

void CQuestManager::QuestShowRequired(CPlayer* pPlayer, QuestBotInfo& pBot, char* aBufQuestTask, int Size)
{
	const int QuestID = pBot.m_QuestID;
	CPlayerQuest* pQuest = pPlayer->GetQuest(QuestID);
	CPlayerQuestStep* pStep = pQuest->GetStepByMob(pBot.m_SubBotID);
	pStep->FormatStringTasks(aBufQuestTask, Size);
}

void CQuestManager::AppendDefeatProgress(CPlayer* pPlayer, int DefeatedBotID)
{
	// TODO Optimize algoritm check complected steps
	const int ClientID = pPlayer->GetCID();
	for(auto& pQuest : CPlayerQuest::Data()[ClientID])
	{
		// only for accepted quests
		if(pQuest.second.GetState() != QuestState::ACCEPT)
			continue;

		// check current steps and append
		for(auto& pStepBot : pQuest.second.m_aPlayerSteps)
		{
			if(pQuest.second.GetCurrentStepPos() == pStepBot.second.m_Bot.m_Step)
				pStepBot.second.AppendDefeatProgress(DefeatedBotID);
		}
	}
}

void CQuestManager::ShowDailyQuests(CPlayer* pPlayer, const CQuestsDailyBoard* pBoard) const
{
	const int ClientID = pPlayer->GetCID();
	GS()->AVH(ClientID, TAB_STORAGE, "Board :: {STR}", pBoard->GetName());

	int HideID = NUM_TAB_MENU + CQuestsDailyBoard::Data().size() + 3200;
	for(auto& pDailyQuestInfo : pBoard->m_DailyQuestsInfoList)
	{
		CPlayerQuest* pQuest = pPlayer->GetQuest(pDailyQuestInfo.GetID());
		if(pQuest->IsCompleted())
			continue;

		// show trade slot actions
		GS()->AVM(ClientID, "null", NOPE, HideID, "({STR}){STR}", (pQuest->IsActive() ? "v" : "x"), pDailyQuestInfo.GetName());
		if(pQuest->IsActive())
			GS()->AVM(ClientID, "ACCEPT_DAILY_QUEST", pDailyQuestInfo.GetID(), HideID, "Accept {STR}", pDailyQuestInfo.GetName());
		else
			GS()->AVM(ClientID, "REFUSE_DAILY_QUEST", pDailyQuestInfo.GetID(), HideID, "Refuse {STR}", pDailyQuestInfo.GetName());

		GS()->AVM(ClientID, "null", NOPE, HideID, "\0");
		HideID++;
	}

	GS()->AV(ClientID, "null");
}

// The function takes a parameter "Pos" of type "vec2" and returns a pointer to "CQuestsDailyBoard" object
CQuestsDailyBoard* CQuestManager::GetDailyBoard(vec2 Pos) const
{
	// Iterate through the map "CQuestsDailyBoard::Data()"
	for(auto it = CQuestsDailyBoard::Data().begin(); it != CQuestsDailyBoard::Data().end(); ++it)
	{
		// Check if the distance between the position of the current "CQuestsDailyBoard" object and the given "Pos" is less than 200
		if(distance(it->second.GetPos(), Pos) < 200)
			return &(it->second);
	}

	return nullptr;
}

void CQuestManager::UpdateSteps(CPlayer* pPlayer)
{
	// TODO Optimize algoritm check complected steps
	const int ClientID = pPlayer->GetCID();
	for(auto& pQuest : CPlayerQuest::Data()[ClientID])
	{
		if(pQuest.second.GetState() != QuestState::ACCEPT)
			continue;

		for(auto& pStepBot : pQuest.second.m_aPlayerSteps)
		{
			if(pQuest.second.GetCurrentStepPos() == pStepBot.second.m_Bot.m_Step)
			{
				pStepBot.second.UpdatePathNavigator();
				pStepBot.second.UpdateTaskMoveTo();
			}
		}
	}
}

void CQuestManager::AcceptNextStoryQuest(CPlayer* pPlayer, int CheckQuestID)
{
	const CQuestDescription CheckingQuest = CQuestDescription::Data()[CheckQuestID];
	for(auto pQuestData = CQuestDescription::Data().find(CheckQuestID); pQuestData != CQuestDescription::Data().end(); ++pQuestData)
	{
		// search next quest story step
		if(str_comp_nocase(CheckingQuest.GetStory(), pQuestData->second.GetStory()) == 0)
		{
			// skip all if a quest story is found that is still active
			if(pPlayer->GetQuest(pQuestData->first)->GetState() == QuestState::ACCEPT)
				break;

			// accept next quest step
			if(pPlayer->GetQuest(pQuestData->first)->Accept())
				break;
		}
	}
}

void CQuestManager::AcceptNextStoryQuestStep(CPlayer* pPlayer)
{
	// check first quest story step search active quests
	std::list < std::string /*stories was checked*/ > StoriesChecked;
	for(const auto& pPlayerQuest : CPlayerQuest::Data()[pPlayer->GetCID()])
	{
		// allow accept next story quest only for complected some quest on story
		if(pPlayerQuest.second.GetState() != QuestState::FINISHED)
			continue;

		// accept next story quest
		const auto& IsAlreadyChecked = std::find_if(StoriesChecked.begin(), StoriesChecked.end(), [=](const std::string& stories)
		{ return (str_comp_nocase(CQuestDescription::Data()[pPlayerQuest.first].GetStory(), stories.c_str()) == 0); });
		if(IsAlreadyChecked == StoriesChecked.end())
		{
			StoriesChecked.emplace_front(CQuestDescription::Data()[pPlayerQuest.first].GetStory());
			AcceptNextStoryQuest(pPlayer, pPlayerQuest.first);
		}
	}
}

int CQuestManager::GetUnfrozenItemValue(CPlayer* pPlayer, int ItemID) const
{
	const int ClientID = pPlayer->GetCID();
	int AvailableValue = pPlayer->GetItem(ItemID)->GetValue();
	for(const auto& pQuest : CPlayerQuest::Data()[ClientID])
	{
		if(pQuest.second.GetState() != QuestState::ACCEPT)
			continue;

		for(auto& pStepBot : pQuest.second.m_aPlayerSteps)
		{
			if(!pStepBot.second.m_StepComplete)
				AvailableValue -= pStepBot.second.GetNumberBlockedItem(ItemID);
		}
	}
	return max(AvailableValue, 0);
}

int CQuestManager::GetClientComplectedQuestsSize(int ClientID) const
{
	int Total = 0;
	for(const auto& [QuestID, Data] : CPlayerQuest::Data()[ClientID])
	{
		if(Data.IsCompleted())
			Total++;
	}

	return Total;
}