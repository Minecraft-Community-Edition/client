#include "stdafx.h"

#include "UI.h"
#include "UIScene_DLCOffersMenu.h"
#include "..\..\..\Minecraft.World\StringHelpers.h"

#if defined(__PS3__) || defined(__ORBIS__) || defined (__PSVITA__)
#include "Common\Network\Sony\SonyHttp.h"
#endif

#ifdef __PSVITA__
#include "PSVita\Network\SonyCommerce_Vita.h"
#endif

// Windows64 DLC store stuff blahhhh - whisper
#ifdef _WINDOWS64
#include "Windows64_DLCOffers.h"
#include "..\..\User.h"
#include "Windows64_Minecraft.h"
#include <unordered_map>
static std::unordered_map<int,int> s_offerIndexToListPos;
#endif

#define PLAYER_ONLINE_TIMER_ID   0
#define PLAYER_ONLINE_TIMER_TIME 100


UIScene_DLCOffersMenu::UIScene_DLCOffersMenu(int iPad, void *initData, UILayer *parentLayer)
    : UIScene(iPad, parentLayer)
{
    m_bProductInfoShown  = false;
    DLCOffersParam *param = (DLCOffersParam *)initData;
    m_iProductInfoIndex  = param->iType;
    m_iCurrentDLC        = 0;
    m_iTotalDLC          = 0;
#if defined(__PS3__) || defined(__ORBIS__) || defined (__PSVITA__)
    m_pvProductInfo = NULL;
#endif
    m_bAddAllDLCButtons = true;

    initialiseMovie();
    app.SetLiveLinkRequired(true);

    m_bIsSD = !RenderManager.IsHiDef() && !RenderManager.IsWidescreen();

    m_labelOffers.init(app.GetString(IDS_DOWNLOADABLE_CONTENT_OFFERS));
    m_buttonListOffers.init(eControl_OffersList);
    m_labelHTMLSellText.init(" ");
    m_labelPriceTag.init(" ");
    TelemetryManager->RecordMenuShown(m_iPad, eUIScene_DLCOffersMenu, 0);

    m_bHasPurchased = false;
    m_bIsSelected   = false;

    if (m_loadedResolution == eSceneResolution_1080)
    {
#ifdef _DURANGO
        m_labelXboxStore.init(app.GetString(IDS_XBOX_STORE));
#else
        m_labelXboxStore.init(L"");
#endif
    }

#ifdef _DURANGO
    m_pNoImageFor_DLC        = NULL;
    m_bDLCRequiredIsRetrieved = false;
    m_bIgnorePress           = true;
    m_bSelectionChanged      = true;
    m_Timer.setVisible(true);
#elif defined(_WINDOWS64)
    m_pNoImageFor_DLC        = NULL;
    m_bDLCRequiredIsRetrieved = false;
    m_bIgnorePress           = true;
    m_bSelectionChanged      = true;
    m_Timer.setVisible(true);
#endif

#ifdef __ORBIS__
    //sceNpCommerceShowPsStoreIcon(SCE_NP_COMMERCE_PS_STORE_ICON_CENTER);
#endif

#if defined(__PS3__) || defined(__ORBIS__) || defined (__PSVITA__)
    addTimer(PLAYER_ONLINE_TIMER_ID, PLAYER_ONLINE_TIMER_TIME);
#endif

#ifdef __PSVITA__
    ui.TouchBoxRebuild(this);
#endif
}

UIScene_DLCOffersMenu::~UIScene_DLCOffersMenu()
{
    app.SetLiveLinkRequired(false);
}


