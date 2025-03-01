/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_SERVER_COMPONENT_MAIL_CORE_H
#define GAME_SERVER_COMPONENT_MAIL_CORE_H
#include <game/server/core/mmo_component.h>

class CMailboxManager : public MmoComponent
{
	bool OnHandleVoteCommands(CPlayer* pPlayer, const char* CMD, int VoteID, int VoteID2, int Get, const char* GetText) override;
	bool OnHandleMenulist(CPlayer* pPlayer, int Menulist) override;

public:
	int GetLettersCount(int AccountID);
	void ShowMailboxMenu(CPlayer *pPlayer);
	void SendInbox(const char* pFrom, int AccountID, const char* pName, const char* pDesc, int ItemID = -1, int Value = -1, int Enchant = -1);
	bool SendInbox(const char* pFrom, const char* pNickname, const char* pName, const char* pDesc, int ItemID = -1, int Value = -1, int Enchant = -1);

private:
	void DeleteLetter(int LetterID);
	void AcceptLetter(CPlayer* pPlayer, int LetterID);
};

#endif