#if _WIN32
#include "glfw include.h"
#include "console.h"
#include "renderer.h"
#include "platform_vsync.h"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#define ANY_SYNC_SUPPORTED 1
#define SYNC_IN_RENDER_THREAD 1
#define SCANLINE_VSYNC 1
#define SYNC_IN_SEPARATE_THREAD 0
//make sure at most one of SYNC_IN_SEPARATE_THREAD and SYNC_IN_RENDER_THREAD is true. not both!
//if you want to use the vblank waiting system (WaitForVerticalBlank) instead of the scanline system, then change: SYNC_IN_SEPARATE_THREAD 1, SYNC_IN_RENDER_THREAD 0, and in render.h, sync_mode = separate_heartbeat

//credits:
//EnumDisplayDevices code plagiarized from https://stackoverflow.com/questions/9524309/enumdisplaydevices-function-not-working-for-me
//Jerry Jongerius testing Windows DWM/D3D behavior at http://www.duckware.com/test/chrome/467617-source-code.zip
//Mark Rejhon describing where to find hAdapter for D3DKMTWaitForVerticalBlankEvent at https://github.com/libretro/RetroArch/issues/6984, saving me time digging through a MSDN maze

#include "d3dkmthk_fake.h"
void DumpDevice(const DISPLAY_DEVICE& dd, size_t nSpaceCount) {
	if (1) return; //don't output
	outc("Device Name:", dd.DeviceName);
	outc("Device String:", dd.DeviceString);
	outc("State Flags:", dd.StateFlags);
	outc("DeviceID:", dd.DeviceID);
	outc("DeviceKey: ...", dd.DeviceKey + 42);
}

D3DKMT_WAITFORVERTICALBLANKEVENT VBlankHandle;
D3DKMT_GETSCANLINE scanline_windows;
bool vertical_sync_acquire = []() {
	//https://docs.microsoft.com/en-us/windows/desktop/gdi/getting-information-on-a-display-monitor
	DISPLAY_DEVICE dd;
	dd.cb = sizeof(DISPLAY_DEVICE);

	DWORD deviceNum = 0;
	while (EnumDisplayDevices(NULL, deviceNum, &dd, 0)) {
		DumpDevice(dd, 0);

		if (dd.StateFlags & DISPLAY_DEVICE_PRIMARY_DEVICE)
			break;
		//DISPLAY_DEVICE newdd = {0};
		//newdd.cb = sizeof(DISPLAY_DEVICE);
		//DWORD monitorNum = 0;
		//while (EnumDisplayDevices(dd.DeviceName, monitorNum, &newdd, 0)) {
		//	DumpDevice(newdd, 4);
		//	monitorNum++;
		//}
		deviceNum++;
	}

	HDC hdc = CreateDC(NULL, dd.DeviceName, NULL, NULL);
	if (hdc == NULL) {
		error("CreateDC failed");
	}

	D3DKMT_OPENADAPTERFROMHDC OpenAdapterData;

	OpenAdapterData.hDc = hdc;
	if (STATUS_SUCCESS == D3DKMTOpenAdapterFromHdc(&OpenAdapterData)) {
		DeleteDC(hdc);
	}
	else {
		DeleteDC(hdc);
		error("failed to open adapter from hDc");
	}
	if (render::sync_mode == render::separate_heartbeat) {
		VBlankHandle.hAdapter = OpenAdapterData.hAdapter;
		VBlankHandle.hDevice = 0; //optional. maybe OpenDeviceHandle will give it to us, https://docs.microsoft.com/en-us/windows/desktop/api/dxva2api/nf-dxva2api-idirect3ddevicemanager9-opendevicehandle
		VBlankHandle.VidPnSourceId = OpenAdapterData.VidPnSourceId;
	}
	else if (render::sync_mode == render::sync_in_render_thread) {
		scanline_windows.hAdapter = OpenAdapterData.hAdapter;
		scanline_windows.VidPnSourceId = OpenAdapterData.VidPnSourceId;
	}
	return 0;
}();

