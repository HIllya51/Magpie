
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
#include <shlobj_core.h>
#pragma warning(disable : 4312)
#pragma warning(disable : 4100)
#pragma warning(disable : 4267)
#pragma comment(lib, "Shcore.lib")
#pragma comment(lib, "Dwmapi.lib")
#pragma comment(lib, "Magnification.lib")
#pragma comment(lib, "Dcomp.lib")
#pragma comment(lib, "bcp47mrm.lib")
using namespace Magpie;

const auto Magpie_Core_CLI_Message_Exit = L"Magpie_Core_CLI_Message_Exit";
const auto Magpie_Core_CLI_Message_Stop = L"Magpie_Core_CLI_Message_Stop";
const auto Magpie_Core_CLI_Message_Start = L"Magpie_Core_CLI_Message_Start";
const auto Magpie_Core_CLI_Message_Start_WindowedMode = L"Magpie_Core_CLI_Message_Start_WindowedMode";

const auto WNDCLS_Magpie_Core_CLI_Message = L"WNDCLS_Magpie_Core_CLI_Message";
static auto Magpie_Core_CLI_ErrorMessage = RegisterWindowMessage(L"Magpie_Core_CLI_ErrorMessage");
static auto Magpie_Core_CLI_ToastMessage = RegisterWindowMessage(L"Magpie_Core_CLI_ToastMessage");
static auto Magpie_Core_CLI_ScalingOptions_Save = RegisterWindowMessage(L"Magpie_Core_CLI_ScalingOptions_Save");

enum class InitialWindowedScaleFactor {
	Auto,
	x1_25,
	x1_5,
	x1_75,
	x2,
	x3,
	Custom,
	COUNT
};

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
void solve_initialWindowedScaleFactor(ScalingOptions& options, InitialWindowedScaleFactor initialWindowedScaleFactor, float customInitialWindowedScaleFactor) {
	switch (initialWindowedScaleFactor) {
	case InitialWindowedScaleFactor::Auto:
		options.initialWindowedScaleFactor = 0.0f;
		break;
	case InitialWindowedScaleFactor::x1_25:
		options.initialWindowedScaleFactor = 1.25f;
		break;
	case InitialWindowedScaleFactor::x1_5:
		options.initialWindowedScaleFactor = 1.5f;
		break;
	case InitialWindowedScaleFactor::x1_75:
		options.initialWindowedScaleFactor = 1.5f;
		break;
	case InitialWindowedScaleFactor::x2:
		options.initialWindowedScaleFactor = 2.0f;
		break;
	case InitialWindowedScaleFactor::x3:
		options.initialWindowedScaleFactor = 3.0f;
		break;
	case InitialWindowedScaleFactor::Custom:
		options.initialWindowedScaleFactor = customInitialWindowedScaleFactor;
		break;
	default:
		options.initialWindowedScaleFactor = 0.0f;
		break;
	}
}

void solvecursorscale(ScalingOptions& options, CursorScaling cursorScaling, float customCursorScaling) {

    switch (cursorScaling) {
    case CursorScaling::x0_5:
        options.cursorScaleFactor = 0.5;
        break;
    case CursorScaling::x0_75:
        options.cursorScaleFactor = 0.75;
        break;
    case CursorScaling::NoScaling:
        options.cursorScaleFactor = 1.0;
        break;
    case CursorScaling::x1_25:
        options.cursorScaleFactor = 1.25;
        break;
    case CursorScaling::x1_5:
        options.cursorScaleFactor = 1.5;
        break;
    case CursorScaling::x2:
        options.cursorScaleFactor = 2.0;
        break;
    case CursorScaling::Source:
        // 0 或负值表示和源窗口缩放比例相同
        options.cursorScaleFactor = 0;
        break;
    case CursorScaling::Custom:
        options.cursorScaleFactor = customCursorScaling;
        break;
    default:
        options.cursorScaleFactor = 1.0;
        break;
    }
}
void loadeffects(std::vector<EffectOption>& effects, const nlohmann::json& __effects) {

    for (int i = 0; i < __effects.size(); i++) {
        auto& ei = __effects[i];
        EffectOption ef;
        ef.name = ei["name"];
        if (ei.find("scalingType") != ei.end())
            ef.scalingType = ei["scalingType"];
        if (ei.find("scale") != ei.end())
            ef.scale = { ei["scale"]["x"], ei["scale"]["y"] };
        if (ei.find("parameters") != ei.end()) {
            ef.parameters = ei["parameters"];
        }
        effects.push_back(ef);
    }
}

