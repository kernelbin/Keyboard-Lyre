#include <Windows.h>
#include <dwrite.h>
#include <d2d1.h>
#include <dwmapi.h>
#include <wincodec.h>
#include <math.h>

#include "EasyWindow.h"
#include "resource.h"

#include "minisdl_audio.h"
#include "ResourceFontContext.h"

#define TSF_IMPLEMENTATION
#include "tsf.h"

#pragma comment(lib, "Dwmapi.lib")
#pragma comment(lib, "Dwrite.lib")
#pragma comment(lib, "Windowscodecs.lib")

const WCHAR szAppName[] = L"Keyboard Lyre";
DWM_TIMING_INFO DwmTimingInfo;

// WinCodeC stuff
IWICImagingFactory* pWICFactory;

// DirectWrite Stuff
IDWriteFactory* g_dwriteFactory = NULL;
ResourceFontContext fontContext;
IDWriteFontCollection* fontCollection = NULL;

int g_dpiX;
int g_dpiY;

// TinySoundFont stuff
static tsf* g_TinySoundFont;

SRWLOCK tsfLock = SRWLOCK_INIT;

// Direct 2D Stuff
ID2D1SolidColorBrush* pGlobalSolidBrush = NULL;

// for easier access
static EZWND SharpButton, FlatButton;

float pi = (float)acos(-1);

FLOAT PixelsToDipsY(FLOAT y)
{
    return y * 96.0f / g_dpiY;
}

typedef struct
{
    FLOAT Animation1; // Control both button size and color mask
    FLOAT Animation2; // Control both rippling effect and note color

    BOOL MousePressed, KeyPressed;
    int row, col;
} BUTTON_DATA, * PBUTTON_DATA;

typedef struct
{
    int ResID;
    ID2D1Bitmap* pBitmap;
    BOOL bPressed;
} PIC_BUTTON_DATA, *PPIC_BUTTON_DATA;


static void AudioCallback(void* data, Uint8* stream, int len)
{
    // Render the audio samples in float format
    int SampleCount = (len / (2 * sizeof(float))); // 2 output channels
    //AcquireSRWLockExclusive(&tsfLock);
    tsf_render_float(g_TinySoundFont, (float*)stream, SampleCount, 0);
    //ReleaseSRWLockExclusive(&tsfLock);
}



HRESULT LoadResourceBitmap(
    ID2D1RenderTarget* pRenderTarget,
    IWICImagingFactory* pIWICFactory,
    PCWSTR resourceName,
    PCWSTR resourceType,
    UINT destinationWidth,
    UINT destinationHeight,
    ID2D1Bitmap** ppBitmap
)
{
    HRESULT hr = S_OK;
    IWICBitmapDecoder* pDecoder = NULL;
    IWICBitmapFrameDecode* pSource = NULL;
    IWICStream* pStream = NULL;
    IWICFormatConverter* pConverter = NULL;
    IWICBitmapScaler* pScaler = NULL;

    HRSRC imageResHandle = NULL;
    HGLOBAL imageResDataHandle = NULL;
    void* pImageFile = NULL;
    DWORD imageFileSize = 0;

    // Locate the resource.
    imageResHandle = FindResourceW(NULL, resourceName, resourceType);

    hr = imageResHandle ? S_OK : E_FAIL;
    if (SUCCEEDED(hr))
    {
        // Load the resource.
        imageResDataHandle = LoadResource(NULL, imageResHandle);

        hr = imageResDataHandle ? S_OK : E_FAIL;
    }
    if (SUCCEEDED(hr))
    {
        // Lock it to get a system memory pointer.
        pImageFile = LockResource(imageResDataHandle);

        hr = pImageFile ? S_OK : E_FAIL;
    }
    if (SUCCEEDED(hr))
    {
        // Calculate the size.
        imageFileSize = SizeofResource(NULL, imageResHandle);

        hr = imageFileSize ? S_OK : E_FAIL;
    }
    if (SUCCEEDED(hr))
    {
        // Create a WIC stream to map onto the memory.
        hr = pIWICFactory->CreateStream(&pStream);
    }
    if (SUCCEEDED(hr))
    {
        // Initialize the stream with the memory pointer and size.
        hr = pStream->InitializeFromMemory(
            reinterpret_cast<BYTE*>(pImageFile),
            imageFileSize
        );
    }
    if (SUCCEEDED(hr))
    {
        // Create a decoder for the stream.
        hr = pIWICFactory->CreateDecoderFromStream(
            pStream,
            NULL,
            WICDecodeMetadataCacheOnLoad,
            &pDecoder
        );
    }
    if (SUCCEEDED(hr))
    {
        // Create the initial frame.
        hr = pDecoder->GetFrame(0, &pSource);
    }
    if (SUCCEEDED(hr))
    {
        // Convert the image format to 32bppPBGRA
        // (DXGI_FORMAT_B8G8R8A8_UNORM + D2D1_ALPHA_MODE_PREMULTIPLIED).
        hr = pIWICFactory->CreateFormatConverter(&pConverter);
    }
    if (SUCCEEDED(hr))
    {
        // If a new width or height was specified, create an
        // IWICBitmapScaler and use it to resize the image.
        if (destinationWidth != 0 || destinationHeight != 0)
        {
            UINT originalWidth, originalHeight;
            hr = pSource->GetSize(&originalWidth, &originalHeight);
            if (SUCCEEDED(hr))
            {
                if (destinationWidth == 0)
                {
                    FLOAT scalar = static_cast<FLOAT>(destinationHeight) / static_cast<FLOAT>(originalHeight);
                    destinationWidth = static_cast<UINT>(scalar * static_cast<FLOAT>(originalWidth));
                }
                else if (destinationHeight == 0)
                {
                    FLOAT scalar = static_cast<FLOAT>(destinationWidth) / static_cast<FLOAT>(originalWidth);
                    destinationHeight = static_cast<UINT>(scalar * static_cast<FLOAT>(originalHeight));
                }

                hr = pIWICFactory->CreateBitmapScaler(&pScaler);
                if (SUCCEEDED(hr))
                {
                    hr = pScaler->Initialize(
                        pSource,
                        destinationWidth,
                        destinationHeight,
                        WICBitmapInterpolationModeCubic
                    );
                    if (SUCCEEDED(hr))
                    {
                        hr = pConverter->Initialize(
                            pScaler,
                            GUID_WICPixelFormat32bppPBGRA,
                            WICBitmapDitherTypeNone,
                            NULL,
                            0.f,
                            WICBitmapPaletteTypeMedianCut
                        );
                    }
                }
            }
        }
        else
        {
            hr = pConverter->Initialize(
                pSource,
                GUID_WICPixelFormat32bppPBGRA,
                WICBitmapDitherTypeNone,
                NULL,
                0.f,
                WICBitmapPaletteTypeMedianCut
            );
        }
    }
    if (SUCCEEDED(hr))
    {
        //create a Direct2D bitmap from the WIC bitmap.
        hr = pRenderTarget->CreateBitmapFromWicBitmap(
            pConverter,
            NULL,
            ppBitmap
        );
    }

    if (pDecoder) pDecoder->Release();
    if (pSource) pSource->Release();
    if (pStream) pStream->Release();
    if (pConverter) pConverter->Release();
    if (pScaler) pScaler->Release();

    return hr;
}

