/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_SERVER_COMPONENT_DUNGEON_CORE_H
#define GAME_SERVER_COMPONENT_DUNGEON_CORE_H
#include <game/server/core/mmo_component.h>

#include "DungeonData.h"

class CDungeonManager : public MmoComponent
{
	~CDungeonManager() override
	{
		CDungeonData::ms_aDungeon.clear();
	};

	void OnInit() override;
	bool OnHandleVoteCommands(CPlayer* pPlayer, const char* CMD, int VoteID, int VoteID2, int Get, const char* GetText) override;
	bool OnHandleMenulist(CPlayer* pPlayer, int Menulist) override;

public:
	static bool IsDungeonWorld(int WorldID);
	static void SaveDungeonRecord(CPlayer* pPlayer, int DungeonID, CPlayerDungeonRecord *pPlayerDungeonRecord);
	void InsertVotesDungeonTop(int DungeonID, class CVoteWrapper* pWrapper) const;
	bool ShowDungeonsList(CPlayer* pPlayer, bool Story) const;
	void NotifyUnlockedDungeonsByQuest(CPlayer* pPlayer, int QuestID) const;
	void ShowInsideDungeonMenu(CPlayer* pPlayer) const;
};
#endif