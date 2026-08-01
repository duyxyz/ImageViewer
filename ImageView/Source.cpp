#include <windows.h>
#include <d2d1.h>
#include <wincodec.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <shlobj.h>
#include <algorithm>
#include <string>
#include <vector>

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "Dwmapi.lib")
#pragma comment(lib, "Shell32.lib")
#pragma comment(lib, "uxtheme.lib")
#pragma comment(linker, "/subsystem:windows")

#define IDM_RESET     1001
#define IDM_COPY      1002
#define IDM_COPY_PATH 1003
#define IDM_ROTATE_90 1004
#define IDM_FLIP_H    1005
#define IDM_FLIP_V    1006
#define IDM_SET_WALL  1007
#define TIMER_GIF     2001

typedef enum PreferredAppMode { AllowDark, ForceDark, ForceLight, Max } PreferredAppMode;
typedef PreferredAppMode(WINAPI* fnSetPreferredAppMode)(PreferredAppMode appMode);

void EnableMenuDarkMode() {
    HMODULE hUxtheme = LoadLibraryExW(L"uxtheme.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (hUxtheme) {
        auto SetPreferredAppMode = (fnSetPreferredAppMode)GetProcAddress(hUxtheme, MAKEINTRESOURCEA(135));
        if (SetPreferredAppMode) SetPreferredAppMode(ForceDark);
        FreeLibrary(hUxtheme);
    }
}

void RegisterAsAppHandler() {
    wchar_t szExePath[MAX_PATH];
    GetModuleFileNameW(NULL, szExePath, MAX_PATH);

    std::wstring appProgID = L"MyD2DImageViewer.Image";
    std::wstring appName = L"My D2D Image Viewer";
    std::wstring openCmd = std::wstring(L"\"") + szExePath + L"\" \"%1\"";
    std::wstring iconCmd = std::wstring(L"\"") + szExePath + L"\",0";

    HKEY hKey;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, (L"Software\\Classes\\" + appProgID).c_str(), 0, NULL, 0, KEY_WRITE, NULL, &hKey, NULL) == ERROR_SUCCESS) {
        RegSetValueExW(hKey, NULL, 0, REG_SZ, (BYTE*)appName.c_str(), (DWORD)((appName.length() + 1) * sizeof(wchar_t)));
        RegCloseKey(hKey);
    }

    if (RegCreateKeyExW(HKEY_CURRENT_USER, (L"Software\\Classes\\" + appProgID + L"\\DefaultIcon").c_str(), 0, NULL, 0, KEY_WRITE, NULL, &hKey, NULL) == ERROR_SUCCESS) {
        RegSetValueExW(hKey, NULL, 0, REG_SZ, (BYTE*)iconCmd.c_str(), (DWORD)((iconCmd.length() + 1) * sizeof(wchar_t)));
        RegCloseKey(hKey);
    }

    if (RegCreateKeyExW(HKEY_CURRENT_USER, (L"Software\\Classes\\" + appProgID + L"\\shell\\open\\command").c_str(), 0, NULL, 0, KEY_WRITE, NULL, &hKey, NULL) == ERROR_SUCCESS) {
        RegSetValueExW(hKey, NULL, 0, REG_SZ, (BYTE*)openCmd.c_str(), (DWORD)((openCmd.length() + 1) * sizeof(wchar_t)));
        RegCloseKey(hKey);
    }

    std::wstring capPath = L"Software\\MyD2DImageViewer\\Capabilities";
    if (RegCreateKeyExW(HKEY_CURRENT_USER, capPath.c_str(), 0, NULL, 0, KEY_WRITE, NULL, &hKey, NULL) == ERROR_SUCCESS) {
        RegSetValueExW(hKey, L"ApplicationName", 0, REG_SZ, (BYTE*)appName.c_str(), (DWORD)((appName.length() + 1) * sizeof(wchar_t)));
        RegSetValueExW(hKey, L"ApplicationDescription", 0, REG_SZ, (BYTE*)L"Fast D2D Image Viewer", sizeof(L"Fast D2D Image Viewer"));
        RegCloseKey(hKey);
    }

    HKEY hKeyAssoc;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, (capPath + L"\\FileAssociations").c_str(), 0, NULL, 0, KEY_WRITE, NULL, &hKeyAssoc, NULL) == ERROR_SUCCESS) {
        const wchar_t* exts[] = { L".jpg", L".jpeg", L".png", L".gif", L".bmp", L".webp", L".ico" };
        for (const wchar_t* ext : exts) {
            RegSetValueExW(hKeyAssoc, ext, 0, REG_SZ, (BYTE*)appProgID.c_str(), (DWORD)((appProgID.length() + 1) * sizeof(wchar_t)));
        }
        RegCloseKey(hKeyAssoc);
    }

    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\RegisteredApplications", 0, NULL, 0, KEY_WRITE, NULL, &hKey, NULL) == ERROR_SUCCESS) {
        std::wstring val = L"Software\\MyD2DImageViewer\\Capabilities";
        RegSetValueExW(hKey, L"MyD2DImageViewer", 0, REG_SZ, (BYTE*)val.c_str(), (DWORD)((val.length() + 1) * sizeof(wchar_t)));
        RegCloseKey(hKey);
    }

    const wchar_t* exts[] = { L".jpg", L".jpeg", L".png", L".gif", L".bmp", L".webp", L".ico" };
    for (const wchar_t* ext : exts) {
        std::wstring subKey = std::wstring(L"Software\\Classes\\") + ext + L"\\OpenWithProgids";
        if (RegCreateKeyExW(HKEY_CURRENT_USER, subKey.c_str(), 0, NULL, 0, KEY_WRITE, NULL, &hKey, NULL) == ERROR_SUCCESS) {
            RegSetValueExW(hKey, appProgID.c_str(), 0, REG_NONE, NULL, 0);
            RegCloseKey(hKey);
        }
    }

    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, NULL, NULL);
}