// Rotate the point 90 degree clockwise with (0, 0) as center (using screen coordinates, not math one)
VOID RotatePoint90(POINTF& a)
{
    a = { -a.y, a.x };
}

VOID PaintNote(ID2D1RenderTarget* pRT, D2D1_POINT_2F Center, FLOAT Width, ID2D1SolidColorBrush* pNoteBrush)
{
    ID2D1EllipseGeometry* pOuterEllipseGeo = NULL;
    ID2D1EllipseGeometry* pInnerEllipseGeo = NULL;
    ID2D1PathGeometry* pCombinedNoteGeo = NULL;
    ID2D1GeometrySink* pGeometrySink = NULL;
    __try
    {
        ID2D1Factory* pD2DFactory;
        pRT->GetFactory(&pD2DFactory);
        if (FAILED(pD2DFactory->CreateEllipseGeometry({ Center, Width, Width * 0.86f }, &pOuterEllipseGeo))) __leave;
        if (FAILED(pD2DFactory->CreateEllipseGeometry({ Center, Width * 0.73f, Width * 0.58f }, &pInnerEllipseGeo))) __leave;
        if (FAILED(pD2DFactory->CreatePathGeometry(&pCombinedNoteGeo))) __leave;
        if (FAILED(pCombinedNoteGeo->Open(&pGeometrySink))) __leave;
        pOuterEllipseGeo->CombineWithGeometry(pInnerEllipseGeo, D2D1_COMBINE_MODE_EXCLUDE, D2D1::Matrix3x2F::Rotation(40.0f, Center), pGeometrySink);
        pGeometrySink->Close();

        pRT->FillGeometry(pCombinedNoteGeo, pNoteBrush);
    }
    __finally
    {
        if (pOuterEllipseGeo) pOuterEllipseGeo->Release();
        if (pInnerEllipseGeo)pInnerEllipseGeo->Release();
        if (pGeometrySink) pGeometrySink->Release();
        if (pCombinedNoteGeo) pCombinedNoteGeo->Release();
    }
}

VOID PaintLedgerLine(ID2D1RenderTarget* pRT, D2D1_POINT_2F Center, FLOAT Width, FLOAT Height, ID2D1SolidColorBrush* pNoteBrush)
{
    D2D1_RECT_F Rect = { Center.x - Width / 2.0f, Center.y - Height / 2.0f, Center.x + Width / 2.0f, Center.y + Height / 2.0f };
    pRT->FillRoundedRectangle({ Rect, Height / 2.0f, Height / 2.0f }, pNoteBrush);
}

