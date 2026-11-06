/*
 * Optional Steam achievements bridge (Windows only). See PvzpSteam.h.
 *
 * The Steamworks DLL is loaded at runtime through its flat C API, so no SDK headers or import
 * libraries are needed to build, and builds without the DLL behave exactly as before.
 * Supports both the older (SteamAPI_Init + SteamUserStats_v012) and newer
 * (SteamAPI_InitFlat + SteamUserStats_v013) generations of steam_api64.dll.
 */

#include "PvzpSteam.h"
#include "PvzpDebug.h"

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <cstdlib>
#include <chrono>
#include <set>
#include <string>

namespace
{
	using FnBool = bool (__cdecl*)();
	using FnInitFlat = int (__cdecl*)(char* theErrMsg);	// ESteamAPIInitResult, 0 == OK
	using FnVoid = void (__cdecl*)();
	using FnGetIface = void* (__cdecl*)();
	using FnSetAchievement = bool (__cdecl*)(void* theStats, const char* theName);
	using FnStats = bool (__cdecl*)(void* theStats);

	// Plants vs. Zombies GOTY on Steam
	constexpr const char* kAppId = "3590";

	// Steam API names, in AchievementId order (see AchievementsScreen.h)
	constexpr const char* kApiNames[] = {
		"finished_adventure",		// HomeSecurity
		"gold_sunflower_trophy",	// NovelPeasPrize
		"izombie_streak",			// BetterOffDead
		"vasebreaker_streak",		// ChinaShop
		"potato_mine",				// Spudow
		"cherry_bomb",				// Explodonator
		"all_plants",				// Morticulturalist
		"pea_pool",					// DontPea
		"roll_heads",				// RollSomeHeads
		"grounded",					// Grounded
		"secret_zombie",			// Zombologist
		"penny_pincher",			// PennyPincher
		"sunny_days",				// SunnyDays
		"popcorn",					// PopcornParty
		"good_morning",				// GoodMorning
		"no_fungus",				// NoFungusAmongUs
		"minigames",				// BeyondTheGrave
		"survival_streak",			// Immortal
		"tall_tree",				// ToweringWisdom
		"mustache_mode",			// MustacheMode
	};
	constexpr int kNumApiNames = static_cast<int>(sizeof(kApiNames) / sizeof(kApiNames[0]));

	HMODULE gDll = nullptr;
	void* gStats = nullptr;
	FnVoid gRunCallbacks = nullptr;
	FnVoid gShutdown = nullptr;
	FnSetAchievement gSetAchievement = nullptr;
	FnStats gStoreStats = nullptr;
	FnStats gRequestCurrentStats = nullptr;
	bool gActive = false;
	std::set<int> gPending;
	std::chrono::steady_clock::time_point gLastTry;

	template <typename T>
	T Proc(const char* theName)
	{
		return reinterpret_cast<T>(reinterpret_cast<void*>(GetProcAddress(gDll, theName)));
	}

	void FlushPending()
	{
		if (!gActive || gPending.empty())
			return;
		gLastTry = std::chrono::steady_clock::now();
		bool aAnySet = false;
		for (auto it = gPending.begin(); it != gPending.end();)
		{
			// With the older API this fails until the current stats have arrived; we just retry later.
			if (gSetAchievement(gStats, kApiNames[*it]))
			{
				PvzpLogLn("Steam: achievement '{}' set", kApiNames[*it]);
				aAnySet = true;
				it = gPending.erase(it);
			}
			else
				++it;
		}
		if (aAnySet)
			gStoreStats(gStats);
	}
}

void PvzpSteam::Init()
{
	if (gActive || gDll)
		return;

	wchar_t aPath[MAX_PATH];
	DWORD aLen = GetModuleFileNameW(nullptr, aPath, MAX_PATH);
	if (aLen == 0 || aLen >= MAX_PATH)
		return;
	std::wstring aDllPath(aPath, aLen);
	aDllPath.erase(aDllPath.find_last_of(L"\\/") + 1);
#ifdef _WIN64
	aDllPath += L"steam_api64.dll";
#else
	aDllPath += L"steam_api.dll";
#endif
	if (GetFileAttributesW(aDllPath.c_str()) == INVALID_FILE_ATTRIBUTES)
		return;	// no DLL next to the exe: Steam integration disabled

	gDll = LoadLibraryW(aDllPath.c_str());
	if (!gDll)
	{
		PvzpLogLn("Steam: failed to load steam_api DLL");
		return;
	}

	_putenv_s("SteamAppId", kAppId);
	_putenv_s("SteamGameId", kAppId);

	bool aOk = false;
	if (auto aInitFlat = Proc<FnInitFlat>("SteamAPI_InitFlat"))
	{
		char aErr[1024] = {};
		aOk = aInitFlat(aErr) == 0;
		if (!aOk)
			PvzpLogLn("Steam: init failed: {}", aErr);
	}
	else if (auto aInit = Proc<FnBool>("SteamAPI_Init"))
	{
		aOk = aInit();
		if (!aOk)
			PvzpLogLn("Steam: init failed (is Steam running and do you own the game?)");
	}

	if (aOk)
	{
		for (const char* aName : { "SteamAPI_SteamUserStats_v013", "SteamAPI_SteamUserStats_v012", "SteamAPI_SteamUserStats_v011" })
		{
			if (auto aGet = Proc<FnGetIface>(aName))
			{
				gStats = aGet();
				if (gStats)
					break;
			}
		}
		gRunCallbacks = Proc<FnVoid>("SteamAPI_RunCallbacks");
		gShutdown = Proc<FnVoid>("SteamAPI_Shutdown");
		gSetAchievement = Proc<FnSetAchievement>("SteamAPI_ISteamUserStats_SetAchievement");
		gStoreStats = Proc<FnStats>("SteamAPI_ISteamUserStats_StoreStats");
		gRequestCurrentStats = Proc<FnStats>("SteamAPI_ISteamUserStats_RequestCurrentStats");	// absent in newer SDKs
	}

	if (!aOk || !gStats || !gRunCallbacks || !gSetAchievement || !gStoreStats)
	{
		if (aOk && gShutdown)
			gShutdown();
		FreeLibrary(gDll);
		gDll = nullptr;
		gStats = nullptr;
		if (aOk)
			PvzpLogLn("Steam: unsupported steam_api DLL (no flat user stats API)");
		return;
	}

	if (gRequestCurrentStats)
		gRequestCurrentStats(gStats);
	gActive = true;
	PvzpLogLn("Steam: achievements enabled (app {})", kAppId);
}

void PvzpSteam::Shutdown()
{
	if (gActive)
	{
		FlushPending();
		gShutdown();
	}
	gActive = false;
	gStats = nullptr;
	if (gDll)
	{
		FreeLibrary(gDll);
		gDll = nullptr;
	}
}

void PvzpSteam::RunCallbacks()
{
	if (!gActive)
		return;
	gRunCallbacks();
	if (!gPending.empty() && std::chrono::steady_clock::now() - gLastTry > std::chrono::seconds(1))
		FlushPending();
}

void PvzpSteam::UnlockAchievement(int theAchievementId)
{
	if (!gActive || theAchievementId < 0 || theAchievementId >= kNumApiNames)
		return;
	gPending.insert(theAchievementId);
	FlushPending();
}

bool PvzpSteam::IsActive()
{
	return gActive;
}

#else // !_WIN32

void PvzpSteam::Init() {}
void PvzpSteam::Shutdown() {}
void PvzpSteam::RunCallbacks() {}
void PvzpSteam::UnlockAchievement(int) {}
bool PvzpSteam::IsActive() { return false; }

#endif
