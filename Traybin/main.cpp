#include <iostream>
#include <windows.h>
#include <shellapi.h>
#include <thread>
#include <chrono>
#include <memory>
#include <string>
#include "resource.h"

constexpr const wchar_t *AUTOSTART_KEY = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr const wchar_t *APP_NAME = L"Traybin";

static UINT WM_TASKBARCREATED = 0;

bool IsAutoStartEnabled()
{
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, AUTOSTART_KEY, 0, KEY_READ, &hKey) != ERROR_SUCCESS)
    {
        return false;
    }

    wchar_t path[MAX_PATH];
    DWORD size = sizeof(path);
    const bool exists = RegQueryValueExW(hKey, APP_NAME, nullptr, nullptr, reinterpret_cast<LPBYTE>(path), &size) == ERROR_SUCCESS;
    RegCloseKey(hKey);
    return exists;
}

void SetAutoStart()
{
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);

    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, AUTOSTART_KEY, 0, KEY_SET_VALUE, &hKey) == ERROR_SUCCESS)
    {
        RegSetValueExW(hKey, APP_NAME, 0, REG_SZ, reinterpret_cast<const BYTE *>(exePath), static_cast<DWORD>((wcslen(exePath) + 1) * sizeof(wchar_t)));
        RegCloseKey(hKey);
    }
}

void RemoveAutoStart()
{
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, AUTOSTART_KEY, 0, KEY_SET_VALUE, &hKey) == ERROR_SUCCESS)
    {
        RegDeleteValueW(hKey, APP_NAME);
        RegCloseKey(hKey);
    }
}

class IRecycleBinOperations
{
public:
    virtual ~IRecycleBinOperations() = default;
    virtual void emptyBin() = 0;
    virtual bool isEmpty() = 0;
    virtual void open() = 0;
};

class WindowsRecycleBinOperations final : public IRecycleBinOperations
{
public:
    void emptyBin() override
    {
        SHEmptyRecycleBinW(nullptr, nullptr, SHERB_NOCONFIRMATION);
    }

    [[nodiscard]] bool isEmpty() override
    {
        SHQUERYRBINFO recycleBinInfo = {};
        recycleBinInfo.cbSize = sizeof(SHQUERYRBINFO);
        if (const HRESULT result = SHQueryRecycleBinW(nullptr, &recycleBinInfo); result != S_OK)
        {
            std::cerr << "Error querying recycle bin state." << std::endl;
            return false;
        }
        return recycleBinInfo.i64NumItems == 0;
    }

    void open() override
    {
        ShellExecuteW(nullptr, L"open", L"shell:RecycleBinFolder", nullptr, nullptr, SW_SHOWNORMAL);
    }
};

class INotificationSystem
{
public:
    virtual ~INotificationSystem() = default;
    virtual void showNotification(const std::wstring &title, const std::wstring &message) = 0;
    virtual void updateTrayIcon(bool isEmpty) = 0;
};

class WindowsNotificationSystem final : public INotificationSystem
{
public:
    explicit WindowsNotificationSystem(HWND windowHandle) : windowHandle{windowHandle}
    {
        notifyIconData.cbSize = sizeof(NOTIFYICONDATA);
        notifyIconData.hWnd = windowHandle;
        notifyIconData.uID = 1;
        notifyIconData.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
        notifyIconData.uCallbackMessage = WM_APP;
        updateTrayIcon(true);
        addTrayIcon();
    }

    void addTrayIcon()
    {
        Shell_NotifyIcon(NIM_ADD, &notifyIconData);
    }

    void removeTrayIcon()
    {
        Shell_NotifyIcon(NIM_DELETE, &notifyIconData);
    }

    ~WindowsNotificationSystem() override
    {
        removeTrayIcon();
    }

    void showNotification(const std::wstring &title, const std::wstring &message) override
    {
        NOTIFYICONDATA notificationData = notifyIconData;
        notificationData.uFlags |= NIF_INFO;
        wcscpy_s(notificationData.szInfoTitle, title.c_str());
        wcscpy_s(notificationData.szInfo, message.c_str());
        Shell_NotifyIcon(NIM_MODIFY, &notificationData);
    }