VOID PaintButtonBkgnd(ID2D1RenderTarget* pRT, FLOAT cWidth, FLOAT cHeight, FLOAT Animation1)
{
    if (!pGlobalSolidBrush)
        return;

    ID2D1PathGeometry* pPathGeo = NULL;
    ID2D1GeometrySink* pSink = NULL;

    FLOAT Zoom = 0.95f + 0.05f * Animation1;
    __try
    {
        D2D1_ELLIPSE Circle = D2D1::Ellipse({ cWidth / 2.0f, cHeight / 2.0f }, Zoom * cWidth / 4.0f, Zoom * cHeight / 4.0f);

        ID2D1Factory* pD2DFactory;
        pRT->GetFactory(&pD2DFactory);

        if (FAILED(pD2DFactory->CreatePathGeometry(&pPathGeo))) __leave;

        FLOAT DecorationWidth = 0.875f * cWidth / 4.0f;
        FLOAT DecorationInnerWidth = 0.735f * cWidth / 4.0f;
        FLOAT RoundWidth = 0.32f * cWidth / 4.0f;
        pPathGeo->Open(&pSink);

        POINTF Offset[] = {
            { DecorationWidth, 0.0 },
            { 0.0, DecorationWidth },
            { (float)sin(15.0f * pi / 180.0) * DecorationInnerWidth, (float)cos(15.0f * pi / 180.0f) * DecorationInnerWidth},
            { (float)cos(15.0f * pi / 180.0) * DecorationInnerWidth, (float)sin(15.0f * pi / 180.0f) * DecorationInnerWidth}
        };

        for (int i = 0; i < 4; i++)
        {
            pSink->BeginFigure({ cWidth / 2.0f + Offset[0].x * Zoom, cWidth / 2.0f + Offset[0].y * Zoom }, D2D1_FIGURE_BEGIN_FILLED);
            pSink->AddArc({ { cWidth / 2.0f + Offset[1].x * Zoom, cWidth / 2.0f + Offset[1].y * Zoom}, {DecorationWidth * Zoom, DecorationWidth * Zoom}, 0.0f, D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_SMALL });
            pSink->AddArc({ { cWidth / 2.0f + Offset[2].x * Zoom, cWidth / 2.0f + Offset[2].y * Zoom}, {RoundWidth * Zoom, RoundWidth * Zoom}, 0.0f, D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_SMALL });
            pSink->AddArc({ { cWidth / 2.0f + Offset[3].x * Zoom, cWidth / 2.0f + Offset[3].y * Zoom}, {DecorationInnerWidth * Zoom, DecorationInnerWidth * Zoom}, 0.0f, D2D1_SWEEP_DIRECTION_COUNTER_CLOCKWISE, D2D1_ARC_SIZE_SMALL });
            pSink->AddArc({ { cWidth / 2.0f + Offset[0].x * Zoom, cWidth / 2.0f + Offset[0].y * Zoom}, {RoundWidth * Zoom, RoundWidth * Zoom}, 0.0f, D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_SMALL });

            RotatePoint90(Offset[0]);
            RotatePoint90(Offset[1]);
            RotatePoint90(Offset[2]);
            RotatePoint90(Offset[3]);
            pSink->EndFigure(D2D1_FIGURE_END_CLOSED);
        }
        pSink->Close();

        pGlobalSolidBrush->SetColor(D2D1::ColorF(0.99f, 0.97f, 0.92f));
        pRT->FillEllipse(Circle, pGlobalSolidBrush);
        pGlobalSolidBrush->SetColor(D2D1::ColorF(0.92f, 0.92f, 0.85f));
        pRT->FillGeometry(pPathGeo, pGlobalSolidBrush);
        pGlobalSolidBrush->SetColor(D2D1::ColorF(0.0 / 255.0, 240 / 255.0, 210 / 255.0, 0.41 * (1 - Animation1)));
        pRT->FillEllipse(Circle, pGlobalSolidBrush);
    }
    __finally
    {
        if (pPathGeo) pPathGeo->Release();
        if (pSink) pSink->Release();
    }
}

