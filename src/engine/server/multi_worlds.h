#ifndef ENGINE_SERVER_MULTIWORLDS_CONTEXT_H
#define ENGINE_SERVER_MULTIWORLDS_CONTEXT_H
#include <base/hash.h>

struct CMapDetail
{
	void Clear()
	{
		m_aSize = 0;
		free(m_apData);
		m_aCrc = 0;
		m_aSha256 = {};
	}

	SHA256_DIGEST m_aSha256{};
	unsigned m_aCrc{};
	unsigned char* m_apData{};
	unsigned int m_aSize{};
};

class CMultiWorlds
{
	bool Add(int WorldID, class IKernel* pKernel);
	void Clear(bool Shutdown = true);

public:
	struct CWorldGameServer
	{
		char m_aName[64]{};
		char m_aPath[512]{};
		class IGameServer* m_pGameServer{};
		class IEngineMap* m_pLoadedMap{};
		CMapDetail m_MapDetail{};
	};

	CMultiWorlds()
	{
		m_WasInitilized = 0;
		m_NextIsReloading = false;
	}
	~CMultiWorlds()
	{
		Clear(true);
	}
	
	CWorldGameServer* GetWorld(int WorldID) { return &m_Worlds[WorldID]; }
	bool IsValid(int WorldID) const { return WorldID >= 0 && WorldID < ENGINE_MAX_WORLDS && m_Worlds[WorldID].m_pGameServer; }
	int GetSizeInitilized() const { return m_WasInitilized; }
	bool LoadWorlds(class IKernel* pKernel, class IStorageEngine* pStorage, class IConsole* pConsole);

private:
	int m_WasInitilized{};
	bool m_NextIsReloading{};
	CWorldGameServer m_Worlds[ENGINE_MAX_WORLDS]{};
};


#endif