template <class T> void SafeRelease(T** ppT) { if (*ppT) { (*ppT)->Release(); *ppT = NULL; } }

IWICImagingFactory* g_pWICFactory = NULL;
ID2D1Factory* g_pD2DFactory = NULL;
ID2D1HwndRenderTarget* g_pRenderTarget = NULL;
IWICBitmapDecoder* g_pDecoder = NULL;
ID2D1Bitmap* g_pD2DBitmap = NULL;

UINT g_ImgWidth = 0, g_ImgHeight = 0, g_FrameCount = 0, g_CurrentFrame = 0;
std::wstring g_FileName = L"", g_CurrentFilePath = L"";

struct FrameInfo { IWICBitmapSource* pSource = NULL; UINT delayMs = 100; };
std::vector<FrameInfo> g_OriginalFrames;
std::vector<FrameInfo> g_Frames;

float g_Scale = 1.0f, g_OffsetX = 0.0f, g_OffsetY = 0.0f;
bool g_IsDragging = false;
POINT g_LastMousePos = { 0, 0 };
D2D1_RECT_F g_ImgRect = { 0, 0, 0, 0 };

bool g_IsFullscreen = false;
WINDOWPLACEMENT g_wpPrev = { sizeof(g_wpPrev) };

void ToggleFullscreen(HWND hWnd) {
    DWORD dwStyle = GetWindowLong(hWnd, GWL_STYLE);
    if (g_IsFullscreen) {
        SetWindowLong(hWnd, GWL_STYLE, dwStyle | WS_OVERLAPPEDWINDOW);
        SetWindowPlacement(hWnd, &g_wpPrev);
        SetWindowPos(hWnd, NULL, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED | SWP_SHOWWINDOW);
        g_IsFullscreen = false;
    }
    else {
        MONITORINFO mi = { sizeof(mi) };
        if (GetWindowPlacement(hWnd, &g_wpPrev) && GetMonitorInfo(MonitorFromWindow(hWnd, MONITOR_DEFAULTTOPRIMARY), &mi)) {
            SetWindowLong(hWnd, GWL_STYLE, dwStyle & ~WS_OVERLAPPEDWINDOW);
            SetWindowPos(hWnd, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top,
                mi.rcMonitor.right - mi.rcMonitor.left,
                mi.rcMonitor.bottom - mi.rcMonitor.top,
                SWP_NOOWNERZORDER | SWP_FRAMECHANGED | SWP_SHOWWINDOW);
            g_IsFullscreen = true;
        }
    }
}