VOID PaintPitch(ID2D1RenderTarget* pRT, FLOAT cWidth, FLOAT cHeight, int col, FLOAT Animation1, FLOAT Animation2)
{
    IDWriteTextFormat* textFormat = NULL;
    __try
    {
        D2D1::ColorF PitchColor =
        { 0.375f * Animation2 + (0.92f - Animation2) * 1.0f,
            0.75f * Animation2 + (0.97f - Animation2) * 1.0f,
            0.62f * Animation2 + (0.97f - Animation2) * 1.0f };

        D2D1::ColorF TextColor =
        { 0.396f * Animation2 + (0.92f - Animation2) * 1.0f,
            0.71f * Animation2 + (0.97f - Animation2) * 1.0f,
            0.77f * Animation2 + (0.97f - Animation2) * 1.0f };

        if (Animation1 == 1.0)
        {
            TextColor = { 0.60f, 0.60f, 0.47f };
        }

        float NoteHeight = 0.83f * 0.34f * cWidth / 4.0f;
        // actually 0.86f * 0.34f * cWidth / 4.0f
        // but we modify it a little for better UI effect

        HRESULT hr = g_dwriteFactory->CreateTextFormat(
            L"HYWenHei 85W",
            fontCollection,
            DWRITE_FONT_WEIGHT_NORMAL,
            DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL,
            PixelsToDipsY(0.165f * cWidth / 2.0f),
            L"en-us",
            &textFormat
        );
        if (FAILED(hr)) __leave;

        textFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);

        WCHAR SyllableNameText[7][3] = { L"do", L"re", L"mi", L"fa", L"so", L"la", L"ti" };
        pGlobalSolidBrush->SetColor(TextColor);
        pRT->DrawTextW(SyllableNameText[col], 2, textFormat, { 0,0.585f * cWidth,cWidth,cWidth }, pGlobalSolidBrush);

        pGlobalSolidBrush->SetColor(PitchColor);

        switch (col)
        {
        case 0:
            PaintNote(pRT, { cWidth / 2.0f, cWidth * 0.475f }, 0.34 * cWidth / 4.0f, pGlobalSolidBrush);
            PaintLedgerLine(pRT, { cWidth / 2.0f,  cWidth * 0.475f }, 0.414f * cWidth / 2.0f, 0.046 * cWidth / 2.0f, pGlobalSolidBrush);
            break;
        case 1:
            PaintNote(pRT, { cWidth / 2.0f, cWidth * 0.475f }, 0.34 * cWidth / 4.0f, pGlobalSolidBrush);
            PaintLedgerLine(pRT, { cWidth / 2.0f,  cWidth * 0.475f + NoteHeight }, 0.414f * cWidth / 2.0f, 0.046 * cWidth / 2.0f, pGlobalSolidBrush);
            break;
        case 2:
            PaintNote(pRT, { cWidth / 2.0f, cWidth * 0.475f }, 0.34 * cWidth / 4.0f, pGlobalSolidBrush);
            PaintLedgerLine(pRT, { cWidth / 2.0f,  cWidth * 0.475f }, 0.414f * cWidth / 2.0f, 0.046 * cWidth / 2.0f, pGlobalSolidBrush);
            PaintLedgerLine(pRT, { cWidth / 2.0f,  cWidth * 0.475f + NoteHeight }, 0.414f * cWidth / 2.0f, 0.046 * cWidth / 2.0f, pGlobalSolidBrush);
            break;
        case 3:
            PaintNote(pRT, { cWidth / 2.0f, cWidth * 0.475f }, 0.34 * cWidth / 4.0f, pGlobalSolidBrush);
            PaintLedgerLine(pRT, { cWidth / 2.0f,  cWidth * 0.475f - NoteHeight }, 0.414f * cWidth / 2.0f, 0.046f * cWidth / 2.0f, pGlobalSolidBrush);
            PaintLedgerLine(pRT, { cWidth / 2.0f,  cWidth * 0.475f + NoteHeight }, 0.414f * cWidth / 2.0f, 0.046f * cWidth / 2.0f, pGlobalSolidBrush);
            break;
        case 4:
            PaintNote(pRT, { cWidth / 2.0f, cWidth * 0.475f }, 0.34 * cWidth / 4.0f, pGlobalSolidBrush);
            PaintLedgerLine(pRT, { cWidth / 2.0f,  cWidth * 0.475f }, 0.414f * cWidth / 2.0f, 0.046f * cWidth / 2.0f, pGlobalSolidBrush);
            PaintLedgerLine(pRT, { cWidth / 2.0f,  cWidth * 0.475f - NoteHeight }, 0.414f * cWidth / 2.0f, 0.046f * cWidth / 2.0f, pGlobalSolidBrush);
            PaintLedgerLine(pRT, { cWidth / 2.0f,  cWidth * 0.475f + NoteHeight }, 0.414f * cWidth / 2.0f, 0.046f * cWidth / 2.0f, pGlobalSolidBrush);
            break;
        case 5:
            PaintNote(pRT, { cWidth / 2.0f, cWidth * 0.445f }, 0.34 * cWidth / 4.0f, pGlobalSolidBrush);
            PaintLedgerLine(pRT, { cWidth / 2.0f,  cWidth * 0.445f - NoteHeight }, 0.414f * cWidth / 2.0f, 0.046f * cWidth / 2.0f, pGlobalSolidBrush);
            PaintLedgerLine(pRT, { cWidth / 2.0f,  cWidth * 0.445f + NoteHeight }, 0.414f * cWidth / 2.0f, 0.046f * cWidth / 2.0f, pGlobalSolidBrush);
            PaintLedgerLine(pRT, { cWidth / 2.0f,  cWidth * 0.5625f }, 0.414f * cWidth / 2.0f, 0.046 * cWidth / 2.0f, pGlobalSolidBrush);
            break;
        case 6:
            PaintNote(pRT, { cWidth / 2.0f, cWidth * 0.445f }, 0.34 * cWidth / 4.0f, pGlobalSolidBrush);
            PaintLedgerLine(pRT, { cWidth / 2.0f,  cWidth * 0.445f }, 0.414f * cWidth / 2.0f, 0.046 * cWidth / 2.0f, pGlobalSolidBrush);
            PaintLedgerLine(pRT, { cWidth / 2.0f,  cWidth * 0.445f - NoteHeight }, 0.414f * cWidth / 2.0f, 0.046f * cWidth / 2.0f, pGlobalSolidBrush);
            PaintLedgerLine(pRT, { cWidth / 2.0f,  cWidth * 0.445f + NoteHeight }, 0.414f * cWidth / 2.0f, 0.046f * cWidth / 2.0f, pGlobalSolidBrush);
            PaintLedgerLine(pRT, { cWidth / 2.0f,  cWidth * 0.5625f }, 0.414f * cWidth / 2.0f, 0.046 * cWidth / 2.0f, pGlobalSolidBrush);
            break;
        }
    }
    __finally
    {
        if (textFormat)textFormat->Release();
    }
}

VOID PaintFlatSharp(ID2D1RenderTarget* pRT, FLOAT cWidth, FLOAT cHeight, int Type, FLOAT Animation1, FLOAT Animation2)
{
    IDWriteTextFormat* textFormat = NULL;
    __try
    {
        D2D1::ColorF PitchColor =
        { 0.375f * Animation2 + (0.92f - Animation2) * 1.0f,
            0.75f * Animation2 + (0.97f - Animation2) * 1.0f,
            0.62f * Animation2 + (0.97f - Animation2) * 1.0f };

        HRESULT hr = g_dwriteFactory->CreateTextFormat(
            L"HYWenHei 85W",
            fontCollection,
            DWRITE_FONT_WEIGHT_NORMAL,
            DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL,
            PixelsToDipsY(0.5f * cWidth / 2.0f),
            L"en-us",
            &textFormat
        );
        if (FAILED(hr)) __leave;
        textFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        textFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

        const WCHAR* FlatSharpString[2] = { L"♭", L"♯" };
        pGlobalSolidBrush->SetColor(PitchColor);
        pRT->DrawTextW(FlatSharpString[Type], wcslen(FlatSharpString[Type]), textFormat, {0,0,cWidth,cWidth}, pGlobalSolidBrush);
    }
    __finally
    {
        if (textFormat)textFormat->Release();
    }
}

VOID PaintRipple(ID2D1RenderTarget* pRT, FLOAT cWidth, FLOAT cHeight, FLOAT Animation2)
{
    if (Animation2 != 1.0f)
    {
        pGlobalSolidBrush->SetColor(D2D1::ColorF(0.0f, 0.8f, 1.0f, 0.7 * (1.0 - Animation2)));

        pRT->DrawEllipse({ {cWidth / 2.0f, cHeight / 2.0f}, cWidth / 4.0f * (1 + 0.5f * Animation2), cWidth / 4.0f * (1 + 0.5f * Animation2) },
            pGlobalSolidBrush, 0.04 * cWidth / 2.0f);
    }
}

