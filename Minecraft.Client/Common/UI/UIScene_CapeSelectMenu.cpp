#include "stdafx.h"
#include "UI.h"
#include "UIScene_CapeSelectMenu.h"
#include "..\..\Minecraft.h"
#include "..\..\..\Minecraft.World\StringHelpers.h"
#include "..\..\..\Minecraft.World\Definitions.h"

// Cape names — index 0 = No Cape, 1-52 correspond to dlccape00000001.png ... dlccape00000052.png
const wchar_t *UIScene_CapeSelectMenu::k_capeNames[UIScene_CapeSelectMenu::k_capeCount] =
{
	L"No Cape",
	L"15th Anniversary",
	L"MCC 15th Year",
	L"Minecraft Experience",
	L"MINECON 2011",
	L"MINECON 2012",
	L"MINECON 2013",
	L"MINECON 2015",
	L"MINECON 2016",
	L"Mojang",
	L"Mojang Classic",
	L"Mojang Studios",
	L"Mojang Office",
	L"4J Studios",
	L"Microsoft Xbox 360",
	L"Xbox 1st Birthday",
	L"Founder's",
	L"Common",
	L"The Pan",
	L"Migrator",
	L"One Vanilla",
	L"Purple Heart",
	L"Progress Pride",
	L"Cherry Blossom",
	L"Follower's",
	L"Home",
	L"Menace",
	L"Yearn",
	L"Idaho",
	L"Copper",
	L"Zombie Horse",
	L"Prismarine",
	L"Turtle",
	L"Birthday",
	L"Valentine",
	L"Oxeye",
	L"Cobalt",
	L"Snail",
	L"Size M",
	L"Scrolls Champion",
	L"Millionth Customer",
	L"Unidentified",
	L"Translator",
	L"Chinese Translator",
	L"Moderator",
	L"Mapmaker",
	L"Christmas 2010",
	L"New Year's 2011",
	L"Bacon",
	L"DannyBstyle's",
	L"JulianClark's",
	L"Cheapsh0t's",
	L"MrMessiah's",
};

// Maps a default skin carousel index to the corresponding built-in TEXTURE_NAME.
// eDefaultSkins enum: ServerSelected=0, Skin0=1, Skin1=2, ..., Skin7=8
// This must match UIScene_SkinSelectMenu::getTextureId().
TEXTURE_NAME UIScene_CapeSelectMenu::getDefaultSkinTexture(int defaultSkinIndex)
{
	switch(defaultSkinIndex)
	{
	case eDefaultSkins_ServerSelected:
	case eDefaultSkins_Skin0: return TN_MOB_CHAR;
	case eDefaultSkins_Skin1: return TN_MOB_CHAR1;
	case eDefaultSkins_Skin2: return TN_MOB_CHAR2;
	case eDefaultSkins_Skin3: return TN_MOB_CHAR3;
	case eDefaultSkins_Skin4: return TN_MOB_CHAR4;
	case eDefaultSkins_Skin5: return TN_MOB_CHAR5;
	case eDefaultSkins_Skin6: return TN_MOB_CHAR6;
	case eDefaultSkins_Skin7: return TN_MOB_CHAR7;
	default:                  return TN_MOB_CHAR;
	}
}

// Reads the player's current skin and populates m_currentSkinPath / m_currentSkinTexture
void UIScene_CapeSelectMenu::refreshCurrentSkinTexture()
{
	DWORD dwSkin = app.GetPlayerSkinId(m_iPad);
	if(GET_IS_DLC_SKIN_FROM_BITMASK(dwSkin) || GET_UGC_SKIN_ID_FROM_BITMASK(dwSkin) != 0)
	{
		// DLC or UGC skin — path is in MEM_Files
		m_currentSkinPath    = app.GetPlayerSkinName(m_iPad);
		m_currentSkinTexture = TN_MOB_CHAR;
	}
	else
	{
		// Default skin (Steve/Alex/etc.) — use built-in engine texture, no file path
		m_currentSkinPath    = L"";
		m_currentSkinTexture = getDefaultSkinTexture(GET_DEFAULT_SKIN_ID_FROM_BITMASK(dwSkin));
	}
}

