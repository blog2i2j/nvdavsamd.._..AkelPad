#include "SmartSel.h"

//Include string functions
#define xstrlenW
#define xstrcpynW
#define xatoiW
#define xitoaW
#define xuitoaW
#define dec2hexW
#define xprintfW
#include "StrFunc.h"

//Include wide functions
#define DialogBoxWide
#define SetDlgItemTextWide
#define SetWindowTextWide
#include "WideFunc.h"

//Include AEC functions
#define AEC_FUNCTIONS
#include "AkelEdit.h"

/* >>>>>>>>>>>>>>>>>>>>>>>> selection state >>>>>>>>>>>>>>>>>>>>>>>> */
typedef struct tSelectionState {
    BOOL        bMouseLButtonDown;
    BOOL        bShiftDown;
    BOOL        bColumnSelection;
    BOOL        bSpecialAction;
    CHARRANGE64 SelKeyDownRange;
} SelectionState;

void clearSelectionState(SelectionState* pSelState)
{
    pSelState->bMouseLButtonDown = FALSE;
    pSelState->bShiftDown = FALSE;
    pSelState->bColumnSelection = FALSE;
    pSelState->bSpecialAction = FALSE;
    pSelState->SelKeyDownRange.cpMin = 0;
    pSelState->SelKeyDownRange.cpMax = 0;
}

void initializeSelectionState(SelectionState* pSelState)
{
    clearSelectionState(pSelState);
}
/* <<<<<<<<<<<<<<<<<<<<<<<< selection state <<<<<<<<<<<<<<<<<<<<<<<< */


/* >>>>>>>>>>>>>>>>>>>>>>>> plugin state >>>>>>>>>>>>>>>>>>>>>>>> */
typedef struct tPluginState {
    DWORD        dwInitializedMode;
    HWND         hMainWnd;
    BOOL         bOldWindows;
    BOOL         bOldRichEdit; // TRUE means Rich Edit 2.0
    BOOL         nMDI;
    BOOL         bAkelEdit;
    WNDPROCDATA* pMainProcData;
    WNDPROCDATA* pEditProcData;
    WNDPROCDATA* pFrameProcData;
} PluginState;

void initializePluginState(PluginState* pPlugin)
{
    pPlugin->dwInitializedMode = 0;
    pPlugin->bAkelEdit = FALSE;
    pPlugin->hMainWnd = NULL;
    pPlugin->pMainProcData = NULL;
    pPlugin->pEditProcData = NULL;
    pPlugin->pFrameProcData = NULL;
}
/* <<<<<<<<<<<<<<<<<<<<<<<< plugin state <<<<<<<<<<<<<<<<<<<<<<<< */


/* >>>>>>>>>>>>>>>>>>>>>>>> data buffer >>>>>>>>>>>>>>>>>>>>>>>> */
typedef struct tDataBuffer {
    void* pData;      // pointer to the data
    int   nDataSize;  // data size in bytes
} DataBuffer;

void bufInitialize(DataBuffer* pBuf)
{
    pBuf->pData = NULL;
    pBuf->nDataSize = 0;
}

void bufFree(DataBuffer* pBuf)
{
    if ( pBuf->pData )
    {
        GlobalFree( (HGLOBAL) pBuf->pData );
        pBuf->pData = NULL;
    }
    pBuf->nDataSize = 0;
}

void bufFreeIfSizeExceeds(DataBuffer* pBuf, int nMaxDataSizeLimit)
{
    if ( pBuf->pData )
    {
        if ( pBuf->nDataSize > nMaxDataSizeLimit )
        {
            GlobalFree( (HGLOBAL) pBuf->pData );
            pBuf->pData = NULL;
            pBuf->nDataSize = 0;
        }
    }
    else
    {
        pBuf->nDataSize = 0;
    }
}

BOOL bufReserve(DataBuffer* pBuf, int nBytesToReserve)
{
    if ( pBuf->nDataSize < nBytesToReserve )
    {
        bufFree(pBuf);

        nBytesToReserve = (1 + nBytesToReserve/64)*64;

        if ( pBuf->pData = GlobalAlloc(GMEM_FIXED | GMEM_ZEROINIT, nBytesToReserve) )
            pBuf->nDataSize = nBytesToReserve;
        else
            return FALSE;
    }
    return TRUE;
}
/* <<<<<<<<<<<<<<<<<<<<<<<< data buffer <<<<<<<<<<<<<<<<<<<<<<<< */


//Defines
#define STRID_JUMPBETWEENSPACES 1
#define STRID_INVERT            2
#define STRID_CONTINUEWRAP      3
#define STRID_SKIPWRAP          4
#define STRID_SMARTPAGEUPDOWN   5
#define STRID_SINGLELINE        6
#define STRID_PLUGIN            7
#define STRID_OK                8
#define STRID_CANCEL            9

#define SMARTHOME_DEFAULT              0x000
#define SMARTHOME_JUMPINVERT           0x001
#define SMARTHOME_CONTINUEWRAP         0x002
#define SMARTHOME_SKIPWRAP             0x004
#define SMARTHOME_NOJUMPBETWEENSPACES  0x100

#define SMARTEND_DEFAULT              0x000
#define SMARTEND_JUMPINVERT           0x001
#define SMARTEND_CONTINUEWRAP         0x002
#define SMARTEND_SKIPWRAP             0x004
#define SMARTEND_NOJUMPBETWEENSPACES  0x100

#define SMARTUPDOWN_DEFAULT  0x0
#define SMARTUPDOWN_PAGE     0x1

#define NOSELEOL_DEFAULT     0x0
#define NOSELEOL_SINGLELINE  0x1

#define MODE_SMARTHOME       0x01
#define MODE_SMARTEND        0x02
#define MODE_SMARTUPDOWN     0x04
#define MODE_SMARTBACKSPACE  0x08
#define MODE_NOSELEOL        0x10

#define MODE_ALL          (MODE_NOSELEOL | MODE_SMARTEND | MODE_SMARTHOME | MODE_SMARTUPDOWN | MODE_SMARTBACKSPACE)

#define MAX_TEXTBUF_SIZE  (16*1024)

// consts
const char* cszPluginName = "SmartSel Plugin";
const char* cszUnloadPluginQuestion = "Plugin is active. Unload it?";

// vars
PluginState    g_Plugin;
SelectionState g_SelState;
DataBuffer     g_TextBuf;

//Global variables
wchar_t wszPluginName[MAX_PATH];
wchar_t wszPluginTitle[MAX_PATH];
HWND hMainWnd;
HINSTANCE hInstanceDLL;
LANGID wLangModule;
BOOL bInitCommon=FALSE;
DWORD dwSettings=0;
DWORD g_dwSmartHomeSettings=SMARTHOME_DEFAULT;
DWORD g_dwSmartEndSettings=SMARTEND_DEFAULT;
DWORD g_dwSmartUpDownSettings=SMARTUPDOWN_DEFAULT;
DWORD g_dwNoSelEOLSettings=NOSELEOL_DEFAULT;

