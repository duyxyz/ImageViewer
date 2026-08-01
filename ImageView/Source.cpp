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

// Tên class window + tên mutex phải cố định, dùng để nhận diện "đã có instance đang chạy"
static const wchar_t* kClassName = L"ImageViewerClassD2D";
static const wchar_t* kMutexName = L"Local\\MyD2DImageViewer_SingleInstance_Mutex_v1";

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

bool IsAlreadyRegistered(const std::wstring& appProgID, const std::wstring& expectedOpenCmd) {
    HKEY hKey;
    std::wstring subKey = L"Software\\Classes\\" + appProgID + L"\\shell\\open\\command";
    if (RegOpenKeyExW(HKEY_CURRENT_USER, subKey.c_str(), 0, KEY_READ, &hKey) != ERROR_SUCCESS)
        return false;

    wchar_t buffer[MAX_PATH * 2] = {};
    DWORD size = sizeof(buffer);
    LONG res = RegQueryValueExW(hKey, NULL, NULL, NULL, (BYTE*)buffer, &size);
    RegCloseKey(hKey);

    if (res != ERROR_SUCCESS) return false;
    return expectedOpenCmd == buffer;
}

void RegisterAsAppHandler() {
    wchar_t szExePath[MAX_PATH];
    GetModuleFileNameW(NULL, szExePath, MAX_PATH);

    std::wstring appProgID = L"MyD2DImageViewer.Image";
    std::wstring appName = L"My D2D Image Viewer";
    std::wstring openCmd = std::wstring(L"\"") + szExePath + L"\" \"%1\"";
    std::wstring iconCmd = std::wstring(L"\"") + szExePath + L"\",0";

    if (IsAlreadyRegistered(appProgID, openCmd)) return;

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

struct FrameInfo { IWICBitmapSource* pSource = NULL; UINT delayMs = 100; };

// 🔧 THAY ĐỔI QUAN TRỌNG:
// Trước đây mọi dữ liệu ảnh/trạng thái (frames, scale, offset, fullscreen...) là biến GLOBAL,
// nghĩa là chỉ phục vụ được ĐÚNG 1 cửa sổ. Giờ gom hết vào struct AppState, mỗi cửa sổ mới
// sẽ có 1 AppState riêng, gắn vào cửa sổ đó qua GWLP_USERDATA. Nhờ vậy 1 process có thể
// mở nhiều cửa sổ độc lập, mỗi cửa sổ hiển thị 1 ảnh khác nhau, không đụng dữ liệu của nhau.
struct AppState {
    IWICBitmapDecoder* pDecoder = NULL;
    ID2D1HwndRenderTarget* pRenderTarget = NULL;
    ID2D1Bitmap* pD2DBitmap = NULL;

    UINT ImgWidth = 0, ImgHeight = 0, FrameCount = 0, CurrentFrame = 0;
    std::wstring FileName, CurrentFilePath;

    std::vector<FrameInfo> OriginalFrames;
    std::vector<FrameInfo> Frames;

    float Scale = 1.0f, OffsetX = 0.0f, OffsetY = 0.0f;
    bool IsDragging = false;
    POINT LastMousePos = { 0, 0 };
    D2D1_RECT_F ImgRect = { 0, 0, 0, 0 };

    bool IsFullscreen = false;
    WINDOWPLACEMENT wpPrev = { sizeof(WINDOWPLACEMENT) };
};

// Các factory D2D/WIC dùng chung cho toàn process (không cần tách theo cửa sổ)
IWICImagingFactory* g_pWICFactory = NULL;
ID2D1Factory* g_pD2DFactory = NULL;
HINSTANCE g_hInst = NULL;
HICON g_hExeIcon = NULL;
int g_WindowCount = 0; // đếm số cửa sổ đang mở, hết cửa sổ mới thật sự thoát app

AppState* GetState(HWND hWnd) {
    return (AppState*)GetWindowLongPtr(hWnd, GWLP_USERDATA);
}

void ClearVector(std::vector<FrameInfo>& vec) {
    for (auto& f : vec) SafeRelease(&f.pSource);
    vec.clear();
}

void ClearFrames(AppState* st) {
    ClearVector(st->OriginalFrames);
    ClearVector(st->Frames);
    st->FrameCount = 0; st->CurrentFrame = 0;
}

void UpdateWindowTitle(HWND hWnd, AppState* st) {
    if (!st->FileName.empty()) {
        std::wstring title = std::to_wstring(st->ImgWidth) + L" x " + std::to_wstring(st->ImgHeight) + L" - " + st->FileName;
        SetWindowTextW(hWnd, title.c_str());
    }
    else {
        SetWindowTextW(hWnd, L"My D2D Image Viewer");
    }
}

void UpdateD2DBitmap(HWND hWnd, AppState* st) {
    SafeRelease(&st->pD2DBitmap);
    if (st->Frames.empty() || st->CurrentFrame >= st->Frames.size() || !st->pRenderTarget || !g_pWICFactory) return;
    if (!st->Frames[st->CurrentFrame].pSource) return;

    IWICFormatConverter* pConverter = NULL;
    if (SUCCEEDED(g_pWICFactory->CreateFormatConverter(&pConverter))) {
        if (SUCCEEDED(pConverter->Initialize(st->Frames[st->CurrentFrame].pSource, GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, NULL, 0.f, WICBitmapPaletteTypeCustom))) {
            st->pRenderTarget->CreateBitmapFromWicBitmap(pConverter, NULL, &st->pD2DBitmap);
            st->Frames[st->CurrentFrame].pSource->GetSize(&st->ImgWidth, &st->ImgHeight);
            if (hWnd) UpdateWindowTitle(hWnd, st);
        }
        SafeRelease(&pConverter);
    }
}

void DiscardResources(AppState* st) { SafeRelease(&st->pD2DBitmap); SafeRelease(&st->pRenderTarget); }

HRESULT CreateResources(HWND hWnd, AppState* st) {
    if (st->pRenderTarget) return S_OK;
    RECT rc; GetClientRect(hWnd, &rc);
    HRESULT hr = g_pD2DFactory->CreateHwndRenderTarget(
        D2D1::RenderTargetProperties(),
        D2D1::HwndRenderTargetProperties(hWnd, D2D1::SizeU(rc.right - rc.left, rc.bottom - rc.top), D2D1_PRESENT_OPTIONS_IMMEDIATELY),
        &st->pRenderTarget
    );
    if (SUCCEEDED(hr)) UpdateD2DBitmap(hWnd, st);
    return hr;
}

void LoadImageFile(HWND hWnd, AppState* st, const std::wstring& path) {
    st->CurrentFilePath = path;
    size_t lastSlash = path.find_last_of(L"\\/");
    st->FileName = (lastSlash != std::wstring::npos) ? path.substr(lastSlash + 1) : path;
    ClearFrames(st); SafeRelease(&st->pDecoder);

    bool isIco = false;
    if (path.length() >= 4) {
        std::wstring ext = path.substr(path.length() - 4);
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        if (ext == L".ico") isIco = true;
    }

    if (SUCCEEDED(g_pWICFactory->CreateDecoderFromFilename(path.c_str(), NULL, GENERIC_READ, WICDecodeMetadataCacheOnLoad, &st->pDecoder))) {
        st->pDecoder->GetFrameCount(&st->FrameCount);

        UINT bestFrameIndex = 0;
        UINT maxArea = 0;

        for (UINT i = 0; i < st->FrameCount; ++i) {
            IWICBitmapFrameDecode* pFrame = NULL;
            if (SUCCEEDED(st->pDecoder->GetFrame(i, &pFrame))) {
                FrameInfo info;
                if (FAILED(g_pWICFactory->CreateBitmapFromSource(pFrame, WICBitmapCacheOnLoad, (IWICBitmap**)&info.pSource))) {
                    SafeRelease(&pFrame);
                    continue;
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

                st->OriginalFrames.push_back(info);

                FrameInfo workInfo;
                if (FAILED(g_pWICFactory->CreateBitmapFromSource(info.pSource, WICBitmapCacheOnLoad, (IWICBitmap**)&workInfo.pSource))) {
                    workInfo.pSource = NULL;
                }
                workInfo.delayMs = info.delayMs;
                st->Frames.push_back(workInfo);

                SafeRelease(&pFrame);
            }
        }

        if (isIco && !st->Frames.empty() && bestFrameIndex < st->Frames.size()) {
            std::swap(st->Frames[0], st->Frames[bestFrameIndex]);
            std::swap(st->OriginalFrames[0], st->OriginalFrames[bestFrameIndex]);

            for (size_t i = 1; i < st->Frames.size(); ++i) SafeRelease(&st->Frames[i].pSource);
            for (size_t i = 1; i < st->OriginalFrames.size(); ++i) SafeRelease(&st->OriginalFrames[i].pSource);

            st->Frames.resize(1);
            st->OriginalFrames.resize(1);

            st->FrameCount = 1;
            st->CurrentFrame = 0;

            if (st->pRenderTarget) {
                UpdateD2DBitmap(hWnd, st);
            }
        }
    }

    KillTimer(hWnd, TIMER_GIF);
    if (st->FrameCount > 1 && !st->Frames.empty()) {
        SetTimer(hWnd, TIMER_GIF, st->Frames[0].delayMs, NULL);
    }
}

void ResetAll(HWND hWnd, AppState* st) {
    st->Scale = 1.0f; st->OffsetX = 0.0f; st->OffsetY = 0.0f; st->CurrentFrame = 0;

    ClearVector(st->Frames);
    for (const auto& org : st->OriginalFrames) {
        if (!org.pSource) continue;
        FrameInfo workInfo;
        if (FAILED(g_pWICFactory->CreateBitmapFromSource(org.pSource, WICBitmapCacheOnLoad, (IWICBitmap**)&workInfo.pSource))) {
            workInfo.pSource = NULL;
        }
        workInfo.delayMs = org.delayMs;
        st->Frames.push_back(workInfo);
    }

    KillTimer(hWnd, TIMER_GIF);
    if (st->FrameCount > 1 && !st->Frames.empty()) {
        SetTimer(hWnd, TIMER_GIF, st->Frames[0].delayMs, NULL);
    }

    UpdateD2DBitmap(hWnd, st);
}

void CopyImageToClipboard(HWND hWnd, AppState* st) {
    if (st->Frames.empty() || st->CurrentFrame >= st->Frames.size()) return;
    IWICFormatConverter* pConverter = NULL;
    if (SUCCEEDED(g_pWICFactory->CreateFormatConverter(&pConverter))) {
        if (FAILED(pConverter->Initialize(st->Frames[st->CurrentFrame].pSource, GUID_WICPixelFormat32bppBGR, WICBitmapDitherTypeNone, NULL, 0.f, WICBitmapPaletteTypeCustom))) {
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

void CopyPathToClipboard(HWND hWnd, AppState* st) {
    if (st->CurrentFilePath.empty()) return;
    std::wstring path = L"\"" + st->CurrentFilePath + L"\"";
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

void TransformImage(HWND hWnd, AppState* st, WICBitmapTransformOptions options) {
    if (st->Frames.empty()) return;
    for (auto& frame : st->Frames) {
        if (!frame.pSource) continue;
        IWICBitmapFlipRotator* pRotator = NULL;
        if (SUCCEEDED(g_pWICFactory->CreateBitmapFlipRotator(&pRotator))) {
            if (SUCCEEDED(pRotator->Initialize(frame.pSource, options))) {
                IWICBitmap* pBitmap = NULL;
                if (SUCCEEDED(g_pWICFactory->CreateBitmapFromSource(pRotator, WICBitmapCacheOnLoad, &pBitmap))) {
                    SafeRelease(&frame.pSource);
                    frame.pSource = pBitmap;
                }
            }
            SafeRelease(&pRotator);
        }
    }
    UpdateD2DBitmap(hWnd, st);
}

void OnRender(HWND hWnd, AppState* st) {
    if (FAILED(CreateResources(hWnd, st))) return;
    st->pRenderTarget->BeginDraw();
    st->pRenderTarget->Clear(D2D1::ColorF(0.125f, 0.125f, 0.125f, 1.0f));

    if (st->pD2DBitmap && st->ImgWidth > 0) {
        RECT rc; GetClientRect(hWnd, &rc);
        float winW = (float)(rc.right - rc.left), winH = (float)(rc.bottom - rc.top);

        float fitScale = (std::min)(winW / st->ImgWidth, winH / st->ImgHeight);
        float baseScale = (fitScale < 1.0f) ? fitScale : 1.0f;

        float drawW = st->ImgWidth * baseScale * st->Scale;
        float drawH = st->ImgHeight * baseScale * st->Scale;
        float x = (winW - drawW) / 2.0f + st->OffsetX;
        float y = (winH - drawH) / 2.0f + st->OffsetY;

        st->ImgRect = D2D1::RectF(x, y, x + drawW, y + drawH);

        st->pRenderTarget->DrawBitmap(
            st->pD2DBitmap,
            st->ImgRect,
            1.0f,
            D2D1_BITMAP_INTERPOLATION_MODE_LINEAR
        );
    }
    else {
        st->ImgRect = { 0, 0, 0, 0 };
    }
    if (st->pRenderTarget->EndDraw() == D2DERR_RECREATE_TARGET) DiscardResources(st);
}

void ToggleFullscreen(HWND hWnd, AppState* st) {
    DWORD dwStyle = GetWindowLong(hWnd, GWL_STYLE);
    if (st->IsFullscreen) {
        SetWindowLong(hWnd, GWL_STYLE, dwStyle | WS_OVERLAPPEDWINDOW);
        SetWindowPlacement(hWnd, &st->wpPrev);
        SetWindowPos(hWnd, NULL, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED | SWP_SHOWWINDOW);
        st->IsFullscreen = false;
    }
    else {
        MONITORINFO mi = { sizeof(mi) };
        if (GetWindowPlacement(hWnd, &st->wpPrev) && GetMonitorInfo(MonitorFromWindow(hWnd, MONITOR_DEFAULTTOPRIMARY), &mi)) {
            SetWindowLong(hWnd, GWL_STYLE, dwStyle & ~WS_OVERLAPPEDWINDOW);
            SetWindowPos(hWnd, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top,
                mi.rcMonitor.right - mi.rcMonitor.left,
                mi.rcMonitor.bottom - mi.rcMonitor.top,
                SWP_NOOWNERZORDER | SWP_FRAMECHANGED | SWP_SHOWWINDOW);
            st->IsFullscreen = true;
        }
    }
}

// Khai báo trước vì WndProc (WM_COPYDATA) cần gọi hàm này để tạo cửa sổ mới
HWND CreateNewViewerWindow(const std::wstring& path);

LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    // Gắn AppState* vào window ngay khi window được tạo (trước cả WM_CREATE)
    if (msg == WM_NCCREATE) {
        CREATESTRUCTW* cs = (CREATESTRUCTW*)lParam;
        SetWindowLongPtr(hWnd, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams);
        return DefWindowProc(hWnd, msg, wParam, lParam);
    }

    AppState* st = GetState(hWnd);

    switch (msg) {
    case WM_CREATE:
        if (st) UpdateWindowTitle(hWnd, st);
        break;

    // 🆕 Nhận yêu cầu mở ảnh mới từ 1 tiến trình khác (đã bị chặn single-instance).
    // Thay vì tạo process mới, ta tạo THÊM 1 CỬA SỔ MỚI ngay trong process hiện tại.
    case WM_COPYDATA: {
        COPYDATASTRUCT* pcds = (COPYDATASTRUCT*)lParam;
        if (pcds && pcds->dwData == 1 && pcds->lpData && pcds->cbData > 0) {
            std::wstring path((wchar_t*)pcds->lpData, pcds->cbData / sizeof(wchar_t));
            // bỏ ký tự null thừa nếu có
            size_t nul = path.find(L'\0');
            if (nul != std::wstring::npos) path.resize(nul);
            CreateNewViewerWindow(path);
        }
        return TRUE;
    }

    case WM_TIMER:
        if (st && wParam == TIMER_GIF && st->FrameCount > 1 && !st->Frames.empty()) {
            st->CurrentFrame = (st->CurrentFrame + 1) % st->FrameCount;
            if (st->CurrentFrame < st->Frames.size()) {
                UpdateD2DBitmap(hWnd, st);
                InvalidateRect(hWnd, NULL, FALSE);
                if (st->Frames[st->CurrentFrame].delayMs > 0) {
                    SetTimer(hWnd, TIMER_GIF, st->Frames[st->CurrentFrame].delayMs, NULL);
                }
                else {
                    SetTimer(hWnd, TIMER_GIF, 100, NULL);
                }
            }
        }
        break;

    case WM_LBUTTONDOWN: {
        if (!st) break;
        POINT pt = { LOWORD(lParam), HIWORD(lParam) };
        if (pt.x >= st->ImgRect.left && pt.x <= st->ImgRect.right &&
            pt.y >= st->ImgRect.top && pt.y <= st->ImgRect.bottom) {
            st->IsDragging = true;
            st->LastMousePos = pt;
            SetCapture(hWnd);
        }
        break;
    }

    case WM_LBUTTONDBLCLK: {
        if (!st) break;
        POINT pt = { LOWORD(lParam), HIWORD(lParam) };
        if (pt.x >= st->ImgRect.left && pt.x <= st->ImgRect.right &&
            pt.y >= st->ImgRect.top && pt.y <= st->ImgRect.bottom) {
            if (st->IsDragging) {
                st->IsDragging = false;
                ReleaseCapture();
            }
            ToggleFullscreen(hWnd, st);
        }
        break;
    }

    case WM_MOUSEMOVE:
        if (st && st->IsDragging) {
            POINT currentPos = { LOWORD(lParam), HIWORD(lParam) };
            st->OffsetX += (currentPos.x - st->LastMousePos.x);
            st->OffsetY += (currentPos.y - st->LastMousePos.y);
            st->LastMousePos = currentPos;
            InvalidateRect(hWnd, NULL, FALSE);
        }
        break;

    case WM_LBUTTONUP:
        if (st && st->IsDragging) {
            st->IsDragging = false;
            ReleaseCapture();
        }
        break;

    case WM_KEYDOWN:
        if (st && (wParam == VK_F11 || (wParam == VK_ESCAPE && st->IsFullscreen))) {
            ToggleFullscreen(hWnd, st);
        }
        break;

    case WM_SETCURSOR:
        if (st && LOWORD(lParam) == HTCLIENT) {
            POINT mPos; GetCursorPos(&mPos); ScreenToClient(hWnd, &mPos);
            bool overImage = (mPos.x >= st->ImgRect.left && mPos.x <= st->ImgRect.right &&
                mPos.y >= st->ImgRect.top && mPos.y <= st->ImgRect.bottom);
            SetCursor(LoadCursor(NULL, st->IsDragging ? IDC_SIZEALL : (overImage ? IDC_HAND : IDC_ARROW)));
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
        if (!st) break;
        switch (LOWORD(wParam)) {
        case IDM_RESET: ResetAll(hWnd, st); InvalidateRect(hWnd, NULL, FALSE); break;
        case IDM_COPY: CopyImageToClipboard(hWnd, st); break;
        case IDM_COPY_PATH: CopyPathToClipboard(hWnd, st); break;
        case IDM_ROTATE_90: TransformImage(hWnd, st, WICBitmapTransformRotate90); InvalidateRect(hWnd, NULL, FALSE); break;
        case IDM_FLIP_H: TransformImage(hWnd, st, WICBitmapTransformFlipHorizontal); InvalidateRect(hWnd, NULL, FALSE); break;
        case IDM_FLIP_V: TransformImage(hWnd, st, WICBitmapTransformFlipVertical); InvalidateRect(hWnd, NULL, FALSE); break;
        case IDM_SET_WALL: SystemParametersInfoW(SPI_SETDESKWALLPAPER, 0, (void*)st->CurrentFilePath.c_str(), SPIF_UPDATEINIFILE | SPIF_SENDCHANGE); break;
        }
        break;

    case WM_MOUSEWHEEL: {
        if (!st) break;
        POINT mPos; GetCursorPos(&mPos); ScreenToClient(hWnd, &mPos);
        if (mPos.x >= st->ImgRect.left && mPos.x <= st->ImgRect.right &&
            mPos.y >= st->ImgRect.top && mPos.y <= st->ImgRect.bottom) {

            short zDelta = GET_WHEEL_DELTA_WPARAM(wParam);
            RECT rc; GetClientRect(hWnd, &rc);
            float winW = (float)(rc.right - rc.left), winH = (float)(rc.bottom - rc.top);

            float oldScale = st->Scale;
            float factor = (zDelta > 0) ? 1.2f : (1.0f / 1.2f);
            st->Scale = (std::min)((std::max)(st->Scale * factor, 0.1f), 50.0f);

            float ratio = st->Scale / oldScale;
            st->OffsetX = mPos.x - winW / 2.0f - (mPos.x - winW / 2.0f - st->OffsetX) * ratio;
            st->OffsetY = mPos.y - winH / 2.0f - (mPos.y - winH / 2.0f - st->OffsetY) * ratio;

            InvalidateRect(hWnd, NULL, FALSE);
        }
        break;
    }

    case WM_MBUTTONDOWN: {
        if (!st) break;
        POINT mPos = { LOWORD(lParam), HIWORD(lParam) };
        if (mPos.x >= st->ImgRect.left && mPos.x <= st->ImgRect.right &&
            mPos.y >= st->ImgRect.top && mPos.y <= st->ImgRect.bottom) {
            ResetAll(hWnd, st);
            InvalidateRect(hWnd, NULL, FALSE);
        }
        break;
    }

    case WM_SIZE:
        if (st && st->pRenderTarget) st->pRenderTarget->Resize(D2D1::SizeU(LOWORD(lParam), HIWORD(lParam)));
        InvalidateRect(hWnd, NULL, FALSE);
        break;

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT:
        if (st) OnRender(hWnd, st);
        ValidateRect(hWnd, NULL);
        break;

    case WM_DESTROY:
        if (st) {
            KillTimer(hWnd, TIMER_GIF);
            DiscardResources(st);
            ClearFrames(st);
            SafeRelease(&st->pDecoder);
            delete st;
            SetWindowLongPtr(hWnd, GWLP_USERDATA, 0);
        }
        // 🆕 Chỉ thoát app khi KHÔNG CÒN cửa sổ nào mở (thay vì thoát ngay khi 1 cửa sổ đóng)
        g_WindowCount--;
        if (g_WindowCount <= 0) PostQuitMessage(0);
        break;

    default: return DefWindowProc(hWnd, msg, wParam, lParam);
    }
    return 0;
}

// 🆕 Tạo 1 cửa sổ viewer mới trong process hiện tại. Dùng cho:
// - lần mở đầu tiên của app
// - mỗi lần app (đã chạy sẵn) nhận yêu cầu mở thêm ảnh qua WM_COPYDATA
HWND CreateNewViewerWindow(const std::wstring& path) {
    AppState* st = new AppState();

    HWND hWnd = CreateWindowExW(0, kClassName, L"My D2D Image Viewer", WS_OVERLAPPEDWINDOW,
        (GetSystemMetrics(SM_CXSCREEN) - 900) / 2, (GetSystemMetrics(SM_CYSCREEN) - 600) / 2, 900, 600,
        NULL, NULL, g_hInst, st); // st được truyền qua lpCreateParams, WM_NCCREATE sẽ gắn vào window

    if (!hWnd) { delete st; return NULL; }
    g_WindowCount++;

    BOOL dark = TRUE; DwmSetWindowAttribute(hWnd, 20, &dark, sizeof(dark));
    DWORD corner = 2; DwmSetWindowAttribute(hWnd, 33, &corner, sizeof(corner));

    if (!path.empty() && g_pWICFactory) {
        CreateResources(hWnd, st);
        LoadImageFile(hWnd, st, path);
        UpdateD2DBitmap(hWnd, st);
        if (st->FrameCount > 1 && !st->Frames.empty()) {
            SetTimer(hWnd, TIMER_GIF, st->Frames[0].delayMs, NULL);
        }
    }

    ShowWindow(hWnd, SW_SHOWNORMAL);
    UpdateWindow(hWnd);
    SetForegroundWindow(hWnd);
    return hWnd;
}

int WINAPI wWinMain(_In_ HINSTANCE hInst, _In_opt_ HINSTANCE, _In_ LPWSTR, _In_ int nCmdShow) {
    g_hInst = hInst;
    (void)CoInitialize(NULL);

    // Lấy đường dẫn file từ command line (nếu có) trước khi quyết định single-instance
    std::wstring filePath;
    int argc; LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv) {
        if (argc > 1) filePath = argv[1];
        LocalFree(argv);
    }

    // 🆕 SINGLE-INSTANCE: nếu mutex đã tồn tại nghĩa là app đang chạy rồi.
    // Thay vì mở process mới, gửi đường dẫn ảnh sang cửa sổ đang chạy rồi thoát ngay.
    HANDLE hMutex = CreateMutexW(NULL, TRUE, kMutexName);
    bool alreadyRunning = (GetLastError() == ERROR_ALREADY_EXISTS);

    if (alreadyRunning) {
        HWND hExisting = FindWindowW(kClassName, NULL);
        if (hExisting) {
            // 🔧 SỬA: Windows chặn app "nền" tự gọi SetForegroundWindow để tránh cướp focus.
            // Tiến trình MỚI này vừa được Explorer cấp quyền foreground tạm thời (do người dùng
            // vừa double-click), nên ta "chuyển nhượng" quyền đó cho tiến trình đang chạy sẵn
            // bằng AllowSetForegroundWindow — nhờ vậy khi nó gọi SetForegroundWindow bên trong
            // lúc xử lý WM_COPYDATA sẽ THÀNH CÔNG, cửa sổ mới sẽ nhảy lên trên.
            DWORD dwExistingPid = 0;
            GetWindowThreadProcessId(hExisting, &dwExistingPid);
            if (dwExistingPid) AllowSetForegroundWindow(dwExistingPid);

            if (!filePath.empty()) {
                COPYDATASTRUCT cds;
                cds.dwData = 1;
                cds.cbData = (DWORD)((filePath.length() + 1) * sizeof(wchar_t));
                cds.lpData = (void*)filePath.c_str();
                SendMessageW(hExisting, WM_COPYDATA, 0, (LPARAM)&cds);
            }
            else {
                SetForegroundWindow(hExisting);
            }
        }
        if (hMutex) CloseHandle(hMutex);
        CoUninitialize();
        return 0; // process này thoát ngay, không tạo cửa sổ/process riêng
    }

    // Từ đây là luồng khởi tạo bình thường cho tiến trình ĐẦU TIÊN (chưa có instance nào chạy)
    RegisterAsAppHandler();
    EnableMenuDarkMode();

    D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &g_pD2DFactory);
    (void)CoCreateInstance(CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&g_pWICFactory));

    wchar_t szExePath[MAX_PATH];
    GetModuleFileNameW(NULL, szExePath, MAX_PATH);
    g_hExeIcon = ExtractIconW(NULL, szExePath, 0);

    WNDCLASSEXW wc = { sizeof(WNDCLASSEX), CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS, WndProc, 0, 0, hInst, NULL, NULL, NULL, NULL, kClassName, NULL };
    wc.hIcon = g_hExeIcon;
    wc.hIconSm = g_hExeIcon;
    RegisterClassExW(&wc);

    CreateNewViewerWindow(filePath);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) { TranslateMessage(&msg); DispatchMessage(&msg); }

    // Cleanup tài nguyên dùng chung, chỉ chạy 1 lần khi TOÀN BỘ app thoát (hết cửa sổ)
    if (g_hExeIcon) DestroyIcon(g_hExeIcon);
    SafeRelease(&g_pWICFactory);
    SafeRelease(&g_pD2DFactory);
    if (hMutex) CloseHandle(hMutex);
    CoUninitialize();
    return (int)msg.wParam;
}
