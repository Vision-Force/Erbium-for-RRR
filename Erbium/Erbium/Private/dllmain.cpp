#include "pch.h"
#include "../../Engine/Public/NetDriver.h"
#include "../../Erbium/Plugins/CrashReporter/Public/CrashReporter.h"
#include "../../FortniteGame/Public/DelMar.h"
#include "../../FortniteGame/Public/FortInventory.h"
#include "../../FortniteGame/Public/FortPlayerControllerAthena.h"
#include "../Public/Configuration.h"
#include "../Public/Finders.h"
#include "../Public/GUI.h"
#include "../Public/Misc.h"
#include "../Public/Utils.h"
#include <chrono>
#include <iostream>
#include <thread>
#pragma comment(lib, "libcurl/libcurl.lib")
#pragma comment(lib, "libcurl/zlib.lib")
#pragma comment(lib, "Ws2_32.lib")
#pragma comment(lib, "Wldap32.lib")
#pragma comment(lib, "Crypt32.lib")
#pragma comment(lib, "Normaliz.lib")

void Main()
{
    DelMar::InitLog();

    {
        char Exe[MAX_PATH] = {};
        GetModuleFileNameA(nullptr, Exe, MAX_PATH);
        DelMar::Log("attached to %s (pid %lu)", Exe, GetCurrentProcessId());
    }

    if (!DelMar::WaitForLobbySignal(FConfiguration::DelMarLobbySignalWaitMs))
    {
        DelMar::Log("!! no lobby signal - aborting the bring-up, the client is left untouched");
        return;
    }

    if constexpr (!FConfiguration::bGUI)
    {
        if constexpr (FConfiguration::bStdoutToFile)
        {
            FILE* s;
            freopen_s(&s, DelMar::StdoutLogPath(), "w", stdout);
            freopen_s(&s, DelMar::StdoutLogPath(), "w+", stderr);
            setvbuf(stdout, nullptr, _IONBF, 0);
        }
        else
        {
            AllocConsole();
            FILE* s;
            freopen_s(&s, "CONOUT$", "w", stdout);
            freopen_s(&s, "CONOUT$", "w+", stderr);
            freopen_s(&s, "CONIN$", "r", stdin);
        }
    }
    else if constexpr (!FConfiguration::bUseStdoutLog)
    {
        if (GetConsoleWindow())
        {
            FILE* s;
            freopen_s(&s, "CONOUT$", "w", stdout);
            freopen_s(&s, "CONOUT$", "w+", stderr);
            freopen_s(&s, "CONIN$", "r", stdin);
        }
    }

    if constexpr (FConfiguration::bCustomCrashReporter)
        FCrashReporter::Register();

    printf("Initializing SDK...\n");
    SDK::Init();
    DelMar::Log("SDK::Init done: FN %.2f, UE %.1f", VersionInfo.FortniteVersion, VersionInfo.EngineVersion);

    DelMar::HookTravel();

    DelMar::HookInputMarshal();

    if constexpr (FConfiguration::bGUI)
    {
        if constexpr (FConfiguration::bUseStdoutLog)
        {
            FILE* s;
            freopen_s(&s, "stdout.log", "w", stdout);
            freopen_s(&s, "stdout.log", "w+", stderr);
        }

        CreateThread(0, 0, (LPTHREAD_START_ROUTINE)GUI::Init, 0, 0, 0);
    }

    if (wcscmp(FConfiguration::Playlist, L"/DurianPlaylist/Playlist/Playlist_Durian.Playlist_Durian") == 0)
        FConfiguration::bEnableIris = false;

    if (!DelMar::WaitForFrontend(FConfiguration::DelMarFrontendWaitMs))
    {
        DelMar::Log("!! the frontend lobby never appeared - aborting the bring-up, the client is left untouched");
        return;
    }

    if (!DelMar::Preflight())
    {
        DelMar::Log("!! preflight failed - aborting the bring-up, the client is left untouched");
        return;
    }

    if (DelMar::IsEnabled() && FConfiguration::bDelMarEnableNetPrediction)
        DelMar::EnableNetPrediction();
    if (DelMar::IsEnabled() && FConfiguration::bDelMarEnableRollbackInputs)
        DelMar::EnableRollbackInputs();

    if (VersionInfo.EngineVersion >= 5.0)
    {
        UKismetSystemLibrary::ExecuteConsoleCommand(UWorld::GetWorld(), FString(L"log LogFortUIDirector None"), nullptr);
        UKismetSystemLibrary::ExecuteConsoleCommand(UWorld::GetWorld(), FString(L"log LogFortUIManager None"), nullptr);
    }
    if (VersionInfo.FortniteVersion == 20.40)
    {
        UKismetSystemLibrary::ExecuteConsoleCommand(UWorld::GetWorld(), FString(L"log LogSpecialRelevancyHealthComponent None"), nullptr);
    }
    if (VersionInfo.EngineVersion >= 5.1)
    {
        UKismetSystemLibrary::ExecuteConsoleCommand(UWorld::GetWorld(), FString(L"net.AllowEncryption 0"), nullptr);

        auto DefaultCurieGlobals = FindClass("CurieGlobals")->GetDefaultObj();

        if (DefaultCurieGlobals)
        {
            uint32 Offset = DefaultCurieGlobals->GetOffset("bEnableCurie");

            // if (Offset != -1)
            //     *(bool*)(uintptr_t(DefaultCurieGlobals) + Offset) = false;
        }
    }
    if (VersionInfo.EngineVersion >= 5.3 && FConfiguration::bEnableIris)
    {
        UKismetSystemLibrary::ExecuteConsoleCommand(UWorld::GetWorld(), FString(L"log LogIris None"), nullptr);
        UKismetSystemLibrary::ExecuteConsoleCommand(UWorld::GetWorld(), FString(L"log LogIrisRpc None"), nullptr);
        UKismetSystemLibrary::ExecuteConsoleCommand(UWorld::GetWorld(), FString(L"log LogIrisBridge None"), nullptr);

        auto IrisBool = FindCVar<uint32_t>(L"net.Iris.UseIrisReplication");

        if (IrisBool)
            *IrisBool = true;

        if (VersionInfo.FortniteVersion >= 29)
        {
            auto ReplicationBridgeConfig = UObjectReplicationBridgeConfig::GetDefaultObj();

            auto FortInventoryName = FName(L"/Script/FortniteGame.FortInventory");
            for (int i = 0; i < ReplicationBridgeConfig->FilterConfigs.Num(); i++)
            {
                auto& FilterConfig = ReplicationBridgeConfig->FilterConfigs.Get(i, FObjectReplicationBridgeFilterConfig::Size());

                if (FilterConfig.ClassName == FortInventoryName)
                {
                    FilterConfig.DynamicFilterName = FName(0);
                    break;
                }
            }
        }
    }
    if (VersionInfo.EngineVersion >= 5.4)
    {
        // sprint fix
        auto SlideCVar = FindCVar<uint32_t>(L"Fort.MME.Sliding");
        auto MantleCVar = FindCVar<uint32_t>(L"Fort.MME.Clambering");

        if (SlideCVar)
            *SlideCVar = false;

        if (MantleCVar)
            *MantleCVar = false;
        UKismetSystemLibrary::ExecuteConsoleCommand(UWorld::GetWorld(), FString(L"Fort.MME.TacticalSprint 0"), nullptr);
        UKismetSystemLibrary::ExecuteConsoleCommand(UWorld::GetWorld(), FString(L"Fort.MME.Sliding 0"), nullptr);
        UKismetSystemLibrary::ExecuteConsoleCommand(UWorld::GetWorld(), FString(L"Fort.MME.Clambering 0"), nullptr);
    }
    UKismetSystemLibrary::ExecuteConsoleCommand(UWorld::GetWorld(), FString(L"log LogSpecialEventScript VeryVerbose"), nullptr);

    if (DelMar::IsEnabled())
    {
        for (auto Cmd : { L"log LogGameFeatures Verbose", L"log LogLevelStreaming Log", L"log LogWorldPartition Log", L"log LogNet Log", L"log LogGameMode Verbose",
                          L"log LogPlayspaces VeryVerbose", L"log LogDelMarPlayspace VeryVerbose", L"log LogDelMarLevelManager VeryVerbose", L"log LogDelMarStateMachine VeryVerbose",
                          L"log LogFortHermesLoadContext VeryVerbose", L"log LogDelMar VeryVerbose", L"log LogDelMarCore VeryVerbose", L"log LogDelMarCheat Verbose", L"log LogDelMarRaceManager Verbose", L"log LogDelMarTrackManager Verbose",
                          L"log LogDelMarRespawnManager Verbose", L"log LogDelMarCheckpointManager Verbose", L"log LogDelMarVehicle Verbose", L"log LogDelMarGameMode Verbose", L"log LogDelMarPostRace Verbose",
                          L"log LogDelMarVehicleInput Verbose", L"log LogDelMarNetworkInput Verbose", L"log LogDelMarVehiclePhysics Verbose",
                          L"log LogDelMarVehicleNetworkPhysics Verbose", L"log LogDelMarVehicleAbility Verbose", L"log LogDelMarCamera Verbose",
                          L"log LogDelMarNetPredictionMutator Verbose", L"log LogDelMarVehicleCollision Verbose",
                          L"log LogPlayspacePlayerSpawningController Verbose", L"log LogPlayspacePlayerSpawningManager Verbose", L"log LogPlayspaceComponent_PlayerManager Verbose",
                          L"log LogGameplayEventRouter Verbose", L"log LogAssetManager Verbose", L"log LogFortPlaylist Verbose" })
            UKismetSystemLibrary::ExecuteConsoleCommand(UWorld::GetWorld(), FString(Cmd), nullptr);
    }

#ifdef CLIENT
    Misc::InitClient();

    return;
#endif

    if constexpr (FConfiguration::WebhookURL && *FConfiguration::WebhookURL)
        curl_global_init(CURL_GLOBAL_ALL);

    sprintf_s(GUI::windowTitle,
              VersionInfo.EngineVersion >= 5.0 ? "Erbium (FN %.2f, UE %.1f): Setting up"
                                               : (VersionInfo.FortniteVersion >= 5.00 || VersionInfo.FortniteVersion < 1.2 ? "Erbium (FN %.2f, UE %.2f): Setting up" : "Erbium (FN %.1f, UE %.2f): Setting up"),
              VersionInfo.FortniteVersion, VersionInfo.EngineVersion);
    SetConsoleTitleA(GUI::windowTitle);

    printf("Hooking & finding offsets... (this may take a while)\n");

    FindNullsAndRetTrues();
    DelMar::Log("patching %d null-funcs and %d ret-true funcs", (int)NullFuncs.size(), (int)RetTrueFuncs.size());

    for (auto& NullFunc : NullFuncs)
        if (NullFunc != 0)
        {
            Hooking::Patch<uint8_t>(NullFunc, 0xc3);
        }

    for (auto& RetTrueFunc : RetTrueFuncs)
    {
        if (RetTrueFunc == 0)
            continue;

        Hooking::Patch<uint32_t>(RetTrueFunc, 0xc0ffc031);
        Hooking::Patch<uint8_t>(RetTrueFunc + 4, 0xc3);
    }

    auto GameSessionPatch = FindGameSessionPatch();
    if (GameSessionPatch)
        Hooking::Patch<uint8_t>(GameSessionPatch, 0x85);

    for (auto& HookFunc : _HookFuncs)
        HookFunc();
    DelMar::Log("%d hook groups installed", (int)_HookFuncs.size());

    if (!FConfiguration::bStandalone)
        *(bool*)FindGIsClient() = false;
    if (VersionInfo.EngineVersion > 4.20) // 3.6 and below have a crash on ALandscapeProxy
        *(bool*)FindGIsServer() = true;

    srand((uint32_t)time(0));

    if (!FConfiguration::bStandalone)
        UWorld::GetWorld()->OwningGameInstance->LocalPlayers.Remove(0);
    const wchar_t* terrainOpen = L"open Athena_Terrain";

    if (DelMar::IsEnabled())
    {
        DelMar::Log("DelMar bring-up: discovery -> core plugin activation -> %ls", DelMar::MapOpenCommand());
        DelMar::RunOnGameThreadAndWait([] { DelMar::DiscoverGameFeatureApi(); }, 180000);

        if (FConfiguration::bDelMarActivateGameFeatures)
        {
            DelMar::RunOnGameThreadAndWait([] { DelMar::ActivateCorePlugins(); }, 60000);
            DelMar::WaitForCorePlugins(FConfiguration::DelMarCorePluginWaitMs);
        }
        else
            DelMar::Log("bDelMarActivateGameFeatures is off - relying on the playlist's native activation");

        terrainOpen = DelMar::MapOpenCommand();
    }
    else if (wcsstr(FConfiguration::Playlist, L"/MoleGame/Playlists/Playlist_MoleGame"))
    {
        UKismetSystemLibrary::ExecuteConsoleCommand(UWorld::GetWorld(), FString(L"Mole.WorstCasePlayerCount 1"), nullptr);
        terrainOpen = L"open Mole_UnderBase_Parent";
    }
    else if (VersionInfo.FortniteVersion >= 12.00 && wcsstr(FConfiguration::Playlist, L"/Game/Athena/Playlists/Creative/Playlist_PlaygroundV2.Playlist_PlaygroundV2"))
        terrainOpen = L"open Creative_NoApollo_Terrain";
    else
    {
        if (VersionInfo.FortniteVersion >= 27.00)
        {
            if (VersionInfo.FortniteVersion >= 28.00)
                terrainOpen = L"open Helios_Terrain";
        }
        else if (VersionInfo.FortniteVersion >= 23.00)
            terrainOpen = L"open Asteria_Terrain";
        else if (VersionInfo.FortniteVersion >= 19.00)
            terrainOpen = L"open Artemis_Terrain";
        else if (VersionInfo.FortniteVersion >= 11.00)
            terrainOpen = L"open Apollo_Terrain";
    }

    if (DelMar::IsEnabled())
        DelMar::RunOnGameThreadAndWait([] { DelMar::ConfigurePhysics(); }, 30000);

    DelMar::Log("console: %ls", terrainOpen);
    UKismetSystemLibrary::ExecuteConsoleCommand(UWorld::GetWorld(), FString(terrainOpen), nullptr);
    if (DelMar::IsEnabled())
        DelMar::OnMapOpenIssued();

    auto EncryptionPatch = FindEncryptionPatch();
    if (EncryptionPatch)
    {
        auto* EncBytes = (uint8_t*)EncryptionPatch;
        if (EncBytes[0] == 0x0F && EncBytes[1] == 0x8F)
        {
            DWORD OldProt = 0;
            if (VirtualProtect(EncBytes, 6, PAGE_EXECUTE_READWRITE, &OldProt))
            {
                memset(EncBytes, 0x90, 6);
                VirtualProtect(EncBytes, 6, OldProt, &OldProt);
                DelMar::Log("[net] encryption branch at %p neutralised (6-byte near jg -> nops)", EncBytes);
            }
        }
        else
        {
            Hooking::Patch<uint8_t>(EncryptionPatch, 0x74);
        }
    }
    else
        printf("Matchmaking is NOT supported on this version, please make a github issue.\n");

    for (auto& HookFunc : _PostLoadHookFuncs)
        HookFunc();

    Misc::bHookedAll = true;
    DelMar::Log("all hooks installed - waiting for the map to load and ReadyToStartMatch to fire");
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
        std::thread(Main).detach();
    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
    case DLL_PROCESS_DETACH:
        break;
    }
    return TRUE;
}