//https://stackoverflow.com/questions/1387064/how-to-get-the-error-message-from-the-error-code-returned-by-getlasterror
//std::string GetLastErrorAsString() {
//	//Get the error message ID, if any.
//	DWORD errorMessageID = ::GetLastError();
//	if (errorMessageID == 0) {
//		return std::string(); //No error message has been recorded
//	}

//	LPSTR messageBuffer = nullptr;

//	//Ask Win32 to give us the string version of that message ID.
//	//The parameters we pass in, tell Win32 to create the buffer that holds the message for us (because we don't yet know how long the message string will be).
//	size_t size = FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
//		NULL, errorMessageID, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPSTR)&messageBuffer, 0, NULL);

//	//Copy the error message into a std::string.
//	std::string message(messageBuffer, size);

//	//Free the Win32's string's buffer.
//	LocalFree(messageBuffer);

//	return message;
//}

//#include <ddraw.h>

//this is nonsense. it's a documentation bug in MSDN.
//auto WaitForVerticalBlank = []() {
//	using func_ptr_type = HRESULT (*)(DWORD unnamedParam1, HANDLE unnamedParam2);
//	HINSTANCE hDLL = LoadLibraryA("ddraw.dll");
//	check(hDLL != NULL, "failed to load ddraw");
//	outc(hDLL);
//	func_ptr_type returned_function_pointer = (func_ptr_type)GetProcAddress(hDLL, "WaitForVerticalBlank");
//	if (!returned_function_pointer) {
//		FreeLibrary(hDLL);
//		outc(GetLastErrorAsString());
//		error("couldn't load vblank handle");
//	}
//	return returned_function_pointer;
//}();

//#pragma comment(lib, "dxguid.lib") //not supported by gcc (mingw)
//#pragma comment(lib, "ddraw.lib")
//https://stackoverflow.com/questions/9934824/directdraw-get-procedure-address-using-getprocaddress
//LPVOID lplpDD;
//bool create_ddraw_vblank = []() {
//	DirectDrawCreateEx((LPGUID)DDCREATE_HARDWAREONLY, &lplpDD, IID_IDirectDraw7, NULL);
//	return 0;
//}();

bool wait_for_vblank() {
	return (D3DKMTWaitForVerticalBlankEvent(&VBlankHandle) != STATUS_SUCCESS);
	//auto result = ((IDirectDraw7*)lplpDD)->WaitForVerticalBlank(DDWAITVB_BLOCKBEGIN, NULL); //this completely fails. it returns two timepoints in a row, almost exactly equal. I don't know why. I assume it spinwaits and checks for a region
	//auto result = ((IDirectDraw7*)lplpDD)->WaitForVerticalBlank(DDWAITVB_BLOCKEND, NULL); //this spinwaits. which makes it take up a full core. that's unacceptable. its precision is amazing though, 20x as good as D3D. 0.002 ms, thanks to spinwaiting
	//if (result != DD_OK) outc("vblank error", result);
	//return result != DD_OK;
}

int first_scanline_in_display = -1;
int last_scanline_before_display = 0; //first line must always be vsync, according to ToastyX.
//future: Mark Rejhon reports that D3DKMTGetScanLine takes 8 scanlines on his 1080 Ti; maybe his code has a problem? he suspects maybe it's just slow on Nvidia cards
//future: Mark Rejhon reports that the scanline counter doesn't increment properly in the porch (when InVerticalBlank is true). I should investigate that when I get a new graphics card. it works fine on my Intel HD 4000
uint64_t get_scanline() {
	auto result = D3DKMTGetScanLine(&scanline_windows); //runtime is 0.005-0.015 ms on my Intel HD 4000. 1000 calls in a loop takes 2-5 ms.
	//standard deviation is 0.004 ms. Quantization error of the scanline is (16.666/1125/sqrt12 = 0.00427 ms). that means D3DKMTGetScanLine() is perfectly accurate up to its theoretical limit.
	check_assert(result == STATUS_SUCCESS, "scanline error", result); //happens if you're doing double buffer vsync

	//outc(scanline_windows.ScanLine, scanline_windows.InVerticalBlank);
	return scanline_windows.ScanLine;
}

