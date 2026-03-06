#pragma once
#include "UIScene.h"
#include "UIControl_PlayerSkinPreview.h"
#include <vector>

class UIScene_CapeSelectMenu : public UIScene
{
private:
	static const int k_capeCount = 53;
	static const wchar_t *k_capeNames[k_capeCount];

	static const BYTE sidePreviewControls = 4;

	struct CapeEntry
	{
		wstring name;
		wstring path;
	};

	struct CapeTab
	{
		wstring      packName;
		vector<CapeEntry> capes;
	};

	enum ECapeNavigation
	{
		eCapeNavigation_Cape,
		eCapeNavigation_Tab,
	};

	enum ECharacters
	{
		eCharacter_Current,
		eCharacter_Next1,
		eCharacter_Next2,
		eCharacter_Next3,
		eCharacter_Next4,
		eCharacter_Previous1,
		eCharacter_Previous2,
		eCharacter_Previous3,
		eCharacter_Previous4,

		eCharacter_COUNT,
	};

	UIControl_PlayerSkinPreview m_characters[eCharacter_COUNT];
	UIControl_Label m_labelCapeName;
	UIControl_Label m_labelSkinOrigin;
	UIControl_Label m_labelSelected;
	UIControl m_controlSkinNamePlate, m_controlSelectedPanel, m_controlIggyCharacters, m_controlTimer;

	IggyName m_funcSetPlayerCharacterSelected, m_funcSetCharacterLocked;
	IggyName m_funcSetLeftLabel, m_funcSetCentreLabel, m_funcSetRightLabel;

	UI_BEGIN_MAP_ELEMENTS_AND_NAMES(UIScene)
		UI_MAP_ELEMENT( m_controlSkinNamePlate, "SkinNamePlate")
		UI_BEGIN_MAP_CHILD_ELEMENTS( m_controlSkinNamePlate )
			UI_MAP_ELEMENT( m_labelCapeName,   "SkinTitle1")
		UI_MAP_ELEMENT( m_labelSkinOrigin, "SkinTitle2")
		UI_END_MAP_CHILD_ELEMENTS()

		UI_MAP_ELEMENT( m_controlSelectedPanel, "SelectedPanel" )
		UI_BEGIN_MAP_CHILD_ELEMENTS( m_controlSelectedPanel )
			UI_MAP_ELEMENT( m_labelSelected, "SelectedPanelLabel" )
		UI_END_MAP_CHILD_ELEMENTS()

		UI_MAP_ELEMENT( m_controlTimer, "Timer" )

		UI_MAP_ELEMENT( m_controlIggyCharacters, "IggyCharacters" )
		UI_BEGIN_MAP_CHILD_ELEMENTS( m_controlIggyCharacters )
			UI_MAP_ELEMENT( m_characters[eCharacter_Current],   "iggy_Character0" )
			UI_MAP_ELEMENT( m_characters[eCharacter_Next1],     "iggy_Character1" )
			UI_MAP_ELEMENT( m_characters[eCharacter_Next2],     "iggy_Character2" )
			UI_MAP_ELEMENT( m_characters[eCharacter_Next3],     "iggy_Character3" )
			UI_MAP_ELEMENT( m_characters[eCharacter_Next4],     "iggy_Character4" )
			UI_MAP_ELEMENT( m_characters[eCharacter_Previous1], "iggy_Character5" )
			UI_MAP_ELEMENT( m_characters[eCharacter_Previous2], "iggy_Character6" )
			UI_MAP_ELEMENT( m_characters[eCharacter_Previous3], "iggy_Character7" )
			UI_MAP_ELEMENT( m_characters[eCharacter_Previous4], "iggy_Character8" )
		UI_END_MAP_CHILD_ELEMENTS()

		UI_MAP_NAME( m_funcSetPlayerCharacterSelected, L"SetPlayerCharacterSelected" )
		UI_MAP_NAME( m_funcSetCharacterLocked,         L"SetCharacterLocked" )
		UI_MAP_NAME( m_funcSetLeftLabel,   L"SetLeftLabel" )
		UI_MAP_NAME( m_funcSetCentreLabel, L"SetCenterLabel" )
		UI_MAP_NAME( m_funcSetRightLabel,  L"SetRightLabel" )
	UI_END_MAP_ELEMENTS_AND_NAMES()

	vector<CapeTab>  m_tabs;
	int              m_tabIndex;
	int              m_confirmedTabIndex;
	int              m_capeIndex;
	int              m_confirmedCapeIndex;
	ECapeNavigation  m_currentNavigation;

	wstring      m_currentSkinPath;
	TEXTURE_NAME m_currentSkinTexture;
	bool m_bSlidingSkins;
	bool m_bAnimatingMove;
	bool m_bCapeIndexChanged;

public:
	UIScene_CapeSelectMenu(int iPad, void *initData, UILayer *parentLayer);

	virtual EUIScene getSceneType() { return eUIScene_CapeSelectMenu; }

	virtual void tick();
	virtual void updateTooltips();
	virtual void updateComponents();
	virtual void handleAnimationEnd();
	virtual void HandleDLCInstalled();
	virtual void HandleDLCMountingComplete();

protected:
	virtual wstring getMoviePath();

public:
	virtual void handleInput(int iPad, int key, bool repeat, bool pressed, bool released, bool &handled);
	virtual void customDraw(IggyCustomDrawCallbackRegion *region);

private:
	void handleCapeIndexChanged();
	int  getNextCapeIndex(int idx);
	int  getPreviousCapeIndex(int idx);

	void setCharacterSelected(bool selected);
	void setCharacterLocked(bool locked);
	void setCentreLabel(const wstring &label);

	static wstring       getCapePathForIndex(int idx);
	static TEXTURE_NAME  getDefaultSkinTexture(int defaultSkinIndex);

	void refreshCurrentSkinTexture();

	void buildTabs();
	void handleTabIndexChanged();
	void updateTabDisplay();
	int  getNextTabIndex(int idx) const;
	int  getPreviousTabIndex(int idx) const;

	void setLeftLabel(const wstring &label);
	void setRightLabel(const wstring &label);
};