// funcs
void InitCommon(PLUGINDATA *pd);
void Initialize(PLUGINDATA* pd, DWORD dwMode);
void Uninitialize(DWORD dwMode);
BOOL CALLBACK SetupDlgProc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK NewEditProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK NewFrameProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK NewMainProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
void OnEditSelKeyDown(HWND hEdit, unsigned int uKey);
void OnEditSelKeyUp(HWND hEdit, unsigned int uKey);
void OnEditSelectionChanged(HWND hEdit, CHARRANGE64* cr);
BOOL OnEditEndKeyDown(HWND hEdit, LPARAM lParam);
BOOL OnEditBackspaceKeyDown(HWND hEdit, LPARAM lParam);
BOOL OnEditHomeKeyDown(HWND hEdit, LPARAM lParam);
BOOL OnEditArrowDownOrUpKeyDown(HWND hEdit, WPARAM wKey);
BOOL SmartHomeA(HWND hEdit);
BOOL SmartHomeW(HWND hEdit);
BOOL GetLineSpaces(AECHARINDEX *ciFirstCharInLine, int nWrapCharInLine, int nTabStopSize, int *lpnLineSpaces);
void CheckEditNotification(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

INT_PTR WideOption(HANDLE hOptions, const wchar_t *pOptionName, DWORD dwType, BYTE *lpData, DWORD dwData);
void ReadOptions(DWORD dwFlags);
void SaveOptions(DWORD dwFlags);
const char* GetLangStringA(LANGID wLangID, int nStringID);
const wchar_t* GetLangStringW(LANGID wLangID, int nStringID);


// Identification
/* extern "C" */
void __declspec(dllexport) DllAkelPadID(PLUGINVERSION *pv)
{
    pv->dwAkelDllVersion  = AKELDLL;
    pv->dwExeMinVersion3x = MAKE_IDENTIFIER(-1, -1, -1, -1);
    pv->dwExeMinVersion4x = MAKE_IDENTIFIER(4, 9, 7, 0);
    pv->pPluginName = "SmartSel";
}

// Plugin extern function
/* extern "C" */
void __declspec(dllexport) NoSelEOL(PLUGINDATA *pd)
{
    pd->dwSupport |= PDS_SUPPORTALL;
    if ( pd->dwSupport & PDS_GETSUPPORT )
        return;

    // Is plugin already loaded?
    if ( g_Plugin.dwInitializedMode & MODE_NOSELEOL )
    {
        Uninitialize(MODE_NOSELEOL);
        pd->nUnload = UD_NONUNLOAD_NONACTIVE;
        return;
    }
    else
    {
        Initialize(pd, MODE_NOSELEOL);
    }

    // Stay in memory, and show as active
    pd->nUnload = UD_NONUNLOAD_ACTIVE;
}

// Plugin extern function
/* extern "C" */
void __declspec(dllexport) SmartBackspace(PLUGINDATA *pd)
{
    pd->dwSupport |= PDS_SUPPORTALL;
    if ( pd->dwSupport & PDS_GETSUPPORT )
        return;

    // Is plugin already loaded?
    if ( g_Plugin.dwInitializedMode & MODE_SMARTBACKSPACE )
    {
        Uninitialize(MODE_SMARTBACKSPACE);
        pd->nUnload = UD_NONUNLOAD_NONACTIVE;
        return;
    }
    else
    {
        Initialize(pd, MODE_SMARTBACKSPACE);
    }

    // Stay in memory, and show as active
    pd->nUnload = UD_NONUNLOAD_ACTIVE;
}

// Plugin extern function
/* extern "C" */
void __declspec(dllexport) SmartHome(PLUGINDATA *pd)
{
    pd->dwSupport |= PDS_SUPPORTALL;
    if ( pd->dwSupport & PDS_GETSUPPORT )
        return;

    // Is plugin already loaded?
    if ( g_Plugin.dwInitializedMode & MODE_SMARTHOME )
    {
        Uninitialize(MODE_SMARTHOME);
        pd->nUnload = UD_NONUNLOAD_NONACTIVE;
        return;
    }
    else
    {
        Initialize(pd, MODE_SMARTHOME);
    }

    // Stay in memory, and show as active
    pd->nUnload = UD_NONUNLOAD_ACTIVE;
}

// Plugin extern function
/* extern "C" */
void __declspec(dllexport) SmartEnd(PLUGINDATA *pd)
{
    pd->dwSupport |= PDS_SUPPORTALL;
    if ( pd->dwSupport & PDS_GETSUPPORT )
        return;

    // Is plugin already loaded?
    if ( g_Plugin.dwInitializedMode & MODE_SMARTEND )
    {
        Uninitialize(MODE_SMARTEND);
        pd->nUnload = UD_NONUNLOAD_NONACTIVE;
        return;
    }
    else
    {
        Initialize(pd, MODE_SMARTEND);
    }

    // Stay in memory, and show as active
    pd->nUnload = UD_NONUNLOAD_ACTIVE;
}

// Plugin extern function
/* extern "C" */
void __declspec(dllexport) SmartUpDown(PLUGINDATA *pd)
{
    pd->dwSupport |= PDS_SUPPORTALL;
    if ( pd->dwSupport & PDS_GETSUPPORT )
        return;

    // Is plugin already loaded?
    if ( g_Plugin.dwInitializedMode & MODE_SMARTUPDOWN )
    {
        Uninitialize(MODE_SMARTUPDOWN);
        pd->nUnload = UD_NONUNLOAD_NONACTIVE;
        return;
    }
    else
    {
        Initialize(pd, MODE_SMARTUPDOWN);
    }

    // Stay in memory, and show as active
    pd->nUnload = UD_NONUNLOAD_ACTIVE;
}

void __declspec(dllexport) Settings(PLUGINDATA *pd)
{
  pd->dwSupport|=PDS_NOAUTOLOAD;
  if (pd->dwSupport & PDS_GETSUPPORT)
    return;

  if (!bInitCommon) InitCommon(pd);

  DialogBoxWide(hInstanceDLL, MAKEINTRESOURCEW(IDD_SETUP), hMainWnd, (DLGPROC)SetupDlgProc);

  //If plugin already loaded, stay in memory, but show as non-active
  if (pd->bInMemory) pd->nUnload=UD_NONUNLOAD_NONACTIVE;
}

// Entry point
/* extern "C" */
BOOL WINAPI DllMain(HINSTANCE hInstDLL, DWORD dwReason, LPVOID lpReserved)
{
    switch ( dwReason )
    {
        case DLL_PROCESS_ATTACH:
            initializePluginState(&g_Plugin);
            initializeSelectionState(&g_SelState);
            bufInitialize(&g_TextBuf);
            break;
        case DLL_PROCESS_DETACH:
            Uninitialize(MODE_ALL);
            break;
        case DLL_THREAD_ATTACH:
            break;
        case DLL_THREAD_DETACH:
            break;
        default:
            break;
    }
    return TRUE;
}

void InitCommon(PLUGINDATA *pd)
{
  bInitCommon=TRUE;
  hInstanceDLL=pd->hInstanceDLL;
  hMainWnd=pd->hMainWnd;
  wLangModule=PRIMARYLANGID(pd->wLangModule);

  //Initialize WideFunc.h header
  WideInitialize();

  //Plugin name
  {
    int i;

    for (i=0; pd->wszFunction[i] != L':'; ++i)
      wszPluginName[i]=pd->wszFunction[i];
    wszPluginName[i]=L'\0';
  }
  xprintfW(wszPluginTitle, GetLangStringW(wLangModule, STRID_PLUGIN), wszPluginName);
  ReadOptions(0);
}

void Initialize(PLUGINDATA* pd, DWORD dwMode)
{
    g_Plugin.hMainWnd     = pd->hMainWnd;
    g_Plugin.bOldWindows  = pd->bOldWindows;
    g_Plugin.bOldRichEdit = pd->bOldRichEdit;
    g_Plugin.nMDI         = pd->nMDI;
    g_Plugin.bAkelEdit    = pd->bAkelEdit;

    // dwMode mask (depends on possible mode flags)
    dwMode &= (MODE_ALL);

    if ( dwMode & MODE_NOSELEOL )
    {
        // this SubClassing is required
        // only for MODE_NOSELEOL
        if ( (g_Plugin.dwInitializedMode & MODE_NOSELEOL) == 0 )
        {
            clearSelectionState(&g_SelState);

            // SubClass main window
            g_Plugin.pMainProcData = NULL;
            SendMessage( g_Plugin.hMainWnd, AKD_SETMAINPROC,
                (WPARAM) NewMainProc, (LPARAM) &g_Plugin.pMainProcData );

            if ( g_Plugin.nMDI )
            {
                // SubClass frame window
                g_Plugin.pFrameProcData = NULL;
                SendMessage( g_Plugin.hMainWnd, AKD_SETFRAMEPROC,
                    (WPARAM) NewFrameProc, (LPARAM) &g_Plugin.pFrameProcData );
            }
        }
    }

    if ( dwMode )
    {
        // this SubClassing is required
        // for all MODE_NOSELEOL, MODE_SMARTEND, MODE_SMARTHOME and MODE_SMARTUPDOWN
        if ( !g_Plugin.dwInitializedMode )
        {
            // SubClass edit window
            g_Plugin.pEditProcData = NULL;
            SendMessage( g_Plugin.hMainWnd, AKD_SETEDITPROC,
                (WPARAM) NewEditProc, (LPARAM) &g_Plugin.pEditProcData );
        }
    }

    g_Plugin.dwInitializedMode |= dwMode;

    if (!bInitCommon) InitCommon(pd);
}

void Uninitialize(DWORD dwMode)
{
    if ( !g_Plugin.dwInitializedMode )
        return;

    // dwMode mask (depends on current value of dwInitializedMode)
    dwMode &= g_Plugin.dwInitializedMode;

    if ( dwMode & MODE_NOSELEOL )
    {
        // these functions were subclassed
        // only for MODE_NOSELEOL
        if ( g_Plugin.dwInitializedMode & MODE_NOSELEOL )
        {
            if ( g_Plugin.pMainProcData )
            {
                // Remove subclass (main window)
                SendMessage( g_Plugin.hMainWnd, AKD_SETMAINPROC,
                    (WPARAM) NULL, (LPARAM) &g_Plugin.pMainProcData );
                g_Plugin.pMainProcData = NULL;
            }
            if ( g_Plugin.nMDI && g_Plugin.pFrameProcData )
            {
                // Remove subclass (frame window)
                SendMessage( g_Plugin.hMainWnd, AKD_SETFRAMEPROC,
                    (WPARAM) NULL, (LPARAM) &g_Plugin.pFrameProcData );
                g_Plugin.pFrameProcData = NULL;
            }
        }
    }

    if ( dwMode )
    {
        // this function was subclassed
        // for all MODE_NOSELEOL, MODE_SMARTEND, MODE_SMARTHOME and MODE_SMARTUPDOWN
        if ( g_Plugin.dwInitializedMode == dwMode ) // dwMode is already masked
        {
            if ( g_Plugin.pEditProcData )
            {
                // Remove subclass (edit window)
                SendMessage( g_Plugin.hMainWnd, AKD_SETEDITPROC,
                    (WPARAM) NULL, (LPARAM) &g_Plugin.pEditProcData );
                g_Plugin.pEditProcData = NULL;
            }
        }
    }

    g_Plugin.dwInitializedMode -= dwMode;

    if ( !(g_Plugin.dwInitializedMode & MODE_SMARTEND) &&
         !(g_Plugin.dwInitializedMode & MODE_SMARTHOME) &&
         !(g_Plugin.dwInitializedMode & MODE_SMARTBACKSPACE) )
    {
        bufFree(&g_TextBuf);
    }

}

BOOL CALLBACK SetupDlgProc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
  static HICON hPluginIcon;
  static HWND hWndSmartHomeGroup;
  static HWND hWndSmartHomeJumpBetweenSpacesEnable;
  static HWND hWndSmartHomeJumpInvertEnable;
  static HWND hWndSmartHomeContinueWrapEnable;
  static HWND hWndSmartHomeSkipWrapEnable;
  static HWND hWndSmartEndGroup;
  static HWND hWndSmartEndJumpBetweenSpacesEnable;
  static HWND hWndSmartEndJumpInvertEnable;
  static HWND hWndSmartEndContinueWrapEnable;
  static HWND hWndSmartEndSkipWrapEnable;
  static HWND hWndSmartUpDownGroup;
  static HWND hWndSmartPageUpDownEnable;
  static HWND hWndNoSelEOLGroup;
  static HWND hWndNoSelEOLSingleLineEnable;

  if (uMsg == WM_INITDIALOG)
  {
    //Load plugin icon
    hPluginIcon=LoadIconA(hInstanceDLL, MAKEINTRESOURCEA(IDI_ICON_PLUGIN));
    SendMessage(hDlg, WM_SETICON, (WPARAM)ICON_BIG, (LPARAM)hPluginIcon);

    hWndSmartHomeGroup=GetDlgItem(hDlg, IDC_SMARTHOME_GROUP);
    hWndSmartHomeJumpBetweenSpacesEnable=GetDlgItem(hDlg, IDC_SMARTHOME_JUMPBETWEENSPACES_ENABLE);
    hWndSmartHomeJumpInvertEnable=GetDlgItem(hDlg, IDC_SMARTHOME_JUMPINVERT_ENABLE);
    hWndSmartHomeContinueWrapEnable=GetDlgItem(hDlg, IDC_SMARTHOME_CONTINUEWRAP_ENABLE);
    hWndSmartHomeSkipWrapEnable=GetDlgItem(hDlg, IDC_SMARTHOME_SKIPWRAP_ENABLE);
    hWndSmartEndGroup=GetDlgItem(hDlg, IDC_SMARTEND_GROUP);
    hWndSmartEndJumpBetweenSpacesEnable=GetDlgItem(hDlg, IDC_SMARTEND_JUMPBETWEENSPACES_ENABLE);
    hWndSmartEndJumpInvertEnable=GetDlgItem(hDlg, IDC_SMARTEND_JUMPINVERT_ENABLE);
    hWndSmartEndContinueWrapEnable=GetDlgItem(hDlg, IDC_SMARTEND_CONTINUEWRAP_ENABLE);
    hWndSmartEndSkipWrapEnable=GetDlgItem(hDlg, IDC_SMARTEND_SKIPWRAP_ENABLE);
    hWndSmartUpDownGroup=GetDlgItem(hDlg, IDC_SMARTUPDOWN_GROUP);
    hWndSmartPageUpDownEnable=GetDlgItem(hDlg, IDC_SMARTPAGEUPDOWN_ENABLE);
    hWndNoSelEOLGroup=GetDlgItem(hDlg, IDC_NOSELEOL_GROUP);
    hWndNoSelEOLSingleLineEnable=GetDlgItem(hDlg, IDC_NOSELEOLSINGLE_ENABLE);

    SetWindowTextWide(hDlg, wszPluginTitle);
    SetDlgItemTextWide(hDlg, IDC_SMARTHOME_JUMPBETWEENSPACES_ENABLE, GetLangStringW(wLangModule, STRID_JUMPBETWEENSPACES));
    SetDlgItemTextWide(hDlg, IDC_SMARTHOME_JUMPINVERT_ENABLE, GetLangStringW(wLangModule, STRID_INVERT));
    SetDlgItemTextWide(hDlg, IDC_SMARTHOME_CONTINUEWRAP_ENABLE, GetLangStringW(wLangModule, STRID_CONTINUEWRAP));
    SetDlgItemTextWide(hDlg, IDC_SMARTHOME_SKIPWRAP_ENABLE, GetLangStringW(wLangModule, STRID_SKIPWRAP));
    SetDlgItemTextWide(hDlg, IDC_SMARTEND_JUMPBETWEENSPACES_ENABLE, GetLangStringW(wLangModule, STRID_JUMPBETWEENSPACES));
    SetDlgItemTextWide(hDlg, IDC_SMARTEND_JUMPINVERT_ENABLE, GetLangStringW(wLangModule, STRID_INVERT));
    SetDlgItemTextWide(hDlg, IDC_SMARTEND_CONTINUEWRAP_ENABLE, GetLangStringW(wLangModule, STRID_CONTINUEWRAP));
    SetDlgItemTextWide(hDlg, IDC_SMARTEND_SKIPWRAP_ENABLE, GetLangStringW(wLangModule, STRID_SKIPWRAP));
    SetDlgItemTextWide(hDlg, IDC_SMARTPAGEUPDOWN_ENABLE, GetLangStringW(wLangModule, STRID_SMARTPAGEUPDOWN));
    SetDlgItemTextWide(hDlg, IDC_NOSELEOLSINGLE_ENABLE, GetLangStringW(wLangModule, STRID_SINGLELINE));
    SetDlgItemTextWide(hDlg, IDOK, GetLangStringW(wLangModule, STRID_OK));
    SetDlgItemTextWide(hDlg, IDCANCEL, GetLangStringW(wLangModule, STRID_CANCEL));

    EnableWindow(hWndSmartEndGroup, (g_Plugin.dwInitializedMode & MODE_SMARTEND));
    EnableWindow(hWndSmartUpDownGroup, (g_Plugin.dwInitializedMode & MODE_SMARTUPDOWN));
    EnableWindow(hWndNoSelEOLGroup, (g_Plugin.dwInitializedMode & MODE_NOSELEOL));

    if (!(g_dwSmartHomeSettings & SMARTHOME_NOJUMPBETWEENSPACES))
      SendMessage(hWndSmartHomeJumpBetweenSpacesEnable, BM_SETCHECK, BST_CHECKED, 0);
    if (g_dwSmartHomeSettings & SMARTHOME_JUMPINVERT)
      SendMessage(hWndSmartHomeJumpInvertEnable, BM_SETCHECK, BST_CHECKED, 0);
    if (g_dwSmartHomeSettings & SMARTHOME_CONTINUEWRAP)
      SendMessage(hWndSmartHomeContinueWrapEnable, BM_SETCHECK, BST_CHECKED, 0);
    if (g_dwSmartHomeSettings & SMARTHOME_SKIPWRAP)
      SendMessage(hWndSmartHomeSkipWrapEnable, BM_SETCHECK, BST_CHECKED, 0);
    SendMessage(hDlg, WM_COMMAND, IDC_SMARTHOME_JUMPBETWEENSPACES_ENABLE, 0);

    if (!(g_Plugin.dwInitializedMode & MODE_SMARTHOME))
    {
      EnableWindow(hWndSmartHomeJumpBetweenSpacesEnable, FALSE);
      EnableWindow(hWndSmartHomeJumpInvertEnable, FALSE);
      EnableWindow(hWndSmartHomeContinueWrapEnable, FALSE);
      EnableWindow(hWndSmartHomeSkipWrapEnable, FALSE);
    }

    if (!(g_dwSmartEndSettings & SMARTEND_NOJUMPBETWEENSPACES))
      SendMessage(hWndSmartEndJumpBetweenSpacesEnable, BM_SETCHECK, BST_CHECKED, 0);
    if (g_dwSmartEndSettings & SMARTEND_JUMPINVERT)
      SendMessage(hWndSmartEndJumpInvertEnable, BM_SETCHECK, BST_CHECKED, 0);
    if (g_dwSmartEndSettings & SMARTEND_CONTINUEWRAP)
      SendMessage(hWndSmartEndContinueWrapEnable, BM_SETCHECK, BST_CHECKED, 0);
    if (g_dwSmartEndSettings & SMARTEND_SKIPWRAP)
      SendMessage(hWndSmartEndSkipWrapEnable, BM_SETCHECK, BST_CHECKED, 0);
    SendMessage(hDlg, WM_COMMAND, IDC_SMARTEND_JUMPBETWEENSPACES_ENABLE, 0);

    if (!(g_Plugin.dwInitializedMode & MODE_SMARTEND))
    {
      EnableWindow(hWndSmartEndJumpBetweenSpacesEnable, FALSE);
      EnableWindow(hWndSmartEndJumpInvertEnable, FALSE);
      EnableWindow(hWndSmartEndContinueWrapEnable, FALSE);
      EnableWindow(hWndSmartEndSkipWrapEnable, FALSE);
    }

    if (g_dwSmartUpDownSettings & SMARTUPDOWN_PAGE)
      SendMessage(hWndSmartPageUpDownEnable, BM_SETCHECK, BST_CHECKED, 0);
    if (!(g_Plugin.dwInitializedMode & MODE_SMARTUPDOWN))
      EnableWindow(hWndSmartPageUpDownEnable, FALSE);

    if (g_dwNoSelEOLSettings & NOSELEOL_SINGLELINE)
      SendMessage(hWndNoSelEOLSingleLineEnable, BM_SETCHECK, BST_CHECKED, 0);
    if (!(g_Plugin.dwInitializedMode & MODE_NOSELEOL))
      EnableWindow(hWndNoSelEOLSingleLineEnable, FALSE);
  }
  else if (uMsg == WM_COMMAND)
  {
    if (LOWORD(wParam) == IDC_SMARTHOME_JUMPBETWEENSPACES_ENABLE ||
        LOWORD(wParam) == IDC_SMARTHOME_CONTINUEWRAP_ENABLE)
    {
      BOOL bEnable;

      bEnable=(BOOL)SendMessage(hWndSmartHomeJumpBetweenSpacesEnable, BM_GETCHECK, 0, 0);
      EnableWindow(hWndSmartHomeJumpInvertEnable, bEnable);
      bEnable=(BOOL)SendMessage(hWndSmartHomeContinueWrapEnable, BM_GETCHECK, 0, 0);
      EnableWindow(hWndSmartHomeSkipWrapEnable, bEnable);
    }
    else if (LOWORD(wParam) == IDC_SMARTEND_JUMPBETWEENSPACES_ENABLE ||
             LOWORD(wParam) == IDC_SMARTEND_CONTINUEWRAP_ENABLE)
    {
      BOOL bEnable;

      bEnable=(BOOL)SendMessage(hWndSmartEndJumpBetweenSpacesEnable, BM_GETCHECK, 0, 0);
      EnableWindow(hWndSmartEndJumpInvertEnable, bEnable);
      bEnable=(BOOL)SendMessage(hWndSmartEndContinueWrapEnable, BM_GETCHECK, 0, 0);
      EnableWindow(hWndSmartEndSkipWrapEnable, bEnable);
    }
    else if (LOWORD(wParam) == IDOK)
    {
      g_dwSmartHomeSettings=0;
      if (SendMessage(hWndSmartHomeJumpBetweenSpacesEnable, BM_GETCHECK, 0, 0) != BST_CHECKED)
        g_dwSmartHomeSettings|=SMARTHOME_NOJUMPBETWEENSPACES;
      if (SendMessage(hWndSmartHomeJumpInvertEnable, BM_GETCHECK, 0, 0) == BST_CHECKED)
        g_dwSmartHomeSettings|=SMARTHOME_JUMPINVERT;
      if (SendMessage(hWndSmartHomeContinueWrapEnable, BM_GETCHECK, 0, 0) == BST_CHECKED)
        g_dwSmartHomeSettings|=SMARTHOME_CONTINUEWRAP;
      if (SendMessage(hWndSmartHomeSkipWrapEnable, BM_GETCHECK, 0, 0) == BST_CHECKED)
        g_dwSmartHomeSettings|=SMARTHOME_SKIPWRAP;

      g_dwSmartEndSettings=0;
      if (SendMessage(hWndSmartEndJumpBetweenSpacesEnable, BM_GETCHECK, 0, 0) != BST_CHECKED)
        g_dwSmartEndSettings|=SMARTEND_NOJUMPBETWEENSPACES;
      if (SendMessage(hWndSmartEndJumpInvertEnable, BM_GETCHECK, 0, 0) == BST_CHECKED)
        g_dwSmartEndSettings|=SMARTEND_JUMPINVERT;
      if (SendMessage(hWndSmartEndContinueWrapEnable, BM_GETCHECK, 0, 0) == BST_CHECKED)
        g_dwSmartEndSettings|=SMARTEND_CONTINUEWRAP;
      if (SendMessage(hWndSmartEndSkipWrapEnable, BM_GETCHECK, 0, 0) == BST_CHECKED)
        g_dwSmartEndSettings|=SMARTEND_SKIPWRAP;

      g_dwSmartUpDownSettings=0;
      if (SendMessage(hWndSmartPageUpDownEnable, BM_GETCHECK, 0, 0) == BST_CHECKED)
        g_dwSmartUpDownSettings|=SMARTUPDOWN_PAGE;

      g_dwNoSelEOLSettings=0;
      if (SendMessage(hWndNoSelEOLSingleLineEnable, BM_GETCHECK, 0, 0) == BST_CHECKED)
        g_dwNoSelEOLSettings|=NOSELEOL_SINGLELINE;

      SaveOptions(0);
      EndDialog(hDlg, 0);
      return TRUE;
    }
    else if (LOWORD(wParam) == IDCANCEL)
    {
      EndDialog(hDlg, 0);
      return TRUE;
    }
  }
  else if (uMsg == WM_CLOSE)
  {
    PostMessage(hDlg, WM_COMMAND, IDCANCEL, 0);
    return TRUE;
  }
  else if (uMsg == WM_DESTROY)
  {
    //Destroy plugin icon
    DestroyIcon(hPluginIcon);
  }
  return FALSE;
}

