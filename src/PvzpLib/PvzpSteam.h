/*
 * Optional Steam achievements bridge (Windows only).
 *
 * If steam_api64.dll (steam_api.dll for 32-bit builds) is placed next to the executable and the
 * Steam client is running, achievements earned in the game are also unlocked on Steam for
 * Plants vs. Zombies GOTY (app 3590). Without the DLL nothing happens.
 */

#ifndef __PVZPSTEAM_H__
#define __PVZPSTEAM_H__

namespace PvzpSteam
{
	void Init();
	void Shutdown();
	void RunCallbacks();							// call regularly from the main loop
	void UnlockAchievement(int theAchievementId);	// AchievementId from AchievementsScreen.h
	bool IsActive();
}

#endif
