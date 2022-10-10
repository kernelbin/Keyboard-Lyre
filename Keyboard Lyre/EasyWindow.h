#pragma once

#include <Windows.h>
#include <d2d1.h>

EXTERN_C_START

// Easy Window message defines.

// Send when a window is creating. return -1 to indicate failure.
#define EZWM_CREATE        1

// Send when a window is destorying.
#define EZWM_DESTROY       2

// Send when a window is moved.
#define EZWM_MOVE          3

// Send when a window is resized.
#define EZWM_SIZE          4

// Send when window is requested to render.
// wParam is a pointer to DRAW_TARGET.
#define EZWM_NCRENDER      5 // non-client render
#define EZWM_RENDER        6 // client area render

// Send when Device dependent resources should be discarded. for example Render Target is recreated.
#define EZWM_DISCARD_RES   7

// Send when client area need to be calcutated.
// WPARAM is a pointer to D2D_RECT_F, contain window area when passed in. It's supposed to be modified to client area after handling.
#define EZWM_CALCCLIENT    8

// Mouse related message.
// determine what part of the window is at a specific coordinates.
// lParam is a pointer to POINTFLOAT.
#define EZWM_NCHITTEST     9

// wParam indicates whether various virtual keys are down, same as windows.
// lParam is a pointer to MOUSE_INFO
#define EZWM_LBUTTONDOWN   10
#define EZWM_LBUTTONUP     11
#define EZWM_RBUTTONDOWN   12
#define EZWM_RBUTTONUP     13
#define EZWM_MOUSEMOVE     14

#define EZWM_NCLBUTTONDOWN 15
#define EZWM_NCLBUTTONUP   16
#define EZWM_NCRBUTTONDOWN 17
#define EZWM_NCRBUTTONUP   18
#define EZWM_NCMOUSEMOVE   19

// Send when mouse enter / leave a windows's area.
// Mouse move to a child window will also considered as leave.
#define EZWM_MOUSECOME     20 // wParam indicates the window that has lost the mouse.
#define EZWM_MOUSELEAVE    21 // wParam indicates the window that will receive the mouse.

// Send when a window gains/loses input focus.
#define EZWM_SETFOCUS      22 // wParam indicates the window that has lost the focus.
#define EZWM_KILLFOCUS     23 // wParam indicates the window that will receive the focus.

#define EZWM_KEYDOWN       24 // Posted to the window with the keyboard focus when a nonsystem key is pressed (simplly forwarding WM_KEYDOWN).
#define EZWM_KEYUP         25 // Posted to the window with the keyboard focus when a nonsystem key is released (simplly forwarding WM_KEYUP).
#define EZWM_CHAR          26 // Posted to the window with the keyboard focus when a character code is translated (simplly forwarding WM_CHAR).


#define EZWM_COMMAND       27

// For EZWM_NCHITTEST.
#define EZHT_DEFAULT     0
#define EZHT_CLIENT      1
#define EZHT_NONCLIENT   2
#define EZHT_NOWHERE     3
#define EZHT_TRANSPARENT 4

typedef struct _EZWND_STRUCT EZWND_STRUCT, * EZWND;

// EZWnd Callback Proc typedef
typedef LRESULT(CALLBACK* EZWNDPROC)(EZWND, UINT, WPARAM, LPARAM);

// struct for per EZWND
typedef struct _EZWND_STRUCT
{
    // Parent window. NULL if is root window.
    EZWND ParentWnd;

    // Base Windows window it belongs to.
    HWND hwndBase;

    // If this is a non-client window
    BOOL IsNonClient;

    // First child window and first Non-Client child window
    EZWND FirstChild;
    EZWND FirstNCChild;

    // Sibling window linked list. (both child and nc-child use this)
    EZWND LastWnd, NextWnd;

    // Window coordinates.
    // for root window, x and y is always 0, Width and Height is the client area size of native window.
    FLOAT x, y, Width, Height;

    // client coordinates, relative to window coordinates.
    FLOAT cx, cy, cWidth, cHeight;

    // Non-client opacity and client opacity.
    FLOAT NCOpacity, Opacity;

    // Window procedure.
    EZWNDPROC Proc;

    PVOID pExtra;
}EZWND_STRUCT, * EZWND;

// struct for Draw Target
typedef struct _DRAW_TARGET
{
    ID2D1RenderTarget* pRT;
}DRAW_TARGET, * PDRAW_TARGET;

// struct for mouse event.
typedef struct _MOUSE_INFO
{
    FLOAT x, y;
}MOUSE_INFO, * PMOUSE_INFO;

LPVOID AllocEZArray(_In_ SIZE_T cbElemSize, _In_ SIZE_T InitialCnt);

BOOL FreeEZArray(_In_ _Frees_ptr_ LPVOID lpArray);

LPVOID ResizeEZArray(_Inout_ LPVOID lpArray, _In_ SIZE_T NewCnt);


EXTERN_C LRESULT EZSendMessage(
    _In_ EZWND ezWnd,
    _In_ UINT message,
    _Pre_maybenull_ _Post_valid_ WPARAM wParam,
    _Pre_maybenull_ _Post_valid_ LPARAM lParam);

BOOL InitEZWnd();

EZWND CreateEZRootWnd(
    _In_opt_ HWND hParent,
    _In_ DWORD WinExStyle,
    _In_ DWORD WinStyle,
    _In_opt_ LPCWSTR lpWindowName,
    _In_ INT x,
    _In_ INT y,
    _In_ INT Width,
    _In_ INT Height,
    _In_ EZWNDPROC ezWndProc,
    _In_opt_ LPARAM lpParam);

EZWND CreateEZWnd(
    _In_ EZWND ezParent,
    _In_ FLOAT x,
    _In_ FLOAT y,
    _In_ FLOAT Width,
    _In_ FLOAT Height,
    _In_ EZWNDPROC ezWndProc,
    _In_opt_ LPARAM lpParam);

EXTERN_C PVOID EZGetExtra(
    _In_ EZWND ezWnd);

EXTERN_C PVOID EZSetCbExtra(
    _In_ EZWND ezWnd,
    _In_ SIZE_T cbExtra);

EXTERN_C BOOL EZMoveWnd(
    _In_ EZWND ezWnd,
    _In_ FLOAT x,
    _In_ FLOAT y,
    _In_ FLOAT Width,
    _In_ FLOAT Height);

WPARAM EZWndMessageLoop();

EXTERN_C_END