LRESULT CALLBACK NewEditProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch ( uMsg )
    {
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:
        {
            if ( g_Plugin.dwInitializedMode & MODE_SMARTBACKSPACE )
            {
                if ( wParam == VK_BACK )
                {
                    if ( ((GetKeyState(VK_CONTROL) & 0x80) == 0) &&
                         ((GetKeyState(VK_MENU) & 0x80) == 0) )
                    {
                        if ( OnEditBackspaceKeyDown(hWnd, lParam) )
                            return TRUE;
                    }
                }
            }

            if ( g_Plugin.dwInitializedMode & MODE_SMARTHOME )
            {
                if ( wParam == VK_HOME )
                {
                    if ( GetKeyState(VK_CONTROL) >= 0 )
                    {
                        if ( OnEditHomeKeyDown(hWnd, lParam) )
                            return TRUE;
                    }
                }
            }

            if ( g_Plugin.dwInitializedMode & MODE_NOSELEOL )
            {
                if ( !g_SelState.bShiftDown )
                {
                    if ( wParam == VK_SHIFT /*GetKeyState(VK_SHIFT) & 0x80*/ )
                    {
                        LRESULT lResult = 0;

                        if ( g_Plugin.pEditProcData && g_Plugin.pEditProcData->NextProc )
                            lResult = g_Plugin.pEditProcData->NextProc(hWnd, uMsg, wParam, lParam);

                        OnEditSelKeyDown(hWnd, VK_SHIFT);

                        return lResult;
                    }
                }
            }

            if ( uMsg == WM_SYSKEYDOWN )
                break;

            if ( g_Plugin.dwInitializedMode & MODE_SMARTEND )
            {
                if ( wParam == VK_END )
                {
                    if ( ((GetKeyState(VK_CONTROL) & 0x80) == 0) &&
                         ((GetKeyState(VK_MENU) & 0x80) == 0) )
                    {
                        if ( OnEditEndKeyDown(hWnd, lParam) )
                            return TRUE;
                    }
                }
            }

            if ( g_Plugin.dwInitializedMode & MODE_SMARTUPDOWN )
            {
                if ( (wParam == VK_DOWN) ||  // Arrow Down
                     (wParam == VK_UP)   ||  // Arrow Up
                     ( g_dwSmartUpDownSettings &&
                       ( (wParam == VK_PRIOR) ||  // Page Up
                         (wParam == VK_NEXT) )    // Page Down
                     )
                   )
                {
                    if ( ((GetKeyState(VK_CONTROL) & 0x80) == 0) &&
                         ((GetKeyState(VK_MENU) & 0x80) == 0) )
                    {
                        if ( OnEditArrowDownOrUpKeyDown(hWnd, wParam) )
                            return TRUE;
                    }
                }
            }

            break;
        }
        case WM_KEYUP:
        case WM_SYSKEYUP:
        {
            if ( g_Plugin.dwInitializedMode & MODE_NOSELEOL )
            {
                if ( g_SelState.bShiftDown )
                {
                    if ( wParam == VK_SHIFT /*(GetKeyState(VK_SHIFT) & 0x80) == 0*/ )
                    {
                        LRESULT lResult = 0;

                        if ( g_Plugin.pEditProcData && g_Plugin.pEditProcData->NextProc )
                            lResult = g_Plugin.pEditProcData->NextProc(hWnd, uMsg, wParam, lParam);

                        OnEditSelKeyUp(hWnd, VK_SHIFT);

                        return lResult;
                    }
                }
            }
            break;
        }
        case WM_LBUTTONDOWN:
        {
            if ( g_Plugin.dwInitializedMode & MODE_NOSELEOL )
            {
                if ( !g_SelState.bMouseLButtonDown )
                {
                    LRESULT lResult = 0;

                    if ( g_Plugin.pEditProcData && g_Plugin.pEditProcData->NextProc )
                        lResult = g_Plugin.pEditProcData->NextProc(hWnd, uMsg, wParam, lParam);

                    OnEditSelKeyDown(hWnd, VK_LBUTTON);

                    return lResult;
                }
            }
            break;
        }
        case WM_LBUTTONUP:
        {
            if ( g_Plugin.dwInitializedMode & MODE_NOSELEOL )
            {
                if ( g_SelState.bMouseLButtonDown )
                {
                    LRESULT lResult = 0;

                    if ( g_Plugin.pEditProcData && g_Plugin.pEditProcData->NextProc )
                        lResult = g_Plugin.pEditProcData->NextProc(hWnd, uMsg, wParam, lParam);

                    OnEditSelKeyUp(hWnd, VK_LBUTTON);

                    return lResult;
                }
            }
            break;
        }
    }

    if ( g_Plugin.pEditProcData && g_Plugin.pEditProcData->NextProc )
        return g_Plugin.pEditProcData->NextProc(hWnd, uMsg, wParam, lParam);
    else
        return 0;
}

