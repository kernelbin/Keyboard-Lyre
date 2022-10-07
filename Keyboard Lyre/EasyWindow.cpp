#include <Windows.h>
#include <WindowsX.h>
#include <d2d1.h>
#include "EasyWindow.h"

#pragma comment(lib, "d2d1.lib")

// Utilities for alloc / realloc / free memory from process heap.
#define Alloc(bZero, dwBytes)          HeapAlloc(GetProcessHeap(), bZero ? HEAP_ZERO_MEMORY : 0, dwBytes)
#define ReAlloc(bZero, Block, dwBytes) HeapReAlloc(GetProcessHeap(), bZero ? HEAP_ZERO_MEMORY : 0, Block, dwBytes)
#define Free(Block)                    HeapFree(GetProcessHeap(), 0, Block)

// structs for EZArray
typedef struct {
    SIZE_T Capability;
    SIZE_T Cnt;
    SIZE_T cbElemSize;
}EZARRAY, * PEZARRAY;

// D2D
ID2D1Factory* pD2DFactory;

template<class Interface>
inline static void D2DRelease(
    Interface** ppInterfaceToRelease)
{
    (*ppInterfaceToRelease)->Release();
    (*ppInterfaceToRelease) = NULL;
}

// Easy Window Class Name
const WCHAR EasyWindowClassName[] = L"EasyWindowClass";

typedef struct
{
    EZWND ezWnd;
    ID2D1HwndRenderTarget* pRT;
    EZWND MouseAbove; // Which window currently mouse is above, or NULL if mouse is outside the window.
    EZWND FocusWnd;   // Which window currently have input focus, or NULL if none have,
}WND_EXTRA_STRUCT, * PWND_EXTRA;

// for passing params when using CreateWindow
typedef struct
{
    EZWNDPROC ezWndProc;
    LPARAM lpParam;
}CREATE_WINDOW_PARAM_PACK;

// EZArray
// Calculate the capability by count. usually is ceil(log(cnt)), with a few exceptions 
// when cnt = 0 will return 0, and when ceil(log(cnt)) is going to exceed MAXSIZE_T will return MAXSIZE_T.
static SIZE_T EZArrayCalcCap(_In_ SIZE_T Cnt)
{
    if (Cnt >> (sizeof(SIZE_T) * 8 - 1)) // don't think this would be possible, but handle it anyway
        return MAXSIZE_T;
    if (Cnt == 0)
        return 0;

    SIZE_T Capability = 1;
    if (Cnt & (Cnt - 1)) Capability = 2; // check if Cnt is power of 2.

    // binary split & scan, takes only log(bit) step.
    for (UINT MaskLen = sizeof(SIZE_T) * 8 / 2; MaskLen; MaskLen >>= 1)
    {
        SIZE_T Mask = (((SIZE_T)1 << MaskLen) - 1) << MaskLen;
        if (Cnt & Mask)
        {
            Capability <<= MaskLen;
            Cnt >>= MaskLen;
        }
    }
    return Capability;
}

// Allocate an EZArray.
// Return the array address directly.
// Return NULL when failed to allocate memory.
EXTERN_C LPVOID AllocEZArray(
    _In_ SIZE_T cbElemSize,
    _In_ SIZE_T InitialCnt)
{
    SIZE_T InitialCap = EZArrayCalcCap(InitialCnt);
    PEZARRAY pEZArray = (PEZARRAY)Alloc(TRUE, sizeof(EZARRAY) + cbElemSize * InitialCap);
    if (!pEZArray) return NULL;

    pEZArray->Capability = InitialCap;
    pEZArray->Cnt = InitialCnt;
    pEZArray->cbElemSize = cbElemSize;

    return (LPVOID)((PBYTE)pEZArray + sizeof(EZARRAY));
}

// Free an EZArray.
// Return FALSE when failed, call GetLastError for extended error information.
EXTERN_C BOOL FreeEZArray(_In_ _Frees_ptr_ LPVOID lpArray)
{
    PEZARRAY pEZArray = (PEZARRAY)((PBYTE)lpArray - sizeof(EZARRAY));
    return Free(pEZArray);
}

