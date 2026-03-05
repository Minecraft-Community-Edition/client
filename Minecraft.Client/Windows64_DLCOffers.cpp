#include "stdafx.h"
#ifdef _WINDOWS64
#include "Windows64_DLCOffers.h"
#include <winhttp.h>
#pragma comment(lib, "winhttp.lib")
#include <shlobj.h>
#include "../Windows64_Minecraft.h"
#include "../User.h"

static bool ParseUrl(const wchar_t* url,
					 std::wstring& host, INTERNET_PORT& port,
					 std::wstring& path)
{
	URL_COMPONENTS uc = {};
	uc.dwStructSize       = sizeof(uc);
	uc.dwHostNameLength   = (DWORD)-1;
	uc.dwUrlPathLength    = (DWORD)-1;
	uc.dwExtraInfoLength  = (DWORD)-1;
	if (!WinHttpCrackUrl(url, 0, 0, &uc)) return false;
	host.assign(uc.lpszHostName, uc.dwHostNameLength);
	path.assign(uc.lpszUrlPath,  uc.dwUrlPathLength);
	if (uc.lpszExtraInfo && uc.dwExtraInfoLength)
		path.append(uc.lpszExtraInfo, uc.dwExtraInfoLength);
	port = uc.nPort;
	return true;
}

bool Windows64_DLCOffers::FetchBytesFromUrl(const wchar_t* url,
											PBYTE* ppData, DWORD* pdwSize)
{
	*ppData  = nullptr;
	*pdwSize = 0;

	printf("[DLC] FetchBytesFromUrl: '%ls'\n", url);

	std::wstring host, path;
	INTERNET_PORT port = 80;
	if (!ParseUrl(url, host, port, path))
	{
		printf("[DLC] FetchBytesFromUrl: ParseUrl FAILED (err=%u)\n", GetLastError());
		return false;
	}

	printf("[DLC] FetchBytesFromUrl: host='%ls' port=%u path='%ls'\n",
		host.c_str(), (unsigned)port, path.c_str());

	HINTERNET hSession = WinHttpOpen(L"W64DLC/1.0",
		WINHTTP_ACCESS_TYPE_NO_PROXY,
		WINHTTP_NO_PROXY_NAME,
		WINHTTP_NO_PROXY_BYPASS, 0);
	if (!hSession)
	{
		printf("[DLC] FetchBytesFromUrl: WinHttpOpen FAILED (err=%u)\n", GetLastError());
		return false;
	}

	HINTERNET hConn = WinHttpConnect(hSession, host.c_str(), port, 0);
	if (!hConn)
	{
		printf("[DLC] FetchBytesFromUrl: WinHttpConnect FAILED (err=%u)\n", GetLastError());
		WinHttpCloseHandle(hSession);
		return false;
	}

	HINTERNET hReq = WinHttpOpenRequest(hConn, L"GET", path.c_str(),
		nullptr, WINHTTP_NO_REFERER,
		WINHTTP_DEFAULT_ACCEPT_TYPES, 0);
	if (!hReq)
	{
		printf("[DLC] FetchBytesFromUrl: WinHttpOpenRequest FAILED (err=%u)\n", GetLastError());
		WinHttpCloseHandle(hConn);
		WinHttpCloseHandle(hSession);
		return false;
	}

	if (!WinHttpSendRequest(hReq, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
		WINHTTP_NO_REQUEST_DATA, 0, 0, 0))
	{
		printf("[DLC] FetchBytesFromUrl: WinHttpSendRequest FAILED (err=%u)\n", GetLastError());
		WinHttpCloseHandle(hReq);
		WinHttpCloseHandle(hConn);
		WinHttpCloseHandle(hSession);
		return false;
	}

	if (!WinHttpReceiveResponse(hReq, nullptr))
	{
		printf("[DLC] FetchBytesFromUrl: WinHttpReceiveResponse FAILED (err=%u)\n", GetLastError());
		WinHttpCloseHandle(hReq);
		WinHttpCloseHandle(hConn);
		WinHttpCloseHandle(hSession);
		return false;
	}

	DWORD dwStatusCode = 0;
	DWORD dwStatusSize = sizeof(dwStatusCode);
	WinHttpQueryHeaders(hReq,
		WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
		WINHTTP_HEADER_NAME_BY_INDEX,
		&dwStatusCode, &dwStatusSize,
		WINHTTP_NO_HEADER_INDEX);
	printf("[DLC] FetchBytesFromUrl: HTTP status = %u\n", dwStatusCode);

	if (dwStatusCode != 200)
	{
		printf("[DLC] FetchBytesFromUrl: non-200 response, aborting\n");
		WinHttpCloseHandle(hReq);
		WinHttpCloseHandle(hConn);
		WinHttpCloseHandle(hSession);
		return false;
	}

	std::vector<BYTE> buf;
	DWORD dwRead = 0;
	BYTE  chunk[8192];
	while (WinHttpReadData(hReq, chunk, sizeof(chunk), &dwRead) && dwRead)
		buf.insert(buf.end(), chunk, chunk + dwRead);

	printf("[DLC] FetchBytesFromUrl: read %zu bytes\n", buf.size());

	bool bOk = false;
	if (!buf.empty())
	{
		*ppData  = new BYTE[buf.size()];
		*pdwSize = (DWORD)buf.size();
		memcpy(*ppData, buf.data(), buf.size());
		bOk = true;
	}
	else
	{
		printf("[DLC] FetchBytesFromUrl: response body was empty\n");
	}

	WinHttpCloseHandle(hReq);
	WinHttpCloseHandle(hConn);
	WinHttpCloseHandle(hSession);
	return bOk;
}