LRESULT CALLBACK NewFrameProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch ( uMsg )
    {
        /*
        case WM_NOTIFY:
        {
            if ( g_Plugin.dwInitializedMode & MODE_NOSELEOL )
            {
                LPNMHDR pnmh = (LPNMHDR) lParam;
                if ( pnmh->code == EN_SELCHANGE )
                {
                    EDITINFO ei;

                    SendMessage( g_Plugin.hMainWnd,
                        AKD_GETEDITINFO, (WPARAM) NULL, (LPARAM) &ei );

                    if ( ei.hWndEdit == pnmh->hwndFrom )
                    {

                    }
                }
            }
            break;
        }
        */
        case WM_MDIACTIVATE:
        {
            if ( g_Plugin.dwInitializedMode & MODE_NOSELEOL )
            {
                clearSelectionState(&g_SelState);
            }
            break;
        }
        default:
        {
            break;
        }
    }

    CheckEditNotification(hWnd, uMsg, wParam, lParam);

    if ( g_Plugin.pFrameProcData && g_Plugin.pFrameProcData->NextProc )
        return g_Plugin.pFrameProcData->NextProc(hWnd, uMsg, wParam, lParam);
    else
        return 0;
}

LRESULT CALLBACK NewMainProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch ( uMsg )
    {
        /*
        case WM_NOTIFY:
        {
            if ( g_Plugin.dwInitializedMode & MODE_NOSELEOL )
            {
                LPNMHDR pnmh = (LPNMHDR) lParam;
                if ( pnmh->code == EN_SELCHANGE )
                {
                    EDITINFO ei;

                    SendMessage( g_Plugin.hMainWnd,
                        AKD_GETEDITINFO, (WPARAM) NULL, (LPARAM) &ei );

                    if ( ei.hWndEdit == pnmh->hwndFrom )
                    {

                    }
                }
            }
            break;
        }
        */
        case AKDN_EDIT_ONSTART:
        {
            if ( g_Plugin.dwInitializedMode & MODE_NOSELEOL )
            {
                clearSelectionState(&g_SelState);
            }
            break;
        }
        case AKDN_EDIT_ONFINISH:
        {
            if ( g_Plugin.dwInitializedMode & MODE_NOSELEOL )
            {
                clearSelectionState(&g_SelState);
            }
            break;
        }
    }

    CheckEditNotification(hWnd, uMsg, wParam, lParam);

    if ( g_Plugin.pMainProcData && g_Plugin.pMainProcData->NextProc )
        return g_Plugin.pMainProcData->NextProc(hWnd, uMsg, wParam, lParam);
    else
        return 0;
}