// Modify the count of the element of an EZArray.
// Return the array address, might or might not be modified.
// Return NULL when failed to reallocate memory. Notice in this case the original memory is not freed.
EXTERN_C LPVOID ResizeEZArray(
    _Inout_ LPVOID lpArray,
    _In_ SIZE_T NewCnt)
{
    PEZARRAY pEZArray = (PEZARRAY)((PBYTE)lpArray - sizeof(EZARRAY));
    SIZE_T NewCap = EZArrayCalcCap(NewCnt);

    if (NewCap != pEZArray->Capability)
    {
        pEZArray = (PEZARRAY)ReAlloc(TRUE, pEZArray, sizeof(EZARRAY) + pEZArray->cbElemSize * NewCap);
        if (!pEZArray) return NULL;

        pEZArray->Capability = NewCap;
        pEZArray->Cnt = NewCnt;
    }
    return (LPVOID)((PBYTE)pEZArray + sizeof(EZARRAY));
}

// Send a message to a window by calling the procedure directly.
// Unlike the WINAPI SendMessage, EZSendMessage does not do any check about thread.
EXTERN_C LRESULT EZSendMessage(
    _In_ EZWND ezWnd,
    _In_ UINT message,
    _Pre_maybenull_ _Post_valid_ WPARAM wParam,
    _Pre_maybenull_ _Post_valid_ LPARAM lParam)
{
    return ezWnd->Proc(ezWnd, message, wParam, lParam);
}

// Send EZWM_CALCCLIENT to a window in order to update client area
static void IntEZSendCalcClient(_Inout_ EZWND ezWnd)
{
    D2D_RECT_F RectClient = { 0.0f, 0.0f, ezWnd->Width, ezWnd->Height }; // perhaps define RECT_F by ourself?
    EZSendMessage(ezWnd, EZWM_CALCCLIENT, (WPARAM)&RectClient, NULL);

    ezWnd->cx = RectClient.left;
    ezWnd->cy = RectClient.top;
    ezWnd->cWidth = RectClient.right - RectClient.left;
    ezWnd->cHeight = RectClient.bottom - RectClient.top;
}

// Move a EZWnd. Updates window coordinates and call IntEZSendCalcClient to update client area.
// Will Send EZWM_MOVE / EZWM_SIZE if corresponding client coordinates is changed.
static BOOL IntEZMoveWnd(
    _Inout_ EZWND ezWnd,
    _In_ FLOAT x,
    _In_ FLOAT y,
    _In_ FLOAT Width,
    _In_ FLOAT Height)
{
    Width = max(Width, 0);
    Height = max(Height, 0);

    FLOAT OldX = ezWnd->x + ezWnd->cx;
    FLOAT OldY = ezWnd->y + ezWnd->cy;
    FLOAT OldW = ezWnd->cWidth;
    FLOAT OldH = ezWnd->cHeight;

    ezWnd->x = x;
    ezWnd->y = y;
    ezWnd->Width = Width;
    ezWnd->Height = Height;

    IntEZSendCalcClient(ezWnd);

    if (ezWnd->cWidth != OldW || ezWnd->cHeight != OldH)
        EZSendMessage(ezWnd, EZWM_SIZE, NULL, NULL);

    if (x + ezWnd->cx != OldX || y + ezWnd->cy != OldY)
        EZSendMessage(ezWnd, EZWM_MOVE, NULL, NULL);

    return TRUE;
}

// Free all related resource, all child window, and free the memory.
// Do nothing to siblings and parent, including detaching this window from linked list.
static BOOL IntDestroyEZWnd(_In_ _Frees_ptr_ EZWND ezWnd)
{
    EZSendMessage(ezWnd, EZWM_DESTROY, NULL, NULL);

    // Free all child window as well.
    for (EZWND ChildWnd = ezWnd->FirstChild; ChildWnd;)
    {
        EZWND NextWnd = ChildWnd->NextWnd;
        IntDestroyEZWnd(ChildWnd);
        ChildWnd = NextWnd;
    }

    for (EZWND ChildWnd = ezWnd->FirstNCChild; ChildWnd;)
    {
        EZWND NextWnd = ChildWnd->NextWnd;
        IntDestroyEZWnd(ChildWnd);
        ChildWnd = NextWnd;
    }

    if (ezWnd->pExtra)
        Free(ezWnd->pExtra);

    Free(ezWnd);
    return TRUE;
}