    void updateTrayIcon(bool isEmpty) override
    {
        const int iconResource = isEmpty ? IDI_EMPTY_ICON : IDI_FULL_ICON;
        notifyIconData.hIcon = LoadIcon(GetModuleHandle(nullptr), MAKEINTRESOURCE(iconResource));
        const wchar_t *tooltipText = isEmpty ? L"Recycle Bin is empty" : L"Recycle Bin contains items";
        wcscpy_s(notifyIconData.szTip, tooltipText);
        Shell_NotifyIcon(NIM_MODIFY, &notifyIconData);
        addTrayIcon();
    }

private:
    HWND windowHandle;
    NOTIFYICONDATA notifyIconData{};
};

class RecycleBinManager
{
public:
    RecycleBinManager(
        std::unique_ptr<IRecycleBinOperations> binOperations,
        std::unique_ptr<INotificationSystem> notificationSystem)
        : binOperations{std::move(binOperations)}, notificationSystem{std::move(notificationSystem)}
    {
        updateIcon();
    }

    void emptyRecycleBin()
    {
        binOperations->emptyBin();
        updateIcon();
    }

    void openRecycleBin()
    {
        binOperations->open();
    }

    void updateIcon()
    {
        const bool isEmpty = binOperations->isEmpty();
        notificationSystem->updateTrayIcon(isEmpty);
    }

private:
    std::unique_ptr<IRecycleBinOperations> binOperations;
    std::unique_ptr<INotificationSystem> notificationSystem;
};

LRESULT CALLBACK WindowProc(HWND windowHandle, UINT message, WPARAM wParam, LPARAM lParam)
{
    static RecycleBinManager *recycleBinManager = nullptr;

    if (message == WM_TASKBARCREATED)
    {
        if (recycleBinManager)
        {
            recycleBinManager->updateIcon();
            return 0;
        }
    }

    switch (message)
    {
    case WM_CREATE:
        recycleBinManager = new RecycleBinManager(
            std::make_unique<WindowsRecycleBinOperations>(),
            std::make_unique<WindowsNotificationSystem>(windowHandle));
        break;
    case WM_APP:
        if (lParam == WM_LBUTTONDBLCLK)
        {
            if (recycleBinManager)
            {
                recycleBinManager->emptyRecycleBin();
            }
        }
        else if (lParam == WM_RBUTTONUP && (GetAsyncKeyState(VK_LSHIFT) & 0x8000))
        {
            RemoveAutoStart();
            DestroyWindow(windowHandle);
        }
        else if (lParam == WM_RBUTTONUP)
        {
            if (recycleBinManager)
            {
                recycleBinManager->openRecycleBin();
            }
        }
        break;
    case WM_APP + 1:
        if (recycleBinManager)
        {
            recycleBinManager->updateIcon();
        }
        break;
    case WM_DESTROY:
        if (recycleBinManager)
        {
            delete recycleBinManager;
            recycleBinManager = nullptr;
        }
        PostQuitMessage(0);
        break;
    }
    return DefWindowProc(windowHandle, message, wParam, lParam);
}

int WINAPI wWinMain(_In_ HINSTANCE instanceHandle,
                    _In_opt_ HINSTANCE prevInstanceHandle,
                    _In_ LPWSTR commandLine,
                    _In_ int showCommand)
{
    WM_TASKBARCREATED = RegisterWindowMessageW(L"TaskbarCreated");

    if (!IsAutoStartEnabled())
    {
        SetAutoStart();
    }

    ShowWindow(GetConsoleWindow(), SW_HIDE);

    WNDCLASS windowClass = {};
    windowClass.lpfnWndProc = WindowProc;
    windowClass.hInstance = instanceHandle;
    windowClass.lpszClassName = L"Traybin";

    RegisterClass(&windowClass);

    const HWND windowHandle = CreateWindowEx(
        0, L"Traybin", L"Traybin application", WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
        nullptr, nullptr, instanceHandle, nullptr);

    ShowWindow(windowHandle, SW_HIDE);

    std::atomic<bool> isRunning{true};
    std::thread updateThread([windowHandle, &isRunning]
                             {
        while (isRunning) {
            SendMessage(windowHandle, WM_APP + 1, 0, 0);
            std::this_thread::sleep_for(std::chrono::seconds(1));
        } });
    updateThread.detach();

    MSG message{};
    while (GetMessage(&message, nullptr, 0, 0))
    {
        TranslateMessage(&message);
        DispatchMessage(&message);
    }

    isRunning = false;

    return EXIT_SUCCESS;
}