void OnEditSelKeyDown(HWND hEdit, unsigned int uKey)
{
    if ( uKey == VK_SHIFT )
        g_SelState.bShiftDown = TRUE;
    else if ( uKey == VK_LBUTTON )
        g_SelState.bMouseLButtonDown = TRUE;

    g_SelState.SelKeyDownRange.cpMin = 0;
    g_SelState.SelKeyDownRange.cpMax = 0;
    SendMessage( hEdit, EM_EXGETSEL64, 0, (LPARAM) &g_SelState.SelKeyDownRange );
}

void OnEditSelKeyUp(HWND hEdit, unsigned int uKey)
{
    CHARRANGE64 SelKeyUpRange;

    SelKeyUpRange.cpMin = 0;
    SelKeyUpRange.cpMax = 0;
    SendMessage( hEdit, EM_EXGETSEL64, 0, (LPARAM) &SelKeyUpRange );
    if ( SelKeyUpRange.cpMin != SelKeyUpRange.cpMax )
    {
        if ( (SelKeyUpRange.cpMax != g_SelState.SelKeyDownRange.cpMax) ||
             (SelKeyUpRange.cpMin > g_SelState.SelKeyDownRange.cpMin) ||
             (g_SelState.bSpecialAction && (SelKeyUpRange.cpMin == g_SelState.SelKeyDownRange.cpMin)) )
        {
            // selection from right to left is not processed here
            // (the condition for cpMin works as "==" in case of
            //  mouse-triple-click and left-margin-mouse-click)
            OnEditSelectionChanged(hEdit, &SelKeyUpRange);
        }
    }

    if ( uKey == VK_SHIFT )
        g_SelState.bShiftDown = FALSE;
    else if ( uKey == VK_LBUTTON )
        g_SelState.bMouseLButtonDown = FALSE;

    g_SelState.bSpecialAction = FALSE;
    g_SelState.SelKeyDownRange.cpMin = 0;
    g_SelState.SelKeyDownRange.cpMax = 0;
}

void OnEditSelectionChanged(HWND hEdit, CHARRANGE64* cr)
{
    if ( !g_SelState.bColumnSelection )
    {
        INT_PTR i;
        wchar_t szTextW[4];

        if ( cr->cpMin == 0 )
        {
            GETTEXTLENGTHEX gtl;
            INT_PTR nTextLen;

            gtl.flags = GTL_DEFAULT | GTL_PRECISE;
            gtl.codepage = 0;
            nTextLen = (INT_PTR) SendMessage(hEdit, EM_GETTEXTLENGTHEX, (WPARAM)&gtl, 0);

            if ( cr->cpMax >= nTextLen )
            {
                // selection at EOF
                return;
            }
        }

        if ( g_dwNoSelEOLSettings )
        {
            if ( g_Plugin.bAkelEdit )
            {
                AECHARINDEX aeIndex;
                AECHARINDEX aeEnd;
                int         nRealEOL;

                nRealEOL = 0;
                SendMessage(hEdit, AEM_RICHOFFSETTOINDEX, cr->cpMin, (LPARAM) &aeIndex);
                SendMessage(hEdit, AEM_RICHOFFSETTOINDEX, cr->cpMax, (LPARAM) &aeEnd);
                while ( (nRealEOL < 2) && (aeIndex.nLine < aeEnd.nLine) )
                {
                    if ( aeIndex.lpLine->nLineBreak != AELB_WRAP )
                        ++nRealEOL;
                    AEC_NextLine(&aeIndex);
                }

                if ( nRealEOL >= 2 )
                    return;
            }
        }

        if ( g_Plugin.bOldWindows )
        {
            TEXTRANGE64A trA;

            trA.chrg.cpMin = (cr->cpMax >= 2) ? (cr->cpMax - 2) : 0;
            trA.chrg.cpMax = cr->cpMax;
            trA.lpstrText = (LPSTR) szTextW;
            // get last 2 characters from the selection to inspect for EOL
            i = (INT_PTR) SendMessageA( hEdit, EM_GETTEXTRANGE64A, 0, (LPARAM) &trA );
            if ( i < (trA.chrg.cpMax - trA.chrg.cpMin) )
            {
                // last symbol is '\0' - end of file
                if ( cr->cpMax - cr->cpMin == 1 ) // just '\0' is selected
                    --cr->cpMax;
            }
            else
            {
                if ( (i > 0) && (trA.lpstrText[i-1] == '\n') )
                {
                    --i;
                    --cr->cpMax;
                }
                if ( (i > 0) && (trA.lpstrText[i-1] == '\r') )
                {
                    --cr->cpMax;
                }
            }
            if ( cr->cpMax != trA.chrg.cpMax )
            {
                if ( g_Plugin.bAkelEdit )
                {
                    AESELECTION aes;

                    aes.dwFlags = AESELT_LOCKSCROLL;
                    aes.dwType = 0;
                    SendMessage( hEdit, AEM_RICHOFFSETTOINDEX, cr->cpMin, (LPARAM) &aes.crSel.ciMin );
                    SendMessage( hEdit, AEM_RICHOFFSETTOINDEX, cr->cpMax, (LPARAM) &aes.crSel.ciMax );
                    SendMessage( hEdit, AEM_SETSEL, 0, (LPARAM) &aes );
                }
                else
                {
                    // EM_GETSCROLLPOS - WM_SETREDRAW - EM_EXSETSEL64 - EM_SETSCROLLPOS - WM_SETREDRAW
                    SendMessage( hEdit, EM_EXSETSEL64, 0, (LPARAM) cr );
                }
            }
        }
        else
        {
            TEXTRANGE64W trW;

            trW.chrg.cpMin = (cr->cpMax >= 2) ? (cr->cpMax - 2) : 0;
            trW.chrg.cpMax = cr->cpMax;
            trW.lpstrText = (LPWSTR) szTextW;
            i = (INT_PTR) SendMessageW( hEdit, EM_GETTEXTRANGE64W, 0, (LPARAM) &trW );
            if ( i < (trW.chrg.cpMax - trW.chrg.cpMin) )
            {
                // last symbol is L'\0' - end of file
                if ( cr->cpMax - cr->cpMin == 1 ) // just L'\0' is selected
                    --cr->cpMax;
            }
            else
            {
                if ( (i > 0) && (trW.lpstrText[i-1] == L'\n') )
                {
                    --i;
                    --cr->cpMax;
                }
                if ( (i > 0) && (trW.lpstrText[i-1] == L'\r') )
                {
                    --cr->cpMax;
                }
            }
            if ( cr->cpMax != trW.chrg.cpMax )
            {
                if ( g_Plugin.bAkelEdit )
                {
                    AESELECTION aes;

                    aes.dwFlags = AESELT_LOCKSCROLL;
                    aes.dwType = 0;
                    SendMessageW( hEdit, AEM_RICHOFFSETTOINDEX, cr->cpMin, (LPARAM) &aes.crSel.ciMin );
                    SendMessageW( hEdit, AEM_RICHOFFSETTOINDEX, cr->cpMax, (LPARAM) &aes.crSel.ciMax );
                    SendMessageW( hEdit, AEM_SETSEL, 0, (LPARAM) &aes );
                }
                else
                {
                    // EM_GETSCROLLPOS - WM_SETREDRAW - EM_EXSETSEL64 - EM_SETSCROLLPOS - WM_SETREDRAW
                    SendMessageW( hEdit, EM_EXSETSEL64, 0, (LPARAM) cr );
                }
            }
        }
    }
    else
    {
        g_SelState.bColumnSelection = FALSE;
    }
}