// Allocate space and assign field, append to linked list if needed.
// Send EZWM_CREATE and other messages.
// return valid EZWND if success.
static EZWND IntCreateEZWnd(
    _In_opt_ EZWND ezParent,
    _In_opt_ HWND hwndBase,
    _In_ BOOL IsNonClient,
    _In_ FLOAT x,
    _In_ FLOAT y,
    _In_ FLOAT Width,
    _In_ FLOAT Height,
    _In_ EZWNDPROC ezWndProc,
    _In_opt_ LPARAM lpParam)
{
    EZWND ezWnd = (EZWND)Alloc(TRUE, sizeof(EZWND_STRUCT));
    if (!ezWnd)
        return FALSE;

    ezWnd->ParentWnd = ezParent;
    ezWnd->hwndBase = hwndBase;
    ezWnd->IsNonClient = IsNonClient;
    ezWnd->x = x;
    ezWnd->y = y;
    ezWnd->Width = Width;
    ezWnd->Height = Height;
    ezWnd->NCOpacity = 1.0f;
    ezWnd->Opacity = 1.0f;
    ezWnd->Proc = ezWndProc;

    LRESULT Result = EZSendMessage(ezWnd, EZWM_CREATE, NULL, lpParam);

    if (Result == -1) // failure
    {
        Free(ezWnd);
        return NULL;
    }

    if (ezParent)
    {
        // Append the window to the begining of linked list
        if (IsNonClient)
        {
            if (ezParent->FirstNCChild)
            {
                ezWnd->NextWnd = ezParent->FirstNCChild;
                ezParent->FirstNCChild->LastWnd = ezWnd;
            }
            ezParent->FirstNCChild = ezWnd;
        }
        else
        {
            if (ezParent->FirstChild)
            {
                ezWnd->NextWnd = ezParent->FirstChild;
                ezParent->FirstChild->LastWnd = ezWnd;
            }
            ezParent->FirstChild = ezWnd;
        }
    }

    IntEZSendCalcClient(ezWnd);

    EZSendMessage(ezWnd, EZWM_SIZE, NULL, NULL);
    EZSendMessage(ezWnd, EZWM_MOVE, NULL, NULL);

    return ezWnd;
}

// Get the intersect area of two D2D1_RECT_F.
// if there's no intersect, all field will be set to 0
static VOID D2DRectIntersect(_In_ D2D1_RECT_F* R1, _In_ D2D1_RECT_F* R2, _Out_ D2D1_RECT_F* Result)
{
    Result->left = max(R1->left, R2->left);
    Result->right = min(R1->right, R2->right);

    Result->top = max(R1->top, R2->top);
    Result->bottom = min(R1->bottom, R2->bottom);

    if (Result->right < Result->left || Result->bottom < Result->top)
    {
        Result->left = Result->right = Result->top = Result->bottom = 0.f;
    }
}

