#pragma once

class Language
{
private:
	static Language *singleton;
public:
	Language();
    static Language *getInstance();
    wstring getElement(const wstring elementId, ...);
	wstring getElement(const wstring& elementId, va_list args);
    wstring getElementName(const wstring& elementId);
    wstring getElementDescription(const wstring& elementId);
};