static std::filesystem::path GetSystemScreenshotsDir() noexcept {
    // 如果 Screenshots 文件夹不存在将失败
    wil::unique_cotaskmem_string folder;
    HRESULT hr = SHGetKnownFolderPath(
        FOLDERID_Screenshots, KF_FLAG_DEFAULT, NULL, folder.put());
    if (SUCCEEDED(hr)) {
        return folder.get();
    }

    // 屏幕截图文件夹默认路径是 %USERPROFILE%\Pictures\Screenshots

    hr = SHGetKnownFolderPath(
        FOLDERID_Pictures, KF_FLAG_DEFAULT, NULL, folder.put());
    if (SUCCEEDED(hr)) {
        return StrHelper::Concat(folder.get(), L"\\Screenshots");
    }

    hr = SHGetKnownFolderPath(
        FOLDERID_Profile, KF_FLAG_DEFAULT, NULL, folder.put());
    if (SUCCEEDED(hr)) {
        return StrHelper::Concat(folder.get(), L"\\Pictures\\Screenshots");
    }

    Logger::Get().ComError("SHGetKnownFolderPath 失败", hr);
    return {};
}


// 失败时返回空字符串
std::filesystem::path ScreenshotsDir(const std::filesystem::path& _screenshotsDir) noexcept {
    if (_screenshotsDir.empty()) {
        // 系统“屏幕截图”文件夹
        return GetSystemScreenshotsDir();
    } else if (_screenshotsDir.is_relative()) {
        // 相对路径
        std::wstring workingDir;
        HRESULT hr = wil::GetCurrentDirectoryW(workingDir);
        if (FAILED(hr)) {
            Logger::Get().ComError("wil::GetCurrentDirectoryW 失败", hr);
            return {};
        }

        return (std::filesystem::path(std::move(workingDir)) / _screenshotsDir).lexically_normal();
    } else {
        // 绝对路径
        return _screenshotsDir;
    }
}