void UIScene_DLCOffersMenu::handleTimerComplete(int id)
{
#if defined(__PS3__) || defined(__ORBIS__) || defined(__PSVITA__)
    switch (id)
    {
    case PLAYER_ONLINE_TIMER_ID:
#ifndef _WINDOWS64
        if (ProfileManager.IsSignedInLive(ProfileManager.GetPrimaryPad()) == false)
        {
            unsigned int uiIDA[1];
            uiIDA[0] = IDS_OK;
            C4JStorage::EMessageResult result = ui.RequestMessageBox(
                IDS_CONNECTION_LOST,
                g_NetworkManager.CorrectErrorIDS(IDS_CONNECTION_LOST_LIVE_NO_EXIT),
                uiIDA, 1, ProfileManager.GetPrimaryPad(),
                UIScene_DLCOffersMenu::ExitDLCOffersMenu, this,
                app.GetStringTable());
        }
#endif
        break;
    }
#endif
}

int UIScene_DLCOffersMenu::ExitDLCOffersMenu(void *pParam, int iPad, C4JStorage::EMessageResult result)
{
    UIScene_DLCOffersMenu *pClass = (UIScene_DLCOffersMenu *)pParam;

#ifdef __ORBIS__
    sceNpCommerceHidePsStoreIcon();
#elif defined(__PSVITA__)
    sceNpCommerce2HidePsStoreIcon();
#endif
    ui.NavigateToHomeMenu();
    return 0;
}

wstring UIScene_DLCOffersMenu::getMoviePath()
{
    return L"DLCOffersMenu";
}

void UIScene_DLCOffersMenu::updateTooltips()
{
    int iA = -1;
    if (m_bIsSelected)
    {
        iA = m_bHasPurchased ? IDS_TOOLTIPS_REINSTALL : IDS_TOOLTIPS_INSTALL;
    }
    ui.SetTooltips(m_iPad, iA, IDS_TOOLTIPS_BACK);
}


void UIScene_DLCOffersMenu::handleInput(int iPad, int key, bool repeat, bool pressed, bool released, bool &handled)
{
    ui.AnimateKeyPress(m_iPad, key, repeat, pressed, released);

    switch (key)
    {
    case ACTION_MENU_CANCEL:
        if (pressed)
            navigateBack();
        break;

    case ACTION_MENU_OK:
#ifdef __ORBIS__
    case ACTION_MENU_TOUCHPAD_PRESS:
#endif
#ifdef _WINDOWS64
    if (pressed && !m_bIgnorePress && m_bDLCRequiredIsRetrieved)
    {
        int iIndex = getControlChildFocus();
        if (iIndex >= 0 && iIndex < Windows64_DLCOffers::Get().GetOfferCount())
        {
			Minecraft *pMinecraft = Minecraft::GetInstance();

            Windows64_DLCOffers::Get().InstallOffer(iIndex,
                [](const wchar_t* id, bool ok, void*) {
                    wprintf(L"[DLC] Install %ls: %ls\n", id, ok ? L"OK" : L"FAILED");
                }, nullptr, pMinecraft->user->name.c_str());
        }
    }
#endif
        sendInputToMovie(key, repeat, pressed, released);
        break;

    case ACTION_MENU_UP:
        if (pressed)
        {
            if (m_iTotalDLC > 0)
            {
                if (m_iCurrentDLC > 0)
                    m_iCurrentDLC--;
                m_bProductInfoShown = false;
            }
        }
        sendInputToMovie(key, repeat, pressed, released);
        break;

    case ACTION_MENU_DOWN:
        if (pressed)
        {
            if (m_iTotalDLC > 0)
            {
                if (m_iCurrentDLC < (m_iTotalDLC - 1))
                    m_iCurrentDLC++;
                m_bProductInfoShown = false;
            }
        }
        sendInputToMovie(key, repeat, pressed, released);
        break;

    case ACTION_MENU_LEFT:
    case ACTION_MENU_RIGHT:
    case ACTION_MENU_OTHER_STICK_DOWN:
    case ACTION_MENU_OTHER_STICK_UP:
        sendInputToMovie(key, repeat, pressed, released);
        break;
    }
}