void ClearVector(std::vector<FrameInfo>& vec) {
    for (auto& f : vec) SafeRelease(&f.pSource);
    vec.clear();
}

void ClearFrames() {
    ClearVector(g_OriginalFrames);
    ClearVector(g_Frames);
    g_FrameCount = 0; g_CurrentFrame = 0;
}

void UpdateWindowTitle(HWND hWnd) {
    if (!g_FileName.empty()) {
        std::wstring title = std::to_wstring(g_ImgWidth) + L" x " + std::to_wstring(g_ImgHeight) + L" - " + g_FileName;
        SetWindowTextW(hWnd, title.c_str());
    }
    else {
        SetWindowTextW(hWnd, L"My D2D Image Viewer");
    }
}

void UpdateD2DBitmap(HWND hWnd = NULL) {
    SafeRelease(&g_pD2DBitmap);
    // ✅ FIX: Add bounds checking
    if (g_Frames.empty() || g_CurrentFrame >= g_Frames.size() || !g_pRenderTarget || !g_pWICFactory) return;
    if (!g_Frames[g_CurrentFrame].pSource) return; // ✅ FIX: Check NULL source

    IWICFormatConverter* pConverter = NULL;
    if (SUCCEEDED(g_pWICFactory->CreateFormatConverter(&pConverter))) {
        if (SUCCEEDED(pConverter->Initialize(g_Frames[g_CurrentFrame].pSource, GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, NULL, 0.f, WICBitmapPaletteTypeCustom))) {
            g_pRenderTarget->CreateBitmapFromWicBitmap(pConverter, NULL, &g_pD2DBitmap);
            g_Frames[g_CurrentFrame].pSource->GetSize(&g_ImgWidth, &g_ImgHeight);
            if (hWnd) UpdateWindowTitle(hWnd);
        }
        SafeRelease(&pConverter);
    }
}

void DiscardResources() { SafeRelease(&g_pD2DBitmap); SafeRelease(&g_pRenderTarget); }

HRESULT CreateResources(HWND hWnd) {
    if (g_pRenderTarget) return S_OK;
    RECT rc; GetClientRect(hWnd, &rc);
    HRESULT hr = g_pD2DFactory->CreateHwndRenderTarget(
        D2D1::RenderTargetProperties(),
        D2D1::HwndRenderTargetProperties(hWnd, D2D1::SizeU(rc.right - rc.left, rc.bottom - rc.top), D2D1_PRESENT_OPTIONS_IMMEDIATELY),
        &g_pRenderTarget
    );
    if (SUCCEEDED(hr)) UpdateD2DBitmap(hWnd);
    return hr;
}