// Returns "dlccapeXXXXXXXX.png" for idx >= 1, or "" for idx == 0 (No Cape)
wstring UIScene_CapeSelectMenu::getCapePathForIndex(int idx)
{
	if(idx <= 0) return L"";
	wchar_t buf[32];
	swprintf(buf, 32, L"dlccape%08d.png", idx);
	return wstring(buf);
}

int UIScene_CapeSelectMenu::getNextCapeIndex(int idx)
{
	int size = (int)m_tabs[m_tabIndex].capes.size();
	return (idx + 1) % size;
}

int UIScene_CapeSelectMenu::getPreviousCapeIndex(int idx)
{
	int size = (int)m_tabs[m_tabIndex].capes.size();
	return (idx + size - 1) % size;
}

int UIScene_CapeSelectMenu::getNextTabIndex(int idx) const
{
	return (idx + 1) % (int)m_tabs.size();
}

int UIScene_CapeSelectMenu::getPreviousTabIndex(int idx) const
{
	return (idx + (int)m_tabs.size() - 1) % (int)m_tabs.size();
}

// Build m_tabs from the hardcoded cape list (tab 0) and any DLC skin packs that have capes.
// Called once at construction after DLC is mounted. Fully dynamic — no pack names hardcoded.
void UIScene_CapeSelectMenu::buildTabs()
{
	m_tabs.clear();

	// Tab 0: "Capes"
	{
		CapeTab mainTab;
		mainTab.packName = L"Capes";
		for(int i = 0; i < k_capeCount; ++i)
		{
			CapeEntry e;
			e.name = k_capeNames[i];
			e.path = getCapePathForIndex(i);
			mainTab.capes.push_back(e);
		}
		m_tabs.push_back(mainTab);
	}

	// Tabs 1+: one per DLC skin pack that has at least one capped skin
	DWORD packCount = app.m_dlcManager.getPackCount(DLCManager::e_DLCType_Skin);
	for(DWORD p = 0; p < packCount; ++p)
	{
		DLCPack *pack = app.m_dlcManager.getPack(p, DLCManager::e_DLCType_Skin);
		if(!pack) continue;

		CapeTab tab;
		tab.packName = pack->getName();

		// "No Cape" is always the first entry in a pack tab
		CapeEntry noCape;
		noCape.name = L"No Cape";
		noCape.path = L"";
		tab.capes.push_back(noCape);

		DWORD skinCount = pack->getSkinCount();
		for(DWORD s = 0; s < skinCount; ++s)
		{
			DLCSkinFile *skinFile = pack->getSkinFile(s);
			if(!skinFile) continue;
			wstring capePath = skinFile->getParameterAsString(DLCManager::e_DLCParamType_Cape);
			if(capePath.empty()) continue;

			CapeEntry e;
			wstring skinName = skinFile->getParameterAsString(DLCManager::e_DLCParamType_DisplayName);
			e.name = skinName + L"'s Cape";
			e.path = capePath;
			tab.capes.push_back(e);
		}

		// Only add the tab if at least one skin had a cape
		if(tab.capes.size() > 1)
		{
			m_tabs.push_back(tab);
		}
	}
}

// Switch to m_tabIndex, reset to cape 0, refresh display
void UIScene_CapeSelectMenu::handleTabIndexChanged()
{
	m_capeIndex = 0;
	handleCapeIndexChanged();
	updateTabDisplay();
}

// Set left/centre/right labels for the current tab and its neighbours
void UIScene_CapeSelectMenu::updateTabDisplay()
{
	if(m_tabs.size() <= 1)
	{
		setLeftLabel(L"");
		setRightLabel(L"");
		return;
	}
	setLeftLabel(m_tabs[getPreviousTabIndex(m_tabIndex)].packName);
	setRightLabel(m_tabs[getNextTabIndex(m_tabIndex)].packName);
}

