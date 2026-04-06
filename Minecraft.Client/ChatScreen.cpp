#include "stdafx.h"
#include "ChatScreen.h"
#include "MultiplayerLocalPlayer.h"
#include "..\Minecraft.World\SharedConstants.h"
#include "..\Minecraft.World\StringHelpers.h"

const wstring ChatScreen::allowedChars = SharedConstants::acceptableLetters;

ChatScreen::ChatScreen()
{
	frame = 0;
    caretPos = 0;
}

void ChatScreen::init()
{
	Keyboard::enableRepeatEvents(true);
}

void ChatScreen::removed()
{
	Keyboard::enableRepeatEvents(false);
}

void ChatScreen::tick()
{
	frame++;
}

void ChatScreen::keyPressed(wchar_t ch, int eventKey)
{
    if (eventKey == Keyboard::KEY_ESCAPE)
	{
        minecraft->setScreen(NULL);
        return;
    }
    if (eventKey == Keyboard::KEY_RETURN)
	{
        wstring msg = trimString(message);
        if (msg.length() > 0)
		{
            wstring trim = trimString(message);
            if (!minecraft->handleClientSideCommand(trim))
			{
                minecraft->player->chat(trim);
            }
        }
        minecraft->setScreen(NULL);
        return;
    }
    if (eventKey == Keyboard::KEY_LEFT)
    {
        if (caretPos > 0)
        {
            caretPos--;
        }
        return;
    }
    if (eventKey == Keyboard::KEY_RIGHT)
    {
        if (caretPos < message.length())
        {
            caretPos++;
        }
        return;
    }
    if (eventKey == Keyboard::KEY_BACK && caretPos > 0 && message.length() > 0)
    {
        message.erase(caretPos - 1, 1);
        caretPos--;
        return;
    }
    if (ch >= 32 && SharedConstants::acceptableLetters.find(ch) != wstring::npos && message.length() < SharedConstants::maxChatLength)
	{
        message.insert(caretPos, 1, ch);
        caretPos++;
    }

}

void ChatScreen::render(int xm, int ym, float a)
{
    wstring displayMessage = L"> " + message;
    if (frame / 6 % 2 == 0)
    {
        displayMessage.insert(2 + caretPos, 1, L'_');
    }

    fill(2, height - 14, width - 2, height - 2, 0x80000000);
    drawString(font, displayMessage, 4, height - 12, 0xe0e0e0);

    Screen::render(xm, ym, a);
}

void ChatScreen::mouseClicked(int x, int y, int buttonNum)
{
    if (buttonNum == 0)
	{
        if (minecraft->gui->selectedName != L"")	// 4J - was NULL comparison
		{
            if (caretPos > 0 && message[caretPos - 1] != L' ')
			{
                message.insert(caretPos, 1, L' ');
                caretPos++;
            }
            message.insert(caretPos, minecraft->gui->selectedName);
            caretPos += (unsigned int)minecraft->gui->selectedName.length();
            unsigned int maxLength = SharedConstants::maxChatLength;
            if (message.length() > maxLength)
			{
                message = message.substr(0, maxLength);
                if (caretPos > message.length())
                {
                    caretPos = (unsigned int)message.length();
                }
            }
        }
		else
		{
            Screen::mouseClicked(x, y, buttonNum);
        }
    }

}