VOID ButtonPressed(int col, int row, int offset)
{
    int BaseNote[3] = { 72, 60, 48 };
    int OffsetNote[7] = { 0, 2, 4, 5, 7, 9, 11 };
    int Note = BaseNote[col] + OffsetNote[row] + offset;
    //AcquireSRWLockExclusive(&tsfLock);
    tsf_note_on(g_TinySoundFont, 0, Note, 1.0f);
    //ReleaseSRWLockExclusive(&tsfLock);
}

LRESULT CALLBACK PictureButtonProc(EZWND ezWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    PPIC_BUTTON_DATA pButtonData = (PPIC_BUTTON_DATA)EZGetExtra(ezWnd);
    switch (message)
    {
    case EZWM_CREATE:
        pButtonData = (PPIC_BUTTON_DATA)EZSetCbExtra(ezWnd, sizeof(PIC_BUTTON_DATA));
        if (!pButtonData) return -1;
        pButtonData->ResID = lParam;
        break;
    case EZWM_RENDER:
    {
        PDRAW_TARGET pDT = (PDRAW_TARGET)wParam;
        if (!pButtonData->pBitmap)
            LoadResourceBitmap(pDT->pRT, pWICFactory, MAKEINTRESOURCE(pButtonData->ResID), L"PNG", 0, 0, &pButtonData->pBitmap);

        D2D1_SIZE_F Size = pButtonData->pBitmap->GetSize();
        pDT->pRT->DrawBitmap(pButtonData->pBitmap, { 0, 0, ezWnd->cWidth, ezWnd->cHeight }, pButtonData->bPressed ? 0.5f : 0.9f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, { 0,0,Size.width, Size.height });
        break;
    }
    case EZWM_LBUTTONDOWN:
    {
        pButtonData->bPressed = TRUE;                               
        break;
    }
    case EZWM_LBUTTONUP:
    {
        if (pButtonData->bPressed)
        {
            EZSendMessage(ezWnd->ParentWnd, EZWM_COMMAND, 0, (LPARAM)ezWnd);
            pButtonData->bPressed = FALSE;
        }
        break;
    }
    case EZWM_MOUSELEAVE:
    {
        pButtonData->bPressed = FALSE;
        break;
    }
    case EZWM_DISCARD_RES:
        if (pButtonData->pBitmap)
        {
            pButtonData->pBitmap->Release();
            pButtonData->pBitmap = NULL;
        }
        break;
    }
    return 0;
}

LRESULT CALLBACK SharpFlatButtonProc(EZWND ezWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    PBUTTON_DATA pButtonData = (PBUTTON_DATA)EZGetExtra(ezWnd);
    switch (message)
    {
    case EZWM_CREATE:
    {
        pButtonData = (PBUTTON_DATA)EZSetCbExtra(ezWnd, sizeof(BUTTON_DATA));
        if (!pButtonData) return -1;

        pButtonData->Animation1 = 1.0f;
        pButtonData->Animation2 = 1.0f;

        pButtonData->MousePressed = FALSE;
        int Index = (int)lParam;
        pButtonData->col = Index;

        break;
    }
    case EZWM_RENDER:
    {
        PDRAW_TARGET pDT = (PDRAW_TARGET)wParam;

        float TimeElapsed = (float)DwmTimingInfo.rateCompose.uiDenominator / (float)DwmTimingInfo.rateCompose.uiNumerator;
        if (pButtonData->MousePressed || pButtonData->KeyPressed)
        {
            pButtonData->Animation1 -= (TimeElapsed / 0.02f);
            pButtonData->Animation1 = max(pButtonData->Animation1, 0.0f);
        }
        else
        {
            pButtonData->Animation1 += (TimeElapsed / 0.08f);
            pButtonData->Animation1 = min(pButtonData->Animation1, 1.0f);
        }

        if (pButtonData->Animation2 != 1.0f)
        {
            pButtonData->Animation2 += (TimeElapsed / 0.4f);
            pButtonData->Animation2 = min(pButtonData->Animation2, 1.0f);
        }

        PaintButtonBkgnd(pDT->pRT, ezWnd->cWidth, ezWnd->cHeight, pButtonData->Animation1);
        PaintFlatSharp(pDT->pRT, ezWnd->cWidth, ezWnd->cHeight, pButtonData->col, pButtonData->Animation1, pButtonData->Animation2);
        PaintRipple(pDT->pRT, ezWnd->cWidth, ezWnd->cHeight, pButtonData->Animation2);

        return 0;
    }
    case EZWM_NCHITTEST:
    {
        PPOINTFLOAT pPoint = (PPOINTFLOAT)lParam;
        FLOAT dx = pPoint->x - ezWnd->cWidth / 2.0f;
        FLOAT dy = pPoint->y - ezWnd->cHeight / 2.0f;
        FLOAT Zoom = 0.95f + 0.05f * pButtonData->Animation1;
        FLOAT VisualButtonWidth = ezWnd->cHeight / 4.0f * Zoom;
        if (dx * dx + dy * dy > VisualButtonWidth * VisualButtonWidth)
            return EZHT_TRANSPARENT;
        return EZHT_DEFAULT;
    }
    case EZWM_LBUTTONDOWN:
    {
        pButtonData->MousePressed = TRUE;
        pButtonData->Animation2 = 0.0;
        return 0;
    }
    case EZWM_LBUTTONUP:
    {
        pButtonData->MousePressed = FALSE;
        return 0;
    }
    case EZWM_MOUSELEAVE:
    {
        pButtonData->MousePressed = FALSE;
        return 0;
    }
    case EZWM_KEYDOWN:
    case EZWM_KEYUP:
    {
        // forward it to parent.
        EZSendMessage(ezWnd->ParentWnd, message, wParam, lParam);
        break;
    }
    }
    return 0;
}