BOOL OnEditBackspaceKeyDown(HWND hEdit, LPARAM lParam)
{
  if (g_Plugin.bAkelEdit)
  {
    // AkelEdit
    AESELECTION aes;
    AECHARINDEX ciCaret;

    if (!SendMessage(hEdit, AEM_GETSEL, (WPARAM)&ciCaret, (LPARAM)&aes))
    {
      AECHARINDEX ciCaretLineBegin;
      AECHARINDEX ciPrevLineBegin;
      AECHARINDEX ciMinSel;
      wchar_t *wszIndent=NULL;
      int nLineSpaces=0;
      int nPrevLineSpaces=0;
      int nTabStopSize;
      int nCaretWrapCharInLine;

      nTabStopSize=(int)SendMessage(hEdit, AEM_GETTABSTOP, 0, 0);
      nCaretWrapCharInLine=AEC_WrapLineBeginEx(&ciCaret, &ciCaretLineBegin);

      if (ciCaretLineBegin.lpLine->prev)
      {
        if (GetLineSpaces(&ciCaretLineBegin, nCaretWrapCharInLine, nTabStopSize, &nLineSpaces))
        {
          ciPrevLineBegin=ciCaretLineBegin;

          while (AEC_PrevLine(&ciPrevLineBegin))
          {
            AEC_WrapLineBeginEx(&ciPrevLineBegin, &ciPrevLineBegin);
            if (!GetLineSpaces(&ciPrevLineBegin, 0x7FFFFFFF, nTabStopSize, &nPrevLineSpaces))
            {
              if (ciPrevLineBegin.lpLine->nLineLen && nPrevLineSpaces < nLineSpaces)
              {
                ciMinSel.nLine=ciCaretLineBegin.nLine;
                ciMinSel.lpLine=ciCaretLineBegin.lpLine;
                ciMinSel.nCharInLine=nPrevLineSpaces;
                SendMessage(hEdit, AEM_COLUMNTOINDEX, MAKELONG(nTabStopSize, AECTI_WRAPLINEBEGIN|AECTI_FIT), (LPARAM)&ciMinSel);
                nLineSpaces=(int)SendMessage(hEdit, AEM_INDEXTOCOLUMN, MAKELONG(nTabStopSize, AECTI_WRAPLINEBEGIN), (LPARAM)&ciMinSel);

                if (nPrevLineSpaces > nLineSpaces)
                {
                  //In:
                  //---->---->·123
                  //---->---->---->---->|456
                  //Out:
                  //---->---->·123
                  //---->---->·|456
                  if ( bufReserve(&g_TextBuf, (nPrevLineSpaces - nLineSpaces + 1) * sizeof(wchar_t)) )
                  {
                    wchar_t *wpIndentMax;
                    wchar_t *wpCount;

                    wszIndent = (wchar_t *) g_TextBuf.pData;
                    wpIndentMax=wszIndent + (nPrevLineSpaces - nLineSpaces);
                    wpCount=wszIndent;

                    for (; wpCount < wpIndentMax; ++wpCount)
                      *wpCount=L' ';
                    *wpCount=0; // trailing NULL
                  }
                }
                SendMessage(hEdit, AEM_EXSETSEL, (WPARAM)&ciMinSel, (LPARAM)&ciCaret);
                SendMessage(g_Plugin.hMainWnd, AKD_REPLACESELW, (WPARAM)hEdit, (LPARAM)(wszIndent ? wszIndent : L""));
                if (wszIndent != NULL)
                  bufFreeIfSizeExceeds(&g_TextBuf, MAX_TEXTBUF_SIZE);
                return TRUE;
              }
            }
          }
        }
      }
    }
  }
  return FALSE;
}

BOOL OnEditHomeKeyDown(HWND hEdit, LPARAM lParam)
{
    if ( g_Plugin.bAkelEdit )
    {
      // AkelEdit
      AESELECTION aes;
      AECHARINDEX ciCaret;
      AECHARINDEX ciDefPos;
      AECHARINDEX ciNewPos;
      AECHARINDEX ciLineStart;
      AECHARINDEX ciSpacePos;
      BOOL bShift=(GetKeyState(VK_SHIFT) < 0);

      SendMessage(hEdit, AEM_GETSEL, (WPARAM)&ciCaret, (LPARAM)&aes);
      if (bShift)
        ciNewPos=ciCaret;
      else
      {
        ciNewPos=aes.crSel.ciMin;
        if (AEC_IndexCompare(&aes.crSel.ciMin, &ciCaret))
        {
          if (aes.crSel.ciMin.nLine == aes.crSel.ciMax.nLine)
            ciNewPos.nCharInLine=aes.crSel.ciMax.nCharInLine;
          else
            ciNewPos.nCharInLine=ciNewPos.lpLine->nLineLen;
        }
      }
      ciLineStart=ciNewPos;
      ciLineStart.nCharInLine=0;
      ciDefPos=ciLineStart;
      ciSpacePos=ciLineStart;

      if (g_dwSmartHomeSettings & SMARTHOME_CONTINUEWRAP)
      {
        if ((g_dwSmartHomeSettings & SMARTHOME_SKIPWRAP) || (!ciNewPos.nCharInLine && ciNewPos.lpLine->prev && ciNewPos.lpLine->prev->nLineBreak == AELB_WRAP))
        {
          AEC_WrapLineBeginEx(&ciNewPos, &ciLineStart);
          ciSpacePos=ciLineStart;
        }
      }
      for (;;)
      {
        if (ciSpacePos.lpLine->wpLine[ciSpacePos.nCharInLine] != L' ' &&
            ciSpacePos.lpLine->wpLine[ciSpacePos.nCharInLine] != L'\t')
          break;
        if (!AEC_NextCharInLine(&ciSpacePos))
          break;
      }
      if (!(g_dwSmartHomeSettings & SMARTHOME_NOJUMPBETWEENSPACES) &&
          (!AEC_IndexCompare(&ciNewPos, &ciLineStart) || (!(g_dwSmartHomeSettings & SMARTHOME_JUMPINVERT) &&
                                                          AEC_IndexCompare(&ciNewPos, &ciSpacePos) &&
                                                          !(ciNewPos.nLine == ciSpacePos.nLine && ciNewPos.nCharInLine == 0))))
      {
        ciNewPos=ciSpacePos;
      }
      else ciNewPos=ciLineStart;

      if (AEC_IndexCompare(&ciDefPos, &ciNewPos))
      {
        if (!bShift)
        {
          aes.crSel.ciMin=ciNewPos;
          aes.crSel.ciMax=ciNewPos;
        }
        else
        {
          if (!AEC_IndexCompare(&ciCaret, &aes.crSel.ciMin))
            aes.crSel.ciMin=ciNewPos;
          else
            aes.crSel.ciMax=ciNewPos;
          if (AEC_IndexCompare(&aes.crSel.ciMin, &aes.crSel.ciMax) > 0)
          {
            ciDefPos=aes.crSel.ciMin;
            aes.crSel.ciMin=aes.crSel.ciMax;
            aes.crSel.ciMax=ciDefPos;
          }
        }
        SendMessage(hEdit, AEM_SETSEL, (WPARAM)&ciNewPos, (LPARAM)&aes);
        return TRUE;
      }
      return FALSE;
    }
    else
    {
        // RichEdit
        if ( g_Plugin.bOldWindows )
        {
            if ( SmartHomeA(hEdit) )
                return TRUE;
        }
        else
        {
            if ( SmartHomeW(hEdit) )
                return TRUE;
        }
    }

    return FALSE;
}

