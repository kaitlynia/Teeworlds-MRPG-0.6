/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_SERVER_INVENTORY_RANDOM_BOX_H
#define GAME_SERVER_INVENTORY_RANDOM_BOX_H
#include <game/server/entity.h>

#include "ItemData.h"

class CPlayer;

struct StRandomItem
{
	ItemIdentifier m_ItemID{};
	int m_Value{};
	float m_Chance{};
};

class CRandomBox
{
	std::vector <StRandomItem> m_ArrayItems{};

public:
	CRandomBox(CRandomBox&) = delete;
	CRandomBox(const CRandomBox&) = delete;
	CRandomBox(const std::initializer_list<StRandomItem>& pList)
	{
		for(auto& p : pList)
			m_ArrayItems.push_back(p);
	}

	bool Start(CPlayer* pPlayer, int Seconds, CPlayerItem* pPlayerUsesItem = nullptr, int UseValue = 1);
};

class CRandomBoxRandomizer : public CEntity
{
	int m_UseValue;
	int m_LifeTime;
	int m_PlayerAccountID;
	CPlayer* m_pPlayer;
	CPlayerItem* m_pPlayerUsesItem;
	std::vector<StRandomItem> m_List;

public:
	CRandomBoxRandomizer(CGameWorld* pGameWorld, CPlayer* pPlayer, int PlayerAccountID, int LifeTime, std::vector<StRandomItem> List, CPlayerItem* pPlayerUsesItem, int UseValue);

	std::vector<StRandomItem>::iterator SelectRandomItem();
	void Tick() override;
	void Snap(int SnappingClient) override;
};

#endif