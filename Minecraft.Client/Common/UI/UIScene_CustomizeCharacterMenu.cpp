#include "stdafx.h"
#include "UI.h"
#include "UIScene_CustomizeCharacterMenu.h"
#include "..\..\Minecraft.h"

UIScene_CustomizeCharacterMenu::UIScene_CustomizeCharacterMenu(int iPad, void *initData, UILayer *parentLayer) : UIScene(iPad, parentLayer)
{
	initialiseMovie();

	m_buttons[BUTTON_CCM_CHANGESKIN].init(L"Change Skin", BUTTON_CCM_CHANGESKIN);
	m_buttons[BUTTON_CCM_CHANGECAPE].init(L"Change Cape", BUTTON_CCM_CHANGECAPE);

	m_buttons[BUTTON_CCM_UNUSED2].init(L"", BUTTON_CCM_UNUSED2);
	m_buttons[BUTTON_CCM_UNUSED3].init(L"", BUTTON_CCM_UNUSED3);
	m_buttons[BUTTON_CCM_UNUSED4].init(L"", BUTTON_CCM_UNUSED4);
	m_buttons[BUTTON_CCM_UNUSED5].init(L"", BUTTON_CCM_UNUSED5);
	m_buttons[BUTTON_CCM_UNUSED6].init(L"", BUTTON_CCM_UNUSED6);

	removeControl( &m_buttons[BUTTON_CCM_UNUSED2], false );
	removeControl( &m_buttons[BUTTON_CCM_UNUSED3], false );
	removeControl( &m_buttons[BUTTON_CCM_UNUSED4], false );
	removeControl( &m_buttons[BUTTON_CCM_UNUSED5], false );
	removeControl( &m_buttons[BUTTON_CCM_UNUSED6], false );

	doHorizontalResizeCheck();

	if(!ProfileManager.IsFullVersion())
	{
		removeControl( &m_buttons[BUTTON_CCM_CHANGESKIN], false );
		removeControl( &m_buttons[BUTTON_CCM_CHANGECAPE], false );
	}
}

UIScene_CustomizeCharacterMenu::~UIScene_CustomizeCharacterMenu()
{
}

void UIScene_CustomizeCharacterMenu::handleReload()
{
	removeControl( &m_buttons[BUTTON_CCM_UNUSED2], false );
	removeControl( &m_buttons[BUTTON_CCM_UNUSED3], false );
	removeControl( &m_buttons[BUTTON_CCM_UNUSED4], false );
	removeControl( &m_buttons[BUTTON_CCM_UNUSED5], false );
	removeControl( &m_buttons[BUTTON_CCM_UNUSED6], false );

	doHorizontalResizeCheck();

	if(!ProfileManager.IsFullVersion())
	{
		removeControl( &m_buttons[BUTTON_CCM_CHANGESKIN], false );
		removeControl( &m_buttons[BUTTON_CCM_CHANGECAPE], false );
	}
}

wstring UIScene_CustomizeCharacterMenu::getMoviePath()
{
	if(app.GetLocalPlayerCount() > 1)
	{
		return L"HelpAndOptionsMenuSplit";
	}
	else
	{
		return L"HelpAndOptionsMenu";
	}
}

void UIScene_CustomizeCharacterMenu::updateTooltips()
{
	ui.SetTooltips( m_iPad, IDS_TOOLTIPS_SELECT, IDS_TOOLTIPS_BACK );
}

void UIScene_CustomizeCharacterMenu::updateComponents()
{
	bool bNotInGame = (Minecraft::GetInstance()->level == NULL);
	if(bNotInGame)
	{
		m_parentLayer->showComponent(m_iPad, eUIComponent_Panorama, true);
		m_parentLayer->showComponent(m_iPad, eUIComponent_Logo, true);
	}
	else
	{
		m_parentLayer->showComponent(m_iPad, eUIComponent_Panorama, false);

		if(app.GetLocalPlayerCount() == 1) m_parentLayer->showComponent(m_iPad, eUIComponent_Logo, true);
		else                               m_parentLayer->showComponent(m_iPad, eUIComponent_Logo, false);
	}
}

void UIScene_CustomizeCharacterMenu::handleInput(int iPad, int key, bool repeat, bool pressed, bool released, bool &handled)
{
	ui.AnimateKeyPress(m_iPad, key, repeat, pressed, released);

	switch(key)
	{
	case ACTION_MENU_CANCEL:
		if(pressed && !repeat)
		{
			navigateBack();
		}
		break;
	case ACTION_MENU_OK:
#ifdef __ORBIS__
	case ACTION_MENU_TOUCHPAD_PRESS:
#endif
	if(pressed)
	{
		ui.PlayUISFX(eSFX_Press);
	}

	case ACTION_MENU_UP:
	case ACTION_MENU_DOWN:
		sendInputToMovie(key, repeat, pressed, released);
		break;
	}
}

void UIScene_CustomizeCharacterMenu::handlePress(F64 controlId, F64 childId)
{
	switch((int)controlId)
	{
	case BUTTON_CCM_CHANGESKIN:
		ui.NavigateToScene(m_iPad, eUIScene_SkinSelectMenu);
		break;
	case BUTTON_CCM_CHANGECAPE:
		ui.NavigateToScene(m_iPad, eUIScene_CapeSelectMenu);
		break;
	}
}