static bool WinHttpGetUrl(const wchar_t* fullUrl, PBYTE* ppData, DWORD* pdwSize)
{
	*ppData  = nullptr;
	*pdwSize = 0;

	URL_COMPONENTS uc;
	ZeroMemory(&uc, sizeof(uc));
	uc.dwStructSize = sizeof(uc);

	wchar_t szHostName[256] = {};
	wchar_t szUrlPath[1024] = {};
	uc.lpszHostName     = szHostName;
	uc.dwHostNameLength = _countof(szHostName);
	uc.lpszUrlPath      = szUrlPath;
	uc.dwUrlPathLength  = _countof(szUrlPath);

	if (!WinHttpCrackUrl(fullUrl, 0, 0, &uc))
	{
		printf("[DLC] WinHttpGetUrl: WinHttpCrackUrl failed for '%ls': %u\n", fullUrl, GetLastError());
		return false;
	}

	HINTERNET hSession = WinHttpOpen(L"MC/1.0", WINHTTP_ACCESS_TYPE_NO_PROXY, NULL, NULL, 0);
	if (!hSession) return false;

	HINTERNET hConnect = WinHttpConnect(hSession, szHostName, uc.nPort, 0);
	if (!hConnect) { WinHttpCloseHandle(hSession); return false; }

	HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", szUrlPath,
		NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, 0);
	if (!hRequest)
	{
		WinHttpCloseHandle(hConnect);
		WinHttpCloseHandle(hSession);
		return false;
	}

	if (!WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
		WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
		!WinHttpReceiveResponse(hRequest, NULL))
	{
		WinHttpCloseHandle(hRequest);
		WinHttpCloseHandle(hConnect);
		WinHttpCloseHandle(hSession);
		return false;
	}

	std::string body;
	DWORD dwSize = 0;
	do {
		DWORD dwDownloaded = 0;
		if (!WinHttpQueryDataAvailable(hRequest, &dwSize) || dwSize == 0) break;
		char* buf = new char[dwSize];
		if (WinHttpReadData(hRequest, buf, dwSize, &dwDownloaded))
			body.append(buf, dwDownloaded);
		delete[] buf;
	} while (dwSize > 0);

	WinHttpCloseHandle(hRequest);
	WinHttpCloseHandle(hConnect);
	WinHttpCloseHandle(hSession);

	if (body.empty()) return false;

	*pdwSize = (DWORD)body.size();
	*ppData  = new BYTE[*pdwSize];
	memcpy(*ppData, body.data(), *pdwSize);
	return true;
}

