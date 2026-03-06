#pragma once

#include "UIScene.h"

#define BUTTON_CCM_CHANGESKIN   0
#define BUTTON_CCM_CHANGECAPE   1
#define BUTTON_CCM_UNUSED2      2
#define BUTTON_CCM_UNUSED3      3
#define BUTTON_CCM_UNUSED4      4
#define BUTTON_CCM_UNUSED5      5
#define BUTTON_CCM_UNUSED6      6
#define BUTTONS_CCM_MAX         7

class UIScene_CustomizeCharacterMenu : public UIScene
{
private:
	UIControl_Button m_buttons[BUTTONS_CCM_MAX];
	UI_BEGIN_MAP_ELEMENTS_AND_NAMES(UIScene)
		UI_MAP_ELEMENT( m_buttons[BUTTON_CCM_CHANGESKIN], "Button1")
		UI_MAP_ELEMENT( m_buttons[BUTTON_CCM_CHANGECAPE], "Button2")
		UI_MAP_ELEMENT( m_buttons[BUTTON_CCM_UNUSED2],    "Button3")
		UI_MAP_ELEMENT( m_buttons[BUTTON_CCM_UNUSED3],    "Button4")
		UI_MAP_ELEMENT( m_buttons[BUTTON_CCM_UNUSED4],    "Button5")
		UI_MAP_ELEMENT( m_buttons[BUTTON_CCM_UNUSED5],    "Button6")
		UI_MAP_ELEMENT( m_buttons[BUTTON_CCM_UNUSED6],    "Button7")
	UI_END_MAP_ELEMENTS_AND_NAMES()

public:
	UIScene_CustomizeCharacterMenu(int iPad, void *initData, UILayer *parentLayer);
	virtual ~UIScene_CustomizeCharacterMenu();

	virtual EUIScene getSceneType() { return eUIScene_CustomizeCharacterMenu; }

	virtual void updateTooltips();
	virtual void updateComponents();
	virtual void handleReload();

protected:
	virtual wstring getMoviePath();

public:
	// INPUT
	virtual void handleInput(int iPad, int key, bool repeat, bool pressed, bool released, bool &handled);

protected:
	void handlePress(F64 controlId, F64 childId);
};
