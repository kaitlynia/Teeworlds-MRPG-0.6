/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_SERVER_GAMEMODES_MOD_H
#define GAME_SERVER_GAMEMODES_MOD_H

#include <game/server/gamecontroller.h>

class CGameControllerMain : public IGameController
{
public:

	CGameControllerMain(class CGS *pGameServer);

	void Tick() override;
	bool OnEntity(int Index, vec2 Pos) override;
	void CreateLogic(int Type, int Mode, vec2 Pos, int ParseID) override;

	void OnCharacterDeath(CPlayer* pVictim, CPlayer* pKiller, int Weapon) override;
};
#endif