void LoadImageFile(HWND hWnd, const std::wstring& path) {
    g_CurrentFilePath = path;
    size_t lastSlash = path.find_last_of(L"\\/");
    g_FileName = (lastSlash != std::wstring::npos) ? path.substr(lastSlash + 1) : path;
    ClearFrames(); SafeRelease(&g_pDecoder);

    bool isIco = false;
    if (path.length() >= 4) {
        std::wstring ext = path.substr(path.length() - 4);
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        if (ext == L".ico") isIco = true;
    }

    if (SUCCEEDED(g_pWICFactory->CreateDecoderFromFilename(path.c_str(), NULL, GENERIC_READ, WICDecodeMetadataCacheOnLoad, &g_pDecoder))) {
        g_pDecoder->GetFrameCount(&g_FrameCount);

        UINT bestFrameIndex = 0;
        UINT maxArea = 0;

        for (UINT i = 0; i < g_FrameCount; ++i) {
            IWICBitmapFrameDecode* pFrame = NULL;
            if (SUCCEEDED(g_pDecoder->GetFrame(i, &pFrame))) {
                FrameInfo info;
                // ✅ FIX: Check error for CreateBitmapFromSource
                if (FAILED(g_pWICFactory->CreateBitmapFromSource(pFrame, WICBitmapCacheOnLoad, (IWICBitmap**)&info.pSource))) {
                    SafeRelease(&pFrame);
                    continue; // Skip this frame if failed
                }

                if (isIco && info.pSource) {
                    UINT w = 0, h = 0;
                    info.pSource->GetSize(&w, &h);
                    if (w * h > maxArea) {
                        maxArea = w * h;
                        bestFrameIndex = i;
                    }
                }

                IWICMetadataQueryReader* pMetadata = NULL;
                if (SUCCEEDED(pFrame->GetMetadataQueryReader(&pMetadata))) {
                    PROPVARIANT propValue; PropVariantInit(&propValue);
                    if (SUCCEEDED(pMetadata->GetMetadataByName(L"/grctlext/Delay", &propValue)) && propValue.vt == VT_UI2)
                        info.delayMs = propValue.uiVal * 10;
                    PropVariantClear(&propValue); SafeRelease(&pMetadata);
                }
                if (info.delayMs < 20) info.delayMs = 100;

                g_OriginalFrames.push_back(info);

                FrameInfo workInfo;
                // ✅ FIX: Check error for CreateBitmapFromSource
                if (FAILED(g_pWICFactory->CreateBitmapFromSource(info.pSource, WICBitmapCacheOnLoad, (IWICBitmap**)&workInfo.pSource))) {
                    workInfo.pSource = NULL; // Ensure NULL if failed
                }
                workInfo.delayMs = info.delayMs;
                g_Frames.push_back(workInfo);

                SafeRelease(&pFrame);
            }
        }

        if (isIco && !g_Frames.empty()) {
            FrameInfo bestWork = g_Frames[bestFrameIndex];
            FrameInfo bestOrg = g_OriginalFrames[bestFrameIndex];

            // ✅ FIX: Use SafeRelease instead of manual Release
            for (size_t i = 0; i < g_Frames.size(); ++i) {
                if (i != (size_t)bestFrameIndex) {
                    SafeRelease(&g_Frames[i].pSource);
                }
            }

            for (size_t i = 0; i < g_OriginalFrames.size(); ++i) {
                if (i != (size_t)bestFrameIndex) {
                    SafeRelease(&g_OriginalFrames[i].pSource);
                }
            }

            g_Frames.clear();
            g_OriginalFrames.clear();

            g_Frames.push_back(bestWork);
            g_OriginalFrames.push_back(bestOrg);

            g_FrameCount = 1;
            g_CurrentFrame = 0;

            if (g_pRenderTarget) {
                UpdateD2DBitmap(hWnd);
            }
        }
    }

    KillTimer(hWnd, TIMER_GIF);
    if (g_FrameCount > 1 && !g_Frames.empty()) {
        SetTimer(hWnd, TIMER_GIF, g_Frames[0].delayMs, NULL);
    }
}

void ResetAll(HWND hWnd) {
    g_Scale = 1.0f; g_OffsetX = 0.0f; g_OffsetY = 0.0f; g_CurrentFrame = 0;

    ClearVector(g_Frames);
    for (const auto& org : g_OriginalFrames) {
        if (!org.pSource) continue; // ✅ FIX: Skip NULL sources
        FrameInfo workInfo;
        // ✅ FIX: Check error for CreateBitmapFromSource
        if (FAILED(g_pWICFactory->CreateBitmapFromSource(org.pSource, WICBitmapCacheOnLoad, (IWICBitmap**)&workInfo.pSource))) {
            workInfo.pSource = NULL;
        }
        workInfo.delayMs = org.delayMs;
        g_Frames.push_back(workInfo);
    }

    KillTimer(hWnd, TIMER_GIF);
    if (g_FrameCount > 1 && !g_Frames.empty()) {
        SetTimer(hWnd, TIMER_GIF, g_Frames[0].delayMs, NULL);
    }

    UpdateD2DBitmap(hWnd);
}