void UIScene_DLCOffersMenu::handlePress(F64 controlId, F64 childId)
{
    switch ((int)controlId)
    {
    case eControl_OffersList:
    {
#if defined(__PS3__) || defined(__ORBIS__) || defined(__PSVITA__)
        vector<SonyCommerce::ProductInfo>::iterator it = m_pvProductInfo->begin();
        string teststring;
        for (int i = 0; i < childId; i++)
            it++;

        SonyCommerce::ProductInfo info = *it;

#ifdef __PS3__
        if (info.purchasabilityFlag == 1)
            app.Checkout(info.skuId);
        else if ((info.annotation & (SCE_NP_COMMERCE2_SKU_ANN_PURCHASED_CANNOT_PURCHASE_AGAIN |
                                      SCE_NP_COMMERCE2_SKU_ANN_PURCHASED_CAN_PURCHASE_AGAIN)) != 0)
            app.DownloadAlreadyPurchased(info.skuId);
#else // __ORBIS__
        if (info.purchasabilityFlag == SCE_TOOLKIT_NP_COMMERCE_NOT_PURCHASED)
            app.Checkout(info.skuId);
        else
            app.DownloadAlreadyPurchased(info.skuId);
#endif

#elif defined(_XBOX_ONE)
        int iIndex = (int)childId;
        StorageManager.InstallOffer(1, StorageManager.GetOffer(iIndex).wszProductID, NULL, NULL);

#elif defined(_WINDOWS64)
        int iIndex = (int)childId;
        if (!m_bIgnorePress && iIndex >= 0 &&
            iIndex < Windows64_DLCOffers::Get().GetOfferCount())
        {
            Windows64_DLCOffers::Get().InstallOffer(iIndex,
    [](const wchar_t* id, bool ok, void*) {
        wprintf(L"[DLC] Install %ls: %ls\n", id, ok ? L"OK" : L"FAILED");
    }, nullptr, Minecraft::GetInstance()->user->name.c_str());
        }

#else
        int iIndex = (int)childId;
        ULONGLONG ullIndexA[1];
        ullIndexA[0] = StorageManager.GetOffer(iIndex).qwOfferID;
        StorageManager.InstallOffer(1, ullIndexA, NULL, NULL);
#endif
    }
    break;
    }
}


void UIScene_DLCOffersMenu::handleSelectionChanged(F64 selectedId)
{
}

void UIScene_DLCOffersMenu::handleFocusChange(F64 controlId, F64 childId)
{
    app.DebugPrintf("UIScene_DLCOffersMenu::handleFocusChange\n");

#ifdef __PSVITA__
    if ((int)controlId == eControl_OffersList)
    {
        m_bProductInfoShown = false;
        m_iCurrentDLC = (int)childId;
    }
#endif

#if defined(_DURANGO) || defined(_WINDOWS64)
    m_bSelectionChanged = true;
#endif

#if defined(__PSVITA__) || defined(__ORBIS__)
    if (m_pvProductInfo)
    {
        m_bIsSelected = true;
        vector<SonyCommerce::ProductInfo>::iterator it = m_pvProductInfo->begin();
        for (int i = 0; i < childId; i++)
            it++;

        SonyCommerce::ProductInfo info = *it;
        m_bHasPurchased = (info.purchasabilityFlag != SCE_TOOLKIT_NP_COMMERCE_NOT_PURCHASED);
        updateTooltips();
    }
#endif
}