UIScene_CapeSelectMenu::UIScene_CapeSelectMenu(int iPad, void *initData, UILayer *parentLayer) : UIScene(iPad, parentLayer)
{
	initialiseMovie();

	m_labelSelected.init( app.GetString(IDS_SELECTED) );

	m_bSlidingSkins = false;
	m_bAnimatingMove = false;
	m_bCapeIndexChanged = false;
	m_currentNavigation = eCapeNavigation_Cape;

	// Trigger DLC skin loading so pack capes are in m_MEM_Files before buildTabs()
	app.StartInstallDLCProcess(m_iPad);

	// Build tab list from hardcoded capes + any DLC skin packs that have capes
	buildTabs();

	// Find the tab and entry that match the player's currently saved cape path
	wstring savedCapePath = app.GetPlayerCapeName(m_iPad);
	m_tabIndex  = 0;
	m_capeIndex = 0;
	for(int t = 0; t < (int)m_tabs.size(); ++t)
	{
		for(int c = 0; c < (int)m_tabs[t].capes.size(); ++c)
		{
			if(m_tabs[t].capes[c].path == savedCapePath)
			{
				m_tabIndex  = t;
				m_capeIndex = c;
				goto found_saved_cape;
			}
		}
	}
	found_saved_cape:

	m_confirmedTabIndex  = m_tabIndex;
	m_confirmedCapeIndex = m_capeIndex;
	refreshCurrentSkinTexture();

	m_labelCapeName.init(L"");
	m_labelSkinOrigin.init(L"");

	m_controlTimer.setVisible(false);

	// Character facing — models face backward so the cape is visible
	m_characters[eCharacter_Current].SetFacing(UIControl_PlayerSkinPreview::e_SkinPreviewFacing_Backward);

	m_characters[eCharacter_Next1].SetFacing(UIControl_PlayerSkinPreview::e_SkinPreviewFacing_BackLeft);
	m_characters[eCharacter_Next2].SetFacing(UIControl_PlayerSkinPreview::e_SkinPreviewFacing_BackLeft);
	m_characters[eCharacter_Next3].SetFacing(UIControl_PlayerSkinPreview::e_SkinPreviewFacing_BackLeft);
	m_characters[eCharacter_Next4].SetFacing(UIControl_PlayerSkinPreview::e_SkinPreviewFacing_BackLeft);

	m_characters[eCharacter_Previous1].SetFacing(UIControl_PlayerSkinPreview::e_SkinPreviewFacing_BackRight);
	m_characters[eCharacter_Previous2].SetFacing(UIControl_PlayerSkinPreview::e_SkinPreviewFacing_BackRight);
	m_characters[eCharacter_Previous3].SetFacing(UIControl_PlayerSkinPreview::e_SkinPreviewFacing_BackRight);
	m_characters[eCharacter_Previous4].SetFacing(UIControl_PlayerSkinPreview::e_SkinPreviewFacing_BackRight);

	// Populate the initial preview and tab labels
	handleCapeIndexChanged();
	updateTabDisplay();
}

wstring UIScene_CapeSelectMenu::getMoviePath()
{
	if(app.GetLocalPlayerCount() > 1)
	{
		return L"SkinSelectMenuSplit";
	}
	else
	{
		return L"SkinSelectMenu";
	}
}

void UIScene_CapeSelectMenu::tick()
{
	UIScene::tick();

	if(m_bCapeIndexChanged)
	{
		m_bCapeIndexChanged = false;
		handleCapeIndexChanged();
	}
}

void UIScene_CapeSelectMenu::updateTooltips()
{
	ui.SetTooltips( m_iPad, IDS_TOOLTIPS_SELECT_SKIN, IDS_TOOLTIPS_CANCEL, -1, -1, -1, -1, -1, -1, IDS_TOOLTIPS_NAVIGATE );
}

void UIScene_CapeSelectMenu::updateComponents()
{
	m_parentLayer->showComponent(m_iPad, eUIComponent_Logo, false);
}

