#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <concepts>
#include <cstddef>
#include <functional>
#include <numbers>
#include <string>

#include "utils/MemoryMgr.h"
#include "utils/Patterns.h"

namespace INI
{
	static constexpr std::string appName("MouseFix");
	static constexpr std::string iniPath(".\\settings.ini");

	template <typename T, auto ToString, auto FromString> requires requires(T t, std::string str) {
		{ ToString(t) } -> std::convertible_to<std::string>;
		{ FromString(str) } -> std::convertible_to<T>;
	}
	auto ReadValue(const std::string& key, const T& defaultValue) -> T {
		char buffer[256];
		const auto length = GetPrivateProfileStringA(appName.c_str(), key.c_str(), nullptr, buffer, sizeof(buffer), iniPath.c_str());
		if (length == 0) {
			WritePrivateProfileStringA(appName.c_str(), key.c_str(), ToString(defaultValue).c_str(), iniPath.c_str());
			return defaultValue;
		}
		return FromString(std::string(buffer));
	}

	inline auto ReadString(const std::string& key, const std::string& defaultValue) -> std::string {
		return ReadValue<std::string, std::identity{}, std::identity{}>(key, defaultValue);
	}

	inline auto ReadInt(const std::string& key, const int defaultValue) -> int {
		static constexpr auto ToString = [](int i) -> std::string { return std::to_string(i); };
		static constexpr auto FromString = [](std::string str) -> int { return std::stoi(str); };
		return ReadValue<int, ToString, FromString>(key, defaultValue);
	}

	inline auto ReadBool(const std::string& key, const bool defaultValue) -> bool {
		return static_cast<bool>(ReadInt(key, defaultValue));
	}

	inline auto ReadDouble(const std::string& key, const double defaultValue) -> double {
		static constexpr auto ToString = [](double d) -> std::string { return std::to_string(d); };
		static constexpr auto FromString = [](std::string str) -> double { return std::stod(str); };
		return ReadValue<double, ToString, FromString>(key, defaultValue);
	}
}

static int x = 0;
static int y = 0;
static double mouseRotation;
static double mouseSensitivity;
static double invertMouse;
static float scaledMillisecond;
static float* frameTimePtr;
static int* globalsPtr;
static std::byte* inputUpdateRetAddr;

static constinit double cameraClamp = 0.98;
static constinit double millisecondMultiplier = 60.0 / (1000.0 * 1000.0);

void __declspec(naked) CameraUpdate(void)
{
	__asm {
		xor eax, eax
		mov al, byte ptr[edi + 0x2A0]
		push eax
		fld dword ptr[edi + 0x214] //recoil
		fimul dword ptr[esp] //targeting
		fstp dword ptr[edi + 0x214]
		cmp edx, 0x420BA058 //compare edx to magic number
		jne handle_y //branch to y logic if magic number
	//handle_x:
		fld mouseSensitivity
		fmul mouseRotation
		fimul x
		mov x, 0
		fimul dword ptr[esp] //if targeting multiply by 0
		fadd dword ptr[edi + 0x1E8] //load current camera x
		pop eax
		ret
	handle_y:
		fld cameraClamp
		fld ST(0)
		fchs
		fld mouseSensitivity
		fmul mouseRotation
		fimul y
		mov y, 0
		fmul invertMouse
		fimul dword ptr[esp] //if targeting multiply by 0
		fadd dword ptr[edi + 0x1EC] //load current camera y
		fcomi ST(0), ST(1)
		fcmovbe ST(0), ST(1)
		fstp ST(1)
		fcomi ST(0), ST(1)
		fcmovnbe ST(0), ST(1)
		fstp ST(1)
		pop eax
		ret
	}
}