void UIScene_DLCOffersMenu::tick()
{
    UIScene::tick();

#if defined(__PS3__) || defined(__ORBIS__) || defined(__PSVITA__)

    if (m_bAddAllDLCButtons)
    {
        if (m_bProductInfoShown == false &&
            app.GetCommerceProductListRetrieved() &&
            app.GetCommerceProductListInfoRetrieved())
        {
            m_bAddAllDLCButtons = false;

            if (m_pvProductInfo == NULL)
            {
                m_pvProductInfo = app.GetProductList(m_iProductInfoIndex);
                if (m_pvProductInfo == NULL)
                {
                    m_iTotalDLC = 0;
                    m_labelOffers.setLabel(app.GetString(IDS_NO_DLCCATEGORIES));
                    m_bProductInfoShown = true;
                    return;
                }
                else
                {
                    m_iTotalDLC = (int)m_pvProductInfo->size();
                }
            }

            vector<SonyCommerce::ProductInfo>::iterator it = m_pvProductInfo->begin();
            string teststring;
            bool bFirstItemSet = false;

            for (int i = 0; i < m_iTotalDLC; i++)
            {
                SonyCommerce::ProductInfo info = *it;

                if (strncmp(info.productName, "Minecraft ", 10) == 0)
                    teststring = &info.productName[10];
                else
                    teststring = info.productName;

                bool bDLCIsAvailable = false;

#ifdef __PS3__
                if (info.purchasabilityFlag == 1)
                {
                    m_buttonListOffers.addItem(teststring, false, i);
                    bDLCIsAvailable = true;
                }
                else if ((info.annotation & (SCE_NP_COMMERCE2_SKU_ANN_PURCHASED_CANNOT_PURCHASE_AGAIN |
                                              SCE_NP_COMMERCE2_SKU_ANN_PURCHASED_CAN_PURCHASE_AGAIN)) != 0)
                {
                    m_buttonListOffers.addItem(teststring, true, i);
                    bDLCIsAvailable = true;
                }
#else // __ORBIS__
                if (info.purchasabilityFlag == SCE_TOOLKIT_NP_COMMERCE_NOT_PURCHASED)
                {
                    m_buttonListOffers.addItem(teststring, false, i);
                    bDLCIsAvailable = true;
                }
                else
                {
                    m_buttonListOffers.addItem(teststring, true, i);
                    bDLCIsAvailable = true;
                }
#endif

                if (bDLCIsAvailable && !bFirstItemSet)
                {
                    bFirstItemSet = true;

                    char chLongDescription[SCE_NP_COMMERCE2_PRODUCT_LONG_DESCRIPTION_LEN + 1];
                    memcpy(chLongDescription, info.longDescription, SCE_NP_COMMERCE2_PRODUCT_LONG_DESCRIPTION_LEN);
                    chLongDescription[SCE_NP_COMMERCE2_PRODUCT_LONG_DESCRIPTION_LEN] = 0;
                    m_labelHTMLSellText.setLabel(chLongDescription);

                    if (info.ui32Price == 0)
                        m_labelPriceTag.setLabel(app.GetString(IDS_DLC_PRICE_FREE));
                    else
                    {
                        teststring = info.price;
                        m_labelPriceTag.setLabel(teststring);
                    }

                    wstring textureName = filenametowstring(info.imageUrl);
                    if (!hasRegisteredSubstitutionTexture(textureName))
                    {
                        PBYTE pbImageData;
                        int   iImageDataBytes = 0;
                        bool  bDeleteData;
#ifdef __ORBIS__
                        SONYDLC *pSONYDLCInfo = app.GetSONYDLCInfoFromKeyname(info.productId);
                        if (pSONYDLCInfo->dwImageBytes != 0)
                        {
                            pbImageData    = pSONYDLCInfo->pbImageData;
                            iImageDataBytes = pSONYDLCInfo->dwImageBytes;
                            bDeleteData    = false;
                        }
                        else
#endif
                        if (info.imageUrl[0] != 0)
                        {
                            SonyHttp::getDataFromURL(info.imageUrl, (void **)&pbImageData, &iImageDataBytes);
                            bDeleteData = true;
                        }

                        if (iImageDataBytes != 0)
                        {
                            registerSubstitutionTexture(textureName, pbImageData, iImageDataBytes, bDeleteData);
                            m_bitmapIconOfferImage.setTextureName(textureName);
                        }
                        else
                        {
                            m_bitmapIconOfferImage.setTextureName(L"");
                        }
                    }
                    else
                    {
                        m_bitmapIconOfferImage.setTextureName(textureName);
                    }
                }
                it++;
            }

            if (!bFirstItemSet)
                m_labelOffers.setLabel(app.GetString(IDS_NO_DLCCATEGORIES));
            else if (m_pvProductInfo->size() > 0)
                m_buttonListOffers.setFocus(true);
            else
                m_labelOffers.setLabel(app.GetString(IDS_NO_DLCCATEGORIES));

            m_Timer.setVisible(false);
            m_bProductInfoShown = true;
        }
    }
    else
    {
#ifdef __PSVITA__
        if (SonyCommerce_Vita::getPurchasabilityUpdated() &&
            app.GetCommerceProductListRetrieved() &&
            app.GetCommerceProductListInfoRetrieved() &&
            m_iTotalDLC > 0)
        {
            vector<SonyCommerce::ProductInfo>::iterator it = m_pvProductInfo->begin();
            for (int i = 0; i < m_iTotalDLC; i++)
            {
                SonyCommerce::ProductInfo info = *it;
                m_buttonListOffers.showTick(i,
                    info.purchasabilityFlag != SCE_TOOLKIT_NP_COMMERCE_NOT_PURCHASED);
                it++;
            }
        }
#endif

        if (!m_bProductInfoShown &&
            app.GetCommerceProductListRetrieved() &&
            app.GetCommerceProductListInfoRetrieved() &&
            m_iTotalDLC > 0)
        {
            vector<SonyCommerce::ProductInfo>::iterator it = m_pvProductInfo->begin();
            string teststring;
            for (int i = 0; i < m_iCurrentDLC; i++)
                it++;

            SonyCommerce::ProductInfo info = *it;

            char chLongDescription[SCE_NP_COMMERCE2_PRODUCT_LONG_DESCRIPTION_LEN + 1];
            memcpy(chLongDescription, info.longDescription, SCE_NP_COMMERCE2_PRODUCT_LONG_DESCRIPTION_LEN);
            chLongDescription[SCE_NP_COMMERCE2_PRODUCT_LONG_DESCRIPTION_LEN] = 0;
            m_labelHTMLSellText.setLabel(chLongDescription);

            if (info.ui32Price == 0)
                m_labelPriceTag.setLabel(app.GetString(IDS_DLC_PRICE_FREE));
            else
            {
                teststring = info.price;
                m_labelPriceTag.setLabel(teststring);
            }

            wstring textureName = filenametowstring(info.imageUrl);
            if (!hasRegisteredSubstitutionTexture(textureName))
            {
                PBYTE pbImageData;
                int   iImageDataBytes = 0;
                bool  bDeleteData;
#ifdef __ORBIS__
                SONYDLC *pSONYDLCInfo = app.GetSONYDLCInfoFromKeyname(info.productId);
                if (pSONYDLCInfo->dwImageBytes != 0)
                {
                    pbImageData    = pSONYDLCInfo->pbImageData;
                    iImageDataBytes = pSONYDLCInfo->dwImageBytes;
                    bDeleteData    = false;
                }
                else
#endif
                {
                    SonyHttp::getDataFromURL(info.imageUrl, (void **)&pbImageData, &iImageDataBytes);
                    bDeleteData = true;
                }

                if (iImageDataBytes != 0)
                {
                    registerSubstitutionTexture(textureName, pbImageData, iImageDataBytes, bDeleteData);
                    m_bitmapIconOfferImage.setTextureName(textureName);
                }
                else
                {
                    m_bitmapIconOfferImage.setTextureName(L"");
                }
            }
            else
            {
                m_bitmapIconOfferImage.setTextureName(textureName);
            }

            m_bProductInfoShown = true;
            m_Timer.setVisible(false);
        }
    }

#elif defined(_XBOX_ONE) || defined(_WINDOWS64)

    if (m_bAddAllDLCButtons)
    {
        if (!m_bDLCRequiredIsRetrieved)
        {
            #ifdef _WINDOWS64
    bool bReady = Windows64_DLCOffers::Get().IsReady();
#else
    bool bReady = app.DLCContentRetrieved(e_Marketplace_Content);
#endif
    if (bReady)
    {
                m_bDLCRequiredIsRetrieved = true;

#ifdef _WINDOWS64
                GetDLCInfo(Windows64_DLCOffers::Get().GetOfferCount(), false);
#else
                GetDLCInfo(app.GetDLCOffersCount(), false);
#endif
                m_bIgnorePress      = false;
                m_bAddAllDLCButtons = false;
                m_Timer.setVisible(false);
            }
        }
    }

    if (m_bSelectionChanged && m_bDLCRequiredIsRetrieved)
    {
        if (m_buttonListOffers.hasFocus() && (getControlChildFocus() > -1))
        {
            int iIndex = getControlChildFocus();

            MARKETPLACE_CONTENTOFFER_INFO xOffer;
#ifdef _WINDOWS64
            memcpy(&xOffer, &Windows64_DLCOffers::Get().GetOffer(iIndex), sizeof(xOffer));
#else
            memcpy(&xOffer, &StorageManager.GetOffer(iIndex), sizeof(xOffer));
#endif
            if (UpdateDisplay(xOffer))
                m_bSelectionChanged = false;
        }
    }

#ifdef _WINDOWS64
    {
        int iInstalled = Windows64_DLCOffers::Get().PopLastInstalled();
        if (iInstalled >= 0)
        {
            auto it = s_offerIndexToListPos.find(iInstalled);
            if (it != s_offerIndexToListPos.end())
                m_buttonListOffers.showTick(it->second, true);

            MARKETPLACE_CONTENTOFFER_INFO xOffer;
            memcpy(&xOffer, &Windows64_DLCOffers::Get().GetOffer(iInstalled), sizeof(xOffer));
            UpdateDisplay(xOffer);

            const wchar_t* pszPath =
                Windows64_DLCOffers::Get().PopPendingLoadPath(iInstalled);

            if (pszPath && pszPath[0] != L'\0')
            {
                string narrowPath = wstringtofilename(wstring(pszPath));

                DLCPack* pack = new DLCPack(wstring(xOffer.wszOfferName),0);
                DWORD dwFilesProcessed = 0;

				bool bLoaded = app.m_dlcManager.readDLCDataFile(
                                   dwFilesProcessed, narrowPath, pack, false);

                if (!bLoaded || dwFilesProcessed == 0)
                {
                    printf("[DLC] tick: readDLCDataFile FAILED for '%s'\n",
                           narrowPath.c_str());
                    delete pack;
                }
                else
                {
                    app.m_dlcManager.addPack(pack);
                    printf("[DLC] tick: DLC pack '%ls' loaded live (%u files)\n",
                           xOffer.wszOfferName, dwFilesProcessed);
                }
            }
        }
    }
#endif

#endif
}


