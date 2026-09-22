// modified based on Erbium (https://github.com/plooshi/Erbium), GPL-3.0.
// modified by Vision Force, 2026. See README.md for the change liszt
//Every entry to APPDATA%\local\RocketRacingReborn\erbium-delmar.log.
#pragma once
#include "../../pch.h"
#include <functional>

class AFortGameMode;
class AFortGameStateAthena;
class UFortPlaylistAthena;

namespace DelMar
{
    // logging
    void InitLog();
    void Log(const char* Fmt, ...);
    const char* LogPath();
    const char* StdoutLogPath();

    // mode
    bool IsEnabled();
    bool IsDelMarObject(const UObject* Obj);

    void* StreamTrack(const wchar_t* ObjectPath);
    bool PlacePlayerOnTrack(bool bFallbackToOrigin = false);
    UEAllocatedString ClassChain(const UObject* Obj);

    void RunOnGameThread(std::function<void()> Fn);
    bool RunOnGameThreadAndWait(std::function<void()> Fn, uint32 TimeoutMs = 20000);
    void RunOnGameThreadAfter(double DelaySeconds, std::function<void()> Fn);
    void PumpGameThread();
    void ReportFrameTime(float DeltaTime);
    bool IsGameThreadPumping();

    bool WaitForLobbySignal(uint32 TimeoutMs);
    bool WaitForFrontend(uint32 TimeoutMs);
    bool Preflight();
    void DiscoverGameFeatureApi();
    void DumpPluginStates(const char* Tag);
    bool ActivatePlugin(const wchar_t* PluginName, const wchar_t* RelDir);
    void ActivateCorePlugins();
    void ActivateTrackPlugins();
    bool WaitForCorePlugins(uint32 TimeoutMs);
    void EnableNetPrediction();
    void EnableRollbackInputs();
    void ArmRollbackInputChain(const char* Tag);
    void ProbeSolverRewind(const char* Tag);
    void ConfigureNetModel();
    void ConfigurePhysics();
    const wchar_t* MapOpenCommand();
    void OnMapOpenIssued();
    void* SetupListenServer(bool* bListening);
    bool InGuardedRegion();

    void OnListenStarted(bool bOk);
    void OnPlaylistApplied(AFortGameMode* GameMode, AFortGameStateAthena* GameState, const UFortPlaylistAthena* Playlist);
    void OnNewPlayer(AFortGameMode* GameMode, const UObject* PlayerController);
    void OnPawnSpawned(const UObject* PlayerController, const UObject* Pawn, const UObject* StartSpot);
    void OnWorldReady(AFortGameMode* GameMode);
    void HookTravel();
    void HookInputMarshal();
    void DumpWorldSnapshot(const char* Tag);
}