// Render EZWindow (and all it's child recursively) to a render target.
// Transform is the transform supposed to apply after translation.
// Border is parent window render bounds.
// won't call pRT->BeginDraw() and pRT->EndDraw(). You must call then before/after this function by yourself.
static BOOL IntEZRenderToTarget(
    _In_ EZWND ezWnd,
    _In_ ID2D1RenderTarget* pRT,
    _In_ D2D1_RECT_F Border)
{
    // Handle non-client (and child) render first
    // Then render client area (and child)
    D2D1_RECT_F WindowPositon =
    { ezWnd->x,
      ezWnd->y,
      ezWnd->x + ezWnd->Width,
      ezWnd->y + ezWnd->Height };
    D2D1_RECT_F ClientPositon =
    { ezWnd->cx,
      ezWnd->cy,
      ezWnd->cx + ezWnd->cWidth,
      ezWnd->cy + ezWnd->cHeight };

    DRAW_TARGET NCDrawTarget = { pRT };
    DRAW_TARGET DrawTarget = { pRT };

    // TODO: Consider keep the layer and mask resources in ezWnd, and don't create it everytime.
    ID2D1Layer* pNCLayer = NULL;
    ID2D1Layer* pLayer = NULL;

    ID2D1RectangleGeometry* NCRectMask = NULL;
    ID2D1RectangleGeometry* RectMask = NULL;

    D2D1::Matrix3x2F OldTransform;

    BOOL bSuccess = FALSE;
    __try
    {
        // Get the old transform and restore later.
        pRT->GetTransform(&OldTransform);

        HRESULT hResult;
        hResult = pRT->CreateLayer(&pNCLayer);
        if (FAILED(hResult))
            __leave;
        hResult = pRT->CreateLayer(&pLayer);
        if (FAILED(hResult))
            __leave;

        hResult = pD2DFactory->CreateRectangleGeometry(WindowPositon, &NCRectMask);
        if (FAILED(hResult))
            __leave;

        hResult = pD2DFactory->CreateRectangleGeometry(ClientPositon, &RectMask);
        if (FAILED(hResult))
            __leave;

        pRT->PushLayer(
            D2D1::LayerParameters(
                D2D1::InfiniteRect(),
                NCRectMask,
                D2D1_ANTIALIAS_MODE_PER_PRIMITIVE,
                D2D1::IdentityMatrix(),
                ezWnd->NCOpacity,
                NULL,
                D2D1_LAYER_OPTIONS_NONE),
            pNCLayer);

        D2D1::Matrix3x2F WindowTransform;
        WindowTransform = D2D1::Matrix3x2F::Translation(ezWnd->x, ezWnd->y) * OldTransform;
        pRT->SetTransform(WindowTransform);

        EZSendMessage(ezWnd, EZWM_NCRENDER, (WPARAM)&NCDrawTarget, NULL);

        for (EZWND ezChild = ezWnd->FirstNCChild; ezChild; ezChild = ezChild->NextWnd)
        {
            IntEZRenderToTarget(ezChild, pRT, { 0.0, 0.0, (float)ezWnd->Width, (float)ezWnd->Height });
        }

        pRT->PushLayer(
            D2D1::LayerParameters(
                D2D1::InfiniteRect(),
                RectMask,
                D2D1_ANTIALIAS_MODE_PER_PRIMITIVE,
                D2D1::IdentityMatrix(),
                ezWnd->Opacity,
                NULL,
                D2D1_LAYER_OPTIONS_NONE),
            pLayer);

        D2D1::Matrix3x2F ClientTransform;
        ClientTransform = D2D1::Matrix3x2F::Translation(ezWnd->x + ezWnd->cx, ezWnd->y + ezWnd->cy) * OldTransform;
        pRT->SetTransform(ClientTransform);

        EZSendMessage(ezWnd, EZWM_RENDER, (WPARAM)&DrawTarget, NULL);

        for (EZWND ezChild = ezWnd->FirstChild; ezChild; ezChild = ezChild->NextWnd)
        {
            IntEZRenderToTarget(ezChild, pRT, { 0.0, 0.0, (float)ezWnd->cWidth, (float)ezWnd->cHeight });
        }

        // restore transform.
        pRT->SetTransform(OldTransform);

        pRT->PopLayer();
        pRT->PopLayer();
        bSuccess = TRUE;
    }
    __finally
    {
        if (pNCLayer)   D2DRelease(&pNCLayer);
        if (pLayer)     D2DRelease(&pLayer);
        if (RectMask)   D2DRelease(&RectMask);
        if (NCRectMask) D2DRelease(&NCRectMask);
    }

    return bSuccess;
}