#if defined(_XBOX_ONE) || defined(_WINDOWS64)

void UIScene_DLCOffersMenu::GetDLCInfo(int iOfferC, bool bUpdateOnly)
{
    printf("[DLC] GetDLCInfo called: iOfferC=%d m_iProductInfoIndex=%d\n", iOfferC, m_iProductInfoIndex);
    
    MARKETPLACE_CONTENTOFFER_INFO xOffer;
    int  iCount       = 0;
    bool bNoDLCToDisplay = true;
    unsigned int uiDLCCount = 0;

    if (bUpdateOnly)
        return;

    m_buttonListOffers.clearList();

#ifdef _WINDOWS64
    s_offerIndexToListPos.clear();
#endif

    if (iOfferC == 0)
    {
        printf("[DLC] GetDLCInfo: iOfferC is 0, nothing to do\n");
        wstring wstrTemp = app.GetString(IDS_NO_DLCOFFERS);
        m_labelHTMLSellText.setLabel(wstrTemp);
        m_labelPriceTag.setVisible(false);
        return;
    }

    SORTINDEXSTRUCT *OrderA = new SORTINDEXSTRUCT[iOfferC];

    for (int i = 0; i < iOfferC; i++)
    {
#ifdef _WINDOWS64
        const W64_OFFER_INFO& w64Offer = Windows64_DLCOffers::Get().GetOffer(i);
DLC_INFO *pDLC = app.GetDLCInfoByProductID(w64Offer.wszProductID);
printf("[DLC] First pass [%d]: productID='%ls' pDLC=%p\n", i, w64Offer.wszProductID, pDLC);
#else
        memcpy(&xOffer, &StorageManager.GetOffer(i), sizeof(xOffer));
        DLC_INFO *pDLC = app.GetDLCInfoForFullOfferID(xOffer.wszProductID);
#endif

        if (pDLC != NULL)
        {
            printf("[DLC] First pass [%d]: matched, eDLCType=%d uiSortIndex=%d\n",
                i, pDLC->eDLCType, pDLC->uiSortIndex);
            OrderA[uiDLCCount].uiContentIndex = i;
            OrderA[uiDLCCount++].uiSortIndex  = pDLC->uiSortIndex;
        }
        else
        {
            printf("[DLC] First pass [%d]: NO MATCH for '%ls'\n", i, xOffer.wszOfferName);
        }
    }

    printf("[DLC] After first pass: uiDLCCount=%d\n", uiDLCCount);

    qsort(OrderA, uiDLCCount, sizeof(SORTINDEXSTRUCT), OrderSortFunction);

    for (int i = 0; i < (int)uiDLCCount; i++)
    {
#ifdef _WINDOWS64
        memcpy(&xOffer, &Windows64_DLCOffers::Get().GetOffer(OrderA[i].uiContentIndex), sizeof(xOffer));
        DLC_INFO *pDLC = app.GetDLCInfoByProductID(xOffer.wszProductID);
#else
        memcpy(&xOffer, &StorageManager.GetOffer(OrderA[i].uiContentIndex), sizeof(xOffer));
        DLC_INFO *pDLC = app.GetDLCInfoForFullOfferID(xOffer.wszProductID);
#endif

        if (pDLC == NULL)
        {
            printf("[DLC] Second pass [%d]: pDLC is NULL for '%ls'\n", i, xOffer.wszOfferName);
            continue;
        }

        printf("[DLC] Second pass [%d]: '%ls' eDLCType=%d vs m_iProductInfoIndex=%d\n",
            i, xOffer.wszOfferName, pDLC->eDLCType, m_iProductInfoIndex);

        if (pDLC->eDLCType == (eDLCContentType)m_iProductInfoIndex)
        {
            wstring wstrTemp = xOffer.wszOfferName;
            if (wcsncmp(L"Minecraft ", wstrTemp.c_str(), 10) == 0)
                wstrTemp = &((WCHAR *)wstrTemp.c_str())[10];

            printf("[DLC] Adding to list: '%ls'\n", wstrTemp.c_str());
            m_buttonListOffers.addItem(wstrTemp, xOffer.fUserHasPurchased != 0, OrderA[i].uiContentIndex);
            m_vIconRetrieval.push_back(pDLC->wchBanner);
#ifdef _WINDOWS64
            s_offerIndexToListPos[OrderA[i].uiContentIndex] = iCount;
#endif
            iCount++;
        }
    }

    printf("[DLC] Final iCount=%d bNoDLCToDisplay=%d\n", iCount, iCount == 0 ? 1 : 0);

    if (iCount > 0)
    {
        bNoDLCToDisplay = false;
#ifdef _WINDOWS64
        memcpy(&xOffer, &Windows64_DLCOffers::Get().GetOffer(OrderA[0].uiContentIndex), sizeof(xOffer));
#else
        memcpy(&xOffer, &StorageManager.GetOffer(OrderA[0].uiContentIndex), sizeof(xOffer));
#endif
        UpdateDisplay(xOffer);
    }

    delete[] OrderA;

    if (bNoDLCToDisplay)
    {
        wstring wstrTemp = app.GetString(IDS_NO_DLCOFFERS);
        m_labelHTMLSellText.setLabel(wstrTemp);
        m_labelPriceTag.setVisible(false);
    }
}