void UIScene_CapeSelectMenu::handleAnimationEnd()
{
	if(m_bSlidingSkins)
	{
		m_bSlidingSkins = false;

		m_characters[eCharacter_Current].SetFacing(UIControl_PlayerSkinPreview::e_SkinPreviewFacing_Backward, false);
		m_characters[eCharacter_Next1].SetFacing(UIControl_PlayerSkinPreview::e_SkinPreviewFacing_BackLeft, false);
		m_characters[eCharacter_Previous1].SetFacing(UIControl_PlayerSkinPreview::e_SkinPreviewFacing_BackRight, false);

		m_bCapeIndexChanged = true;
		m_bAnimatingMove = false;
	}
}

void UIScene_CapeSelectMenu::handleCapeIndexChanged()
{
	const CapeTab &tab = m_tabs[m_tabIndex];
	bool isConfirmed = (m_tabIndex == m_confirmedTabIndex && m_capeIndex == m_confirmedCapeIndex);

	setCharacterSelected(isConfirmed);
	setCharacterLocked(false);

	m_controlSkinNamePlate.setVisible(true);
	m_characters[eCharacter_Current].setVisible(true);

	m_characters[eCharacter_Current].SetTexture(m_currentSkinPath, m_currentSkinTexture);
	m_characters[eCharacter_Current].SetCapeTexture(tab.capes[m_capeIndex].path);

	// Update adjacent preview slots
	int nextIdx = m_capeIndex;
	int prevIdx = m_capeIndex;

	for(BYTE i = 0; i < sidePreviewControls; ++i)
	{
		nextIdx = getNextCapeIndex(nextIdx);
		m_characters[eCharacter_Next1 + i].setVisible(true);
		m_characters[eCharacter_Next1 + i].SetTexture(m_currentSkinPath, m_currentSkinTexture);
		m_characters[eCharacter_Next1 + i].SetCapeTexture(tab.capes[nextIdx].path);
	}

	for(BYTE i = 0; i < sidePreviewControls; ++i)
	{
		prevIdx = getPreviousCapeIndex(prevIdx);
		m_characters[eCharacter_Previous1 + i].setVisible(true);
		m_characters[eCharacter_Previous1 + i].SetTexture(m_currentSkinPath, m_currentSkinTexture);
		m_characters[eCharacter_Previous1 + i].SetCapeTexture(tab.capes[prevIdx].path);
	}

	// Name label and centre tab label
	m_labelCapeName.init(tab.capes[m_capeIndex].name);
	setCentreLabel(tab.packName);
}