bool Windows64_DLCOffers::FetchImageFromUrl(const wchar_t* url, PBYTE* ppData, DWORD* pdwSize)
{
	return WinHttpGetUrl(url, ppData, pdwSize);
}

void Windows64_DLCOffers::InstallOffer(int iIndex,
									   W64_INSTALL_CALLBACK pfnCallback,
									   void* pUserData, wstring username)
{
	if (iIndex < 0 || iIndex >= (int)m_offers.size()) return;

	InstallCtx* ctx  = new InstallCtx();
	ctx->offer       = m_offers[iIndex];
	ctx->offerIndex  = iIndex;
	ctx->pfnCallback = pfnCallback;
	ctx->pUserData   = pUserData;
	ctx->username	 = username;

	HANDLE hThread = CreateThread(nullptr, 0, InstallThreadProc, ctx, 0, nullptr);
	if (hThread) CloseHandle(hThread);
	else         delete ctx;
}

DWORD WINAPI Windows64_DLCOffers::InstallThreadProc(LPVOID lpParam)
{
	InstallCtx* ctx = reinterpret_cast<InstallCtx*>(lpParam);
	bool bSuccess   = false;

	// <exe dir>\Windows64Media\DLC\<productID>\folder.pck
	wchar_t wszExeDir[MAX_PATH] = {};
	GetModuleFileNameW(nullptr, wszExeDir, MAX_PATH);
	wchar_t* pLastSlash = wcsrchr(wszExeDir, L'\\');
	if (pLastSlash) *(pLastSlash + 1) = L'\0';

	wchar_t wszInstallDir[MAX_PATH];
	_snwprintf_s(wszInstallDir, _countof(wszInstallDir), _TRUNCATE,
		L"%lsWindows64Media\\DLC\\%ls",
		wszExeDir, ctx->offer.wszProductID);

	CreateDirectoryW(wszInstallDir, nullptr);

	wchar_t wszFilePath[MAX_PATH];
	_snwprintf_s(wszFilePath, _countof(wszFilePath), _TRUNCATE,
		L"%ls\\folder.pck", wszInstallDir);

	wchar_t wszDownloadUrl[512];
	_snwprintf_s(wszDownloadUrl, _countof(wszDownloadUrl), _TRUNCATE,
		L"http://127.0.0.1:3000/download/%ls", ctx->offer.wszProductID);

	PBYTE pData   = nullptr;
	DWORD dwBytes = 0;

	if (FetchBytesFromUrl(wszDownloadUrl, &pData, &dwBytes) && pData)
	{
		HANDLE hFile = CreateFileW(wszFilePath, GENERIC_WRITE, 0, nullptr,
			CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (hFile != INVALID_HANDLE_VALUE)
		{
			DWORD dwWritten = 0;
			WriteFile(hFile, pData, dwBytes, &dwWritten, nullptr);
			CloseHandle(hFile);
			bSuccess = (dwWritten == dwBytes);
		}
		delete[] pData;
	}

	if (bSuccess)
	{
		Windows64_DLCOffers::Get().SetPurchased(ctx->offerIndex);
		Windows64_DLCOffers::Get().SetPendingLoadPath(ctx->offerIndex, wszFilePath);
		InterlockedExchange(&Windows64_DLCOffers::Get().m_iLastInstalled, (LONG)ctx->offerIndex);


		// notify server so purchase persists across sessions
		wchar_t wszPath[256];
		_snwprintf_s(wszPath, _countof(wszPath), _TRUNCATE,
			L"/purchase/%ls?user=%ls",
			ctx->offer.wszProductID, ctx->username.c_str());

		HINTERNET hSession = WinHttpOpen(L"W64DLC/1.0", WINHTTP_ACCESS_TYPE_NO_PROXY, nullptr, nullptr, 0);
		if (hSession)
		{
			HINTERNET hConn = WinHttpConnect(hSession, L"127.0.0.1", 3000, 0);
			if (hConn)
			{
				HINTERNET hReq = WinHttpOpenRequest(hConn, L"POST", wszPath, nullptr,
					WINHTTP_NO_REFERER,
					WINHTTP_DEFAULT_ACCEPT_TYPES, 0);
				if (hReq)
				{
					WinHttpSendRequest(hReq, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
						WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
					WinHttpReceiveResponse(hReq, nullptr);
					WinHttpCloseHandle(hReq);
				}
				WinHttpCloseHandle(hConn);
			}
			WinHttpCloseHandle(hSession);
		}
	}
	else
	{
		printf("[DLC] Install FAILED for '%ls'\n", ctx->offer.wszProductID);
	}

	if (ctx->pfnCallback)
		ctx->pfnCallback(ctx->offer.wszProductID, bSuccess, ctx->pUserData);

	delete ctx;
	return 0;
}

void Windows64_DLCOffers::FetchFromServer()
{
	printf("[DLC] FetchFromServer called\n");
	m_offers.clear();
	m_bReady = false;

	HINTERNET hSession = WinHttpOpen(L"MC/1.0", WINHTTP_ACCESS_TYPE_NO_PROXY, NULL, NULL, 0);
	if (!hSession) return;

	HINTERNET hConnect = WinHttpConnect(hSession, L"localhost", 3000, 0);
	if (!hConnect) { WinHttpCloseHandle(hSession); return; }

	wchar_t wszDlcPath[128];
	_snwprintf_s(wszDlcPath, _countof(wszDlcPath), _TRUNCATE, L"/dlc?user=%ls", Minecraft::GetInstance()->user->name.c_str());

HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", wszDlcPath,
    NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, 0);
	if (!hRequest)
	{
		WinHttpCloseHandle(hConnect);
		WinHttpCloseHandle(hSession);
		return;
	}

	if (!WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
		WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
		!WinHttpReceiveResponse(hRequest, NULL))
	{
		WinHttpCloseHandle(hRequest);
		WinHttpCloseHandle(hConnect);
		WinHttpCloseHandle(hSession);
		return;
	}

	std::string body;
	DWORD dwSize = 0;
	do {
		DWORD dwDownloaded = 0;
		if (!WinHttpQueryDataAvailable(hRequest, &dwSize) || dwSize == 0) break;
		char* buf = new char[dwSize + 1];
		ZeroMemory(buf, dwSize + 1);
		if (WinHttpReadData(hRequest, buf, dwSize, &dwDownloaded))
			body.append(buf, dwDownloaded);
		delete[] buf;
	} while (dwSize > 0);

	WinHttpCloseHandle(hRequest);
	WinHttpCloseHandle(hConnect);
	WinHttpCloseHandle(hSession);

	if (body.empty()) { printf("[DLC] Empty /dlc response\n"); return; }

	printf("[DLC] /dlc response (%d bytes): %s\n", (int)body.size(), body.c_str());

	auto toWide = [](const std::string& s, wchar_t* dst, int maxChars) {
		MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, dst, maxChars);
	};

	size_t pos = 0;
	while ((pos = body.find('{', pos)) != std::string::npos)
	{
		W64_OFFER_INFO offer;
		ZeroMemory(&offer, sizeof(offer));

		auto readField = [&](const char* key) -> std::string {
			std::string search = std::string("\"") + key + "\"";
			size_t k = body.find(search, pos);
			if (k == std::string::npos) return "";
			size_t colon = body.find(':', k);
			if (colon == std::string::npos) return "";
			size_t q1 = body.find('"', colon);
			if (q1 == std::string::npos) return "";
			size_t q2 = body.find('"', q1 + 1);
			if (q2 == std::string::npos) return "";
			return body.substr(q1 + 1, q2 - q1 - 1);
		};

		auto readInt = [&](const char* key) -> int {
			std::string search = std::string("\"") + key + "\"";
			size_t k = body.find(search, pos);
			if (k == std::string::npos) return 0;
			size_t colon = body.find(':', k);
			if (colon == std::string::npos) return 0;
			size_t n = colon + 1;
			while (n < body.size() && (body[n] == ' ' || body[n] == '\t')) n++;
			return atoi(&body[n]);
		};

		toWide(readField("id"),          offer.wszProductID,     64);
		toWide(readField("name"),        offer.wszOfferName,    128);
		toWide(readField("description"), offer.wszSellText,     512);
		toWide(readField("price"),       offer.wszCurrencyPrice, 32);
		toWide(readField("type"),        offer.wszType,          32);
		toWide(readField("bannerUrl"),   offer.wszBannerUrl,    256);
		offer.fUserHasPurchased = readInt("purchased");
		offer.pbBannerData  = nullptr;
		offer.dwBannerBytes = 0;

		if (offer.wszProductID[0] != L'\0')
		{
			printf("[DLC] Parsed: id='%ls' type='%ls' bannerUrl='%ls'\n",
				offer.wszProductID, offer.wszType, offer.wszBannerUrl);
			m_offers.push_back(offer);
		}

		pos = body.find('}', pos);
		if (pos == std::string::npos) break;
		pos++;
	}

	printf("[DLC] FetchFromServer complete - %d offers\n", (int)m_offers.size());
	m_bReady = true;
}