static UINT IntMouseMsgWinToEZ(UINT message, BOOL IsNC)
{
    UINT ezMessage = 0;
    switch (message)
    {
    case WM_LBUTTONDOWN:
        ezMessage = IsNC ? EZWM_NCLBUTTONDOWN : EZWM_LBUTTONDOWN;
        break;
    case WM_LBUTTONUP:
        ezMessage = IsNC ? EZWM_NCLBUTTONUP : EZWM_LBUTTONUP;
        break;
    case WM_RBUTTONDOWN:
        ezMessage = IsNC ? EZWM_NCRBUTTONDOWN : EZWM_RBUTTONDOWN;
        break;
    case WM_RBUTTONUP:
        ezMessage = IsNC ? EZWM_NCRBUTTONUP : EZWM_RBUTTONUP;
        break;
    case WM_MOUSEMOVE:
        ezMessage = IsNC ? EZWM_NCMOUSEMOVE : EZWM_MOUSEMOVE;
        break;
    }
    return ezMessage;
}

// Send a mouse event.
// x and y is the mouse position in the window coordinates.
// return TRUE if click success.
// return FALSE if window is transparent, and the click should fall to the underlying window.
static BOOL IntEZClickWindow(_In_ EZWND ezWnd, _In_ UINT message, _In_ WPARAM wParam, _In_ FLOAT x, _In_ FLOAT y, _Inout_ PWND_EXTRA pWndExt)
{
    POINTFLOAT pt = { x, y };
    UINT Part = (UINT)EZSendMessage(ezWnd, EZWM_NCHITTEST, 0, (LPARAM)&pt);
    if (Part == EZHT_DEFAULT)
    {
        if (x >= ezWnd->cx &&
            y >= ezWnd->cy &&
            x < ezWnd->cx + ezWnd->cWidth &&
            y < ezWnd->cy + ezWnd->cHeight)
        {
            Part = EZHT_CLIENT;
        }
        else
        {
            Part = EZHT_NONCLIENT;
        }
    }

    if (Part == EZHT_CLIENT)
    {
        for (EZWND ezChild = ezWnd->FirstChild; ezChild; ezChild = ezChild->NextWnd)
        {
            FLOAT xChild = x - ezWnd->cx - ezChild->x;
            FLOAT yChild = y - ezWnd->cy - ezChild->y;
            if (xChild >= 0 &&
                yChild >= 0 &&
                xChild < ezChild->Width &&
                yChild < ezChild->Height)
            {
                if (IntEZClickWindow(ezChild, message, wParam, xChild, yChild, pWndExt))
                    return TRUE;
            }
        }

        if (pWndExt->MouseAbove != ezWnd)
        {
            if (pWndExt->MouseAbove)
            {
                EZSendMessage(pWndExt->MouseAbove, EZWM_MOUSELEAVE, (WPARAM)ezWnd, NULL);
            }
            EZWND OldWnd = pWndExt->MouseAbove;
            pWndExt->MouseAbove = ezWnd;
            EZSendMessage(ezWnd, EZWM_MOUSECOME, (WPARAM)OldWnd, NULL);
        }

        if (message == WM_LBUTTONDOWN && pWndExt->FocusWnd != ezWnd)
        {
            if (pWndExt->FocusWnd)
            {
                EZSendMessage(pWndExt->MouseAbove, EZWM_KILLFOCUS, (WPARAM)ezWnd, NULL);
            }
            EZWND OldWnd = pWndExt->FocusWnd;
            pWndExt->FocusWnd = ezWnd;
            EZSendMessage(ezWnd, EZWM_SETFOCUS, (WPARAM)OldWnd, NULL);
        }

        MOUSE_INFO mi = { x - ezWnd->cx, y - ezWnd->cy };
        EZSendMessage(ezWnd, IntMouseMsgWinToEZ(message, FALSE), wParam, (LPARAM)&mi);
        return TRUE;
    }
    else if (Part == EZHT_NONCLIENT)
    {
        for (EZWND ezChild = ezWnd->FirstNCChild; ezChild; ezChild = ezChild->NextWnd)
        {
            FLOAT xChild = x - ezChild->x;
            FLOAT yChild = y - ezChild->y;
            if (xChild >= 0 &&
                yChild >= 0 &&
                xChild < ezChild->Width &&
                yChild < ezChild->Height)
            {
                if (IntEZClickWindow(ezChild, message, wParam, xChild, yChild, pWndExt))
                    return TRUE;
            }
        }

        if (pWndExt->MouseAbove != ezWnd)
        {
            if (pWndExt->MouseAbove)
            {
                EZSendMessage(pWndExt->MouseAbove, EZWM_MOUSELEAVE, (WPARAM)ezWnd, NULL);
            }
            EZWND OldWnd = pWndExt->MouseAbove;
            pWndExt->MouseAbove = ezWnd;
            EZSendMessage(ezWnd, EZWM_MOUSECOME, (WPARAM)OldWnd, NULL);
        }

        MOUSE_INFO mi = { x, y };
        EZSendMessage(ezWnd, IntMouseMsgWinToEZ(message, TRUE), wParam, (LPARAM)&mi);
        return TRUE;
    }
    else if (Part == EZHT_NOWHERE)
    {
        return TRUE;
    }
    else if (Part == EZHT_TRANSPARENT)
    {
        return FALSE;
    }

    // TODO: assert here
    return TRUE;
}