void UIScene_CapeSelectMenu::handleInput(int iPad, int key, bool repeat, bool pressed, bool released, bool &handled)
{
	switch(key)
	{
	case ACTION_MENU_CANCEL:
		if(pressed)
		{
			ui.AnimateKeyPress(iPad, key, repeat, pressed, released);
			// Restore the cape to what it was when the menu opened
			app.SetPlayerCape(iPad, m_tabs[m_confirmedTabIndex].capes[m_confirmedCapeIndex].path);
			app.CheckGameSettingsChanged(true, iPad);
			navigateBack();
		}
		break;

	case ACTION_MENU_OK:
#ifdef __ORBIS__
	case ACTION_MENU_TOUCHPAD_PRESS:
#endif
		if(pressed)
		{
			ui.AnimateKeyPress(iPad, key, repeat, pressed, released);
			// Confirm the currently highlighted cape
			app.SetPlayerCape(iPad, m_tabs[m_tabIndex].capes[m_capeIndex].path);
			app.CheckGameSettingsChanged(true, iPad);
			m_confirmedTabIndex  = m_tabIndex;
			m_confirmedCapeIndex = m_capeIndex;
			setCharacterSelected(true);
			ui.PlayUISFX(eSFX_Press);
		}
		break;

	case ACTION_MENU_UP:
	case ACTION_MENU_DOWN:
		if(pressed && !repeat && m_tabs.size() > 1)
		{
			ui.AnimateKeyPress(iPad, key, repeat, pressed, released);
			ui.PlayUISFX(eSFX_Scroll);
			m_currentNavigation = (m_currentNavigation == eCapeNavigation_Cape)
				? eCapeNavigation_Tab : eCapeNavigation_Cape;
			sendInputToMovie(key, repeat, pressed, released);
		}
		break;

	case ACTION_MENU_LEFT:
		if(pressed)
		{
			if(m_currentNavigation == eCapeNavigation_Tab)
			{
				if(!m_bAnimatingMove && m_tabs.size() > 1)
				{
					ui.AnimateKeyPress(iPad, key, repeat, pressed, released);
					ui.PlayUISFX(eSFX_Scroll);
					m_tabIndex = getPreviousTabIndex(m_tabIndex);
					handleTabIndexChanged();
				}
			}
			else
			{
				if(!m_bAnimatingMove)
				{
					ui.AnimateKeyPress(iPad, key, repeat, pressed, released);
					ui.PlayUISFX(eSFX_Scroll);

					m_capeIndex = getPreviousCapeIndex(m_capeIndex);

					m_bSlidingSkins = true;
					m_bAnimatingMove = true;

					m_characters[eCharacter_Current].SetFacing(UIControl_PlayerSkinPreview::e_SkinPreviewFacing_BackLeft, true);
					m_characters[eCharacter_Previous1].SetFacing(UIControl_PlayerSkinPreview::e_SkinPreviewFacing_Backward, true);

					sendInputToMovie(ACTION_MENU_RIGHT, repeat, pressed, released);
				}
			}
		}
		break;

	case ACTION_MENU_RIGHT:
		if(pressed)
		{
			if(m_currentNavigation == eCapeNavigation_Tab)
			{
				if(!m_bAnimatingMove && m_tabs.size() > 1)
				{
					ui.AnimateKeyPress(iPad, key, repeat, pressed, released);
					ui.PlayUISFX(eSFX_Scroll);
					m_tabIndex = getNextTabIndex(m_tabIndex);
					handleTabIndexChanged();
				}
			}
			else
			{
				if(!m_bAnimatingMove)
				{
					ui.AnimateKeyPress(iPad, key, repeat, pressed, released);
					ui.PlayUISFX(eSFX_Scroll);

					m_capeIndex = getNextCapeIndex(m_capeIndex);

					m_bSlidingSkins = true;
					m_bAnimatingMove = true;

					m_characters[eCharacter_Current].SetFacing(UIControl_PlayerSkinPreview::e_SkinPreviewFacing_BackRight, true);
					m_characters[eCharacter_Next1].SetFacing(UIControl_PlayerSkinPreview::e_SkinPreviewFacing_Backward, true);

					sendInputToMovie(ACTION_MENU_LEFT, repeat, pressed, released);
				}
			}
		}
		break;

	case ACTION_MENU_OTHER_STICK_PRESS:
		if(pressed)
		{
			ui.PlayUISFX(eSFX_Press);
			m_characters[eCharacter_Current].ResetRotation();
		}
		break;

	case ACTION_MENU_OTHER_STICK_LEFT:
		if(pressed)
			m_characters[eCharacter_Current].m_incYRot = true;
		else if(released)
			m_characters[eCharacter_Current].m_incYRot = false;
		break;

	case ACTION_MENU_OTHER_STICK_RIGHT:
		if(pressed)
			m_characters[eCharacter_Current].m_decYRot = true;
		else if(released)
			m_characters[eCharacter_Current].m_decYRot = false;
		break;

	case ACTION_MENU_OTHER_STICK_UP:
		if(pressed)
			m_characters[eCharacter_Current].CyclePreviousAnimation();
		break;

	case ACTION_MENU_OTHER_STICK_DOWN:
		if(pressed)
			m_characters[eCharacter_Current].CycleNextAnimation();
		break;
	}
}