void CopyImageToClipboard(HWND hWnd) {
    if (g_Frames.empty() || g_CurrentFrame >= g_Frames.size()) return; // ✅ FIX: Add bounds check
    IWICFormatConverter* pConverter = NULL;
    if (SUCCEEDED(g_pWICFactory->CreateFormatConverter(&pConverter))) {
        // ✅ FIX: Check error for Initialize
        if (FAILED(pConverter->Initialize(g_Frames[g_CurrentFrame].pSource, GUID_WICPixelFormat32bppBGR, WICBitmapDitherTypeNone, NULL, 0.f, WICBitmapPaletteTypeCustom))) {
            SafeRelease(&pConverter);
            return;
        }
        UINT w = 0, h = 0; pConverter->GetSize(&w, &h);
        UINT stride = w * 4, imageSize = stride * h;
        HGLOBAL hGlobal = GlobalAlloc(GHND, sizeof(BITMAPINFOHEADER) + imageSize);
        if (hGlobal) {
            BYTE* pBuffer = (BYTE*)GlobalLock(hGlobal);
            if (pBuffer) {
                BITMAPINFOHEADER* bih = (BITMAPINFOHEADER*)pBuffer;
                bih->biSize = sizeof(BITMAPINFOHEADER); bih->biWidth = w; bih->biHeight = h;
                bih->biPlanes = 1; bih->biBitCount = 32; bih->biCompression = BI_RGB; bih->biSizeImage = imageSize;
                BYTE* pPixels = pBuffer + sizeof(BITMAPINFOHEADER);
                if (SUCCEEDED(pConverter->CopyPixels(NULL, stride, imageSize, pPixels))) {
                    for (UINT y = 0; y < h / 2; ++y) {
                        BYTE* top = pPixels + y * stride; BYTE* bottom = pPixels + (h - 1 - y) * stride;
                        for (UINT i = 0; i < stride; ++i) std::swap(top[i], bottom[i]);
                    }
                    GlobalUnlock(hGlobal);
                    if (OpenClipboard(hWnd)) { EmptyClipboard(); SetClipboardData(CF_DIB, hGlobal); CloseClipboard(); }
                    else GlobalFree(hGlobal);
                }
                else { GlobalUnlock(hGlobal); GlobalFree(hGlobal); }
            }
            else GlobalFree(hGlobal);
        }
        SafeRelease(&pConverter);
    }
}

void CopyPathToClipboard(HWND hWnd) {
    if (g_CurrentFilePath.empty()) return;
    std::wstring path = L"\"" + g_CurrentFilePath + L"\"";
    size_t bytes = (path.length() + 1) * sizeof(wchar_t);
    HGLOBAL hGlobal = GlobalAlloc(GHND, bytes);
    if (hGlobal) {
        wchar_t* pText = (wchar_t*)GlobalLock(hGlobal);
        if (pText) {
            wcscpy_s(pText, path.length() + 1, path.c_str());
            GlobalUnlock(hGlobal);
            if (OpenClipboard(hWnd)) { EmptyClipboard(); SetClipboardData(CF_UNICODETEXT, hGlobal); CloseClipboard(); }
            else GlobalFree(hGlobal);
        }
        else GlobalFree(hGlobal);
    }
}

void TransformImage(HWND hWnd, WICBitmapTransformOptions options) {
    if (g_Frames.empty()) return;
    for (auto& frame : g_Frames) {
        if (!frame.pSource) continue; // ✅ FIX: Skip NULL sources
        IWICBitmapFlipRotator* pRotator = NULL;
        if (SUCCEEDED(g_pWICFactory->CreateBitmapFlipRotator(&pRotator))) {
            if (SUCCEEDED(pRotator->Initialize(frame.pSource, options))) {
                IWICBitmap* pBitmap = NULL;
                // ✅ FIX: Check error for CreateBitmapFromSource
                if (SUCCEEDED(g_pWICFactory->CreateBitmapFromSource(pRotator, WICBitmapCacheOnLoad, &pBitmap))) {
                    SafeRelease(&frame.pSource);
                    frame.pSource = pBitmap;
                }
            }
            SafeRelease(&pRotator);
        }
    }
    UpdateD2DBitmap(hWnd);
}

