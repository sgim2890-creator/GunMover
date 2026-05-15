#include "EditorUI.h"
#include <d3d11.h>
#include <imgui/imgui.h>
#include <imgui/imgui_impl_dx11.h>
#include <imgui/imgui_impl_win32.h>
#include <windowsx.h>
#include <wrl.h>
#include <Configs.h>
#include <fstream>
#include <nlohmann/json.hpp>
#include <Hooks.h>
#include <Utils.h>
#include <Globals.h>

EditorUI::Hotkey editorHotkey;
EditorUI::Hotkey altPosHotkey;
std::string iniSection = "GMHotkey";

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND a_hWnd, UINT a_msg, WPARAM a_wParam, LPARAM a_lParam);

extern float easePercentage, currentTime, duration;
extern std::string clipName, animationInfo;

namespace EditorUI
{
	bool Window::imguiInitialized = false;
	Window* Window::instance = nullptr;

	REL::Relocation<uintptr_t> ptr_D3D11CreateDeviceAndSwapChainCall{ REL::ID(224250), 0x419 };
	REL::Relocation<uintptr_t> ptr_D3D11CreateDeviceAndSwapChain{ REL::ID(254484) };
	typedef HRESULT (*FnD3D11CreateDeviceAndSwapChain)(IDXGIAdapter*,
		D3D_DRIVER_TYPE,
		HMODULE, UINT,
		const D3D_FEATURE_LEVEL*,
		UINT,
		UINT,
		const DXGI_SWAP_CHAIN_DESC*,
		IDXGISwapChain**,
		ID3D11Device**,
		D3D_FEATURE_LEVEL*,
		ID3D11DeviceContext**);
	FnD3D11CreateDeviceAndSwapChain D3D11CreateDeviceAndSwapChain_Orig;
	typedef HRESULT (*FnD3D11Present)(IDXGISwapChain*, UINT, UINT);
	FnD3D11Present D3D11Present_Orig;
	REL::Relocation<uintptr_t> ptr_ClipCursor{ REL::ID(641385) };
	typedef BOOL (*FnClipCursor)(const RECT*);
	FnClipCursor ClipCursor_Orig;
	WNDPROC WndProc_Orig;

	static float tempX = 0.f;
	static float tempY = 0.f;
	static float tempZ = 0.f;
	static float tempRotX = 0.f;
	static float tempRotY = 0.f;
	static float tempRotZ = 0.f;
	static bool tempRevertOnReload = false;
	static bool tempRevertOnMelee = false;
	static bool tempRevertOnThrow = false;
	static bool tempRevertOnSprint = false;
	static bool tempRevertOnEquip = false;
	static bool tempRevertOnFastEquip = false;
	static bool tempRevertOnUnequip = false;
	static bool tempRevertOnGunDown = false;
	static bool tempPersistent = false;  // 추가: Persistent 모드 임시 변수

	void SyncValues()
	{
		if (Configs::adjustment) {
			tempX = Configs::adjustment->translation.x;
			tempY = Configs::adjustment->translation.y;
			tempZ = Configs::adjustment->translation.z;
			tempRotX = Configs::adjustment->rotation.x / Configs::toRad;
			tempRotY = Configs::adjustment->rotation.y / Configs::toRad;
			tempRotZ = Configs::adjustment->rotation.z / Configs::toRad;
			tempRevertOnReload = Configs::adjustment->GetAdjustmentFlag(Configs::REVERT_FLAG::kRevertOnReload);
			tempRevertOnMelee = Configs::adjustment->GetAdjustmentFlag(Configs::REVERT_FLAG::kRevertOnMelee);
			tempRevertOnThrow = Configs::adjustment->GetAdjustmentFlag(Configs::REVERT_FLAG::kRevertOnThrow);
			tempRevertOnSprint = Configs::adjustment->GetAdjustmentFlag(Configs::REVERT_FLAG::kRevertOnSprint);
			tempRevertOnEquip = Configs::adjustment->GetAdjustmentFlag(Configs::REVERT_FLAG::kRevertOnEquip);
			tempRevertOnFastEquip = Configs::adjustment->GetAdjustmentFlag(Configs::REVERT_FLAG::kRevertOnFastEquip);
			tempRevertOnUnequip = Configs::adjustment->GetAdjustmentFlag(Configs::REVERT_FLAG::kRevertOnUnequip);
			tempRevertOnGunDown = Configs::adjustment->GetAdjustmentFlag(Configs::REVERT_FLAG::kRevertOnGunDown);
			tempPersistent = Configs::adjustment->GetAdjustmentFlag(Configs::REVERT_FLAG::kPersistent);  // 추가
		}
	}

	RECT windowRect;
	ImGuiIO imguiIO;
	Microsoft::WRL::ComPtr<IDXGISwapChain> d3d11SwapChain;
	Microsoft::WRL::ComPtr<ID3D11Device> d3d11Device;
	Microsoft::WRL::ComPtr<ID3D11DeviceContext> d3d11Context;
	HWND window;

	BOOL __stdcall HookedClipCursor(const RECT* lpRect)
	{
		if (Window::GetSingleton() && Window::GetSingleton()->GetShouldDraw())
			lpRect = &windowRect;
		return ClipCursor_Orig(lpRect);
	}