//use InVerticalBlank to determine the boundary between the vblank and display.
//separate this from get_scanline - there's a timer after the scanline call, and this should be after the timer.
void update_scanline_boundaries() {
	check_assert(first_scanline_in_display != last_scanline_before_display, "overlapping scanlines in vblank, should be impossible");
	if (first_scanline_in_display == last_scanline_before_display + 1) //nothing more to be done, it's as good as it gets. this happens very quickly so it's worth bailing out.
		return;
	int scanline = scanline_windows.ScanLine; //signed
	if (scanline_windows.InVerticalBlank) {
		if (scanline > last_scanline_before_display && scanline < first_scanline_in_display) {
			last_scanline_before_display = scanline;
			scanlines_between_sync_and_first_displayed_line = last_scanline_before_display + 1;
		}
	}
	else {
		if (first_scanline_in_display == -1 || scanline < first_scanline_in_display)
			first_scanline_in_display = scanline;
	}
}

//uses QDC
void get_scanline_info() {
	std::vector<DISPLAYCONFIG_PATH_INFO> paths;
	std::vector<DISPLAYCONFIG_MODE_INFO> modes;
	UINT32 flags = QDC_ONLY_ACTIVE_PATHS; //QDC_VIRTUAL_MODE_AWARE not supported on my system
	LONG result = ERROR_SUCCESS;

	do {
		// Determine how many path and mode structures to allocate
		UINT32 pathCount, modeCount;
		result = GetDisplayConfigBufferSizes(flags, &pathCount, &modeCount);

		if (result != ERROR_SUCCESS) {
			error("couldn't get display config buffer sizes", result, ERROR_INVALID_PARAMETER, ERROR_NOT_SUPPORTED, ERROR_ACCESS_DENIED, ERROR_GEN_FAILURE);
		}

		// Allocate the path and mode arrays
		paths.resize(pathCount);
		modes.resize(modeCount);

		// Get all active paths and their modes
		result = QueryDisplayConfig(flags, &pathCount, paths.data(), &modeCount, modes.data(), nullptr);
		//here, flags will contain DISPLAYCONFIG_PATH_BOOST_REFRESH_RATE "to indicate that the path supports virtual refresh rates. Supported starting in Windows 11." but that's totally different from normal VRR (variable refresh rate, FreeSync)
		//DISPLAYCONFIG_PATH_TARGET_INFO contains refreshRate
		//"If the caller specifies target mode information, the operating system will instead use the refresh rate that is stored in the vSyncFreq member of the DISPLAYCONFIG_VIDEO_SIGNAL_INFO structure. In this case, the caller specifies this value in the targetVideoSignalInfo member of the DISPLAYCONFIG_TARGET_MODE structure. A refresh rate with both the numerator and denominator set to zero indicates that the caller does not specify a refresh rate and the operating system should use the most optimal refresh rate available."

		// The function may have returned fewer paths/modes than estimated
		paths.resize(pathCount);
		modes.resize(modeCount);

		// It's possible that between the call to GetDisplayConfigBufferSizes and QueryDisplayConfig
		// that the display state changed, so loop on the case of ERROR_INSUFFICIENT_BUFFER.
	} while (result == ERROR_INSUFFICIENT_BUFFER);

	if (result != ERROR_SUCCESS) {
		error("couldn't run QueryDisplayConfig", result);
	}

	//for (auto& path : paths) {
	//	// Find the target (monitor) friendly name
	//	DISPLAYCONFIG_TARGET_DEVICE_NAME targetName = {};
	//	targetName.header.adapterId = path.targetInfo.adapterId;
	//	targetName.header.id = path.targetInfo.id;
	//	targetName.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME;
	//	targetName.header.size = sizeof(targetName);
	//	result = DisplayConfigGetDeviceInfo(&targetName.header);

	//	if (result != ERROR_SUCCESS) {
	//		error("couldn't run DisplayConfigGetDeviceInfo", result);
	//	}

	//	// Find the adapter device name
	//	DISPLAYCONFIG_ADAPTER_NAME adapterName = {};
	//	adapterName.header.adapterId = path.targetInfo.adapterId;
	//	adapterName.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_ADAPTER_NAME;
	//	adapterName.header.size = sizeof(adapterName);

	//	result = DisplayConfigGetDeviceInfo(&adapterName.header);

	//	if (result != ERROR_SUCCESS) {
	//		error("couldn't run DisplayConfigGetDeviceInfo second", result);
	//	}

	//	outc("Monitor with name", targetName.flags.friendlyNameFromEdid ? targetName.monitorFriendlyDeviceName : L"Unknown", "is connected to adapter", adapterName.adapterDevicePath, "on target", path.targetInfo.id);
	//}
	for (auto mode : modes) {
		auto first_total_scanlines = mode.targetMode.targetVideoSignalInfo.totalSize.cy;
		if (first_total_scanlines == 0)
			continue; //this happens sometimes. I don't know why
		total_scanlines = first_total_scanlines;
		active_scanlines = mode.targetMode.targetVideoSignalInfo.activeSize.cy;
		porch_scanlines = total_scanlines - active_scanlines;
		//outc("porch active total", porch_scanlines, active_scanlines, total_scanlines);
	}
}

