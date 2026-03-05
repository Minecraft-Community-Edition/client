#pragma once
using namespace std;

#include "stdafx.h"
#include "Language.h"

class I18n
{
private:
	static Language *lang;

public:
    static wstring get(const wstring id, ...);
	static wstring get(const wstring& id, va_list args);
};