void OnRender(HWND hWnd) {
    if (FAILED(CreateResources(hWnd))) return;
    g_pRenderTarget->BeginDraw();
    g_pRenderTarget->Clear(D2D1::ColorF(0.125f, 0.125f, 0.125f, 1.0f));

    if (g_pD2DBitmap && g_ImgWidth > 0) {
        RECT rc; GetClientRect(hWnd, &rc);
        float winW = (float)(rc.right - rc.left), winH = (float)(rc.bottom - rc.top);

        float fitScale = (std::min)(winW / g_ImgWidth, winH / g_ImgHeight);
        float baseScale = (fitScale < 1.0f) ? fitScale : 1.0f;

        float drawW = g_ImgWidth * baseScale * g_Scale;
        float drawH = g_ImgHeight * baseScale * g_Scale;
        float x = (winW - drawW) / 2.0f + g_OffsetX;
        float y = (winH - drawH) / 2.0f + g_OffsetY;

        g_ImgRect = D2D1::RectF(x, y, x + drawW, y + drawH);

        g_pRenderTarget->DrawBitmap(
            g_pD2DBitmap,
            g_ImgRect,
            1.0f,
            D2D1_BITMAP_INTERPOLATION_MODE_LINEAR
        );
    }
    else {
        g_ImgRect = { 0, 0, 0, 0 };
    }
    if (g_pRenderTarget->EndDraw() == D2DERR_RECREATE_TARGET) DiscardResources();
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE:
        UpdateWindowTitle(hWnd);
        if (g_FrameCount > 1 && !g_Frames.empty()) { // ✅ FIX: Add g_Frames check
            SetTimer(hWnd, TIMER_GIF, g_Frames[0].delayMs, NULL);
        }
        break;

    case WM_TIMER:
        if (wParam == TIMER_GIF && g_FrameCount > 1 && !g_Frames.empty()) { // ✅ FIX: Add g_Frames check
            g_CurrentFrame = (g_CurrentFrame + 1) % g_FrameCount;
            if (g_CurrentFrame < g_Frames.size()) { // ✅ FIX: Ensure valid index
                UpdateD2DBitmap(hWnd);
                InvalidateRect(hWnd, NULL, FALSE);
                if (g_Frames[g_CurrentFrame].delayMs > 0) { // ✅ FIX: Ensure valid delay
                    SetTimer(hWnd, TIMER_GIF, g_Frames[g_CurrentFrame].delayMs, NULL);
                }
                else {
                    SetTimer(hWnd, TIMER_GIF, 100, NULL); // Fallback to 100ms
                }
            }
        }
        break;

    case WM_LBUTTONDOWN: {
        POINT pt = { LOWORD(lParam), HIWORD(lParam) };
        if (pt.x >= g_ImgRect.left && pt.x <= g_ImgRect.right &&
            pt.y >= g_ImgRect.top && pt.y <= g_ImgRect.bottom) {
            g_IsDragging = true;
            g_LastMousePos = pt;
            SetCapture(hWnd);
        }
        break;
    }

    case WM_LBUTTONDBLCLK: {
        POINT pt = { LOWORD(lParam), HIWORD(lParam) };
        if (pt.x >= g_ImgRect.left && pt.x <= g_ImgRect.right &&
            pt.y >= g_ImgRect.top && pt.y <= g_ImgRect.bottom) {
            if (g_IsDragging) {
                g_IsDragging = false;
                ReleaseCapture();
            }
            ToggleFullscreen(hWnd);
        }
        break;
    }

    case WM_MOUSEMOVE:
        if (g_IsDragging) {
            POINT currentPos = { LOWORD(lParam), HIWORD(lParam) };
            g_OffsetX += (currentPos.x - g_LastMousePos.x);
            g_OffsetY += (currentPos.y - g_LastMousePos.y);
            g_LastMousePos = currentPos;
            InvalidateRect(hWnd, NULL, FALSE);
        }
        break;

    case WM_LBUTTONUP:
        if (g_IsDragging) {
            g_IsDragging = false;
            ReleaseCapture();
        }
        break;

    case WM_KEYDOWN:
        if (wParam == VK_F11 || (wParam == VK_ESCAPE && g_IsFullscreen)) {
            ToggleFullscreen(hWnd);
        }
        break;

    case WM_SETCURSOR:
        if (LOWORD(lParam) == HTCLIENT) {
            POINT mPos; GetCursorPos(&mPos); ScreenToClient(hWnd, &mPos);
            bool overImage = (mPos.x >= g_ImgRect.left && mPos.x <= g_ImgRect.right &&
                mPos.y >= g_ImgRect.top && mPos.y <= g_ImgRect.bottom);
            SetCursor(LoadCursor(NULL, g_IsDragging ? IDC_SIZEALL : (overImage ? IDC_ARROW : IDC_ARROW)));
            return TRUE;
        }
        return DefWindowProc(hWnd, msg, wParam, lParam);

    case WM_CONTEXTMENU: {
        HMENU hMenu = CreatePopupMenu();
        AppendMenuW(hMenu, MF_STRING, IDM_RESET, L"Reset");
        AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
        AppendMenuW(hMenu, MF_STRING, IDM_COPY, L"Copy image");
        AppendMenuW(hMenu, MF_STRING, IDM_COPY_PATH, L"Copy as path");
        AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
        AppendMenuW(hMenu, MF_STRING, IDM_ROTATE_90, L"Rotate 90°");
        AppendMenuW(hMenu, MF_STRING, IDM_FLIP_H, L"Flip horizontally");
        AppendMenuW(hMenu, MF_STRING, IDM_FLIP_V, L"Flip vertically");
        AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
        AppendMenuW(hMenu, MF_STRING, IDM_SET_WALL, L"Set as desktop background");

        POINT pt = { LOWORD(lParam), HIWORD(lParam) };
        if (pt.x == -1 && pt.y == -1) {
            RECT rc; GetClientRect(hWnd, &rc);
            pt = { (rc.right - rc.left) / 2, (rc.bottom - rc.top) / 2 };
            ClientToScreen(hWnd, &pt);
        }
        TrackPopupMenu(hMenu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hWnd, NULL);
        DestroyMenu(hMenu);
        break;
    }

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDM_RESET: ResetAll(hWnd); InvalidateRect(hWnd, NULL, FALSE); break;
        case IDM_COPY: CopyImageToClipboard(hWnd); break;
        case IDM_COPY_PATH: CopyPathToClipboard(hWnd); break;
        case IDM_ROTATE_90: TransformImage(hWnd, WICBitmapTransformRotate90); InvalidateRect(hWnd, NULL, FALSE); break;
        case IDM_FLIP_H: TransformImage(hWnd, WICBitmapTransformFlipHorizontal); InvalidateRect(hWnd, NULL, FALSE); break;
        case IDM_FLIP_V: TransformImage(hWnd, WICBitmapTransformFlipVertical); InvalidateRect(hWnd, NULL, FALSE); break;
        case IDM_SET_WALL: SystemParametersInfoW(SPI_SETDESKWALLPAPER, 0, (void*)g_CurrentFilePath.c_str(), SPIF_UPDATEINIFILE | SPIF_SENDCHANGE); break;
        }
        break;

    case WM_MOUSEWHEEL: {
        POINT mPos; GetCursorPos(&mPos); ScreenToClient(hWnd, &mPos);
        if (mPos.x >= g_ImgRect.left && mPos.x <= g_ImgRect.right &&
            mPos.y >= g_ImgRect.top && mPos.y <= g_ImgRect.bottom) {

            short zDelta = GET_WHEEL_DELTA_WPARAM(wParam);
            RECT rc; GetClientRect(hWnd, &rc);
            float winW = (float)(rc.right - rc.left), winH = (float)(rc.bottom - rc.top);

            float oldScale = g_Scale;
            float factor = (zDelta > 0) ? 1.2f : (1.0f / 1.2f);
            g_Scale = (std::min)((std::max)(g_Scale * factor, 0.1f), 50.0f);

            float ratio = g_Scale / oldScale;
            g_OffsetX = mPos.x - winW / 2.0f - (mPos.x - winW / 2.0f - g_OffsetX) * ratio;
            g_OffsetY = mPos.y - winH / 2.0f - (mPos.y - winH / 2.0f - g_OffsetY) * ratio;

            InvalidateRect(hWnd, NULL, FALSE);
        }
        break;
    }

    case WM_MBUTTONDOWN: {
        POINT mPos = { LOWORD(lParam), HIWORD(lParam) };
        if (mPos.x >= g_ImgRect.left && mPos.x <= g_ImgRect.right &&
            mPos.y >= g_ImgRect.top && mPos.y <= g_ImgRect.bottom) {
            ResetAll(hWnd);
            InvalidateRect(hWnd, NULL, FALSE);
        }
        break;
    }

    case WM_SIZE:
        if (g_pRenderTarget) g_pRenderTarget->Resize(D2D1::SizeU(LOWORD(lParam), HIWORD(lParam)));
        InvalidateRect(hWnd, NULL, FALSE);
        break;

    case WM_ERASEBKGND:
        return 1; // Chặn triệt để hiệu ứng vẽ nền mặc định của Windows

    case WM_PAINT: OnRender(hWnd); ValidateRect(hWnd, NULL); break;

    case WM_DESTROY:
        KillTimer(hWnd, TIMER_GIF); DiscardResources(); ClearFrames();
        SafeRelease(&g_pDecoder); SafeRelease(&g_pWICFactory); SafeRelease(&g_pD2DFactory);
        PostQuitMessage(0);
        break;

    default: return DefWindowProc(hWnd, msg, wParam, lParam);
    }
    return 0;
}