void UIScene_CapeSelectMenu::customDraw(IggyCustomDrawCallbackRegion *region)
{
	int characterId = -1;
	swscanf((wchar_t*)region->name, L"Character%d", &characterId);
	if(characterId == -1)
	{
		app.DebugPrintf("Invalid character to render found\n");
	}
	else
	{
		CustomDrawData *customDrawRegion = ui.setupCustomDraw(this, region);
		delete customDrawRegion;

#ifdef __PS3__
		if(region->stencil_func_ref != 0) RenderManager.StateSetStencil(GL_EQUAL, region->stencil_func_ref, region->stencil_func_mask);
#elif __PSVITA__
		if(region->stencil_func_ref != 0) RenderManager.StateSetStencil(SCE_GXM_STENCIL_FUNC_EQUAL, region->stencil_func_mask, region->stencil_write_mask);
#else
		if(region->stencil_func_ref != 0) RenderManager.StateSetStencil(GL_EQUAL, region->stencil_func_ref, region->stencil_func_mask, region->stencil_write_mask);
#endif
		m_characters[characterId].render(region);

		ui.endCustomDraw(region);
	}
}

void UIScene_CapeSelectMenu::setCharacterSelected(bool selected)
{
	IggyDataValue result;
	IggyDataValue value[1];
	value[0].type = IGGY_DATATYPE_boolean;
	value[0].boolval = selected;
	IggyPlayerCallMethodRS(getMovie(), &result, IggyPlayerRootPath(getMovie()), m_funcSetPlayerCharacterSelected, 1, value);
}

void UIScene_CapeSelectMenu::setCharacterLocked(bool locked)
{
	IggyDataValue result;
	IggyDataValue value[1];
	value[0].type = IGGY_DATATYPE_boolean;
	value[0].boolval = locked;
	IggyPlayerCallMethodRS(getMovie(), &result, IggyPlayerRootPath(getMovie()), m_funcSetCharacterLocked, 1, value);
}

void UIScene_CapeSelectMenu::setLeftLabel(const wstring &label)
{
	IggyDataValue result;
	IggyDataValue value[1];
	value[0].type = IGGY_DATATYPE_string_UTF16;
	value[0].string16.string = (IggyUTF16 *)label.c_str();
	value[0].string16.length = (unsigned int)label.length();
	IggyPlayerCallMethodRS(getMovie(), &result, IggyPlayerRootPath(getMovie()), m_funcSetLeftLabel, 1, value);
}

void UIScene_CapeSelectMenu::setRightLabel(const wstring &label)
{
	IggyDataValue result;
	IggyDataValue value[1];
	value[0].type = IGGY_DATATYPE_string_UTF16;
	value[0].string16.string = (IggyUTF16 *)label.c_str();
	value[0].string16.length = (unsigned int)label.length();
	IggyPlayerCallMethodRS(getMovie(), &result, IggyPlayerRootPath(getMovie()), m_funcSetRightLabel, 1, value);
}

void UIScene_CapeSelectMenu::setCentreLabel(const wstring &label)
{
	IggyDataValue result;
	IggyDataValue value[1];

	value[0].type = IGGY_DATATYPE_string_UTF16;
	value[0].string16.string = (IggyUTF16 *)label.c_str();
	value[0].string16.length = (unsigned int)label.length();
	IggyPlayerCallMethodRS(getMovie(), &result, IggyPlayerRootPath(getMovie()), m_funcSetCentreLabel, 1, value);
}

void UIScene_CapeSelectMenu::HandleDLCInstalled()
{
	app.StartInstallDLCProcess(m_iPad);
}

void UIScene_CapeSelectMenu::HandleDLCMountingComplete()
{
	// DLC skin textures are now in m_MEM_Files — rebuild tabs to pick up newly mounted packs
	refreshCurrentSkinTexture();
	buildTabs();

	// Re-locate the saved cape in the updated tab list
	wstring savedCapePath = app.GetPlayerCapeName(m_iPad);
	m_tabIndex  = 0;
	m_capeIndex = 0;
	for(int t = 0; t < (int)m_tabs.size(); ++t)
	{
		for(int c = 0; c < (int)m_tabs[t].capes.size(); ++c)
		{
			if(m_tabs[t].capes[c].path == savedCapePath)
			{
				m_tabIndex  = t;
				m_capeIndex = c;
				goto dlc_found_cape;
			}
		}
	}
	dlc_found_cape:

	m_confirmedTabIndex  = m_tabIndex;
	m_confirmedCapeIndex = m_capeIndex;

	handleCapeIndexChanged();
	updateTabDisplay();
}