int GetCurrentNoteOffset()
{
    int offset = 0;
    PBUTTON_DATA pFlatData = (PBUTTON_DATA)EZGetExtra(FlatButton);
    PBUTTON_DATA pSharpData = (PBUTTON_DATA)EZGetExtra(SharpButton);
    if (pFlatData->KeyPressed || pFlatData->MousePressed)
    {
        offset--;
    }
    if (pSharpData->KeyPressed || pSharpData->MousePressed)
    {
        offset++;
    }
    return offset;
}

LRESULT CALLBACK NoteButtonProc(EZWND ezWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    PBUTTON_DATA pButtonData = (PBUTTON_DATA)EZGetExtra(ezWnd);
    switch (message)
    {
    case EZWM_CREATE:
    {
        pButtonData = (PBUTTON_DATA)EZSetCbExtra(ezWnd, sizeof(BUTTON_DATA));
        if (!pButtonData) return -1;

        pButtonData->Animation1 = 1.0f;
        pButtonData->Animation2 = 1.0f;

        pButtonData->MousePressed = FALSE;
        int Index = (int)lParam;
        pButtonData->row = Index / 7;
        pButtonData->col = Index % 7;
        break;
    }
    case EZWM_RENDER:
    {
        PDRAW_TARGET pDT = (PDRAW_TARGET)wParam;

        float TimeElapsed = (float)DwmTimingInfo.rateCompose.uiDenominator / (float)DwmTimingInfo.rateCompose.uiNumerator;
        if (pButtonData->MousePressed || pButtonData->KeyPressed)
        {
            pButtonData->Animation1 -= (TimeElapsed / 0.02f);
            pButtonData->Animation1 = max(pButtonData->Animation1, 0.0f);
        }
        else
        {
            pButtonData->Animation1 += (TimeElapsed / 0.08f);
            pButtonData->Animation1 = min(pButtonData->Animation1, 1.0f);
        }

        if (pButtonData->Animation2 != 1.0f)
        {
            pButtonData->Animation2 += (TimeElapsed / 0.4f);
            pButtonData->Animation2 = min(pButtonData->Animation2, 1.0f);
        }

        PaintButtonBkgnd(pDT->pRT, ezWnd->cWidth, ezWnd->cHeight, pButtonData->Animation1);
        PaintPitch(pDT->pRT, ezWnd->cWidth, ezWnd->cHeight, pButtonData->col, pButtonData->Animation1, pButtonData->Animation2);
        PaintRipple(pDT->pRT, ezWnd->cWidth, ezWnd->cHeight, pButtonData->Animation2);

        return 0;
    }
    case EZWM_NCHITTEST:
    {
        PPOINTFLOAT pPoint = (PPOINTFLOAT)lParam;
        FLOAT dx = pPoint->x - ezWnd->cWidth / 2.0f;
        FLOAT dy = pPoint->y - ezWnd->cHeight / 2.0f;
        FLOAT Zoom = 0.95 + 0.05 * pButtonData->Animation1;
        FLOAT VisualButtonWidth = ezWnd->cHeight / 4.0f * Zoom;
        if (dx * dx + dy * dy > VisualButtonWidth * VisualButtonWidth)
            return EZHT_TRANSPARENT;
        return EZHT_DEFAULT;
    }
    case EZWM_LBUTTONDOWN:
    {
        ButtonPressed(pButtonData->row, pButtonData->col, GetCurrentNoteOffset());
        pButtonData->MousePressed = TRUE;
        pButtonData->Animation2 = 0.0;
        return 0;
    }
    case EZWM_LBUTTONUP:
    {
        pButtonData->MousePressed = FALSE;
        return 0;
    }
    case EZWM_MOUSELEAVE:
    {
        pButtonData->MousePressed = FALSE;
        return 0;
    }
    case EZWM_KEYDOWN:
    case EZWM_KEYUP:
    {
        // forward it to parent.
        EZSendMessage(ezWnd->ParentWnd, message, wParam, lParam);
        break;
    }
    }
    return 0;
}