int UIScene_DLCOffersMenu::OrderSortFunction(const void *a, const void *b)
{
    return ((SORTINDEXSTRUCT *)b)->uiSortIndex - ((SORTINDEXSTRUCT *)a)->uiSortIndex;
}


void UIScene_DLCOffersMenu::UpdateTooltips(MARKETPLACE_CONTENTOFFER_INFO &xOffer)
{
    m_bHasPurchased = (xOffer.fUserHasPurchased != 0);
    m_bIsSelected   = true;
    updateTooltips();
}

bool UIScene_DLCOffersMenu::UpdateDisplay(MARKETPLACE_CONTENTOFFER_INFO &xOffer)
{
    bool bImageAvailable = false;

    DLC_INFO *dlc = NULL;

#ifdef _XBOX_ONE
    dlc = app.GetDLCInfoForFullOfferID(xOffer.wszProductID);
#elif defined(_WINDOWS64)
    dlc = app.GetDLCInfoByProductID(xOffer.wszProductID);
#else
    dlc = app.GetDLCInfoForFullOfferID(xOffer.wszOfferName);
#endif

    if (dlc != NULL)
    {
        WCHAR *cString = dlc->wchBanner;

        if (dlc->dwImageBytes != 0)
        {
            registerSubstitutionTexture(cString, dlc->pbImageData, dlc->dwImageBytes, false);
            m_bitmapIconOfferImage.setTextureName(cString);
            bImageAvailable = true;
        }
        else
        {
            bool bPresent = app.IsFileInMemoryTextures(cString);
            if (!bPresent)
            {
                m_pNoImageFor_DLC = dlc;
                app.AddTMSPPFileTypeRequest(dlc->eDLCType, true);
                bImageAvailable = false;
            }
            else
            {
                if (!hasRegisteredSubstitutionTexture(cString))
                {
                    BYTE *pData  = NULL;
                    DWORD dwSize = 0;
                    app.GetMemFileDetails(cString, &pData, &dwSize);
                    registerSubstitutionTexture(cString, pData, dwSize);
                    m_bitmapIconOfferImage.setTextureName(cString);
                }
                else
                {
                    m_bitmapIconOfferImage.setTextureName(cString);
                }
                bImageAvailable = true;
            }
        }

        m_labelHTMLSellText.setLabel(xOffer.wszSellText);
        m_labelPriceTag.setVisible(true);
        m_labelPriceTag.setLabel(xOffer.wszCurrencyPrice);

        UpdateTooltips(xOffer);
    }
    else
    {
        wstring wstrTemp = app.GetString(IDS_NO_DLCOFFERS);
        m_labelHTMLSellText.setLabel(wstrTemp.c_str());
        m_labelPriceTag.setVisible(false);
    }

    return bImageAvailable;
}


void UIScene_DLCOffersMenu::HandleDLCLicenseChange()
{
#ifdef _WINDOWS64
    int iOfferC = Windows64_DLCOffers::Get().GetOfferCount();
#else
    int iOfferC = app.GetDLCOffersCount();
#endif
    GetDLCInfo(iOfferC, false);
}

#endif


#ifdef __PS3__
void UIScene_DLCOffersMenu::HandleDLCInstalled()
{
    app.DebugPrintf(4, "UIScene_DLCOffersMenu::HandleDLCInstalled\n");
}
#endif