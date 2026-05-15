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
			fileContent += "[" + iniSection + "]\n";
			fileContent += "Hotkey=" + editorHotkey.ToString() + "\n";
			fileContent += "AltPosHotkey=" + altPosHotkey.ToString() + "\n";
		}

		std::ofstream fileOut(io.IniFilename);
		if (fileOut.is_open()) {
			fileOut << fileContent;
			fileOut.close();
		}
	}

	void Hotkey::LoadHotkeyFromIni()
	{
		ImGuiIO& io = ImGui::GetIO();
		if (!io.IniFilename)
			return;

		std::ifstream file(io.IniFilename);
		bool sectionFound = false;
		if (file.is_open()) {
			std::string line;

			while (std::getline(file, line)) {
				if (line == "[" + iniSection + "]") {
					sectionFound = true;
				} else if (sectionFound && line.starts_with("Hotkey=")) {
					editorHotkey = Hotkey::FromString(line.substr(7));
					logger::info("Editor Hotkey: {}", line.substr(7).c_str());
				} else if (sectionFound && line.starts_with("AltPosHotkey=")) {
					altPosHotkey = Hotkey::FromString(line.substr(13));
					logger::info("Alt Pos Hotkey: {}", line.substr(13).c_str());
				}
			}
			file.close();
		}

		if (!sectionFound) {
			editorHotkey.mainKey = 0xDC;
			editorHotkey.ctrl = false;
			editorHotkey.shift = false;
			editorHotkey.alt = false;
			altPosHotkey.mainKey = 0xBF;
			altPosHotkey.ctrl = false;
			altPosHotkey.shift = false;
			altPosHotkey.alt = false;
		}
	}

	void Hotkey::CaptureHotkey(Hotkey& hotkey)
	{
		if (hotkey.captureState == 1) {
			if (GetAsyncKeyState(VK_CONTROL) & 0x8000)
				hotkey.ctrl = true;
			else
				hotkey.ctrl = false;
			if (GetAsyncKeyState(VK_SHIFT) & 0x8000)
				hotkey.shift = true;
			else
				hotkey.shift = false;
			if (GetAsyncKeyState(VK_MENU) & 0x8000)
				hotkey.alt = true;
			else
				hotkey.alt = false;

			for (int key = 0x20; key <= 0xDF; ++key) {
				if (key < 0xA0 || key > 0xA5) {
					if (GetAsyncKeyState(key) & 0x8000) {
						hotkey.mainKey = key;
						SaveHotkeyToIni();
						hotkey.captureState = 2;
						break;
					}
				}
			}
		} else {
			if (!(GetAsyncKeyState(hotkey.mainKey) & 0x8000)) {
				hotkey.captureState = 0;
			}
		}
	}

	void Window::HelpTooltip(const char* a_desc)
	{
		ImGui::TextDisabled("(?)");
		if (ImGui::BeginItemTooltip()) {
			ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
			ImGui::TextUnformatted(a_desc);
			ImGui::PopTextWrapPos();
			ImGui::EndTooltip();
		}
	}

	void Window::ImGuiInit()
	{
		IMGUI_CHECKVERSION();

		DXGI_SWAP_CHAIN_DESC sd;
		d3d11SwapChain->GetDesc(&sd);

		WndProc_Orig = (WNDPROC)SetWindowLongPtr(sd.OutputWindow, GWLP_WNDPROC, (LONG_PTR)WndProcHandler);

		ImGui::CreateContext();
		imguiIO = ImGui::GetIO();

		ImGui::StyleColorsDark();

		bool imguiWin32Init = ImGui_ImplWin32_Init(window);
		bool imguiDX11Init = ImGui_ImplDX11_Init(d3d11Device.Get(), d3d11Context.Get());

		if (imguiWin32Init && imguiDX11Init) {
			logger::warn("ImGui Init Success");

			Hotkey::LoadHotkeyFromIni();
		}

		Window::imguiInitialized = true;
	}

	void Window::ToggleEditorUI()
	{
		bool wantDraw = !this->shouldDraw;
		::ShowCursor(wantDraw);
		if (wantDraw) {
			Hooks::shouldAdjust = true;
			if (!Configs::adjustment) {
				Configs::adjustDataMap.at(0xFFFFFFFF).at(0xFFFFFFFF) = Configs::AdjustmentData();
				Configs::adjustment = &Configs::adjustDataMap.at(0xFFFFFFFF).at(0xFFFFFFFF);
			}
			SyncValues();
		}
		this->shouldDraw = wantDraw;
	}

	void CopyToClipboard(const std::string& text)
	{
		OpenClipboard(0);
		EmptyClipboard();
		HGLOBAL hg = GlobalAlloc(GMEM_MOVEABLE, text.size() + 1);
		if (!hg) {
			CloseClipboard();
			return;
		}
		memcpy(GlobalLock(hg), text.c_str(), text.size() + 1);
		GlobalUnlock(hg);
		SetClipboardData(CF_TEXT, hg);
		CloseClipboard();
		GlobalFree(hg);
	}

	std::string GenerateJsonSnippet()
	{
		nlohmann::json jsonSnippet;

		if (Configs::adjustment->translation.x != 0)
			jsonSnippet["X"] = Configs::adjustment->translation.x;
		if (Configs::adjustment->translation.y != 0)
			jsonSnippet["Y"] = Configs::adjustment->translation.y;
		if (Configs::adjustment->translation.z != 0)
			jsonSnippet["Z"] = Configs::adjustment->translation.z;

		if (Configs::adjustment->rotation.x != 0)
			jsonSnippet["rotX"] = Configs::adjustment->rotation.x / Configs::toRad;
		if (Configs::adjustment->rotation.y != 0)
			jsonSnippet["rotY"] = Configs::adjustment->rotation.y / Configs::toRad;
		if (Configs::adjustment->rotation.z != 0)
			jsonSnippet["rotZ"] = Configs::adjustment->rotation.z / Configs::toRad;

		if (Configs::adjustment->GetAdjustmentFlag(Configs::REVERT_FLAG::kRevertOnReload))
			jsonSnippet["RevertOnReload"] = Configs::adjustment->GetAdjustmentFlag(Configs::REVERT_FLAG::kRevertOnReload);
		if (Configs::adjustment->GetAdjustmentFlag(Configs::REVERT_FLAG::kRevertOnMelee))
			jsonSnippet["RevertOnMelee"] = Configs::adjustment->GetAdjustmentFlag(Configs::REVERT_FLAG::kRevertOnMelee);
		if (Configs::adjustment->GetAdjustmentFlag(Configs::REVERT_FLAG::kRevertOnThrow))
			jsonSnippet["RevertOnThrow"] = Configs::adjustment->GetAdjustmentFlag(Configs::REVERT_FLAG::kRevertOnThrow);
		if (Configs::adjustment->GetAdjustmentFlag(Configs::REVERT_FLAG::kRevertOnSprint))
			jsonSnippet["RevertOnSprint"] = Configs::adjustment->GetAdjustmentFlag(Configs::REVERT_FLAG::kRevertOnSprint);
		if (Configs::adjustment->GetAdjustmentFlag(Configs::REVERT_FLAG::kRevertOnEquip))
			jsonSnippet["RevertOnEquip"] = Configs::adjustment->GetAdjustmentFlag(Configs::REVERT_FLAG::kRevertOnEquip);
		if (Configs::adjustment->GetAdjustmentFlag(Configs::REVERT_FLAG::kRevertOnFastEquip))
			jsonSnippet["RevertOnFastEquip"] = Configs::adjustment->GetAdjustmentFlag(Configs::REVERT_FLAG::kRevertOnFastEquip);
		if (Configs::adjustment->GetAdjustmentFlag(Configs::REVERT_FLAG::kRevertOnUnequip))
			jsonSnippet["RevertOnUnequip"] = Configs::adjustment->GetAdjustmentFlag(Configs::REVERT_FLAG::kRevertOnUnequip);
		if (Configs::adjustment->GetAdjustmentFlag(Configs::REVERT_FLAG::kRevertOnGunDown))
			jsonSnippet["RevertOnGunDown"] = Configs::adjustment->GetAdjustmentFlag(Configs::REVERT_FLAG::kRevertOnGunDown);
		if (Configs::adjustment->GetAdjustmentFlag(Configs::REVERT_FLAG::kPersistent))
			jsonSnippet["Persistent"] = Configs::adjustment->GetAdjustmentFlag(Configs::REVERT_FLAG::kPersistent);

		std::string jsonString = jsonSnippet.dump(6);

		if (jsonString.front() == '{' && jsonString.back() == '}') {
			jsonString.erase(0, 1);
			jsonString.erase(jsonString.size() - 1);
		}

		jsonString.erase(0, jsonString.find_first_not_of("\n\r\t"));
		jsonString.erase(jsonString.find_last_not_of("\n\r\t ") + 1);

		return jsonString;
	}

	void Window::Draw()
	{
		if (!this->shouldDraw) {
			return;
		}

		ImVec2 center = ImGui::GetMainViewport()->GetCenter();
		ImVec2 size = ImGui::GetMainViewport()->Size;

		ImGui::SetNextWindowSize(ImVec2(400, 850), ImGuiCond_FirstUseEver);

		ImGuiWindowFlags window_flags = 0;
		window_flags |= ImGuiWindowFlags_MenuBar;
		ImGui::Begin("Quick Adjustment Editor", NULL, window_flags);
		static const ImVec2 sz = ImVec2(-FLT_MIN, 0.0f);

		ImGui::TextColored(ImVec4(0.f, 0.95f, 0.f, 1.f), "THIS IS ONLY FOR PREVIEW!\nEnter those values into json files.");

		ImGui::SeparatorText("Hotkeys");

		std::string hotkeyStr = editorHotkey.ToReadableString();

		ImGui::Text("Editor Hotkey: %s", hotkeyStr.c_str());

		if (editorHotkey.captureState == 0) {
			if (ImGui::Button("Set Editor Hotkey")) {
				editorHotkey.captureState = 1;
			}
		} else {
			ImGui::Text("Press any key combination to set as the hotkey...");
			Hotkey::CaptureHotkey(editorHotkey);
		}

		std::string altPosHotkeyStr = altPosHotkey.ToReadableString();

		ImGui::Text("Alt Pos Hotkey: %s", altPosHotkeyStr.c_str());

		if (altPosHotkey.captureState == 0) {
			if (ImGui::Button("Set Alt Pos Hotkey")) {
				altPosHotkey.captureState = 1;
			}
		} else {
			ImGui::Text("Press any key combination to set as the hotkey...");
			Hotkey::CaptureHotkey(altPosHotkey);
		}

		ImGui::SeparatorText("Adjustments");

		if (ImGui::Button("Reload JSON files", sz)) {
			Configs::LoadConfigs();

			RE::BGSObjectInstance* objInstance = nullptr;
			if (Globals::p && Globals::p->currentProcess) {
				Globals::p->currentProcess->middleHigh->equippedItemsLock.lock();
				RE::BSTArray<RE::EquippedItem>& equipped = Globals::p->currentProcess->middleHigh->equippedItems;
				if (equipped.size() != 0 && equipped[0].data.get()) {
					objInstance = &equipped[0].item;
				}
				Globals::p->currentProcess->middleHigh->equippedItemsLock.unlock();
				if (objInstance) {
					RE::BGSObjectInstance newObjInstance { objInstance->object, nullptr };
					RE::ActorEquipManager::GetSingleton()->UnequipObject(Globals::p, objInstance, 1, 0, 0xFFFFFFFF, false, false, false, true, 0);
					RE::ActorEquipManager::GetSingleton()->EquipObject(Globals::p, newObjInstance, 0, 1, 0, true, false, false, true, false);
				}
			}

			SyncValues();
			Hooks::shouldAdjust = true;
		}

		const static float minTrans = -200.f;
		const static float maxTrans = 200.f;
		if (ImGui::DragFloat("X", &tempX, 0.01f, minTrans, maxTrans, "%.2f")) {
			Configs::adjustment->translation.x = tempX;
		}

		if (ImGui::DragFloat("Y", &tempY, 0.01f, minTrans, maxTrans, "%.2f")) {
			Configs::adjustment->translation.y = tempY;
		}

		if (ImGui::DragFloat("Z", &tempZ, 0.01f, minTrans, maxTrans, "%.2f")) {
			Configs::adjustment->translation.z = tempZ;
		}

		if (ImGui::DragFloat("rotX", &tempRotX, 0.01f, -180.f, 180.f, "%.2f")) {
			Configs::adjustment->rotation.x = tempRotX * Configs::toRad;
		}

		if (ImGui::DragFloat("rotY", &tempRotY, 0.01f, -180.f, 180.f, "%.2f")) {
			Configs::adjustment->rotation.y = tempRotY * Configs::toRad;
		}

		if (ImGui::DragFloat("rotZ", &tempRotZ, 0.01f, -180.f, 180.f, "%.2f")) {
			Configs::adjustment->rotation.z = tempRotZ * Configs::toRad;
		}

		if (ImGui::BeginCombo("Revert Options", "Click here")) {
			if (ImGui::Checkbox("Revert on reload", &tempRevertOnReload)) {
				Configs::adjustment->SetAdjustmentFlag(Configs::REVERT_FLAG::kRevertOnReload, tempRevertOnReload);
			}

			if (ImGui::Checkbox("Revert on melee", &tempRevertOnMelee)) {
				Configs::adjustment->SetAdjustmentFlag(Configs::REVERT_FLAG::kRevertOnMelee, tempRevertOnMelee);
			}

			if (ImGui::Checkbox("Revert on throw", &tempRevertOnThrow)) {
				Configs::adjustment->SetAdjustmentFlag(Configs::REVERT_FLAG::kRevertOnThrow, tempRevertOnThrow);
			}

			if (ImGui::Checkbox("Revert on sprint", &tempRevertOnSprint)) {
				Configs::adjustment->SetAdjustmentFlag(Configs::REVERT_FLAG::kRevertOnSprint, tempRevertOnSprint);
			}

			if (ImGui::Checkbox("Revert on equip", &tempRevertOnEquip)) {
				Configs::adjustment->SetAdjustmentFlag(Configs::REVERT_FLAG::kRevertOnEquip, tempRevertOnEquip);
			}

			if (ImGui::Checkbox("Revert on fast equip", &tempRevertOnFastEquip)) {
				Configs::adjustment->SetAdjustmentFlag(Configs::REVERT_FLAG::kRevertOnFastEquip, tempRevertOnFastEquip);
			}

			if (ImGui::Checkbox("Revert on unequip", &tempRevertOnUnequip)) {
				Configs::adjustment->SetAdjustmentFlag(Configs::REVERT_FLAG::kRevertOnUnequip, tempRevertOnUnequip);
			}

			if (ImGui::Checkbox("Revert on gun down", &tempRevertOnGunDown)) {
				Configs::adjustment->SetAdjustmentFlag(Configs::REVERT_FLAG::kRevertOnGunDown, tempRevertOnGunDown);
			}
			
			ImGui::Separator();
			
			if (ImGui::Checkbox("Persistent (Bodycam Mode)", &tempPersistent)) {
				Configs::adjustment->SetAdjustmentFlag(Configs::REVERT_FLAG::kPersistent, tempPersistent);
			}
			if (ImGui::IsItemHovered()) {
				ImGui::SetTooltip("When enabled, weapon position stays fixed during all animations (reload, sprint, melee, etc.) except zoom.");
			}
			
			ImGui::EndCombo();
		}

		ImGui::SeparatorText("Quality of Life");
		static bool showMessage = false;
		static auto messageTime = std::chrono::steady_clock::now();
		if (ImGui::Button("Generate JSON Snippet", sz)) {
			std::string jsonSnippet = GenerateJsonSnippet();
			CopyToClipboard(jsonSnippet);
			showMessage = true;
			messageTime = std::chrono::steady_clock::now();
		}

		if (showMessage) {
			ImGui::TextColored(ImVec4(0.f, 0.95f, 0.f, 1.f), "JSON snippet copied to clipboard!");

			auto now = std::chrono::steady_clock::now();
			if (std::chrono::duration_cast<std::chrono::seconds>(now - messageTime).count() > 2) {
				showMessage = false;
			}
		}

		ImGui::SeparatorText("Debug");
		if (Globals::p)
			ImGui::TextColored(ImVec4(0.95f, 0.f, 0.f, 1.f), "currentTime %.2f duration %.2f easePercentage %.2f\nGunState %d WeaponState %d\nClip Name %s", currentTime, duration, easePercentage, Globals::p->gunState, Globals::p->weaponState, clipName.c_str());
		ImGui::SeparatorText("Animation");
		if (Globals::p)
			ImGui::TextColored(ImVec4(0.95f, 0.f, 0.f, 1.f), "%s", animationInfo.c_str());

		ImGui::End();
	}
}
