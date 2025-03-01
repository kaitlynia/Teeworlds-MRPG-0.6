/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_SERVER_COMPONENT_ACCOUNT_MINER_CORE_H
#define GAME_SERVER_COMPONENT_ACCOUNT_MINER_CORE_H
#include <game/server/core/mmo_component.h>

class CAccountMinerManager : public MmoComponent
{
	~CAccountMinerManager() override
	{
		ms_aOre.clear();
	};

	struct StructOres
	{
		int m_ItemID;
		int m_Level;
		int m_StartHealth;
		vec2 m_Position;
		int m_Distance;
		int m_WorldID;
	};
	static std::map < int, StructOres > ms_aOre;

	void OnInitAccount(CPlayer* pPlayer) override;
	void OnInitWorld(const char* pWhereLocalWorld) override;
	bool OnHandleVoteCommands(CPlayer* pPlayer, const char* CMD, int VoteID, int VoteID2, int Get, const char* GetText) override;

public:
	int GetOreLevel(vec2 Pos) const;
	int GetOreItemID(vec2 Pos) const;
	int GetOreHealth(vec2 Pos) const;

	//void ShowMenu(CPlayer *pPlayer) const;
	void Work(CPlayer *pPlayer, int Exp);

	bool InsertItemsDetailVotes(CPlayer* pPlayer, int WorldID) const;
};

#endif