BOOL OnEditEndKeyDown(HWND hEdit, LPARAM lParam)
{
    if ( g_Plugin.bAkelEdit )
    {
      // AkelEdit
      AESELECTION aes;
      AECHARINDEX ciCaret;
      AECHARINDEX ciDefPos;
      AECHARINDEX ciNewPos;
      AECHARINDEX ciLineEnd;
      AECHARINDEX ciSpacePos;
      BOOL bShift=(GetKeyState(VK_SHIFT) < 0);

      SendMessage(hEdit, AEM_GETSEL, (WPARAM)&ciCaret, (LPARAM)&aes);
      if (bShift)
        ciNewPos=ciCaret;
      else
      {
        ciNewPos=aes.crSel.ciMax;
        if (AEC_IndexCompare(&aes.crSel.ciMax, &ciCaret))
        {
          if (aes.crSel.ciMin.nLine == aes.crSel.ciMax.nLine)
            ciNewPos.nCharInLine=aes.crSel.ciMin.nCharInLine;
          else
            ciNewPos.nCharInLine=0;
        }
      }
      ciLineEnd=ciNewPos;
      ciLineEnd.nCharInLine=ciLineEnd.lpLine->nLineLen;
      ciDefPos=ciLineEnd;
      ciSpacePos=ciLineEnd;

      if (g_dwSmartEndSettings & SMARTEND_CONTINUEWRAP)
      {
        if ((g_dwSmartEndSettings & SMARTEND_SKIPWRAP) || (ciNewPos.nCharInLine == ciNewPos.lpLine->nLineLen && ciNewPos.lpLine->nLineBreak == AELB_WRAP))
        {
          AEC_WrapLineEndEx(&ciNewPos, &ciLineEnd);
          ciSpacePos=ciLineEnd;
        }
      }
      for (;;)
      {
        if (!AEC_PrevCharInLineEx(&ciSpacePos, &ciSpacePos))
          break;
        if (ciSpacePos.lpLine->wpLine[ciSpacePos.nCharInLine] != L' ' &&
            ciSpacePos.lpLine->wpLine[ciSpacePos.nCharInLine] != L'\t')
        {
          AEC_NextCharInLine(&ciSpacePos);
          break;
        } 
      }
      if (!(g_dwSmartEndSettings & SMARTEND_NOJUMPBETWEENSPACES) &&
          (!AEC_IndexCompare(&ciNewPos, &ciLineEnd) || (!(g_dwSmartEndSettings & SMARTEND_JUMPINVERT) &&
                                                        AEC_IndexCompare(&ciNewPos, &ciSpacePos) &&
                                                        !(ciNewPos.nLine == ciSpacePos.nLine && ciNewPos.nCharInLine == ciNewPos.lpLine->nLineLen))))
      {
        ciNewPos=ciSpacePos;
      }
      else ciNewPos=ciLineEnd;

      if (AEC_IndexCompare(&ciDefPos, &ciNewPos))
      {
        if (!bShift)
        {
          aes.crSel.ciMin=ciNewPos;
          aes.crSel.ciMax=ciNewPos;
        }
        else
        {
          if (!AEC_IndexCompare(&ciCaret, &aes.crSel.ciMin))
            aes.crSel.ciMin=ciNewPos;
          else
            aes.crSel.ciMax=ciNewPos;
          if (AEC_IndexCompare(&aes.crSel.ciMin, &aes.crSel.ciMax) > 0)
          {
            ciDefPos=aes.crSel.ciMin;
            aes.crSel.ciMin=aes.crSel.ciMax;
            aes.crSel.ciMax=ciDefPos;
          }
        }
        SendMessage(hEdit, AEM_SETSEL, (WPARAM)&ciNewPos, (LPARAM)&aes);
        return TRUE;
      }
      return FALSE;
    }
    else
    {
        int         nLine;
        INT_PTR     nLineIndex;
        int         nLineLen;
        CHARRANGE64 cr = { 0, 0 };

        SendMessage(hEdit, EM_EXGETSEL64, 0, (LPARAM) &cr);
        nLine = (int) SendMessage(hEdit, EM_EXLINEFROMCHAR, 0, (LPARAM)(-1));
        // wow, "magic" parameter (-1) gives REAL line number!!!
        nLineIndex = (INT_PTR) SendMessage(hEdit, EM_LINEINDEX, nLine, 0);
        nLineLen = (int) SendMessage(hEdit, EM_LINELENGTH, nLineIndex, 0);
        if ( nLineLen <= 0 )
            return FALSE;

        /*
        if ( nLineIndex == cr.cpMax ) // line beginning or WordWrap'ed line end
        {
            CHARRANGE64 cr2 = { 0, 0 };

            HideCaret(hEdit);                                  // hide caret
            SendMessage(hEdit, WM_SETREDRAW, FALSE, 0);        // forbid redraw
            g_bProcessEndKey = FALSE;
            SendMessage(hEdit, WM_KEYDOWN, VK_END, lParam);    // emulate End pressed
            g_bProcessEndKey = TRUE;
            SendMessage(hEdit, EM_EXGETSEL64, 0, (LPARAM) &cr2); // get new selection
            SendMessage(hEdit, EM_EXSETSEL64, 0, (LPARAM) &cr);  // restore selection
            SendMessage(hEdit, WM_SETREDRAW, TRUE, 0);         // allow redraw
            ShowCaret(hEdit);                                  // show caret

            if ( cr.cpMax == cr2.cpMax ) // WordWrap'ed line end
            {
                --nLine; // previous line
                nLineIndex = (int) SendMessage(hEdit, EM_LINEINDEX, nLine, 0);
                nLineLen = (int) SendMessage(hEdit, EM_LINELENGTH, nLineIndex, 0);
                if ( nLineLen <= 0 )
                    return FALSE;
            }
        }
        */

        if ( g_Plugin.dwInitializedMode & MODE_SMARTEND )
        {
            int  nSize;
            BOOL bShiftPressed = (GetKeyState(VK_SHIFT) & 0x80) ? TRUE : FALSE;

            if ( g_Plugin.bOldWindows )
                nSize = (nLineLen + 1)*sizeof(char);
            else
                nSize = (nLineLen + 1)*sizeof(wchar_t);

            if ( bufReserve(&g_TextBuf, nSize) )
            {
                INT_PTR i;

                if ( g_Plugin.bOldWindows )
                {
                    TEXTRANGE64A trA;
                    LPSTR        lpText = (LPSTR) g_TextBuf.pData;

                    lpText[0] = 0;
                    trA.chrg.cpMin = nLineIndex;
                    trA.chrg.cpMax = nLineIndex + nLineLen;
                    trA.lpstrText = lpText;
                    i = (INT_PTR) SendMessageA( hEdit, EM_GETTEXTRANGE64A, 0, (LPARAM) &trA );
                    while ( --i >= 0 )
                    {
                        if ( (lpText[i] != ' ') && (lpText[i] != '\t') )
                            break;
                    }
                }
                else
                {
                    TEXTRANGE64W trW;
                    LPWSTR       lpText = (LPWSTR) g_TextBuf.pData;

                    lpText[0] = 0;
                    trW.chrg.cpMin = nLineIndex;
                    trW.chrg.cpMax = nLineIndex + nLineLen;
                    trW.lpstrText = lpText;
                    i = (INT_PTR) SendMessageW( hEdit, EM_GETTEXTRANGE64W, 0, (LPARAM) &trW );
                    while ( --i >= 0 )
                    {
                        if ( (lpText[i] != L' ') && (lpText[i] != L'\t') )
                            break;
                    }
                }

                bufFreeIfSizeExceeds(&g_TextBuf, MAX_TEXTBUF_SIZE);
                // lpText is no more valid

                ++i;

                if ( cr.cpMax == nLineIndex + nLineLen )
                {
                    // selection ends at the end of line
                    if ( (cr.cpMax == cr.cpMin) && (i == nLineLen) )
                    {
                        // The caret is at the end of line (no selection).
                        // No trailing tabs or spaces, thus no SmartEnd available:
                        // there's nowhere to move.
                        return FALSE;
                    }
                    else if ( (cr.cpMax != cr.cpMin) && g_dwSmartEndSettings && !bShiftPressed )
                    {
                        // altSmartEnd: jump to the end of line
                        return FALSE;
                    }
                }

                if ( g_dwSmartEndSettings ?
                      (cr.cpMax == nLineIndex + nLineLen) :
                       (cr.cpMax != nLineIndex + i)
                   )
                {
                    if ( bShiftPressed && (cr.cpMin == nLineIndex + i) )
                    {
                        cr.cpMin = cr.cpMax;
                        cr.cpMax = nLineIndex + nLineLen;
                    }
                    else
                    {
                        if ( !bShiftPressed )
                            cr.cpMin = nLineIndex + i;
                        cr.cpMax = nLineIndex + i;
                    }
                }
                else
                {
                    if ( !bShiftPressed )
                        cr.cpMin = nLineIndex + nLineLen;
                    cr.cpMax = nLineIndex + nLineLen;
                }

                if ( cr.cpMax == nLineIndex + nLineLen )
                {
                    if ( nLine < (int) SendMessage(hEdit, EM_EXLINEFROMCHAR, 0, cr.cpMax) )
                        return FALSE;
                }

                SendMessage( hEdit, EM_EXSETSEL64, 0, (LPARAM) &cr );
            }
        }
        else if ( g_Plugin.dwInitializedMode & MODE_NOSELEOL )
        {
            cr.cpMax = nLineIndex + nLineLen;
            SendMessage( hEdit, EM_EXSETSEL64, 0, (LPARAM) &cr );
        }
    }

    return TRUE;
}

BOOL OnEditArrowDownOrUpKeyDown(HWND hEdit, WPARAM wKey)
{
    int         nLine;
    int         nMaxLine;
    CHARRANGE64 crOld;
    CHARRANGE64 cr;

    cr.cpMin = 0;
    cr.cpMax = 0;
    SendMessage(hEdit, EM_EXGETSEL64, 0, (LPARAM) &cr);
    crOld.cpMin = cr.cpMin;
    crOld.cpMax = cr.cpMax;

    if ( wKey == VK_DOWN || wKey == VK_NEXT )
    {
        nLine = (int) SendMessage(hEdit, EM_EXLINEFROMCHAR, 0, cr.cpMax);
        nMaxLine = (int) SendMessage(hEdit, EM_GETLINECOUNT, 0, 0);
        --nMaxLine;
        if ( nLine == nMaxLine )
        {
            GETTEXTLENGTHEX gtl;

            gtl.flags = GTL_DEFAULT | GTL_PRECISE;
            gtl.codepage = 0;
            cr.cpMax = (INT_PTR) SendMessage(hEdit, EM_GETTEXTLENGTHEX, (WPARAM)&gtl, 0);

            if ( (GetKeyState(VK_SHIFT) & 0x80) == 0 )
            {
                cr.cpMin = cr.cpMax;
            }

            if ( cr.cpMin != crOld.cpMin || cr.cpMax != crOld.cpMax )
            {
                SendMessage( hEdit, EM_EXSETSEL64, 0, (LPARAM) &cr );
                return TRUE;
            }
        }
    }
    else // ( wKey == VK_UP || wKey == VK_PRIOR )
    {
        nLine = (int) SendMessage(hEdit, EM_EXLINEFROMCHAR, 0, cr.cpMin);
        if ( nLine == 0 )
        {
            if ( GetKeyState(VK_SHIFT) & 0x80 )
            {
                cr.cpMin = cr.cpMax;
                cr.cpMax = 0;
            }
            else
            {
                cr.cpMin = 0;
                cr.cpMax = 0;
            }

            if ( cr.cpMin != crOld.cpMax || cr.cpMax != crOld.cpMin )
            {
                SendMessage( hEdit, EM_EXSETSEL64, 0, (LPARAM) &cr );
                return TRUE;
            }
        }
    }

    return FALSE;
}

BOOL SmartHomeA(HWND hEdit)
{
    CHARRANGE64  crCurrent;
    CHARRANGE64  crNew;
    TEXTRANGE64A txtrng;
    char*        pText;
    int          nMinLine;
    INT_PTR      nMinLineIndex;
    int          nMinLineLength;
    int          i;
    BOOL         bResult = FALSE;

    SendMessage(hEdit, EM_EXGETSEL64, 0, (LPARAM)&crCurrent);
    nMinLine = (int) SendMessage(hEdit, EM_EXLINEFROMCHAR, 0, crCurrent.cpMin);
    nMinLineIndex = (INT_PTR) SendMessage(hEdit, EM_LINEINDEX, nMinLine, 0);
    nMinLineLength = (int) SendMessage(hEdit, EM_LINELENGTH, nMinLineIndex, 0);

    if ( bufReserve(&g_TextBuf, nMinLineLength + 1) )
    {
        pText = (char *) g_TextBuf.pData;
        txtrng.chrg.cpMin = nMinLineIndex;
        txtrng.chrg.cpMax = nMinLineIndex + nMinLineLength;
        txtrng.lpstrText = pText;
        SendMessageA(hEdit, EM_GETTEXTRANGE64A, 0, (LPARAM)&txtrng);

        for ( i = 0; pText[i] == ' ' || pText[i] == '\t'; ++i ) ;

        bufFreeIfSizeExceeds(&g_TextBuf, MAX_TEXTBUF_SIZE);
        // pText is no more valid

        if ( i )
        {
            if ( (crCurrent.cpMin - nMinLineIndex) > i || (crCurrent.cpMin - nMinLineIndex) == 0 )
                crNew.cpMin = nMinLineIndex + i;
            else
                crNew.cpMin = nMinLineIndex;

            if ( GetKeyState(VK_SHIFT) < 0 )
                crNew.cpMax = crCurrent.cpMax;
            else
                crNew.cpMax = crNew.cpMin;

            SendMessage(hEdit, EM_EXSETSEL64, 0, (LPARAM)&crNew);

            bResult = TRUE;
        }
    }

    return bResult;
}