// Broadcast EZWM_DISCARD_RES to all child window (include NC) and self.
static VOID IntEZSendDiscardResource(_In_ EZWND ezWnd)
{
    for (EZWND ezChild = ezWnd->FirstChild; ezChild; ezChild = ezChild->NextWnd)
    {
        IntEZSendDiscardResource(ezChild);
    }
    for (EZWND ezChild = ezWnd->FirstNCChild; ezChild; ezChild = ezChild->NextWnd)
    {
        IntEZSendDiscardResource(ezChild);
    }
    EZSendMessage(ezWnd, EZWM_DISCARD_RES, NULL, NULL);
}

// Window procedure for Root EZWnd
static LRESULT CALLBACK EZRootWndProc(
    HWND hwnd,
    UINT message,
    WPARAM wParam,
    LPARAM lParam)
{
    PWND_EXTRA pWndExt = NULL;
    EZWND ezWnd = NULL;

    if (message == WM_CREATE)
    {
        CREATE_WINDOW_PARAM_PACK* pParamPack = (CREATE_WINDOW_PARAM_PACK*)(((LPCREATESTRUCTW)lParam)->lpCreateParams);
        EZWNDPROC pWndProc = pParamPack->ezWndProc;
        RECT rect;
        GetClientRect(hwnd, &rect);
        pWndExt = (PWND_EXTRA)Alloc(TRUE, sizeof(WND_EXTRA_STRUCT));
        if (!pWndExt)
            return -1;

        ezWnd = IntCreateEZWnd(NULL, hwnd, FALSE, 0.0f, 0.0f, (FLOAT)(rect.right - rect.left), (FLOAT)(rect.bottom - rect.top), pWndProc, pParamPack->lpParam);
        if (!ezWnd)
        {
            Free(pWndExt);
            return -1; // Destroy the window and fail CreateWindow(Ex).
        }

        pWndExt->ezWnd = ezWnd;

        // in order to receive WM_MOUSELEAVE and WM_MOUSEHOVER
        TRACKMOUSEEVENT tme;
        tme.cbSize = sizeof(TRACKMOUSEEVENT);
        tme.dwFlags = TME_HOVER | TME_LEAVE;
        tme.dwHoverTime = HOVER_DEFAULT;
        tme.hwndTrack = hwnd;

        if (!TrackMouseEvent(&tme))
            return -1;

        SetWindowLongPtrW(hwnd, 0, (LONG_PTR)pWndExt);
        return 0;
    }

    pWndExt = (PWND_EXTRA)GetWindowLongPtrW(hwnd, 0);
    if (!pWndExt) // some message could arrive before WM_CREATE, for example WM_NCCREATE (and more)
    {
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }

    ezWnd = pWndExt->ezWnd;
    switch (message)
    {
    case WM_PAINT:
    {
        // TODO: error check missing here
        PAINTSTRUCT ps;
        BeginPaint(hwnd, &ps);

        if (!pWndExt->pRT)
        {
            D2D1_SIZE_U size = D2D1::SizeU((UINT32)ezWnd->Width, (UINT32)ezWnd->Height);
            HRESULT hResult = pD2DFactory->CreateHwndRenderTarget(
                D2D1::RenderTargetProperties(),
                D2D1::HwndRenderTargetProperties(hwnd, size),
                &pWndExt->pRT);

            if (FAILED(hResult))
            {
                EndPaint(hwnd, &ps);
                break;
            }
        }

        pWndExt->pRT->BeginDraw();
        BOOL bRet = IntEZRenderToTarget(ezWnd, pWndExt->pRT, D2D1::RectF(0.f, 0.f, D2D1::FloatMax(), D2D1::FloatMax()));
        HRESULT hResult = pWndExt->pRT->EndDraw();

        if (FAILED(hResult) || hResult == D2DERR_RECREATE_TARGET)
        {
            IntEZSendDiscardResource(ezWnd);
            D2DRelease(&pWndExt->pRT);
        }
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_SIZE:
    {
        D2D1_SIZE_U size = { (UINT32)GET_X_LPARAM(lParam), (UINT32)GET_Y_LPARAM(lParam) };

        if (pWndExt->pRT)
            pWndExt->pRT->Resize(size);

        IntEZMoveWnd(ezWnd, ezWnd->x, ezWnd->y, (FLOAT)size.width, (FLOAT)size.height);

        return 0;
    }
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
    case WM_RBUTTONDOWN:
    case WM_RBUTTONUP:
    case WM_MOUSEMOVE:
    {
        FLOAT x = (FLOAT)GET_X_LPARAM(lParam);
        FLOAT y = (FLOAT)GET_Y_LPARAM(lParam);

        IntEZClickWindow(ezWnd, message, wParam, x, y, pWndExt);
        return 0;
    }
    case WM_MOUSELEAVE:
    {
        if (pWndExt->MouseAbove)
        {
            EZSendMessage(pWndExt->MouseAbove, EZWM_MOUSELEAVE, NULL, NULL);
            pWndExt->MouseAbove = NULL;
        }
        return 0;
    }
    case WM_KEYDOWN:
    {
        if (pWndExt->FocusWnd)
        {
            EZSendMessage(pWndExt->FocusWnd, EZWM_KEYDOWN, wParam, lParam);
        }
        return 0;
    }
    case WM_KEYUP:
    {
        if (pWndExt->FocusWnd)
        {
            EZSendMessage(pWndExt->FocusWnd, EZWM_KEYUP, wParam, lParam);
        }
        return 0;
    }
    case WM_CHAR:
    {
        if (pWndExt->FocusWnd)
        {
            EZSendMessage(pWndExt->FocusWnd, EZWM_CHAR, wParam, lParam);
        }
        return 0;
    }
    case WM_DESTROY:
    {
        IntDestroyEZWnd(ezWnd);
        if (pWndExt->pRT)
        {
            IntEZSendDiscardResource(ezWnd);
            D2DRelease(&pWndExt->pRT);
        }
        Free(pWndExt);
        SetWindowLongPtrW(hwnd, 0, (LONG_PTR)0);
        return 0;
    }
    }

    return DefWindowProcW(hwnd, message, wParam, lParam);
}

// Initialize EasyWindow Library. Call before calling any other EasyWindow functions.
EXTERN_C BOOL InitEZWnd()
{
    // Register Window Class
    WNDCLASSW WndClass = { 0 };
    WndClass.style = CS_HREDRAW | CS_VREDRAW;
    WndClass.lpfnWndProc = EZRootWndProc;
    WndClass.cbClsExtra = 0;
    WndClass.cbWndExtra = sizeof(LONG_PTR);
    WndClass.hInstance = GetModuleHandleW(NULL);
    WndClass.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    WndClass.hCursor = LoadCursor(NULL, IDC_ARROW);
    WndClass.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);
    WndClass.lpszMenuName = NULL;
    WndClass.lpszClassName = EasyWindowClassName;

    if (!RegisterClassW(&WndClass))
        return FALSE;

    // Initialize Direct 2D
    pD2DFactory = NULL;
    HRESULT hr = D2D1CreateFactory(
        D2D1_FACTORY_TYPE_SINGLE_THREADED,
        &pD2DFactory
    );

    if (FAILED(hr))
        return FALSE;

    return TRUE;
}