//#include <dwmapi.h>
//-ldwmapi

//void get_some_system_data_from_DWM(){
//	DWM_TIMING_INFO pTimingInfo;
//    pTimingInfo.cbSize = sizeof(pTimingInfo);
//	check(DwmGetCompositionTimingInfo(NULL, &pTimingInfo) == S_OK, "couldn't get DWM timing information. only available in windowed mode, not fullscreen");
//	outc(pTimingInfo.qpcRefreshPeriod, pTimingInfo.cRefresh, pTimingInfo.cDXRefresh, pTimingInfo.qpcCompose);
//	//note that occasionally, a frame is reported twice. that is, all information is given again. even though the function is run after each swap.
//	//this is easy to tell if you check cFrame - it gives the same information instead of increasing by 1.
//	//qpcRefreshPeriod: wobbles a bit, but quite stable. tick count of how long a frame takes. might be accurate and testing the actual refresh period
//	//rateRefresh: 60 on my machine. float(pTimingInfo.rateRefresh.uiNumerator) / pTimingInfo.rateRefresh.uiDenominator
//	//rateCompose: 60 on my machine
//	//cRefresh: num, increases by 1 each frame
//	//cFrame, cRefreshFrame: num+1. increases by 1 each frame.
//	//cDXRefresh: some large num, increases by 1 each frame
//	//qpcCompose: increases by about 1 frame's worth of ticks. pretty wobbly. the amount increased is different from qpcRefreshPeriod
//	//qpcFrameDisplayed: always zero
//	//qpcFrameComplete: always zero
//}

//https://stackoverflow.com/questions/4110731/how-to-get-the-screen-refresh-rate
//this will stay an int. on other systems, if floating point frequencies are returned, we'll use floats for those platforms.
//glfwGetVideoMode uses EnumDisplaySettingsW on Windows. see src/win32_monitor.c. so might as well unify the platforms and use glfw.
//uint get_refresh_rate() {
//	DEVMODEA lpDevMode;
//	EnumDisplaySettingsA(NULL, ENUM_CURRENT_SETTINGS, &lpDevMode);
//	//outc("display frequency", lpDevMode.dmDisplayFrequency); //returns 60 on my system
//	return lpDevMode.dmDisplayFrequency;
//}

//Mark Rejhon: "Use Linux Modeline or Windows QueryDisplayConfig() to get the exact horizontal scan rate AND the Vertical Total and subtract vertical resolution from it to get the size of VBI in number of scanlines."
//DISPLAYCONFIG_DESKTOP_IMAGE_INFO seems to specify the rectangle. but Windows 10 only

//IDXGIOutput::WaitForVBlank exists. no idea if it's better than D3DK but since scanlines are better than both, I stopped caring.
#endif