BOOL SmartHomeW(HWND hEdit)
{
    CHARRANGE64  crCurrent;
    CHARRANGE64  crNew;
    TEXTRANGE64W txtrng;
    wchar_t*     wpText;
    int          nMinLine;
    INT_PTR      nMinLineIndex;
    int          nMinLineLength;
    int          i;
    BOOL         bResult = FALSE;

    SendMessage(hEdit, EM_EXGETSEL64, 0, (LPARAM)&crCurrent);
    nMinLine = (int) SendMessage(hEdit, EM_EXLINEFROMCHAR, 0, crCurrent.cpMin);
    nMinLineIndex = (INT_PTR) SendMessage(hEdit, EM_LINEINDEX, nMinLine, 0);
    nMinLineLength = (int) SendMessage(hEdit, EM_LINELENGTH, nMinLineIndex, 0);

    if ( bufReserve(&g_TextBuf, (nMinLineLength + 1)*sizeof(wchar_t)) )
    {
        wpText = (wchar_t *) g_TextBuf.pData;
        txtrng.chrg.cpMin = nMinLineIndex;
        txtrng.chrg.cpMax = nMinLineIndex + nMinLineLength;
        txtrng.lpstrText = wpText;
        SendMessageW(hEdit, EM_GETTEXTRANGE64W, 0, (LPARAM)&txtrng);

        for ( i = 0; wpText[i] == ' ' || wpText[i] == '\t'; ++i ) ;

        bufFreeIfSizeExceeds(&g_TextBuf, MAX_TEXTBUF_SIZE);
        // wpText is no more valid

        if ( i )
        {
            if ( (crCurrent.cpMin - nMinLineIndex) > i || (crCurrent.cpMin - nMinLineIndex) == 0 )
                crNew.cpMin = nMinLineIndex + i;
            else
                crNew.cpMin = nMinLineIndex;

            if ( GetKeyState(VK_SHIFT) < 0 )
                crNew.cpMax = crCurrent.cpMax;
            else
                crNew.cpMax = crNew.cpMin;

            SendMessage(hEdit, EM_EXSETSEL64, 0, (LPARAM)&crNew);

            bResult = TRUE;
        }
    }

    return bResult;
}

BOOL GetLineSpaces(AECHARINDEX *ciFirstCharInLine, int nWrapCharInLine, int nTabStopSize, int *lpnLineSpaces)
{
  AECHARINDEX ciCount=*ciFirstCharInLine;
  int nLineSpaces=0;
  int nCountCharInLine=0;

  while (nCountCharInLine < nWrapCharInLine)
  {
    if (ciCount.lpLine->wpLine[ciCount.nCharInLine] == L' ')
      ++nLineSpaces;
    else if (ciCount.lpLine->wpLine[ciCount.nCharInLine] == L'\t')
      nLineSpaces+=nTabStopSize - nLineSpaces % nTabStopSize;
    else
    {
      *lpnLineSpaces=nLineSpaces;
      return FALSE;
    }
    if (!AEC_NextCharInLine(&ciCount))
      break;
    ++nCountCharInLine;
  }
  *lpnLineSpaces=nLineSpaces;
  return TRUE;
}

void CheckEditNotification(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    if ( g_Plugin.bAkelEdit && (g_Plugin.dwInitializedMode & MODE_NOSELEOL) )
    {
        if ( (uMsg == WM_NOTIFY) && (wParam == ID_EDIT) )
        {
            if ( ((NMHDR *)lParam)->code == AEN_SELCHANGING )
            {
                AENSELCHANGE* psc = (AENSELCHANGE *) lParam;

                if ( psc->bColumnSel )
                    g_SelState.bColumnSelection = TRUE;
                else
                    g_SelState.bColumnSelection = FALSE;

                if ( (psc->dwType & AESCT_MOUSETRIPLECLK) || (psc->dwType & AESCT_MOUSELEFTMARGIN) )
                    g_SelState.bSpecialAction = TRUE;
                else
                    g_SelState.bSpecialAction = FALSE;
            }
        }
    }
}

//// Options

INT_PTR WideOption(HANDLE hOptions, const wchar_t *pOptionName, DWORD dwType, BYTE *lpData, DWORD dwData)
{
  PLUGINOPTIONW po;

  po.pOptionName=pOptionName;
  po.dwType=dwType;
  po.lpData=lpData;
  po.dwData=dwData;
  return SendMessage(hMainWnd, AKD_OPTIONW, (WPARAM)hOptions, (LPARAM)&po);
}

void ReadOptions(DWORD dwFlags)
{
  HANDLE hOptions;

  if (hOptions=(HANDLE)SendMessage(hMainWnd, AKD_BEGINOPTIONSW, POB_READ, (LPARAM)wszPluginName))
  {
    WideOption(hOptions, L"SmartHomeSettings", PO_DWORD, (LPBYTE)&g_dwSmartHomeSettings, sizeof(DWORD));
    WideOption(hOptions, L"SmartEndSettings", PO_DWORD, (LPBYTE)&g_dwSmartEndSettings, sizeof(DWORD));
    WideOption(hOptions, L"SmartUpDownSettings", PO_DWORD, (LPBYTE)&g_dwSmartUpDownSettings, sizeof(DWORD));
    WideOption(hOptions, L"NoSelEOLSettings", PO_DWORD, (LPBYTE)&g_dwNoSelEOLSettings, sizeof(DWORD));

    SendMessage(hMainWnd, AKD_ENDOPTIONS, (WPARAM)hOptions, 0);
  }
}

void SaveOptions(DWORD dwFlags)
{
  HANDLE hOptions;

  if (hOptions=(HANDLE)SendMessage(hMainWnd, AKD_BEGINOPTIONSW, POB_SAVE, (LPARAM)wszPluginName))
  {
    WideOption(hOptions, L"SmartHomeSettings", PO_DWORD, (LPBYTE)&g_dwSmartHomeSettings, sizeof(DWORD));
    WideOption(hOptions, L"SmartEndSettings", PO_DWORD, (LPBYTE)&g_dwSmartEndSettings, sizeof(DWORD));
    WideOption(hOptions, L"SmartUpDownSettings", PO_DWORD, (LPBYTE)&g_dwSmartUpDownSettings, sizeof(DWORD));
    WideOption(hOptions, L"NoSelEOLSettings", PO_DWORD, (LPBYTE)&g_dwNoSelEOLSettings, sizeof(DWORD));

    SendMessage(hMainWnd, AKD_ENDOPTIONS, (WPARAM)hOptions, 0);
  }
}

const char* GetLangStringA(LANGID wLangID, int nStringID)
{
  static char szStringBuf[MAX_PATH];

  WideCharToMultiByte(CP_ACP, 0, GetLangStringW(wLangID, nStringID), -1, szStringBuf, MAX_PATH, NULL, NULL);
  return szStringBuf;
}

const wchar_t* GetLangStringW(LANGID wLangID, int nStringID)
{
  if (wLangID == LANG_RUSSIAN)
  {
    if (nStringID == STRID_JUMPBETWEENSPACES)
      return L"\x041F\x0435\x0440\x0435\x043C\x0435\x0449\x0430\x0442\x044C\x0441\x044F\x0020\x043C\x0435\x0436\x0434\x0443\x0020\x043F\x0440\x043E\x0431\x0435\x043B\x0430\x043C\x0438";
    if (nStringID == STRID_INVERT)
      return L"\x0418\x043D\x0432\x0435\x0440\x0442\x0438\x0440\x043E\x0432\x0430\x0442\x044C";
    if (nStringID == STRID_CONTINUEWRAP)
      return L"\x041F\x0440\x043E\x0434\x043E\x043B\x0436\x0430\x0442\x044C\x0020\x043F\x0440\x0438\x0020\x043F\x0435\x0440\x0435\x043D\x043E\x0441\x0435";
    if (nStringID == STRID_SKIPWRAP)
      return L"\x041F\x0440\x043E\x043F\x0443\x0441\x043A\x0430\x0442\x044C\x0020\x043F\x0435\x0440\x0435\x043D\x043E\x0441";
    if (nStringID == STRID_SMARTPAGEUPDOWN)
      return L"\x0422\x0430\x043A\x0436\x0435\x0020\x0050\x0061\x0067\x0065\x0020\x0055\x0070\x0020\x0438\x0020\x0050\x0061\x0067\x0065\x0020\x0044\x006F\x0077\x006E";
    if (nStringID == STRID_SINGLELINE)
      return L"\x0422\x043E\x043B\x044C\x043A\x043E\x0020\x0434\x043B\x044F\x0020\x043E\x0434\x043D\x043E\x0439\x0020\x0441\x0442\x0440\x043E\x043A\x0438";
    if (nStringID == STRID_PLUGIN)
      return L"%s \x043F\x043B\x0430\x0433\x0438\x043D";
    if (nStringID == STRID_OK)
      return L"\x004F\x004B";
    if (nStringID == STRID_CANCEL)
      return L"\x041E\x0442\x043C\x0435\x043D\x0430";
  }
  else
  {
    if (nStringID == STRID_JUMPBETWEENSPACES)
      return L"Jump between spaces";
    if (nStringID == STRID_INVERT)
      return L"Invert";
    if (nStringID == STRID_CONTINUEWRAP)
      return L"Continue if wrap";
    if (nStringID == STRID_SKIPWRAP)
      return L"Skip wrap";
    if (nStringID == STRID_SMARTPAGEUPDOWN)
      return L"Also Page Up and Page Down";
    if (nStringID == STRID_SINGLELINE)
      return L"Only for single line";
    if (nStringID == STRID_PLUGIN)
      return L"%s plugin";
    if (nStringID == STRID_OK)
      return L"OK";
    if (nStringID == STRID_CANCEL)
      return L"Cancel";
  }
  return L"";
}