// Uninitialize EasyWindow Library.
EXTERN_C BOOL UninitEZWnd()
{
    D2DRelease(&pD2DFactory);
    return UnregisterClassW(EasyWindowClassName, GetModuleHandleW(NULL));
}

EXTERN_C EZWND CreateEZRootWnd(
    _In_opt_ HWND hParent,
    _In_ DWORD WinExStyle,
    _In_ DWORD WinStyle,
    _In_opt_ LPCWSTR lpWindowName,
    _In_ INT x,
    _In_ INT y,
    _In_ INT Width,
    _In_ INT Height,
    _In_ EZWNDPROC ezWndProc,
    _In_opt_ LPARAM lpParam)
{
    CREATE_WINDOW_PARAM_PACK ParamPack = { ezWndProc, lpParam };
    HWND hwnd = CreateWindowExW(WinExStyle,
        EasyWindowClassName,
        lpWindowName,
        WinStyle,
        x, y, Width, Height,
        hParent,
        (HMENU)0,
        GetModuleHandleW(NULL),
        &ParamPack);

    if (!hwnd)
    {
        return NULL;
    }

    PWND_EXTRA pWndExt = (PWND_EXTRA)GetWindowLongPtrW(hwnd, 0);
    return pWndExt->ezWnd;
}

EXTERN_C EZWND CreateEZWnd(
    _In_ EZWND ezParent,
    _In_ FLOAT x,
    _In_ FLOAT y,
    _In_ FLOAT Width,
    _In_ FLOAT Height,
    _In_ EZWNDPROC ezWndProc,
    _In_opt_ LPARAM lpParam)
{
    return IntCreateEZWnd(ezParent, ezParent->hwndBase, FALSE, x, y, Width, Height, ezWndProc, lpParam);
}

