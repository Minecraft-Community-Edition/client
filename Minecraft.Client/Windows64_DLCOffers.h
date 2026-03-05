#pragma once
#ifdef _WINDOWS64
#include <vector>
#include <string>
#include <windows.h>
#include <winhttp.h>

#define MAX_W64_DLC_OFFERS 64

struct W64_OFFER_INFO
{
    wchar_t wszOfferName    [128];
    wchar_t wszProductID    [64];
    wchar_t wszSellText     [512];
    wchar_t wszCurrencyPrice[32];
    wchar_t wszType         [32];       // "SkinPack" | "TexturePack" | "MashUp"
    wchar_t wszBannerUrl    [256];
    int     fUserHasPurchased;
    PBYTE   pbBannerData;
    DWORD   dwBannerBytes;
};

typedef void (*W64_INSTALL_CALLBACK)(const wchar_t* wszProductID, bool bSuccess, void* pUserData);

class Windows64_DLCOffers
{
public:
    static Windows64_DLCOffers& Get()
    {
        static Windows64_DLCOffers instance;
        return instance;
    }

    void FetchFromServer();
    void FetchBanners();

    bool            IsReady()        { return m_bReady; }
    int             GetOfferCount()  { return (int)m_offers.size(); }
    W64_OFFER_INFO& GetOffer(int i)  { return m_offers[i]; }

    void SetPurchased(int i)
    {
        if (i >= 0 && i < (int)m_offers.size())
            m_offers[i].fUserHasPurchased = 1;
    }

    int PopLastInstalled()
    {
        return (int)InterlockedExchange(&m_iLastInstalled, (LONG)-1);
    }

    void SetPendingLoadPath(int idx, const wchar_t* path)
    {
        if (idx < 0 || idx >= MAX_W64_DLC_OFFERS) return;
        wcsncpy_s(m_pendingLoads[idx].wszPath, path, _TRUNCATE);
        MemoryBarrier();
        m_pendingLoads[idx].bReady = true;
    }

    const wchar_t* PopPendingLoadPath(int idx)
    {
        if (idx < 0 || idx >= MAX_W64_DLC_OFFERS) return nullptr;
        if (!m_pendingLoads[idx].bReady) return nullptr;
        m_pendingLoads[idx].bReady = false;
        return m_pendingLoads[idx].wszPath;
    }

    void InstallOffer(int iIndex,
                      W64_INSTALL_CALLBACK pfnCallback = nullptr,
                      void* pUserData = nullptr, wstring username = L"");

    static bool FetchImageFromUrl(const wchar_t* url, PBYTE* ppData, DWORD* pdwSize);
    static bool FetchBytesFromUrl(const wchar_t* url, PBYTE* ppData, DWORD* pdwSize);

private:
    Windows64_DLCOffers() : m_bReady(false), m_iLastInstalled(-1)
    {
        ZeroMemory(m_pendingLoads, sizeof(m_pendingLoads));
    }

    std::vector<W64_OFFER_INFO> m_offers;
    bool          m_bReady;
    volatile LONG m_iLastInstalled;

    struct PendingLoad
    {
        wchar_t wszPath[MAX_PATH];
        bool    bReady;
    };
    PendingLoad m_pendingLoads[MAX_W64_DLC_OFFERS];

    struct InstallCtx
    {
        W64_OFFER_INFO       offer;
        int                  offerIndex;
        W64_INSTALL_CALLBACK pfnCallback;
        void*                pUserData;
		wstring			     username;
    };

    static DWORD WINAPI InstallThreadProc(LPVOID lpParam);
};

#endif