LRESULT CALLBACK MainWindowProc(EZWND ezWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    static EZWND NoteButtons[3][7];

    static const int IndexMapVKTable[] =
    { 'Q', 'W', 'E', 'R', 'T', 'Y', 'U',
      'A', 'S', 'D', 'F', 'G', 'H', 'J',
      'Z', 'X', 'C', 'V', 'B', 'N', 'M' };

    static ID2D1Bitmap* pBitmap = NULL;
    static EZWND TutorialButton;

    switch (message)
    {
    case EZWM_CREATE:
    {
        if (FAILED(DwmGetCompositionTimingInfo(NULL, &DwmTimingInfo)))
        {
            HDC hdc = GetDC(ezWnd->hwndBase);
            DwmTimingInfo.rateCompose.uiDenominator = 1;
            DwmTimingInfo.rateCompose.uiNumerator = GetDeviceCaps(hdc, VREFRESH);
            ReleaseDC(ezWnd->hwndBase, hdc);
        }

        for (int row = 0; row < 3; row++)
        {
            for (int col = 0; col < 7; col++)
            {
                NoteButtons[row][col] =
                    CreateEZWnd(
                        ezWnd, 0, 0, 0, 0,
                        NoteButtonProc, (LPARAM)(row * 7 + col));
                if (!NoteButtons[row][col]) return -1;
            }
        }
        FlatButton = CreateEZWnd(
            ezWnd, 0, 0, 0, 0,
            SharpFlatButtonProc, 0);
        SharpButton = CreateEZWnd(
            ezWnd, 0, 0, 0, 0,
            SharpFlatButtonProc, 1);

        TutorialButton = CreateEZWnd(
            ezWnd, 0, 0, 0, 0,
            PictureButtonProc, IDB_PNG2);

        break;
    }
    case EZWM_COMMAND:
    {
        if ((EZWND)lParam == TutorialButton)
        {
            MessageBoxW(0,
                L"按键盘 [Q-U] [A-J] [Z-M] 键或用鼠标点击按钮来弹奏音乐\n"
                L"按下键盘 - + 键或按下 [♭] [♯] 按钮来弹奏升降音",
                szAppName, MB_DEFAULT_DESKTOP_ONLY | MB_ICONINFORMATION);
        }
        break;
    }
    case EZWM_SIZE:
    {
        float NoteButtonHeight = ezWnd->cHeight / 10.0f;
        float ButtonSpace = NoteButtonHeight * 0.53f;

        float VerticalSpace = NoteButtonHeight * 0.22f;
        float xStart = (ezWnd->cWidth - NoteButtonHeight * 7 - ButtonSpace * 6) / 2.0f;
        float yStart = ezWnd->cHeight * 0.57;

        for (int row = 0; row < 3; row++)
        {
            for (int col = 0; col < 7; col++)
            {
                EZMoveWnd(NoteButtons[row][col], xStart + col * (NoteButtonHeight + ButtonSpace) - NoteButtonHeight / 2.0f,
                    yStart + row * (NoteButtonHeight + VerticalSpace) - NoteButtonHeight / 2.0f,
                    NoteButtonHeight * 2, NoteButtonHeight * 2);
            }
        }
        FLOAT SharpFlatBtnHeight = NoteButtonHeight * 0.8;
        EZMoveWnd(FlatButton, xStart + 7 * (NoteButtonHeight + ButtonSpace) + NoteButtonHeight / 2.0f - SharpFlatBtnHeight,
            yStart + NoteButtonHeight / 2.0f - SharpFlatBtnHeight,
            SharpFlatBtnHeight * 2, SharpFlatBtnHeight * 2);
        EZMoveWnd(SharpButton, xStart + 7 * (NoteButtonHeight + ButtonSpace) + (SharpFlatBtnHeight + ButtonSpace * 0.8) + NoteButtonHeight / 2.0f - SharpFlatBtnHeight,
            yStart + NoteButtonHeight / 2.0f - SharpFlatBtnHeight,
            SharpFlatBtnHeight * 2, SharpFlatBtnHeight * 2);

        float TutorialButtonHeight = 0.035 * ezWnd->cHeight;

        EZMoveWnd(TutorialButton, TutorialButtonHeight * 2, TutorialButtonHeight, TutorialButtonHeight, TutorialButtonHeight);
        break;
    }
    case EZWM_RENDER:
    {
        PDRAW_TARGET pDT = (PDRAW_TARGET)wParam;

        if (!pGlobalSolidBrush)
            pDT->pRT->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.0f, 0.0f, 1.0f), &pGlobalSolidBrush);

        if (!pBitmap)
            LoadResourceBitmap(pDT->pRT, pWICFactory, MAKEINTRESOURCE(IDB_PNG1), L"PNG", 0, 0, &pBitmap);

        if (!pGlobalSolidBrush || !pBitmap)
            return 0;

        pDT->pRT->Clear(D2D1::ColorF(D2D1::ColorF(0.5, 0.7, 0.9)));

        D2D1_SIZE_F Size = pBitmap->GetSize();
        FLOAT ImgWidth = ezWnd->cHeight * Size.width / Size.height;
        pDT->pRT->DrawBitmap(pBitmap, { (ezWnd->cWidth - ImgWidth) / 2, 0, (ezWnd->cWidth + ImgWidth) / 2, ezWnd->cHeight }, 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, { 0, 0, Size.width, Size.height });

        // Paint Line
        pGlobalSolidBrush->SetColor(D2D1::ColorF(1.0f, 1.0f, 1.0f));
        
        for (int i = 0; i < 3; i++)
        {
            float NoteButtonHeight = ezWnd->cHeight / 10.0f;
            float ButtonSpace = NoteButtonHeight * 0.53f;

            float VerticalSpace = NoteButtonHeight * 0.22f;

            float LineSpace = 0.14 * NoteButtonHeight;

            float xStart = (ezWnd->cWidth - NoteButtonHeight * 7 - ButtonSpace * 6) / 2.0f - ButtonSpace / 2.0;

            float xEnd = (ezWnd->cWidth + NoteButtonHeight * 7 + ButtonSpace * 6) / 2.0f + ButtonSpace / 2.0;
            for (int j = 0; j < 5; j++)
            {
                float yStart = ezWnd->cHeight * 0.57 + i * (VerticalSpace + NoteButtonHeight) + j * LineSpace + (NoteButtonHeight - 4 * LineSpace) / 2.0;
                pDT->pRT->DrawLine({ xStart, yStart }, { xEnd, yStart }, pGlobalSolidBrush, 0.007 * NoteButtonHeight);
            }
        }

        InvalidateRect(ezWnd->hwndBase, NULL, NULL);
        break;
    }
    case EZWM_DISCARD_RES:
    {
        if (pBitmap)
        {
            pBitmap->Release();
            pBitmap = NULL;
        }
        if (pGlobalSolidBrush)
        {
            pGlobalSolidBrush->Release();
            pGlobalSolidBrush = NULL;
        }
        break;
    }
    case EZWM_KEYDOWN:
    case EZWM_KEYUP:
    {
        EZWND PressedButton = NULL;
        if (wParam == VK_OEM_MINUS)
        {
            PressedButton = FlatButton;
        }
        else if (wParam == VK_OEM_PLUS)
        {
            PressedButton = SharpButton;
        }
        else
        {
            for (int i = 0; i < _countof(IndexMapVKTable); i++)
            {
                if (wParam == IndexMapVKTable[i])
                {
                    PressedButton = NoteButtons[i / 7][i % 7];
                    break;
                }
            }
        }
        if (PressedButton)
        {
            PBUTTON_DATA pButtonData = (PBUTTON_DATA)EZGetExtra(PressedButton);

            if (message == EZWM_KEYDOWN)
            {
                if (!pButtonData->KeyPressed)
                {
                    if (PressedButton != FlatButton && PressedButton != SharpButton)
                    {
                        ButtonPressed(pButtonData->row, pButtonData->col, GetCurrentNoteOffset());
                    }
                    pButtonData->KeyPressed = TRUE;
                    pButtonData->Animation2 = 0.0;
                }
            }
            else
            {
                pButtonData->KeyPressed = FALSE;
            }
        }
        break;
    }
    case EZWM_DESTROY:
    {
        PostQuitMessage(0);
        break;
    }
    }
    return 0;
}