void LoadOverlayOptions(ScalingOptions& options, const nlohmann::json& config) {
    std::filesystem::path _screenshotsDir = StrHelper::UTF8ToUTF16(config["screenshotsDir"]);
    options.screenshotsDir = ScreenshotsDir(_screenshotsDir);
    if (options.screenshotsDir.empty()) {
        // 回落到使用当前目录
        options.screenshotsDir = L".";
    }
    options.fullscreenInitialToolbarState = config["fullscreenInitialToolbarState"];
    options.windowedInitialToolbarState = config["windowedInitialToolbarState"];
    // 与主程序默认快捷键一致
    options.overlayOptions.scaleShortcut = config.value("scaleShortcut", "Alt+Shift+A");
    options.overlayOptions.windowedModeScaleShortcut = config.value("windowedModeScaleShortcut", "Alt+Shift+Q");
    options.overlayOptions.takeScreenshotShortcut = config.value("takeScreenshotShortcut", "Alt+Shift+S");
    for (auto&& [key, value] : config["windows"].items()) {

        options.overlayOptions.windows.emplace(
            key,
            OverlayWindowOption
            {
                .hArea = value["hArea"],
                .vArea = value["vArea"],
                .hPos = value["hPos"],
                .vPos = value["vPos"],
            }
            );
    }
}
std::string SeriesOverlayOptions(const ScalingOptions& options) {
    nlohmann::json config;
    for (auto&& [key, value] : options.overlayOptions.windows) {
        config[key] = {
            {"hArea",value.hArea },
            {"vArea",value.vArea },
            {"hPos",value.hPos },
            {"vPos",value.vPos },
        };
    }

    nlohmann::json overlay;
    overlay["windows"] = config;
    nlohmann::json _overlay;
    _overlay["overlay"] = overlay;
    return _overlay.dump();
}
std::optional<ScalingOptions> LoadMagOptions(const nlohmann::json& config, int profileindex) {
    ScalingOptions options;
    auto profile = config["profiles"][profileindex];
    int scalingMode = profile["scalingMode"];
    if (scalingMode < 0)
        return {};
    loadeffects(options.effects, config["scalingModes"][scalingMode]["effects"]);

    if (options.effects.empty()) {
        return {};
    } else {
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

    options.graphicsCardId.idx = profile["graphicsCardId"]["idx"];
    options.graphicsCardId.vendorId = profile["graphicsCardId"]["vendorId"];
    options.graphicsCardId.deviceId = profile["graphicsCardId"]["deviceId"];
    options.captureMethod = profile["captureMethod"];
    if (profile["frameRateLimiterEnabled"]) {
        options.maxFrameRate = profile["maxFrameRate"];
        options.minFrameRate = std::min((float)config["minFrameRate"], *options.maxFrameRate);
    } else {
        options.minFrameRate = config["minFrameRate"];
    }
    options.fullscreenInitialToolbarState = profile["fullscreenInitialToolbarState"];
    options.windowedInitialToolbarState = profile["windowedInitialToolbarState"];
    options.multiMonitorUsage = profile["multiMonitorUsage"];
	options.outputAlignment= profile["outputAlignment"];
    options.cursorInterpolationMode = profile["cursorInterpolationMode"];

    // options.IsTouchSupportEnabled(isTouchSupportEnabled);

    if (profile["croppingEnabled"]) {
        auto crop = profile["cropping"];
        options.cropping = { crop["left"], crop["top"], crop["right"], crop["bottom"] };
    }

    solvecursorscale(options, profile["cursorScaling"], profile["customCursorScaling"]);
	solve_initialWindowedScaleFactor(options, profile["initialWindowedScaleFactor"], profile["customInitialWindowedScaleFactor"]);


	if (profile["autoHideCursorEnabled"]) {
		options.autoHideCursorDelay = profile["autoHideCursorDelay"];
	}

    // 应用全局配置
	options.IsKeepScreenOn(config["keepScreenOn"]);
    options.IsDeveloperMode(config["developerMode"]);
    options.IsBenchmarkMode(config["benchmarkMode"]);
	options.IsTopmostDisabled(config["disableTopmost"]);
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

    options.Is3DGameMode(profile["3DGameMode"]);
    options.IsCaptureTitleBar(profile["captureTitleBar"]);
    options.IsAdjustCursorSpeed(profile["adjustCursorSpeed"]);
    options.IsDirectFlipDisabled(profile["disableDirectFlip"]);

    LoadOverlayOptions(options, config["overlay"]);
    options.showToast = [](HWND hwndTarget, std::wstring_view title, std::wstring_view msg) noexcept {
        PostMessage(HWND_BROADCAST, Magpie_Core_CLI_ToastMessage, 
			(WPARAM)GlobalAddAtom(std::wstring(msg).c_str()),
			(LPARAM)GlobalAddAtom(std::wstring(title).c_str()));
    };
	options.showError = [](HWND hWnd, ScalingError error) noexcept {
        PostMessage(HWND_BROADCAST, Magpie_Core_CLI_ErrorMessage, (WPARAM)error, 0);
	};
    options.save = [](const ScalingOptions& options, HWND /*hwndScaling*/) noexcept {
        auto atom = GlobalAddAtomA(SeriesOverlayOptions(options).c_str());
        PostMessage(HWND_BROADCAST, Magpie_Core_CLI_ScalingOptions_Save, (WPARAM)atom, 0);
    };
    return options;
}
void _InitializeLogger() {
    Logger& logger = Logger::Get();
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
    std::optional<T2> get(const T1& key) {
        std::lock_guard _(lock);
        if (_map.find(key) == _map.end())
            return {};
        return _map.at(key);
    }
    void insert(const T1& k, const T2& v) {
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
    using messagecallback_v = std::variant<messagecallback, messagecallback_P>;
    safemap<UINT, messagecallback_v> messageproc;
    safemap<std::wstring, UINT> messagemap;
    LRESULT wndproc(UINT message, WPARAM wParam, LPARAM lParam) {
        auto proc = messageproc.get(message);
        if (!proc.has_value())
            return DefWindowProc(winId, message, wParam, lParam);
        if (auto* func = std::get_if<messagecallback>(&proc.value()))
            (*func)();
        else if (auto* func = std::get_if<messagecallback_P>(&proc.value()))
            (*func)(wParam, lParam);
        return 0;
    }
    msgwindow(LPCWSTR windowname) {

        WNDCLASSEXW message_wc{};
        message_wc.cbSize = sizeof(WNDCLASSEX);
        message_wc.hInstance = GetModuleHandleA(0);
        message_wc.lpszClassName = windowname;
        message_wc.lpfnWndProc = [](HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
            msgwindow* _window = reinterpret_cast<msgwindow*>(GetWindowLongPtrW(hWnd, GWLP_USERDATA));
            if ((!_window) || (_window->winId != hWnd))
                return DefWindowProc(hWnd, message, wParam, lParam);
            return _window->wndproc(message, wParam, lParam);
        };

        static auto _ = RegisterClassExW(&message_wc);
        winId = CreateWindowEx(0, windowname, nullptr, 0, 0, 0, 0, 0, HWND_MESSAGE,
            nullptr, GetModuleHandleA(0), nullptr);
        SetWindowLongPtrW(winId, GWLP_USERDATA, (LONG_PTR)this);
    }
    void registmessage(std::wstring message, messagecallback_v callback) {
        auto msg = RegisterWindowMessageW(message.c_str());
        messageproc.insert(msg, callback);
        messagemap.insert(message, msg);
    }
    void callmessage(std::wstring message) {
        auto msg = messagemap.get(message);
        if (msg.has_value())
            PostMessageW(winId, msg.value(), 0, 0);
    }
    static void runloop() {
        MSG msg;
        while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    static void endloop() {
        PostQuitMessage(0);
    }
};
static void SetWorkingDir() noexcept {
    std::wstring path = Win32Helper::GetExePath();

    FAIL_FAST_IF_FAILED(PathCchRemoveFileSpec(
        path.data(),
        path.size() + 1));

    FAIL_FAST_IF_WIN32_BOOL_FALSE(SetCurrentDirectory(path.c_str()));
}
static void InitializeLogger(const wchar_t* logFilePath) noexcept {
    Logger::Get().Initialize(
        spdlog::level::info,
        logFilePath,
        100000,
        2);
}
static void IncreaseTimerResolution() noexcept {
    PROCESS_POWER_THROTTLING_STATE powerThrottling{
        .Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION,
        .ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED |
                       PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION,
        .StateMask = 0 };
    SetProcessInformation(
        GetCurrentProcess(),
        ProcessPowerThrottling,
        &powerThrottling,
        sizeof(powerThrottling));
}
void notifyprepared(const std::wstring& eventname) {
    SECURITY_DESCRIPTOR sd = {};
    InitializeSecurityDescriptor(&sd, SECURITY_DESCRIPTOR_REVISION);
    SetSecurityDescriptorDacl(&sd, TRUE, NULL, FALSE);
    SECURITY_ATTRIBUTES allAccess = SECURITY_ATTRIBUTES{ sizeof(SECURITY_ATTRIBUTES), &sd, FALSE };
    SetEvent(CreateEvent(&allAccess, FALSE, FALSE, eventname.c_str()));
}
int WINAPI wWinMain(
    _In_ HINSTANCE hInstance,
    _In_opt_ HINSTANCE hPrevInstance,
    _In_ LPWSTR lpCmdLine,
    _In_ int nShowCmd) {
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

    auto Magpie_notify_prepared_ok = L"Magpie_notify_prepared_ok_" + std::to_wstring(GetCurrentProcessId());

    msgwindow _msgwindow(WNDCLS_Magpie_Core_CLI_Message);
    ScalingRuntime magrt;
    /*magrt.IsRunningChanged([&](bool running) {
        if (!running) {
            _msgwindow.callmessage(Magpie_Core_CLI_Message_Stop);
        }
    });*/
    // Sleep(100);
    auto magstart = [argv, &magrt](WPARAM wp, LPARAM lp, bool windowed = false) { 
		auto targethwnd = (HWND)lp;
		try
		{
			auto config = nlohmann::json::parse(std::ifstream(argv[0]));
			int profileindex = (int)wp;
			auto options = LoadMagOptions(config, profileindex);
			if (!options)return;
			if (windowed) {
				options.value().IsWindowedMode(true);
				options.value().Is3DGameMode(false);
			}
			SetForegroundWindow(targethwnd);
			magrt.Start(targethwnd, std::move(options.value()), true);  
		}
		catch(std::exception& e)
		{
			MessageBoxA(0, e.what(), "", 0);
		}
	};
    _msgwindow.registmessage(Magpie_Core_CLI_Message_Start, magstart);
    _msgwindow.registmessage(Magpie_Core_CLI_Message_Start_WindowedMode, [&](WPARAM wp, LPARAM lp) {magstart(wp, lp, true); });
    _msgwindow.registmessage(Magpie_Core_CLI_Message_Stop, [&magrt]() { magrt.Stop(); });
    _msgwindow.registmessage(Magpie_Core_CLI_Message_Exit, [&magrt]() { msgwindow::endloop(); });

    notifyprepared(Magpie_notify_prepared_ok);

    msgwindow::runloop();

    return 0;
}