	LRESULT __stdcall WndProcHandler(HWND a_hWnd, UINT a_msg, WPARAM a_wParam, LPARAM a_lParam)
	{
		switch (a_msg) {
		case WM_KEYDOWN:
			if (editorHotkey.captureState == 0 && altPosHotkey.captureState == 0) {
				bool isPressed = (a_lParam & 0x40000000) == 0x0;
				if (isPressed) {
					bool ctrlPressed = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
					bool shiftPressed = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
					bool altPressed = (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;

					if (a_wParam == editorHotkey.mainKey &&
						ctrlPressed == editorHotkey.ctrl &&
						shiftPressed == editorHotkey.shift &&
						altPressed == editorHotkey.alt) {
						if (auto singleton = Window::GetSingleton()) {
							singleton->ToggleEditorUI();
						}
					} else if (a_wParam == altPosHotkey.mainKey &&
							   ctrlPressed == altPosHotkey.ctrl &&
							   shiftPressed == altPosHotkey.shift &&
							   altPressed == altPosHotkey.alt &&
							   Configs::adjustment) {
						Configs::adjustment->CycleAlternatives();
					}
				}
			}
			break;
		}

		if (Window::GetSingleton() && Window::GetSingleton()->GetShouldDraw()) {
			ImGui_ImplWin32_WndProcHandler(a_hWnd, a_msg, a_wParam, a_lParam);
			return true;
		}

		return CallWindowProc(WndProc_Orig, a_hWnd, a_msg, a_wParam, a_lParam);
	}

	HRESULT __stdcall HookedPresent(IDXGISwapChain* a_swapChain, UINT a_syncInterval, UINT a_flags)
	{
		if (Window::GetSingleton()) {
			if (!Window::imguiInitialized) {
				Window::ImGuiInit();
			}

			ImGui_ImplDX11_NewFrame();
			ImGui_ImplWin32_NewFrame();

			ImGui::NewFrame();

			Window::GetSingleton()->Draw();

			ImGui::Render();
			ImGui::EndFrame();

			auto* drawData = ImGui::GetDrawData();
			if (drawData) {
				ImGui_ImplDX11_RenderDrawData(drawData);
			}
		}

		return D3D11Present_Orig(a_swapChain, a_syncInterval, a_flags);
	}

	HRESULT __stdcall HookedD3D11CreateDeviceAndSwapChain(IDXGIAdapter* a_pAdapter,
		D3D_DRIVER_TYPE a_driverType,
		HMODULE a_software,
		UINT a_flags,
		const D3D_FEATURE_LEVEL* a_pFeatureLevels,
		UINT a_featureLevels,
		UINT a_sdkVersion,
		const DXGI_SWAP_CHAIN_DESC* a_pSwapChainDesc,
		IDXGISwapChain** a_ppSwapChain,
		ID3D11Device** a_ppDevice,
		D3D_FEATURE_LEVEL* a_pFeatureLevel,
		ID3D11DeviceContext** a_ppImmediateContext)

	{
		HRESULT res = D3D11CreateDeviceAndSwapChain_Orig(a_pAdapter, a_driverType, a_software, a_flags, a_pFeatureLevels, a_featureLevels, a_sdkVersion, a_pSwapChainDesc, a_ppSwapChain, a_ppDevice, a_pFeatureLevel, a_ppImmediateContext);

		if (res == S_OK) {
			REL::Relocation<uintptr_t> swapChain_vtbl(*(std::uintptr_t*)(*a_ppSwapChain));
			D3D11Present_Orig = (FnD3D11Present)swapChain_vtbl.write_vfunc(8, &HookedPresent);
			logger::warn("D3D11 Device created. SwapChain vtbl {}", swapChain_vtbl.address());

			window = ::GetActiveWindow();

			::GetWindowRect(window, &windowRect);

			d3d11SwapChain = *a_ppSwapChain;
			d3d11Device = *a_ppDevice;
			d3d11Context = *a_ppImmediateContext;
		}

		return res;
	}

	void HookD3D11()
	{
		logger::warn("Hooking D3D11CreateDeviceAndSwapChain");
		F4SE::Trampoline& trampoline = F4SE::GetTrampoline();
		D3D11CreateDeviceAndSwapChain_Orig = (FnD3D11CreateDeviceAndSwapChain)trampoline.write_call<5>(ptr_D3D11CreateDeviceAndSwapChainCall.address(), &HookedD3D11CreateDeviceAndSwapChain);

		ClipCursor_Orig = *(FnClipCursor*)ptr_ClipCursor.address();
		ptr_ClipCursor.write_vfunc(0, &HookedClipCursor);
		logger::warn("CreateDevice {:p} ClipCursor {:p}", fmt::ptr(D3D11CreateDeviceAndSwapChain_Orig), fmt::ptr(ClipCursor_Orig));
	}

	void Hotkey::SaveHotkeyToIni()
	{
		ImGuiIO& io = ImGui::GetIO();
		if (!io.IniFilename)
			return;

		std::ifstream fileIn(io.IniFilename);
		std::string fileContent, line;
		bool sectionFound = false;

		if (fileIn.is_open()) {
			while (std::getline(fileIn, line)) {
				if (line == "[" + iniSection + "]") {
					sectionFound = true;
					fileContent += "[" + iniSection + "]\n";
					fileContent += "Hotkey=" + editorHotkey.ToString() + "\n";
					fileContent += "AltPosHotkey=" + altPosHotkey.ToString() + "\n";
					while (std::getline(fileIn, line) && !line.empty() && line[0] != '[') {
					}
					fileContent += line + "\n";
				} else {
					fileContent += line + "\n";
				}
			}
			fileIn.close();
		}

		if (!sectionFound) {
			fileContent += "[" + iniSection](streamdown:incomplete-link)