EXTERN_C PVOID EZGetExtra(
    _In_ EZWND ezWnd)
{
    return ezWnd->pExtra;
}

EXTERN_C PVOID EZSetCbExtra(
    _In_ EZWND ezWnd,
    _In_ SIZE_T cbExtra)
{
    if (ezWnd->pExtra)
    {
        PVOID NewExtra = ReAlloc(TRUE, ezWnd->pExtra, cbExtra);
        if (!NewExtra) return NULL;
        return ezWnd->pExtra = NewExtra;
    }
    else
    {
        return ezWnd->pExtra = Alloc(TRUE, cbExtra);
    }
}

EXTERN_C BOOL EZMoveWnd(
    _In_ EZWND ezWnd,
    _In_ FLOAT x,
    _In_ FLOAT y,
    _In_ FLOAT Width,
    _In_ FLOAT Height)
{
    return IntEZMoveWnd(ezWnd, x, y, Width, Height);
}

EXTERN_C BOOL DestroyEZWnd(_In_ _Frees_ptr_ EZWND ezWnd)
{
    if (ezWnd->ParentWnd)
    {
        // Detach this window from it's parent and siblings first.
        if (ezWnd->LastWnd)
        {
            ezWnd->LastWnd->NextWnd = ezWnd->NextWnd;
        }
        else
        {
            if (ezWnd->IsNonClient)
            {
                ezWnd->FirstNCChild = ezWnd->NextWnd;
            }
            else
            {
                ezWnd->FirstChild = ezWnd->NextWnd;
            }
        }

        if (ezWnd->NextWnd)
        {
            ezWnd->NextWnd->LastWnd = ezWnd->LastWnd;
        }

        return IntDestroyEZWnd(ezWnd);
    }
    else
    {
        // everything will be handled when handling WM_DESTROY
        // to keep behavior same as if we call DestroyWindow directly.
        return DestroyWindow(ezWnd->hwndBase);
    }
}

EXTERN_C WPARAM EZWndMessageLoop()
{
    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return msg.wParam;
}