void __declspec(naked) InputUpdate(void)
{
	__asm {
		push esi
		mov esi, globalsPtr
		cmp dword ptr[esi + 0x44], 0x00000040 //GameState
		jne end
		mov esi, [edi + 0x40]
		cmp esi, 0x73756F4D //"Mous"
		jne end
		mov eax, [eax + 0x5C]
		mov eax, [eax + 0x14]
		cmp dword ptr[eax], 0x0 //compare input buffer to 0 which is x
		jne handle_y
	//handle_x:
		mov esi, [eax + 0x4]
		add x, esi
	handle_y:
		cmp dword ptr[eax], 0x1 //compare input buffer to 1 which is y
		jne end
		mov esi, [eax + 0x4]
		add y, esi
	end:
		pop esi
		call dword ptr[edx + 0x28] //Fix clobber
		test eax, eax //Fix clobber
		jmp inputUpdateRetAddr
	}
}

void __declspec(naked) Sync(void)
{
	__asm {
		push esi
		mov esi, frameTimePtr
		fld dword ptr[esi]
		fmul millisecondMultiplier
		fstp scaledMillisecond
		pop esi
		ret
	}
}

void Init(void)
{
	mouseRotation = INI::ReadDouble("DegreesPerCount", 0.022);
	mouseSensitivity = INI::ReadDouble("Sensitivity", 1.0) * std::numbers::pi / 180.0;
	invertMouse = INI::ReadBool("InvertMouse", false) ? 1.0 : -1.0;
	const auto refreshRate = INI::ReadInt("RefreshRate", 60);

	const auto cameraJumpAddress = hook::txn::get_pattern<std::byte>("D9 44 24 14 83 EC 14");
	Memory::InjectHook(cameraJumpAddress, CameraUpdate, PATCH_JUMP);
	globalsPtr = *hook::txn::get_pattern<int*>("B8 ? ? ? ? A3 ? ? ? ? 64 89 0D 00 00 00 00", 1);

	const auto inputJumpAddress = hook::txn::get_pattern<std::byte>("50 FF 52 28 85 C0 7C 3C", 1);
	inputUpdateRetAddr = inputJumpAddress + 0x5;
	Memory::InjectHook(inputJumpAddress, InputUpdate, PATCH_JUMP);

	if (refreshRate != 60) {
		if (auto refreshRatePattern = hook::txn::pattern("2B F1 F7 DE 1B F6"); refreshRatePattern.size() > 0) { //Doesn't exist in 1.0.0.0 but seems uncapped?
			const auto refreshRateClobber = refreshRatePattern.get_first<std::byte>(4);
			Memory::Patch(refreshRateClobber, std::byte(0xBE)); //mov esi, imm32
			Memory::Patch(refreshRateClobber + 0x1, refreshRate); //imm32
			Memory::Nop(refreshRateClobber + 0x5, 1); //nop rest of partially clobbered instruction
		}

		const auto timingHook = hook::txn::get_pattern<std::byte>("83 C4 5C C3 CC CC CC CC CC CC CC 6A FF", 3);
		Memory::InjectHook(timingHook, Sync, PATCH_JUMP);
		frameTimePtr = *hook::txn::get_pattern<float*>("D9 05 ? ? ? ? D8 0D ? ? ? ? D9 C9", 2);

		const auto addressToAnimationMillisecond = hook::txn::get_pattern<std::byte>("8B 44 24 20 D8 0D ? ? ? ?", 6);
		Memory::Patch(addressToAnimationMillisecond, &scaledMillisecond);

		const auto addressToRecoilMillisecond1 = hook::txn::get_pattern<std::byte>("D8 0D ? ? ? ? D9 5C 24 14 D9 85 14 02 00 00", 2);
		Memory::Patch(addressToRecoilMillisecond1, &scaledMillisecond);

		const auto addressToRecoilMillisecond2 = hook::txn::get_pattern<std::byte>("D8 0D ? ? ? ? 80 7C 24 0C 00", 2);
		Memory::Patch(addressToRecoilMillisecond2, &scaledMillisecond);
	}
}

BOOL WINAPI DllMain(HINSTANCE, const DWORD fdwReason, LPVOID) {
	if (fdwReason == DLL_PROCESS_ATTACH)
		Init();
	return TRUE;
}
