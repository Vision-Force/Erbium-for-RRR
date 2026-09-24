#pragma once

struct FConfiguration
{
    static inline auto Playlist = L"/DelMarCore/Playlists/Levels/Playlist_DelMar_Iron.Playlist_DelMar_Iron";
    static inline auto MaxTickRate = 30;
    static inline auto bLateGame = false;
    static inline auto LateGameZone = 3;
    static inline auto bLateGameLongZone = false;
    static inline auto bEnableCheats = true;
    static inline auto SiphonAmount = 50;
    static inline auto bInfiniteMats = false;
    static inline auto bInfiniteAmmo = false;
    static inline auto bForceRespawns = false;
    static inline auto bJoinInProgress = false;
    static inline auto bAutoRestart = false;
    static inline auto bKeepInventory = false;
    static inline auto Port = 7777;
    static inline auto bEnableIris = true;
    static inline constexpr auto bGUI = false;
    static inline constexpr auto bCustomCrashReporter = true;
    static inline constexpr auto bUseStdoutLog = false;
    static inline constexpr auto WebhookURL = "";

    struct FDelMarPlugin
    {
        const wchar_t* Name;
        const wchar_t* RelDir;
    };

    static inline auto DelMarMap = L"/DelMarCore/Playlists/DelMar_RootLevel";

    static inline auto bDelMarHookTravel = true;
    static inline auto DelMarTravelSentinelHost = L"127.0.0.1";
    static inline auto DelMarTravelSentinelPort = 7777;
    static inline auto bStartListenServer = false;
    static inline auto bStandalone = true;
    static inline auto bDelMarActivateGameFeatures = true;
    static inline auto bStreamTrack = true;
    static inline auto DelMarCorePluginWaitMs = 45000u;
    static inline auto DelMarFrontendWaitMs = 600000u;
    static inline auto DelMarLobbySignalWaitMs = 600000u;
    static inline constexpr auto bStdoutToFile = true;

    static inline const FDelMarPlugin DelMarCorePlugins[] = {
        { L"DelMarSettings",     L"DelMar/DelMarSettings" },
        { L"DelMarTrack",        L"DelMar/DelMarTrack" },
        { L"DelMarCore",         L"DelMar/DelMarCore" },
        { L"DelMarAudio",        L"DelMar/DelMarAudio" },
        { L"DelMarCosmetics",    L"DelMar/DelMarCosmetics" },
        { L"DelMarRendering",    L"DelMar/DelMarRendering" },
        { L"DelMarUI",           L"DelMar/DelMarUI" },
        { L"DelMarCommonAssets", L"DelMar/DelMarCommonAssets" },
        { L"DelMarGame",         L"DelMar/DelMarGame" },
    };

    static inline const FDelMarPlugin DelMarTrackPlugins[] = {
        { L"DelMarIron", L"DelMar/DelMarLevels/DelMarIron" },
    };

    static inline auto bDelMarDiagnostics = true;
    static inline auto bDelMarSpawnPlayspace = true;
    static inline auto DelMarPlayspaceSpawnDelayS = 3.0;
    static inline auto bDelMarLinkPlayspaceToManager = true;
    static inline auto bDelMarCallHandleMatchStarted = true;
    static inline auto bDelMarSkipClientLoadWait = true;
    static inline auto bDelMarPushStateMachine = true;
    static inline auto bDelMarRequestLevelIfIdle = true;
    static inline auto bDelMarRegisterRacer = true;
    static inline auto bDelMarRestartPlayerAfterLoad = false;
    static inline auto DelMarMaxTickRate = 60;
    static inline auto DelMarPumpBudgetMs = 4ull;
    static inline auto bDelMarHeavySnapshots = false;
    static inline auto bDelMarLocateFunctions = false;
    static inline auto bDelMarEnableAsyncPhysics = false;
    static inline auto DelMarPhysicsRateHz = 60;
    static inline auto bDelMarBreakSleepLock = true;
    static inline auto bDelMarEnableNetPrediction = true;
    static inline auto bDelMarForceNetPredShadow = true;
    static inline auto DelMarNetPredCVarDataRva = 0x117C8BE8ull;

    static inline auto bDelMarEnableRollbackInputs = false;
    static inline auto Np2EnableGlobalRva = 0x118863E0ull;
    static inline auto bDelMarRollbackRegistrarBackstop = true;
    static inline auto DelMarRollbackRegistrarRva = 0xB864230ull;
    static inline auto bDelMarForceMarshalInputFromPending = true;
    static inline auto MarshalInputRva = 0xB8731E4ull;
    static inline auto bDelMarRearmIfProducerMissing = true;
    static inline auto bDelMarMarkLocallyViewed = true;
    static inline auto bDelMarEnablePhysicsPrediction = true;
    static inline auto bDelMarPresetChaosRollback = false;
    static inline auto DelMarNetModelValue = 1;
    static inline auto bDelMarNetModelProbe = true;
    static inline auto bDelMarForceChaosRollback = false;
    static inline auto bDelMarSpawnMutators = true;
    static inline auto bDelMarPhysicsProbe = true;
    static inline auto bDelMarPhysicsProbeForce = false;
    static inline auto DelMarPhysicsProbeImpulse = 800.0;
    static inline auto bDelMarForceThrottleTest = false;
    static inline auto bDelMarObservePlayerInput = true;
    static inline auto bDelMarObjectScans = false;
    static inline auto bDelMarEquipArchetype = false;
    static inline auto DelMarVehicleArchetypeTag = L"Vehicle.Archetype.SportsCar";
    static inline auto bDelMarDriveDiagnostics = true;
    static inline auto bDelMarReadyUpLocalPlayer = true;
    static inline auto bDelMarCheatSkipWaiting = false;
    static inline auto bDelMarRequestStartRace = true;
    static inline const wchar_t* DelMarTrackRequestTags[] = { L"DelMar.Map.Racing.Iron", L"DelMar.Mode.Competitive" };
    static inline auto DelMarStreamFallbackDelayS = 45.0;
    static inline auto DelMarPlayspaceClassPath = L"/DelMarGame/Playspace/DelMar_Playspace_BP.DelMar_Playspace_BP_C";
    static inline auto DelMarTrackMapTag = L"DelMar.Map.Racing.Iron";
    static inline auto DelMarTrackLevelDataPath = L"/DelMarIron/DelMar_Iron_LevelData.DelMar_Iron_LevelData";
    static inline auto bDelMarSendVerseURI = false;
    static inline auto DelMarVerseTrackFeature = L"/Fortnite.com/GameFeatures/DelMarIron";
    static inline auto DelMarVerseRootFeature = L"/Fortnite.com/GameFeatures/DelMarRoot";
    static inline auto DelMarSubGame = L"Athena";
    static inline auto DelMarGameModeOption = L"/DelMarCore/Core/DelMar_GameMode.DelMar_GameMode_C";
    static inline const wchar_t* DelMarTrackWorlds[] = {
        L"/DelMarIron/DelMar_Iron",
    };
};
