#pragma once
#include "Screen.h"
using namespace std;

class ChatScreen : public Screen
{
protected:
	wstring message;
private:
	int frame;
	unsigned int caretPos;

public:
	ChatScreen();	//4J added
	virtual void init();
    virtual void removed();
    virtual void tick();
private:
	static const wstring allowedChars;
protected:
	void keyPressed(wchar_t ch, int eventKey);
public:
	void render(int xm, int ym, float a);
protected:
	void mouseClicked(int x, int y, int buttonNum);
};