void Windows64_DLCOffers::FetchBanners()
{
	for (int i = 0; i < (int)m_offers.size(); i++)
	{
		W64_OFFER_INFO& offer = m_offers[i];
		if (offer.wszBannerUrl[0] == L'\0') continue;
		if (offer.pbBannerData != nullptr) continue;

		PBYTE pbData  = nullptr;
		DWORD dwBytes = 0;

		if (FetchImageFromUrl(offer.wszBannerUrl, &pbData, &dwBytes))
		{
			offer.pbBannerData  = pbData;
			offer.dwBannerBytes = dwBytes;
			printf("[DLC] Banner fetched for '%ls' - %u bytes\n",
				offer.wszProductID, dwBytes);
		}
		else
		{
			printf("[DLC] Banner fetch FAILED for '%ls' url='%ls'\n",
				offer.wszProductID, offer.wszBannerUrl);
		}
	}

	// Register all offers with the app now that banners are populated.
	for (int i = 0; i < (int)m_offers.size(); i++)
	{
		const W64_OFFER_INFO& offer = m_offers[i];

		eDLCContentType eType = e_DLC_SkinPack;
		if      (wcscmp(offer.wszType, L"MashUp")      == 0) eType = e_DLC_MashupPacks;
		else if (wcscmp(offer.wszType, L"TexturePack")  == 0) eType = e_DLC_TexturePacks;
		else if (wcscmp(offer.wszType, L"SkinPack")     == 0) eType = e_DLC_SkinPack;

		printf("[DLC] Registering W64 DLC: '%ls' type=%d\n",
			offer.wszProductID, (int)eType);

		app.RegisterW64DLC(
			eType,
			offer.wszProductID,     // key AND banner texture name
			offer.wszProductID,     // banner texture name (used as lookup key in UpdateDisplay)
			0,                      // iConfig
			(unsigned int)i,        // uiSortIndex
			offer.pbBannerData,
			offer.dwBannerBytes
			);
	}
}
#endif