int WINAPI wWinMain(_In_ HINSTANCE hInst, _In_opt_ HINSTANCE, _In_ LPWSTR, _In_ int nCmdShow) {
    (void)CoInitialize(NULL);

    RegisterAsAppHandler();
    EnableMenuDarkMode();

    D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &g_pD2DFactory);
    (void)CoCreateInstance(CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&g_pWICFactory));

    wchar_t szExePath[MAX_PATH];
    GetModuleFileNameW(NULL, szExePath, MAX_PATH);
    HICON hExeIcon = ExtractIconW(NULL, szExePath, 0);

    HBRUSH hDarkBrush = CreateSolidBrush(RGB(32, 32, 32));

    WNDCLASSEXW wc = { sizeof(WNDCLASSEX), CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS, WndProc, 0, 0, hInst, NULL, NULL, hDarkBrush, NULL, L"ImageViewerClassD2D", NULL };
    wc.hIcon = hExeIcon;
    wc.hIconSm = hExeIcon;

    RegisterClassExW(&wc);

    // ✅ FIX: Tạo cửa sổ ở trạng thái ẩn để triệt tiêu nháy sáng
    HWND hWnd = CreateWindowExW(0, L"ImageViewerClassD2D", L"My D2D Image Viewer", WS_OVERLAPPEDWINDOW,
        (GetSystemMetrics(SM_CXSCREEN) - 900) / 2, (GetSystemMetrics(SM_CYSCREEN) - 600) / 2, 900, 600, NULL, NULL, hInst, NULL);

    if (!hWnd) {
        DeleteObject(hDarkBrush);
        if (hExeIcon) DestroyIcon(hExeIcon);
        CoUninitialize();
        return 0;
    }

    int argc; LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv) {
        if (argc > 1 && g_pWICFactory) {
            g_CurrentFilePath = argv[1];
            std::wstring pathStr(argv[1]);
            size_t lastSlash = pathStr.find_last_of(L"\\/");
            g_FileName = (lastSlash != std::wstring::npos) ? pathStr.substr(lastSlash + 1) : pathStr;

            CreateResources(hWnd);
            LoadImageFile(hWnd, argv[1]);
            UpdateD2DBitmap(hWnd);

            if (g_FrameCount > 1 && !g_Frames.empty()) {
                SetTimer(hWnd, TIMER_GIF, g_Frames[0].delayMs, NULL);
            }
        }
        LocalFree(argv);
    }

    if (hExeIcon) {
        SendMessageW(hWnd, WM_SETICON, ICON_BIG, (LPARAM)hExeIcon);
        SendMessageW(hWnd, WM_SETICON, ICON_SMALL, (LPARAM)hExeIcon);
    }

    BOOL dark = TRUE; DwmSetWindowAttribute(hWnd, 20, &dark, sizeof(dark));
    DWORD corner = 2; DwmSetWindowAttribute(hWnd, 33, &corner, sizeof(corner));

    // ✅ FIX: Just show window and let WM_PAINT do the rendering
    // No pre-render, no WM_SETREDRAW complexity
    ShowWindow(hWnd, nCmdShow);
    UpdateWindow(hWnd);  // ← Trigger WM_PAINT immediately

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) { TranslateMessage(&msg); DispatchMessage(&msg); }

    DeleteObject(hDarkBrush);
    if (hExeIcon) DestroyIcon(hExeIcon);
    CoUninitialize();
    return (int)msg.wParam;
}