BOOL AudioInit()
{
    // Define the desired audio output format we request
    SDL_AudioSpec OutputAudioSpec;
    OutputAudioSpec.freq = 44100;
    OutputAudioSpec.format = AUDIO_F32;
    OutputAudioSpec.channels = 2;
    OutputAudioSpec.samples = 1024;
    OutputAudioSpec.callback = AudioCallback;

    // Initialize the audio system
    if (SDL_AudioInit(TSF_NULL) < 0)
    {
        return FALSE;
    }
    // Load the SoundFont from resource
    HRSRC hRsrc = FindResourceW(NULL, MAKEINTRESOURCE(IDR_SF21), L"SF2");
    if (!hRsrc) return FALSE;

    HGLOBAL hSoundFontGlobal = LoadResource(NULL, hRsrc);
    if (!hSoundFontGlobal) return FALSE;

    const void* pSoundFont = LockResource(hSoundFontGlobal);

    int SoundFontSize = SizeofResource(NULL, hRsrc);
    g_TinySoundFont = tsf_load_memory(pSoundFont, SoundFontSize);
    // g_TinySoundFont = tsf_load_filename("C:\\Users\\11603\\Downloads\\风物之诗琴.sf2");
    if (!g_TinySoundFont)
    {
        return FALSE;
    }
    tsf_set_max_voices(g_TinySoundFont, 256);
    // Set the SoundFont rendering output mode
    tsf_set_output(g_TinySoundFont, TSF_STEREO_INTERLEAVED, OutputAudioSpec.freq, 0);

    if (SDL_OpenAudio(&OutputAudioSpec, TSF_NULL) < 0)
    {
        return FALSE;
    }

    SDL_PauseAudio(0);
    return TRUE;
}

BOOL FontInit()
{
    HRESULT  hr = DWriteCreateFactory(
        DWRITE_FACTORY_TYPE_SHARED,
        __uuidof(IDWriteFactory),
        reinterpret_cast<IUnknown**>(&g_dwriteFactory)
    );

    if (FAILED(hr)) return FALSE;

    static UINT const fontResourceIDs[] = { IDR_FONT1 };

    hr = fontContext.Initialize();
    if (FAILED(hr))
        return FALSE;

    hr = fontContext.CreateFontCollection(
        fontResourceIDs,
        sizeof(fontResourceIDs),
        &fontCollection
    );

    HDC hdc = GetDC(NULL);
    g_dpiX = GetDeviceCaps(hdc, LOGPIXELSX);
    g_dpiY = GetDeviceCaps(hdc, LOGPIXELSY);
    ReleaseDC(NULL, hdc);

    return TRUE;
}

BOOL WICInit()
{
    // Create WIC factory.
    HRESULT hr = CoCreateInstance(
        CLSID_WICImagingFactory,
        NULL,
        CLSCTX_INPROC_SERVER,
        IID_IWICImagingFactory,
        reinterpret_cast<void**>(&pWICFactory)
    );
    if (FAILED(hr)) return FALSE;

    return TRUE;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int iCmdShow)
{
    if (!InitEZWnd())
    {
        return 1;
    }

    if (!AudioInit())
    {
        MessageBoxW(NULL, L"初始化音频时出现问题，程序即将退出", szAppName, MB_ICONERROR);
        return 1;
    }

    if (!FontInit())
    {
        MessageBoxW(NULL, L"初始化字体时出现问题，程序即将退出", szAppName, MB_ICONERROR);
        return 1;
    }

    if (!WICInit())
    {
        MessageBoxW(NULL, L"初始化图片时出现问题，程序即将退出", szAppName, MB_ICONERROR);
        return 1;
    }
    EZWND ezWnd = CreateEZRootWnd(
        HWND_DESKTOP,
        0,
        WS_OVERLAPPEDWINDOW,
        szAppName,
        100,
        100,
        900,
        550,
        MainWindowProc, NULL);

    ShowWindow(ezWnd->hwndBase, SW_MAXIMIZE);
    UpdateWindow(ezWnd->hwndBase);

    if (!ezWnd) return 1;

    // TODO: I'm fed up with cleaning up resources!
    // let OS do that job for me today!
    return (int)EZWndMessageLoop();
}
