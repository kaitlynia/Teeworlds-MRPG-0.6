/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_SERVER_COMPONENT_ITEM_DATA_H
#define GAME_SERVER_COMPONENT_ITEM_DATA_H

#include "ItemInfoData.h"

class CItem;
using CItemsContainer = std::deque<CItem>;

class CItem
{
protected:
	ItemIdentifier m_ID{};
	int m_Value{};
	int m_Enchant{};
	int m_Durability{};
	int m_Settings{};

public:
	CItem() = default;
	CItem(ItemIdentifier ID, int Value = 0, int Enchant = 0, int Durability = 0, int Settings = 0) : m_ID(ID), m_Value(Value), m_Enchant(Enchant), m_Durability(Durability), m_Settings(Settings) {}

	ItemIdentifier GetID() const { return m_ID; }
	virtual void SetID(ItemIdentifier ID) { m_ID = ID; }

	// getters
	int GetValue() const { return m_Value; }
	int GetEnchant() const{return m_Enchant;}
	int GetDurability() const{return m_Durability;}
	int GetSettings() const{ return m_Settings; }

	// virtual functions
	virtual bool SetValue(int Value) { m_Value = Value; return true; }
	virtual bool SetEnchant(int Enchant) { m_Enchant = Enchant; return true; }
	virtual bool SetDurability(int Durability){ m_Durability = Durability; return true; }
	virtual bool SetSettings(int Settings) { m_Settings = Settings; return true; }

	// valid
	bool IsValid() const { return CItemDescription::Data().find(m_ID) != CItemDescription::Data().end() && m_Value > 0; }

	CItemDescription* Info() const { return &CItemDescription::Data()[m_ID]; }

	std::string StringEnchantLevel() const { return Info()->StringEnchantLevel(m_Enchant); }

	// helper functions
	[[nodiscard]] static CItem FromJSON(const nlohmann::json& json);
	[[nodiscard]] static CItemsContainer FromArrayJSON(const nlohmann::json& json, const char* pField);
};

class CPlayerItem : public CItem, public MultiworldIdentifiableStaticData< std::map < int, std::map < int, CPlayerItem > > >
{
	friend class CInventoryManager;
	int m_ClientID {};

	class CGS* GS() const;
	class CPlayer* GetPlayer() const;

public:
	CPlayerItem() = default;
	CPlayerItem(ItemIdentifier ID, int ClientID) : m_ClientID(ClientID) { m_ID = ID; }
	CPlayerItem(ItemIdentifier ID, int ClientID, int Value, int Enchant, int Durability, int Settings) : CItem(ID, Value, Enchant, Durability, Settings), m_ClientID(ClientID) { }
	
	void Init(int Value, int Enchant, int Durability, int Settings)
	{
		m_Value = Value;
		m_Enchant = Enchant;
		m_Durability = Durability;
		m_Settings = Settings;
		CPlayerItem::m_pData[m_ClientID][m_ID] = *this;
	}
	
	// getters
	int GetEnchantStats(AttributeIdentifier ID) const { return Info()->GetInfoEnchantStats(ID, m_Enchant); }
	int GetEnchantPrice() const { return Info()->GetEnchantPrice(m_Enchant); }
	int GetDysenthis() const { return Info()->GetDysenthis(m_Enchant); }
	bool IsEquipped() const
	{
		return m_Value > 0 && m_Settings > 0 && (Info()->IsType(ItemType::TYPE_POTION) || Info()->IsType(ItemType::TYPE_SETTINGS) 
			|| Info()->IsType(ItemType::TYPE_MODULE) || Info()->IsType(ItemType::TYPE_EQUIP));
	}
	bool IsEnchantMaxLevel() const { return Info()->IsEnchantMaxLevel(m_Enchant); }
	bool HasItem() const { return m_Value > 0; }

	// main functions
	bool Add(int Value, int StartSettings = 0, int StartEnchant = 0, bool Message = true);
	bool Remove(int Value);
	bool Equip(bool SaveItem = true);
	bool Use(int Value);
	bool Drop(int Value);
	void StrFormatAttributes(CPlayer* pPlayer, char* pBuffer, int Size) const { Info()->StrFormatAttributes(pPlayer, pBuffer, Size, m_Enchant); }
	bool Save();

	// override functions
	bool SetValue(int Value) override;
	bool SetEnchant(int Enchant) override;
	bool SetDurability(int Durability) override;
	bool SetSettings(int Settings) override;
};

#endif