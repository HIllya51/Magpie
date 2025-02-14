
#include "pch.h"
#include "ScalingRuntime.h"
#include "ScalingOptions.h"
#include "Logger.h"
#include "json.hpp"
#include "CommonSharedConstants.h"
#include "StrHelper.h"
#include "Win32Helper.h"
#include <fstream>
#include <variant>
#include <shellapi.h>
#pragma warning(disable : 4312)
#pragma warning(disable : 4100)
#pragma warning(disable : 4267)
#pragma comment(lib, "Dwmapi.lib")
#pragma comment(lib, "Magnification.lib")
using namespace Magpie;

enum class CursorScaling
{
    x0_5,
    x0_75,
    NoScaling,
    x1_25,
    x1_5,
    x2,
    Source,
    Custom
};
void solvecursorscale(ScalingOptions &options, CursorScaling cursorScaling, float customCursorScaling)
{

    switch (cursorScaling)
    {
    case CursorScaling::x0_5:
        options.cursorScaling = 0.5;
        break;
    case CursorScaling::x0_75:
        options.cursorScaling = 0.75;
        break;
    case CursorScaling::NoScaling:
        options.cursorScaling = 1.0;
        break;
    case CursorScaling::x1_25:
        options.cursorScaling = 1.25;
        break;
    case CursorScaling::x1_5:
        options.cursorScaling = 1.5;
        break;
    case CursorScaling::x2:
        options.cursorScaling = 2.0;
        break;
    case CursorScaling::Source:
        // 0 或负值表示和源窗口缩放比例相同
        options.cursorScaling = 0;
        break;
    case CursorScaling::Custom:
        options.cursorScaling = customCursorScaling;
        break;
    default:
        options.cursorScaling = 1.0;
        break;
    }
}
void loadeffects(ScalingOptions &options, const nlohmann::json &effects)
{

    for (int i = 0; i < effects.size(); i++)
    {
        auto &ei = effects[i];
        EffectOption ef;
        ef.name = StrHelper::UTF8ToUTF16(ei["name"]);
        if (ei.find("scalingType") != ei.end())
            ef.scalingType = ei["scalingType"];
        if (ei.find("scale") != ei.end())
            ef.scale = {ei["scale"]["x"], ei["scale"]["y"]};
        if (ei.find("parameters") != ei.end())
        {
            auto parameters = ei["parameters"];
            for (auto &param : parameters.items())
                ef.parameters.insert(std::make_pair(StrHelper::UTF8ToUTF16(param.key()), param.value()));
        }
        options.effects.push_back(ef);
    }
}
std::optional<ScalingOptions> LoadMagOptions(const nlohmann::json &config, int profileindex)
{
    ScalingOptions options;
    auto profile = config["profiles"][profileindex];
    int scalingMode = profile["scalingMode"];
    if (scalingMode < 0)
        return {};
    loadeffects(options, config["scalingModes"][scalingMode]["effects"]);

    if (options.effects.empty())
    {
        return {};
    }
    else
    {
        // for (EffectOption& effect : options.effects) {
        //     if (!EffectsService::Get().GetEffect(effect.name)) {
        //         // 存在无法解析的效果
        //         return false;
        //     }
        // }
    }

    //// 尝试启用触控支持
    // bool isTouchSupportEnabled;
    // if (!TouchHelper::TryLaunchTouchHelper(isTouchSupportEnabled)) {
    //     Logger::Get().Error("TryLaunchTouchHelper 失败");
    //     return false;
    // }

    // options.graphicsCardId = profile["graphicsCard"];
    options.captureMethod = profile["captureMethod"];
    // options.IsVSync(profile["VSync"]);
    // options.IsTripleBuffering(profile["tripleBuffering"]);
    if (profile["frameRateLimiterEnabled"])
    {
        options.maxFrameRate = profile["maxFrameRate"];
        options.minFrameRate = std::min((float)config["minFrameRate"], *options.maxFrameRate);
    }
    else
    {
        options.minFrameRate = config["minFrameRate"];
    }
    options.multiMonitorUsage = profile["multiMonitorUsage"];
    options.cursorInterpolationMode = profile["cursorInterpolationMode"];

    // options.IsTouchSupportEnabled(isTouchSupportEnabled);

    if (profile["croppingEnabled"])
    {
        auto crop = profile["cropping"];
        options.cropping = {crop["left"], crop["top"], crop["right"], crop["bottom"]};
    }

    solvecursorscale(options, profile["cursorScaling"], profile["customCursorScaling"]);

    // 应用全局配置
    options.IsBenchmarkMode(config["benchmarkMode"]);
    options.IsFP16Disabled(config["disableFP16"]);
    options.IsInlineParams(config["inlineParams"]);
    options.IsDebugMode(config["debugMode"]);
    options.IsEffectCacheDisabled(config["disableEffectCache"]);
    options.IsFontCacheDisabled(config["disableFontCache"]);
    options.IsSaveEffectSources(config["saveEffectSources"]);
    options.IsWarningsAreErrors(config["warningsAreErrors"]);
    options.IsAllowScalingMaximized(config["allowScalingMaximized"]);
    options.IsSimulateExclusiveFullscreen(config["simulateExclusiveFullscreen"]);
    options.duplicateFrameDetectionMode = config["duplicateFrameDetectionMode"];
    options.IsStatisticsForDynamicDetectionEnabled(config["enableStatisticsForDynamicDetection"]);

    options.IsWindowResizingDisabled(profile["disableWindowResizing"]);
    options.Is3DGameMode(profile["3DGameMode"]);
    options.IsShowFPS(profile["showFPS"]);
    options.IsCaptureTitleBar(profile["captureTitleBar"]);
    options.IsAdjustCursorSpeed(profile["adjustCursorSpeed"]);
    options.IsDrawCursor(profile["drawCursor"]);
    options.IsDirectFlipDisabled(profile["disableDirectFlip"]);

    return options;
}
void _InitializeLogger()
{
    Logger &logger = Logger::Get();
    logger.Initialize(
        spdlog::level::info,
        CommonSharedConstants::LOG_PATH,
        100000,
        2);
}
template <class T1, class T2>
class safemap
{
    std::mutex lock;
    std::unordered_map<T1, T2> _map;

public:
    std::optional<T2> get(const T1 &key)
    {
        std::lock_guard _(lock);
        if (_map.find(key) == _map.end())
            return {};
        return _map.at(key);
    }
    void insert(const T1 &k, const T2 &v)
    {
        std::lock_guard _(lock);
        _map.insert(std::make_pair(k, v));
    }
};
class msgwindow
{

public:
    HWND winId;
    using messagecallback = std::function<void()>;
    using messagecallback_P = std::function<void(WPARAM, LPARAM)>;
    safemap<UINT, std::pair<int, std::variant<messagecallback, messagecallback_P>>> messageproc;
    safemap<std::wstring, UINT> messagemap;
    LRESULT wndproc(UINT message, WPARAM wParam, LPARAM lParam)
    {
        auto proc = messageproc.get(message);
        if (!proc.has_value())
            return DefWindowProc(winId, message, wParam, lParam);
        auto [type, func] = proc.value();
        if (type == 0)
            std::get<messagecallback>(func)();
        else if (type == 1)
            std::get<messagecallback_P>(func)(wParam, lParam);
        return 0;
    }
    msgwindow(LPCWSTR windowname)
    {

        WNDCLASSEXW message_wc{};
        message_wc.cbSize = sizeof(WNDCLASSEX);
        message_wc.hInstance = GetModuleHandleA(0);
        message_wc.lpszClassName = windowname;
        message_wc.lpfnWndProc = [](HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
        {
            msgwindow *_window = reinterpret_cast<msgwindow *>(GetWindowLongPtrW(hWnd, GWLP_USERDATA));
            if ((!_window) || (_window->winId != hWnd))
                return DefWindowProc(hWnd, message, wParam, lParam);
            return _window->wndproc(message, wParam, lParam);
        };

        static auto _ = RegisterClassExW(&message_wc);
        winId = CreateWindowEx(0, windowname, nullptr, 0, 0, 0, 0, 0, HWND_MESSAGE,
                               nullptr, GetModuleHandleA(0), nullptr);
        SetWindowLongPtrW(winId, GWLP_USERDATA, (LONG_PTR)this);
    }
    void registmessage(std::wstring message, messagecallback callback)
    {
        auto msg = RegisterWindowMessageW(message.c_str());
        messageproc.insert(msg, {0, callback});
        messagemap.insert(message, msg);
    }
    void registmessage_P(std::wstring message, messagecallback_P callback)
    {
        auto msg = RegisterWindowMessageW(message.c_str());
        messageproc.insert(msg, {1, callback});
        messagemap.insert(message, msg);
    }
    void callmessage(std::wstring message)
    {
        auto msg = messagemap.get(message);
        if (msg.has_value())
            PostMessageW(winId, msg.value(), 0, 0);
    }
    static void runloop()
    {
        MSG msg;
        while (GetMessageW(&msg, nullptr, 0, 0) > 0)
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    static void endloop()
    {
        PostQuitMessage(0);
    }
};
static void SetWorkingDir() noexcept
{
    std::wstring path = Win32Helper::GetExePath();

    FAIL_FAST_IF_FAILED(PathCchRemoveFileSpec(
        path.data(),
        path.size() + 1));

    FAIL_FAST_IF_WIN32_BOOL_FALSE(SetCurrentDirectory(path.c_str()));
}
static void InitializeLogger(const char *logFilePath) noexcept
{
    Logger::Get().Initialize(
        spdlog::level::info,
        logFilePath,
        100000,
        2);
}
static void IncreaseTimerResolution() noexcept
{
    PROCESS_POWER_THROTTLING_STATE powerThrottling{
        .Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION,
        .ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED |
                       PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION,
        .StateMask = 0};
    SetProcessInformation(
        GetCurrentProcess(),
        ProcessPowerThrottling,
        &powerThrottling,
        sizeof(powerThrottling));
}
void notifyprepared(const std::wstring &eventname)
{
    SECURITY_DESCRIPTOR sd = {};
    InitializeSecurityDescriptor(&sd, SECURITY_DESCRIPTOR_REVISION);
    SetSecurityDescriptorDacl(&sd, TRUE, NULL, FALSE);
    SECURITY_ATTRIBUTES allAccess = SECURITY_ATTRIBUTES{sizeof(SECURITY_ATTRIBUTES), &sd, FALSE};
    SetEvent(CreateEvent(&allAccess, FALSE, FALSE, eventname.c_str()));
}
int WINAPI wWinMain(
    _In_ HINSTANCE hInstance,
    _In_opt_ HINSTANCE hPrevInstance,
    _In_ LPWSTR lpCmdLine,
    _In_ int nShowCmd)
{
    HeapSetInformation(NULL, HeapEnableTerminationOnCorruption, nullptr, 0);

    SetWorkingDir();

    enum
    {
        Normal,
        RegisterTouchHelper,
        UnRegisterTouchHelper
    } mode = Normal; //[&]() {
    //     if (lpCmdLine == L"-r"sv) {
    //         return RegisterTouchHelper;
    //     } else if (lpCmdLine == L"-ur"sv) {
    //         return UnRegisterTouchHelper;
    //     } else {
    //         return Normal;
    //     }
    // }();

    InitializeLogger(mode == Normal ? CommonSharedConstants::LOG_PATH : CommonSharedConstants::REGISTER_TOUCH_HELPER_LOG_PATH);

    // if (mode == RegisterTouchHelper) {
    //     // 使 TouchHelper 获得 UIAccess 权限
    //     return Magpie::TouchHelper::Register() ? 0 : 1;
    // } else if (mode == UnRegisterTouchHelper) {
    //     return Magpie::TouchHelper::Unregister() ? 0 : 1;
    // }

    IncreaseTimerResolution();

    winrt::init_apartment(winrt::apartment_type::single_threaded);

    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    //<dpiAwareness xmlns="http://schemas.microsoft.com/SMI/2016/WindowsSettings">PerMonitorV2</dpiAwareness>

    int argc;
    auto argv = CommandLineToArgvW(lpCmdLine, &argc);

    const auto Magpie_Core_CLI_Message_ToggleOverlay = L"Magpie_Core_CLI_Message_ToggleOverlay";
    const auto Magpie_Core_CLI_Message_Exit = L"Magpie_Core_CLI_Message_Exit";
    const auto Magpie_Core_CLI_Message_Stop = L"Magpie_Core_CLI_Message_Stop";
    const auto Magpie_Core_CLI_Message_Start = L"Magpie_Core_CLI_Message_Start";

    const auto WNDCLS_Magpie_Core_CLI_Message = L"WNDCLS_Magpie_Core_CLI_Message";
    auto Magpie_notify_prepared_ok = L"Magpie_notify_prepared_ok_" + std::to_wstring(GetCurrentProcessId());

    msgwindow _msgwindow(WNDCLS_Magpie_Core_CLI_Message);
    ScalingRuntime magrt;
    /*magrt.IsRunningChanged([&](bool running) {
        if (!running) {
            _msgwindow.callmessage(Magpie_Core_CLI_Message_Stop);
        }
    });*/
    // Sleep(100);

    _msgwindow.registmessage_P(Magpie_Core_CLI_Message_Start, [argv, &magrt](WPARAM wp, LPARAM lp)
                               {
        auto targethwnd = (HWND)lp;
        auto config = nlohmann::json::parse(std::ifstream(argv[0]));
        int profileindex = (int)wp;
        auto options = LoadMagOptions(config, profileindex);
        if (!options)return;
        SetForegroundWindow(targethwnd);
        magrt.Start(targethwnd, std::move(options.value())); });
    _msgwindow.registmessage(Magpie_Core_CLI_Message_ToggleOverlay, [&magrt]()
                             { magrt.ToggleOverlay(); });
    _msgwindow.registmessage(Magpie_Core_CLI_Message_Stop, [&magrt]()
                             { magrt.Stop(); });
    _msgwindow.registmessage(Magpie_Core_CLI_Message_Exit, [&magrt]()
                             { msgwindow::endloop(); });

    notifyprepared(Magpie_notify_prepared_ok);

    msgwindow::runloop();

    return 0;
}