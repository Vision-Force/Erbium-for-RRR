// fmodified work based on Erbium (https://github.com/plooshi/Erbium), via GPL-3.0. license
// DelMar.cpp, made by Shrezee https://github.com/shrezesUveerse, 2026. See README.md for the change list
#include "pch.h"
#include "../Public/DelMar.h"
#include "../Public/FortGameMode.h"
#include "../Public/FortPlaylistAthena.h"
#include "../Public/FortPlayerControllerAthena.h"
#include "../../Engine/Public/NetDriver.h"
#include "../../Erbium/Public/Configuration.h"
#include "../../Erbium/Public/Finders.h"
#include <cctype>
#include <cstdarg>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

//crash cooperation
namespace
{
    thread_local int g_DelMarGuardDepth = 0;
}
bool DelMar::InGuardedRegion()
{
    return g_DelMarGuardDepth > 0;
}

// logging
namespace
{
    CRITICAL_SECTION LogLock;
    bool bLogReady = false;
    char LogDir[MAX_PATH] = {};
    char LogFilePath[MAX_PATH] = {};
    char StdoutFilePath[MAX_PATH] = {};

    void WriteLine(const char* Line)
    {
        SYSTEMTIME t;
        GetLocalTime(&t);
        char Stamp[48];
        sprintf_s(Stamp, "[%02d:%02d:%02d.%03d][%5lu] ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds, GetCurrentThreadId());

        if (bLogReady)
            EnterCriticalSection(&LogLock);

        if (LogFilePath[0])
        {
            FILE* f = nullptr;
            if (fopen_s(&f, LogFilePath, "a") == 0 && f)
            {
                fputs(Stamp, f);
                fputs(Line, f);
                fputc('\n', f);
                fclose(f);
            }
        }
        printf("%s%s\n", Stamp, Line);

        if (bLogReady)
            LeaveCriticalSection(&LogLock);
    }
}

void DelMar::InitLog()
{
    if (bLogReady)
        return;

    InitializeCriticalSection(&LogLock);

    char Base[MAX_PATH] = {};
    if (GetEnvironmentVariableA("LOCALAPPDATA", Base, MAX_PATH) == 0)
        GetTempPathA(MAX_PATH, Base);

    _snprintf_s(LogDir, sizeof(LogDir), _TRUNCATE, "%s\\RocketRacingReborn", Base);
    CreateDirectoryA(LogDir, nullptr);
    _snprintf_s(LogFilePath, sizeof(LogFilePath), _TRUNCATE, "%s\\erbium-delmar.log", LogDir);
    _snprintf_s(StdoutFilePath, sizeof(StdoutFilePath), _TRUNCATE, "%s\\erbium-stdout.log", LogDir);
    bLogReady = true;

    WriteLine("================================================================");
    WriteLine("Erbium + RRR DelMar : log");
}

void DelMar::Log(const char* Fmt, ...)
{
    char Stack[4096];
    va_list ap;
    va_start(ap, Fmt);
    va_list ap2;
    va_copy(ap2, ap);
    int Need = _vscprintf(Fmt, ap2);
    va_end(ap2);
    if (Need < 0 || Need < (int)sizeof(Stack))
    {
        _vsnprintf_s(Stack, sizeof(Stack), _TRUNCATE, Fmt, ap);
        va_end(ap);
        WriteLine(Stack);
        return;
    }
    size_t Cap = (size_t)Need + 1;
    if (Cap > 64 * 1024)
        Cap = 64 * 1024;
    char* Heap = (char*)malloc(Cap);
    if (!Heap)
    {
        _vsnprintf_s(Stack, sizeof(Stack), _TRUNCATE, Fmt, ap);
        va_end(ap);
        WriteLine(Stack);
        return;
    }
    _vsnprintf_s(Heap, Cap, _TRUNCATE, Fmt, ap);
    va_end(ap);
    if ((size_t)Need + 1 > Cap)
    {
        static const char Mark[] = " ...<LINE TRUNCATED>";
        memcpy(Heap + Cap - sizeof(Mark), Mark, sizeof(Mark));
    }
    WriteLine(Heap);
    free(Heap);
}

const char* DelMar::LogPath()
{
    return LogFilePath;
}

const char* DelMar::StdoutLogPath()
{
    return StdoutFilePath;
}

// helpers
namespace
{
    constexpr uint64 CASTCLASS_UClass = 0x20;
    constexpr uint64 CASTCLASS_UFunction = 0x80000;
    constexpr uint64 CASTCLASS_FBoolProperty = 0x20000;

    constexpr uint64 CPF_ConstParm = 0x2;
    constexpr uint64 CPF_Parm = 0x80;
    constexpr uint64 CPF_OutParm = 0x100;
    constexpr uint64 CPF_ReturnParm = 0x400;

    constexpr uint32 FUNC_Native = 0x400;
    constexpr uint32 FUNC_Static = 0x2000;
    constexpr uint32 FUNC_BlueprintCallable = 0x4000000;

    UEAllocatedString Lower(UEAllocatedString S)
    {
        for (auto& c : S)
            c = (char)tolower((unsigned char)c);
        return S;
    }

    bool IContains(const UEAllocatedString& Hay, const char* Needle)
    {
        return Lower(Hay).find(Lower(UEAllocatedString(Needle))) != UEAllocatedString::npos;
    }

    bool IEquals(const UEAllocatedString& A, const char* B)
    {
        return _stricmp(A.c_str(), B) == 0;
    }

    UEAllocatedString Narrow(const wchar_t* W)
    {
        UEAllocatedWString WS(W ? W : L"");
        return UEAllocatedString(WS.begin(), WS.end());
    }

    UEAllocatedWString Widen(const UEAllocatedString& S)
    {
        return UEAllocatedWString(S.begin(), S.end());
    }

    UEAllocatedString ObjName(const UObject* O)
    {
        return O ? O->Name.ToString() : UEAllocatedString("null");
    }

    UEAllocatedString Addr(uint64 V)
    {
        if (!V)
            return "MISSING";
        char b[64];
        if (V >= ImageBase && V < ImageBase + 0x80000000ull)
            sprintf_s(b, "0x%llx (rva 0x%llx)", V, V - ImageBase);
        else
            sprintf_s(b, "0x%llx", V);
        return b;
    }

    UEAllocatedString FieldTypeName(const UField* Prop)
    {
        if (!Prop)
            return "?";
        if (VersionInfo.FortniteVersion < 12.10)
            return Prop->Class ? Prop->Class->Name.ToString() : UEAllocatedString("?");
        auto FieldClass = *(void**)(__int64(Prop) + 0x8);
        if (!FieldClass)
            return "?";
        return (*(FName*)FieldClass).ToString();
    }

    uint32 FunctionFlags(const UFunction* Fn)
    {
        return (Fn && VersionInfo.EngineVersion >= 5.0) ? GetFromOffset<uint32>(Fn, 0xB0) : 0;
    }

    struct FParamInfo
    {
        UEAllocatedString Name;
        UEAllocatedString Type;
        uint32 Offset = 0;
        uint64 Flags = 0;
        uint32 Size = 0;
        const UField* Prop = nullptr;

        bool IsReturn() const { return (Flags & CPF_ReturnParm) != 0; }
        bool IsOut() const { return (Flags & CPF_OutParm) != 0 && (Flags & CPF_ConstParm) == 0 && !IsReturn(); }
        bool IsInput() const { return !IsReturn() && ((Flags & CPF_OutParm) == 0 || (Flags & CPF_ConstParm) != 0); }
    };

    UEAllocatedVector<FParamInfo> GetParams(const UFunction* Fn)
    {
        UEAllocatedVector<FParamInfo> Out;
        if (!Fn)
            return Out;

        if (VersionInfo.FortniteVersion >= 12.10)
        {
            for (auto P = Fn->GetChildProperties(); P; P = P->FField_GetNext())
            {
                auto Flags = GetFromOffset<uint64>(P, Offsets::PropertyFlags);
                if (!(Flags & CPF_Parm))
                    continue;
                FParamInfo I;
                I.Name = P->FField_GetName().ToString();
                I.Type = FieldTypeName(P);
                I.Offset = GetFromOffset<uint32>(P, Offsets::Offset_Internal);
                I.Flags = Flags;
                I.Size = GetFromOffset<uint32>(P, Offsets::ElementSize);
                I.Prop = P;
                Out.push_back(I);
            }
        }
        else
        {
            for (auto P = Fn->GetChildren(); P; P = P->GetNext())
            {
                auto Flags = GetFromOffset<uint64>(P, Offsets::PropertyFlags);
                if (!(Flags & CPF_Parm))
                    continue;
                FParamInfo I;
                I.Name = P->GetName().ToString();
                I.Type = FieldTypeName(P);
                I.Offset = GetFromOffset<uint32>(P, Offsets::Offset_Internal);
                I.Flags = Flags;
                I.Size = GetFromOffset<uint32>(P, Offsets::ElementSize);
                I.Prop = P;
                Out.push_back(I);
            }
        }
        return Out;
    }

    UEAllocatedString Signature(const UFunction* Fn)
    {
        if (!Fn)
            return "null";

        UEAllocatedString Ret = "void", Args;
        for (auto& P : GetParams(Fn))
        {
            if (P.IsReturn())
            {
                Ret = P.Type;
                continue;
            }
            if (!Args.empty())
                Args += ", ";
            Args += P.Type + " " + (P.IsOut() ? "&" : "") + P.Name;
        }

        auto Flags = FunctionFlags(Fn);
        UEAllocatedString Prefix;
        if (Flags & FUNC_Static)
            Prefix += "static ";
        if (Flags & FUNC_Native)
            Prefix += "native ";
        if (Flags & FUNC_BlueprintCallable)
            Prefix += "bp ";

        return Prefix + Ret + " " + ObjName(Fn->Outer) + "::" + Fn->Name.ToString() + "(" + Args + ")";
    }

    UEAllocatedString EnumValueName(const UEnum* Enum, int64 Value)
    {
        char Num[32];
        sprintf_s(Num, "<%lld>", Value);
        if (!Enum)
            return Num;

        auto& Names = *(TArray<TPair<FName, int64>>*)(__int64(Enum) + 0x40);
        for (int i = 0; i < Names.Num(); i++)
        {
            auto& Pair = Names[i];
            if (Pair.Value() != Value)
                continue;
            auto S = Pair.Key().ToString();
            auto c = S.rfind("::");
            return c == UEAllocatedString::npos ? S : S.substr(c + 2);
        }
        return Num;
    }

    UEAllocatedString SoftPathToString(const void* Soft)
    {
        if (!Soft || VersionInfo.FortniteVersion < 23)
            return "?";
        auto& PackageName = *(FName*)(__int64(Soft) + (VersionInfo.EngineVersion < 5.3 ? 0x10 : 0x8));
        auto& AssetName = *(FName*)(__int64(Soft) + (VersionInfo.EngineVersion < 5.3 ? 0x14 : 0xC));
        if (PackageName.ComparisonIndex <= 0)
            return "None";
        auto S = PackageName.ToString();
        if (AssetName.ComparisonIndex > 0)
            S += "." + AssetName.ToString();
        return S;
    }

    int ReadBool(const UObject* Obj, const char* Name)
    {
        if (!Obj)
            return -1;
        auto P = Obj->GetProperty(Name, CASTCLASS_FBoolProperty);
        if (!P)
            return -1;
        auto Off = GetFromOffset<uint32>(P, Offsets::Offset_Internal);
        auto Mask = P->GetFieldMask();
        return (GetFromOffset<uint8>(Obj, Off) & Mask) ? 1 : 0;
    }

    const UObject* ReadObj(const UObject* Obj, const char* Name)
    {
        if (!Obj)
            return nullptr;
        auto Off = Obj->GetOffset(Name);
        return Off == (uint32)-1 ? nullptr : GetFromOffset<const UObject*>(Obj, Off);
    }

    UEAllocatedString ReadFName(const UObject* Obj, const char* Name)
    {
        if (!Obj)
            return "?";
        auto Off = Obj->GetOffset(Name);
        return Off == (uint32)-1 ? UEAllocatedString("<no prop>") : GetFromOffset<FName>(Obj, Off).ToString();
    }
}

bool DelMar::IsEnabled()
{
    return wcsstr(FConfiguration::Playlist, L"DelMar") != nullptr;
}

UEAllocatedString DelMar::ClassChain(const UObject* Obj)
{
    if (!Obj || !Obj->Class)
        return "null";
    UEAllocatedString S;
    for (const UStruct* C = Obj->Class; C; C = C->GetSuper())
    {
        if (!S.empty())
            S += " : ";
        S += C->Name.ToString();
    }
    return S;
}

bool DelMar::IsDelMarObject(const UObject* Obj)
{
    if (!Obj || !Obj->Class)
        return false;
    for (const UStruct* C = Obj->Class; C; C = C->GetSuper())
        if (IContains(C->Name.ToString(), "DelMar"))
            return true;
    return false;
}

//game thread queue
namespace
{
    struct FTask
    {
        uint64 Id = 0;
        std::function<void()> Fn;
        uint64 Due = 0;
        HANDLE Done = nullptr;
    };

    std::mutex QueueLock;
    std::deque<FTask> Queue;
    std::vector<FTask> Delayed;
    uint64 NextTaskId = 1;
    volatile uint64 LastPumpTick = 0;
    volatile long PumpCount = 0;
    volatile DWORD GameThreadId = 0;

    void RunGuarded(std::function<void()>* Fn)
    {
        ++g_DelMarGuardDepth;
        __try
        {
            (*Fn)();
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            DelMar::Log("!! a DelMar game thread task crashed (exception 0x%08x) - continuing", GetExceptionCode());
        }
        --g_DelMarGuardDepth;
    }
}

bool DelMar::IsGameThreadPumping()
{
    return PumpCount > 0 && (GetTickCount64() - LastPumpTick) < 5000;
}

void DelMar::ReportFrameTime(float DeltaTime)
{
    static uint64 WindowStart = 0;
    static int Frames = 0;
    static double Sum = 0.0;
    static float Worst = 0.f;

    auto Now = GetTickCount64();
    if (!WindowStart)
        WindowStart = Now;
    Frames++;
    Sum += DeltaTime;
    if (DeltaTime > Worst)
        Worst = DeltaTime;

    if (Now - WindowStart >= 5000)
    {
        if (Frames > 0 && Sum > 0.0)
            Log("[fps] %.1f fps average over %d frames (%.1f ms mean, worst frame %.0f ms)", Frames / Sum, Frames, (Sum / Frames) * 1000.0, Worst * 1000.f); //debug
        WindowStart = Now;
        Frames = 0;
        Sum = 0.0;
        Worst = 0.f;
    }
}

void DelMar::PumpGameThread()
{
    LastPumpTick = GetTickCount64();
    InterlockedIncrement(&PumpCount);
    GameThreadId = GetCurrentThreadId();

    std::vector<FTask> Run;
    {
        std::lock_guard<std::mutex> g(QueueLock);
        while (!Queue.empty())
        {
            Run.push_back(std::move(Queue.front()));
            Queue.pop_front();
        }
        auto Now = GetTickCount64();
        for (size_t i = 0; i < Delayed.size();)
        {
            if (Delayed[i].Due <= Now)
            {
                Run.push_back(std::move(Delayed[i]));
                Delayed.erase(Delayed.begin() + i);
            }
            else
                ++i;
        }
    }

    auto Start = GetTickCount64();
    for (size_t i = 0; i < Run.size(); i++)
    {
        auto& T = Run[i];
        if (i > 0 && !T.Done && GetTickCount64() - Start > FConfiguration::DelMarPumpBudgetMs)
        {
            std::lock_guard<std::mutex> g(QueueLock);
            for (size_t k = i; k < Run.size(); k++)
                Queue.push_back(std::move(Run[k]));
            break;
        }
        RunGuarded(&T.Fn);
        if (T.Done)
            SetEvent(T.Done);
    }
}

void DelMar::RunOnGameThread(std::function<void()> Fn)
{
    std::lock_guard<std::mutex> g(QueueLock);
    FTask T;
    T.Id = NextTaskId++;
    T.Fn = std::move(Fn);
    Queue.push_back(std::move(T));
}

void DelMar::RunOnGameThreadAfter(double DelaySeconds, std::function<void()> Fn)
{
    std::lock_guard<std::mutex> g(QueueLock);
    FTask T;
    T.Id = NextTaskId++;
    T.Fn = std::move(Fn);
    T.Due = GetTickCount64() + (uint64)(DelaySeconds * 1000.0);
    Delayed.push_back(std::move(T));
}

bool DelMar::RunOnGameThreadAndWait(std::function<void()> Fn, uint32 TimeoutMs)
{
    if (GetCurrentThreadId() == GameThreadId && GameThreadId != 0)
    {
        RunGuarded(&Fn);
        return true;
    }

    if (!IsGameThreadPumping())
    {
        Log("game thread queue is not being booted (GetMaxTickRate hook missing?) - running task inline on thread %lu", GetCurrentThreadId());
        RunGuarded(&Fn);
        return true;
    }

    HANDLE Done = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    uint64 Id;
    {
        std::lock_guard<std::mutex> g(QueueLock);
        FTask T;
        T.Id = Id = NextTaskId++;
        T.Fn = std::move(Fn);
        T.Done = Done;
        Queue.push_back(std::move(T));
    }

    bool bRan = true;
    if (WaitForSingleObject(Done, TimeoutMs) != WAIT_OBJECT_0)
    {
        bool bStillQueued = false;
        {
            std::lock_guard<std::mutex> g(QueueLock);
            for (auto it = Queue.begin(); it != Queue.end(); ++it)
                if (it->Id == Id)
                {
                    Queue.erase(it);
                    bStillQueued = true;
                    break;
                }
        }
        if (bStillQueued)
        {
            Log("game-thread task %llu timed out after %u ms without starting - dropped", Id, TimeoutMs);
            bRan = false;
        }
        else
        {
            Log("game-thread task %llu is still running after %u ms - waiting for it", Id, TimeoutMs);
            WaitForSingleObject(Done, INFINITE);
        }
    }
    CloseHandle(Done);
    return bRan;
}

//GF plumbing
namespace
{
    const UObject* GFSubsystem()
    {
        return TUObjectArray::FindFirstObject("GameFeaturesSubsystem");
    }

    const UEnum* PluginStateEnum()
    {
        static const UEnum* E = FindEnum("EGameFeaturePluginState");
        return E;
    }

    int64 PluginStateValue(const char* Name)
    {
        auto E = PluginStateEnum();
        return E ? E->GetValue(Name) : -1;
    }

    struct FPluginState
    {
        UEAllocatedString URL;
        UEAllocatedString Name;
        int State = -1;
        UEAllocatedString StateName;
        const UObject* SM = nullptr;
    };

    UEAllocatedString PluginNameFromURL(const UEAllocatedString& URL)
    {
        auto S = URL;
        auto dot = S.rfind(".uplugin");
        if (dot != UEAllocatedString::npos)
            S = S.substr(0, dot);
        auto slash = S.find_last_of("/\\");
        return slash == UEAllocatedString::npos ? S : S.substr(slash + 1);
    }

    bool bStateMapWarned = false;

    // UGameFeaturesSubsystem::GameFeaturePluginStateMachines (TMap<FString, UGameFeaturePluginStateMachine*>) ->
    // UGameFeaturePluginStateMachine::CurrentStateInfo.State
    UEAllocatedVector<FPluginState> ReadPluginStates()
    {
        UEAllocatedVector<FPluginState> Out;
        auto Sub = GFSubsystem();
        if (!Sub)
        {
            if (!bStateMapWarned)
                DelMar::Log("ReadPluginStates: no GameFeaturesSubsystem instance");
            bStateMapWarned = true;
            return Out;
        }

        static auto MapOff = Sub->GetOffset("GameFeaturePluginStateMachines");
        if (MapOff == (uint32)-1)
        {
            if (!bStateMapWarned)
                DelMar::Log("ReadPluginStates: %s has no GameFeaturePluginStateMachines property (renamed?)", DelMar::ClassChain(Sub).c_str());
            bStateMapWarned = true;
            return Out;
        }

        auto& Map = GetFromOffset<TMap<FString, UObject*>>(Sub, MapOff);
        for (auto& Pair : Map)
        {
            auto SM = Pair.Value();
            if (!SM)
                continue;

            static auto InfoOff = SM->GetOffset("CurrentStateInfo");
            FPluginState P;
            P.URL = Pair.Key().ToString();
            P.Name = PluginNameFromURL(P.URL);
            P.State = InfoOff == (uint32)-1 ? -1 : (int)GetFromOffset<uint8>(SM, InfoOff);
            P.StateName = P.State < 0 ? UEAllocatedString("?") : EnumValueName(PluginStateEnum(), P.State);
            P.SM = SM;
            Out.push_back(P);
        }
        return Out;
    }

    bool FindPluginState(const UEAllocatedVector<FPluginState>& States, const char* Name, FPluginState& Out)
    {
        for (auto& S : States)
            if (IEquals(S.Name, Name))
            {
                Out = S;
                return true;
            }
        return false;
    }

    struct FApi
    {
        UFunction* Fn = nullptr;
        const UObject* Target = nullptr;
        UEAllocatedString Desc;
        UEAllocatedVector<FParamInfo> Params;
        int StrIn = -1;
        int EnumIn = -1;
        bool bTakesURL = false;
    };

    FApi ActivateApi;
    FApi UrlByNameApi;
    bool bApiResolved = false;
    UEAllocatedVector<UFunction*> GameFeatureFunctions;
    bool bFunctionIndexBuilt = false;

    void BuildFunctionIndex()
    {
        if (bFunctionIndexBuilt)
            return;
        bFunctionIndexBuilt = true;
        GameFeatureFunctions.clear();

        const int Num = TUObjectArray::Num();
        for (int i = 0; i < Num; i++)
        {
            auto Obj = TUObjectArray::GetObjectByIndex(i);
            if (!Obj || !Obj->Class)
                continue;
            if (!(Obj->Class->GetCastFlags() & CASTCLASS_UFunction))
                continue;
            if (IContains(Obj->Name.ToString(), "GameFeature"))
                GameFeatureFunctions.push_back((UFunction*)Obj);
        }
    }

    bool BuildApi(FApi& Api, UFunction* Fn, bool bNeedsOutString)
    {
        Api = FApi{};
        Api.Fn = Fn;
        Api.Params = GetParams(Fn);

        for (int i = 0; i < (int)Api.Params.size(); i++)
        {
            auto& P = Api.Params[i];
            if (Api.StrIn < 0 && IEquals(P.Type, "StrProperty") && P.IsInput())
                Api.StrIn = i;
            if (Api.EnumIn < 0 && (IEquals(P.Type, "EnumProperty") || IEquals(P.Type, "ByteProperty")) && P.IsInput())
                Api.EnumIn = i;
        }
        if (Api.StrIn < 0)
            return false;

        if (bNeedsOutString)
        {
            bool bHasOut = false;
            for (auto& P : Api.Params)
                if (IEquals(P.Type, "StrProperty") && P.IsOut())
                    bHasOut = true;
            if (!bHasOut)
                return false;
        }

        auto& StrName = Api.Params[Api.StrIn].Name;
        Api.bTakesURL = IContains(StrName, "URL") || IContains(StrName, "Path");

        auto Outer = (const UClass*)Fn->Outer;
        if (!Outer)
            return false;

        if (FunctionFlags(Fn) & FUNC_Static)
            Api.Target = Outer->GetDefaultObj();
        else
        {
            auto Sub = GFSubsystem();
            if (Sub && Sub->IsA(Outer))
                Api.Target = Sub;
            else
            {
                Api.Target = TUObjectArray::FindFirstObject(Outer->Name.ToString().c_str());
                // RRR... native UFUNCTION (e.g. FortCheatManager::LoadAndActivateGameFeaturePlugin,
                if (!Api.Target)
                    Api.Target = Outer->GetDefaultObj();
            }
        }
        Api.Desc = Signature(Fn);
        return Api.Target != nullptr;
    }

    void ResolveActivationApi()
    {
        if (bApiResolved)
            return;
        bApiResolved = true;
        BuildFunctionIndex();

        const char* ActivateNames[] = {
            "LoadAndActivateGameFeaturePlugin", "ActivateGameFeaturePlugin", "LoadGameFeaturePlugin", "ChangeGameFeatureTargetState",
            "SetGameFeaturePluginTargetState", "RequestGameFeaturePluginActivation", "LoadAndActivateGameFeature", "ActivateGameFeature",
            "LoadAndActivateGameFeaturePluginByName", "ActivateGameFeaturePluginByName",
            "LoadAndActivateGameFeaturePluginViaFeatureName",
        };
        for (auto Name : ActivateNames)
        {
            if (ActivateApi.Fn)
                break;
            for (auto Fn : GameFeatureFunctions)
            {
                if (!IEquals(Fn->Name.ToString(), Name))
                    continue;
                FApi Api;
                if (BuildApi(Api, Fn, false))
                {
                    ActivateApi = Api;
                    break;
                }
            }
        }

        const char* UrlNames[] = { "GetPluginURLByName", "GetPluginURLForBuiltInPluginByName", "GetGameFeaturePluginURLByName" };
        for (auto Name : UrlNames)
        {
            if (UrlByNameApi.Fn)
                break;
            for (auto Fn : GameFeatureFunctions)
            {
                if (!IEquals(Fn->Name.ToString(), Name))
                    continue;
                FApi Api;
                if (BuildApi(Api, Fn, true))
                {
                    UrlByNameApi = Api;
                    break;
                }
            }
        }

        if (ActivateApi.Fn)
            DelMar::Log("activation API: %s  [target %s, string param '%s' (%s)%s]", ActivateApi.Desc.c_str(), ObjName(ActivateApi.Target).c_str(),
                        ActivateApi.Params[ActivateApi.StrIn].Name.c_str(), ActivateApi.bTakesURL ? "URL" : "name",
                        ActivateApi.EnumIn >= 0 ? ", has target-state enum param" : "");
        else
            DelMar::Log("!! no reflected GameFeature activation function found - manual activation disabled; relying on the playlist its own native activation. "
                        "Pick a candidate from the 'fn' list above (or the Dumper-7 SDK) and add it to ActivateNames in DelMar.cpp, or add a native finder anyway");

        if (UrlByNameApi.Fn)
            DelMar::Log("plugin-URL API: %s", UrlByNameApi.Desc.c_str());
    }

    struct FCallResult
    {
        bool bCalled = false;
        bool bRet = false;
        UEAllocatedString OutStr;
    };

    FCallResult CallStringFn(const FApi& Api, const wchar_t* InStr, int64 EnumValue)
    {
        FCallResult R;
        if (!Api.Fn || !Api.Target || Api.StrIn < 0)
            return R;

        auto Size = Api.Fn->GetPropertiesSize();
        auto Buf = (uint8*)FMemory::Malloc(Size + 16);
        memset(Buf, 0, Size + 16);

        FString S(InStr);
        memcpy(Buf + Api.Params[Api.StrIn].Offset, &S, sizeof(FString));

        if (Api.EnumIn >= 0 && EnumValue >= 0)
        {
            auto& E = Api.Params[Api.EnumIn];
            if (E.Size == 1)
                Buf[E.Offset] = (uint8)EnumValue;
            else if (E.Size == 4)
                *(int32*)(Buf + E.Offset) = (int32)EnumValue;
            else
                *(int64*)(Buf + E.Offset) = EnumValue;
        }

        Api.Target->ProcessEvent(Api.Fn, Buf);
        R.bCalled = true;

        for (auto& P : Api.Params)
        {
            if (P.IsReturn())
            {
                if (P.Size == 1)
                    R.bRet = Buf[P.Offset] != 0;
                else if (P.Size == 4)
                    R.bRet = *(int32*)(Buf + P.Offset) != 0;
                else if (P.Size == 8)
                    R.bRet = *(int64*)(Buf + P.Offset) != 0;
            }
            else if (P.IsOut() && IEquals(P.Type, "StrProperty"))
            {
                auto& Out = *(FString*)(Buf + P.Offset);
                if (Out.Data)
                {
                    R.OutStr = Out.ToString();
                    Out.Free();
                }
            }
        }

        S.Free();
        FMemory::Free(Buf);
        return R;
    }

    UEAllocatedString ResolvePluginURL(const wchar_t* Name, const wchar_t* RelDir)
    {
        auto NameA = Narrow(Name);

        if (UrlByNameApi.Fn)
        {
            auto R = CallStringFn(UrlByNameApi, Name, -1);
            if (R.bCalled && !R.OutStr.empty())
                return R.OutStr;
        }

        for (auto& P : ReadPluginStates())
            if (IEquals(P.Name, NameA.c_str()))
                return P.URL;

        return UEAllocatedString("file:../../../FortniteGame/Plugins/GameFeatures/") + Narrow(RelDir) + "/" + NameA + ".uplugin";
    }

    int64 ActiveTargetState()
    {
        static int64 V = -2;
        if (V == -2)
        {
            auto E = FindEnum("EGameFeatureTargetState");
            V = E ? E->GetValue("Active") : -1;
            if (V < 0)
                V = 3;
        }
        return V;
    }
}

void DelMar::DumpPluginStates(const char* Tag)
{
    auto States = ReadPluginStates();
    if (States.empty())
    {
        Log("[%s] plugin states unavailable by reflection - issuing ListGameFeaturePlugins (output lands in FortniteGame.log)", Tag);
        UKismetSystemLibrary::ExecuteConsoleCommand(UWorld::GetWorld(), FString(L"ListGameFeaturePlugins"), nullptr);
        return;
    }

    bool bAnyReadable = false;
    for (auto& P : States)
        if (P.State >= 0)
        {
            bAnyReadable = true;
            break;
        }
    if (!bAnyReadable)
    {
        Log("[%s] %d GFPs known; states unreadable by reflection on 30.40 - FortniteGame.log's 'transitioned successfully' means therefore lines have them", Tag, (int)States.size());
        return;
    }

    std::map<int, int> Hist;
    int DelMarCount = 0;
    for (auto& P : States)
    {
        Hist[P.State]++;
        if (!IContains(P.Name, "DelMar") && !IContains(P.URL, "/DelMar/"))
            continue;
        DelMarCount++;
        Log("[%s]   %-34s %-22s %s", Tag, P.Name.c_str(), P.StateName.c_str(), P.URL.c_str());
    }

    UEAllocatedString H;
    for (auto& [State, Count] : Hist)
    {
        char b[64];
        sprintf_s(b, "%s=%d ", EnumValueName(PluginStateEnum(), State).c_str(), Count);
        H += b;
    }
    Log("[%s] %d GFPs known, %d DelMar (listed); states: %s", Tag, (int)States.size(), DelMarCount, H.c_str());
}

bool DelMar::ActivatePlugin(const wchar_t* Name, const wchar_t* RelDir)
{
    ResolveActivationApi();

    auto NameA = Narrow(Name);
    FPluginState Before;
    bool bKnown = FindPluginState(ReadPluginStates(), NameA.c_str(), Before);
    Log("activate %-34s state before: %s", NameA.c_str(), bKnown ? Before.StateName.c_str() : "<no state machine yet>");

    if (bKnown && Before.State >= 0 && Before.State == PluginStateValue("Active"))
        return true;
    if (!ActivateApi.Fn)
        return false;

    auto URL = ResolvePluginURL(Name, RelDir);
    auto Arg = ActivateApi.bTakesURL ? URL : NameA;
    auto ArgW = Widen(Arg);
    auto R = CallStringFn(ActivateApi, ArgW.c_str(), ActiveTargetState());
    Log("  -> %s('%s') called=%d ret=%d", ActivateApi.Fn->Name.ToString().c_str(), Arg.c_str(), R.bCalled, R.bRet);
    return R.bCalled;
}

void DelMar::ActivateCorePlugins()
{
    Log("enabliong %d DelMar core plugins", (int)std::size(FConfiguration::DelMarCorePlugins));
    for (auto& P : FConfiguration::DelMarCorePlugins)
        ActivatePlugin(P.Name, P.RelDir);
}

void DelMar::ActivateTrackPlugins()
{
    Log("enabliing %d DelMar track plugins", (int)std::size(FConfiguration::DelMarTrackPlugins));
    for (auto& P : FConfiguration::DelMarTrackPlugins)
        ActivatePlugin(P.Name, P.RelDir);
}

bool DelMar::WaitForCorePlugins(uint32 TimeoutMs)
{
    auto Registered = PluginStateValue("Registered");
    if (Registered < 0)
    {
        Log("EGameFeaturePluginState::Registered unknown - cannot wait for plugin states, continuing");
        return false;
    }

    auto Start = GetTickCount64();
    int LastPending = -1;
    while (GetTickCount64() - Start < TimeoutMs)
    {
        auto Shared = std::make_shared<std::pair<int, UEAllocatedString>>(0, UEAllocatedString());
        bool bRan = RunOnGameThreadAndWait([Shared, Registered]
        {
            auto States = ReadPluginStates();
            for (auto& P : FConfiguration::DelMarCorePlugins)
            {
                FPluginState S;
                auto NameA = Narrow(P.Name);
                if (!FindPluginState(States, NameA.c_str(), S) || S.State < Registered)
                {
                    Shared->first++;
                    Shared->second += NameA + "(" + (S.SM ? S.StateName : UEAllocatedString("-")) + ") ";
                }
            }
        }, 10000);

        if (!bRan)
            return false;

        if (Shared->first == 0)
        {
            Log("all core plugins are >= Registered (content mounted) after %llu ms", GetTickCount64() - Start);
            RunOnGameThreadAndWait([] { DumpPluginStates("core-ready"); }, 10000);
            return true;
        }
        if (Shared->first != LastPending)
            Log("waiting for %d core plugins to mount: %s", Shared->first, Shared->second.c_str());
        LastPending = Shared->first;
        Sleep(500);
    }

    Log("timed out after %u ms waiting for core plugins - opening the level anyway", TimeoutMs);
    RunOnGameThreadAndWait([] { DumpPluginStates("core-timeout"); }, 10000);
    return false;
}

// boot sequence
//lobby signal sdk
#pragma comment(lib, "user32.lib")
namespace
{
    const char* const LobbyNeedle = "Load map complete /Game/Maps/Frontend";

    void EngineLogPath(char* Out, size_t N)
    {
        char Base[MAX_PATH] = {};
        Out[0] = 0;
        if (GetEnvironmentVariableA("LOCALAPPDATA", Base, MAX_PATH) == 0)
            return;
        _snprintf_s(Out, N, _TRUNCATE, "%s\\FortniteGame\\Saved\\Logs\\FortniteGame.log", Base);
    }

    uint64 FileTimeToU64(const FILETIME& F)
    {
        ULARGE_INTEGER U;
        U.LowPart = F.dwLowDateTime;
        U.HighPart = F.dwHighDateTime;
        return U.QuadPart;
    }

    uint64 ProcessStartTime()
    {
        FILETIME Cr = {}, Ex = {}, K = {}, U = {};
        GetProcessTimes(GetCurrentProcess(), &Cr, &Ex, &K, &U);
        return FileTimeToU64(Cr);
    }

    uint64 ParseLogOpenHeader(const char* Head)
    {
        auto P = strstr(Head, "Log file open, ");
        if (!P)
            return 0;
        int M = 0, D = 0, Y = 0, h = 0, m = 0, s = 0;
        if (sscanf_s(P + 15, "%d/%d/%d %d:%d:%d", &M, &D, &Y, &h, &m, &s) != 6)
            return 0;
        SYSTEMTIME L = {};
        L.wYear = (WORD)(Y < 100 ? 2000 + Y : Y);
        L.wMonth = (WORD)M;
        L.wDay = (WORD)D;
        L.wHour = (WORD)h;
        L.wMinute = (WORD)m;
        L.wSecond = (WORD)s;
        SYSTEMTIME Utc = {};
        FILETIME F = {};
        if (!TzSpecificLocalTimeToSystemTime(nullptr, &L, &Utc) || !SystemTimeToFileTime(&Utc, &F))
            return 0;
        return FileTimeToU64(F);
    }

    bool OurWindowExists()
    {
        struct FCtx { DWORD Pid; bool bFound; } C = { GetCurrentProcessId(), false };
        EnumWindows([](HWND H, LPARAM L) -> BOOL
        {
            auto& C = *(FCtx*)L;
            DWORD Pid = 0;
            GetWindowThreadProcessId(H, &Pid);
            if (Pid != C.Pid || !IsWindowVisible(H))
                return TRUE;
            char Cls[64] = {};
            GetClassNameA(H, Cls, 63);
            if (strcmp(Cls, "UnrealWindow") == 0)
            {
                C.bFound = true;
                return FALSE;
            }
            return TRUE;
        }, (LPARAM)&C);
        return C.bFound;
    }

    int ScanEngineLog(const char* Path, uint64& Offset, bool& bStale)
    {
        bStale = false;
        HANDLE F = CreateFileA(Path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
        if (F == INVALID_HANDLE_VALUE)
            return -1;
        char Head[128] = {};
        DWORD Got = 0;
        if (ReadFile(F, Head, 127, &Got, nullptr) && Got > 0)
        {
            Head[Got] = 0;
            auto Opened = ParseLogOpenHeader(Head);
            auto Started = ProcessStartTime();
            if (Opened && Started && Opened + 15ull * 10000000ull < Started)
            {
                bStale = true;
                CloseHandle(F);
                return 0;
            }
        }
        LARGE_INTEGER Size = {};
        GetFileSizeEx(F, &Size);
        if ((uint64)Size.QuadPart < Offset)
            Offset = 0;
        const size_t Keep = strlen(LobbyNeedle) - 1;
        static char Buf[64 * 1024 + 128];
        size_t Carry = 0;
        int Result = 0;
        LARGE_INTEGER Pos;
        Pos.QuadPart = (LONGLONG)(Offset > Keep ? Offset - Keep : 0);
        SetFilePointerEx(F, Pos, nullptr, FILE_BEGIN);
        for (;;)
        {
            DWORD Read = 0;
            if (!ReadFile(F, Buf + Carry, 64 * 1024, &Read, nullptr) || Read == 0)
                break;
            Buf[Carry + Read] = 0;
            for (size_t i = 0; i < Carry + Read; i++)
                if (!Buf[i])
                    Buf[i] = ' ';
            if (strstr(Buf, LobbyNeedle))
            {
                Result = 1;
                break;
            }
            size_t Total = Carry + Read;
            Carry = Total > Keep ? Keep : Total;
            memmove(Buf, Buf + Total - Carry, Carry);
        }
        Offset = (uint64)Size.QuadPart;
        CloseHandle(F);
        return Result;
    }
}

bool DelMar::WaitForLobbySignal(uint32 TimeoutMs)
{
    char Path[MAX_PATH];
    EngineLogPath(Path, MAX_PATH);
    auto Start = GetTickCount64();
    uint64 Offset = 0;
    int Unreadable = 0;
    bool bSaidStale = false, bSaidUnreadable = false;
    Log("lobby-wait: watching %s for '%s' (up to %u s)", Path[0] ? Path : "<no LOCALAPPDATA>", LobbyNeedle, TimeoutMs / 1000);
    while (GetTickCount64() - Start < TimeoutMs)
    {
        bool bStale = false;
        int r = Path[0] ? ScanEngineLog(Path, Offset, bStale) : -1;
        if (bStale)
        {
            if (!bSaidStale)
                Log("lobby-wait: the engine log belongs to an earlier client - waiting for this process to open its own");
            bSaidStale = true;
            Offset = 0;
        }
        else if (r == 1)
        {
            Log("lobby-wait: the engine log reports the frontend map loaded (%llu ms after attach) - continuing", GetTickCount64() - Start);
            return true;
        }
        else if (r == -1)
        {
            Unreadable++;
            if (!bSaidUnreadable && Unreadable >= 10)
            {
                Log("lobby-wait: cannot open the engine log (error %lu) - falling back to window + process age", GetLastError());
                bSaidUnreadable = true;
            }
            uint64 AgeMs = 0;
            {
                FILETIME Now = {};
                GetSystemTimeAsFileTime(&Now);
                auto St = ProcessStartTime();
                AgeMs = St ? (FileTimeToU64(Now) - St) / 10000ull : 0;
            }
            if (Unreadable >= 10 && OurWindowExists() && AgeMs > 90000)
            {
                Log("lobby-wait: UnrealWindow exists and the process is %llu s old - continuing without the log", AgeMs / 1000);
                return true;
            }
        }
        Sleep(500);
    }
    Log("lobby-wait: no frontend signal within %u s", TimeoutMs / 1000);
    return false;
}

bool DelMar::WaitForFrontend(uint32 TimeoutMs)
{
    auto Start = GetTickCount64();
    UEAllocatedString LastMode = "";
    while (GetTickCount64() - Start < TimeoutMs)
    {
        auto Engine = UEngine::GetEngine();
        if (Engine && Engine->HasGameViewport() && Engine->GameViewport && Engine->GameViewport->HasWorld() && Engine->GameViewport->World)
        {
            auto World = (UWorld*)Engine->GameViewport->World;
            auto Mode = World->HasAuthorityGameMode() ? World->AuthorityGameMode : nullptr;
            auto ModeName = Mode ? ObjName(Mode->Class) : UEAllocatedString("none");
            if (ModeName != LastMode)
            {
                Log("world %s, game mode %s", ObjName(World).c_str(), ModeName.c_str());
                LastMode = ModeName;
            }
            if (Mode && IContains(ModeName, "Front"))
            {
                Log("frontend lobby is up after %llu ms - settling 3 s", GetTickCount64() - Start);
                Sleep(3000);
                return true;
            }
        }
        Sleep(500);
    }
    return false;
}

namespace
{
    struct FFinderRow
    {
        const char* Name;
        uint64 (*Fn)();
        bool bCritical;
    };

    uint64 SafeCall(uint64 (*Fn)())
    {
        ++g_DelMarGuardDepth;
        uint64 r;
        __try
        {
            r = Fn();
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            r = ~0ull;
        }
        --g_DelMarGuardDepth;
        return r;
    }
}

bool DelMar::Preflight()
{
    bool bOk = true;
    Log("preflight: FN %.2f  UE %.1f  ImageBase 0x%llx  GObjects %d  playlist %ls  map %ls  port %d  iris %d", VersionInfo.FortniteVersion, VersionInfo.EngineVersion, ImageBase,
        TUObjectArray::Num(), FConfiguration::Playlist, FConfiguration::DelMarMap, FConfiguration::Port, (int)FConfiguration::bEnableIris);

    auto Row = [&](const char* Name, uint64 V, bool bCritical)
    {
        Log("  %-40s %s%s", Name, Addr(V).c_str(), (!V && bCritical) ? "   <-- CRITICAL" : "");
        if (!V && bCritical)
            bOk = false;
    };

    Log("SDK offsets:");
    Row("Offsets::Realloc", Offsets::Realloc, true);
    Row("Offsets::AppendString", Offsets::AppendString, false);
    Row("Offsets::ToString", Offsets::ToString, false);
    if (!Offsets::AppendString && !Offsets::ToString)
    {
        Log("  neither AppendString nor ToString found   <-- CRITICAL");
        bOk = false;
    }
    Row("Offsets::GObjectsChunked", Offsets::GObjectsChunked, true);
    Row("Offsets::Step", Offsets::Step, true);
    Row("Offsets::StepExplicitProperty", Offsets::StepExplicitProperty, false);
    Row("Offsets::GetInterfaceAddress", Offsets::GetInterfaceAddress, false);
    Row("Offsets::StaticFindObject", Offsets::StaticFindObject, true);
    Row("Offsets::StaticLoadObject", Offsets::StaticLoadObject, true);
    Row("Offsets::FNameConstructor", Offsets::FNameConstructor, true);
    Row("Offsets::SpawnActor", Offsets::SpawnActor, true);
    Log("  %-40s %llu%s", "Offsets::ProcessEventVft (index)", Offsets::ProcessEventVft, Offsets::ProcessEventVft ? "" : "   <-- CRITICAL");
    if (!Offsets::ProcessEventVft)
        bOk = false;

    {
        FName T(L"GameNetDriver");
        auto S = T.ToString();
        bool bRound = S == "GameNetDriver";
        Log("  %-40s '%s' %s", "FName round trip", S.c_str(), bRound ? "OK" : "FAIL   <-- CRITICAL");
        if (!bRound)
            bOk = false;
    }

    Log("classes:");
    auto Cls = [&](const char* Name, const void* C, bool bCritical)
    {
        Log("  %-40s %s%s", Name, C ? "found" : "MISSING", (!C && bCritical) ? "   <-- CRITICAL" : "");
        if (!C && bCritical)
            bOk = false;
    };
    Cls("Engine (FortEngine instance)", UEngine::GetEngine(), true);
    Cls("World", UWorld::GetWorld(), true);
    Cls("FortGameMode", AFortGameMode::StaticClass(), true);
    Cls("FortGameModeAthena", AFortGameModeAthena::StaticClass(), true);
    Cls("FortGameStateAthena", AFortGameStateAthena::StaticClass(), true);
    Cls("FortPlayerControllerAthena", AFortPlayerControllerAthena::StaticClass(), true);
    Cls("FortPlaylistAthena", UFortPlaylistAthena::StaticClass(), true);
    Cls("FortGameModeFrontend", FindClass("FortGameModeFrontend"), false);
    Cls("GameFeaturesSubsystem (class)", FindClass("GameFeaturesSubsystem"), false);
    Cls("GameFeaturesSubsystem (instance)", GFSubsystem(), false);
    Cls("EGameFeaturePluginState (enum)", FindEnum("EGameFeaturePluginState"), false);
    Cls("EGameFeatureTargetState (enum)", FindEnum("EGameFeatureTargetState"), false);
    Cls("net.Iris.UseIrisReplication (cvar)", FindCVar<uint32_t>(L"net.Iris.UseIrisReplication"), false);

    Log("finders (Erbium patches with these):");
    const bool bIris = VersionInfo.EngineVersion >= 5.3 && FConfiguration::bEnableIris;
    FFinderRow Rows[] = {
        { "GIsClient", FindGIsClient, true },
        { "GIsServer", FindGIsServer, true },
        { "GetWorldContext", FindGetWorldContext, true },
        { "CreateNetDriverWorldContext", FindCreateNetDriverWorldContext, true },
        { "InitListen", FindInitListen, true },
        { "SetWorld", FindSetWorld, true },
        { "TickFlush", FindTickFlush, true },
        { "GetMaxTickRate (pumps our queue)", FindGetMaxTickRate, true },
        { "UpdateIrisReplicationViews", FindUpdateIrisReplicationViews, bIris },
        { "PreSendUpdate", FindPreSendUpdate, bIris },
        { "SendClientAdjustment", FindSendClientAdjustment, false },
        { "SendRequestNow", FindSendRequestNow, false },
        { "EncryptionPatch", FindEncryptionPatch, false },
        { "GameSessionPatch", FindGameSessionPatch, false },
        { "KickPlayer (ret-true)", FindKickPlayer, false },
        { "FinishWorldInitialization", FindFinishWorldInitialization, false },
        { "PickTeam", FindPickTeam, false },
        { "GetPlayerViewPoint", FindGetPlayerViewPoint, false },
        { "EnterAircraft", FindEnterAircraft, false },
        { "HandleMatchHasStarted", FindHandleMatchHasStarted, false },
        { "ApplyCharacterCustomization", FindApplyCharacterCustomization, false },
        { "NotifyGameMemberAdded", FindNotifyGameMemberAdded, false },
        { "StartStreamingAdditionalPlaylistLevel", FindStartStreamingAdditionalPlaylistLevel, false },
        { "GiveAbility", FindGiveAbility, false },
        { "ConstructAbilitySpec", FindConstructAbilitySpec, false },
        { "InternalTryActivateAbility", FindInternalTryActivateAbility, false },
        { "FinishedTargetSpline", FindFinishedTargetSpline, false },
        { "RemoveFromAlivePlayers", FindRemoveFromAlivePlayers, false },
        { "GiveAbilityAndActivateOnce", FindGiveAbilityAndActivateOnce, false },
        { "ActivatePhase (events)", FindActivatePhase, false },
        { "SetGamePhase", FindSetGamePhase, false },
        { "Reset (GamePhaseLogic)", FindReset, false },
    };
    for (auto& R : Rows)
    {
        auto V = SafeCall(R.Fn);
        if (V == ~0ull)
        {
            Log("  %-40s CRASHED inside the finder%s", R.Name, R.bCritical ? "   <-- CRITICAL" : "");
            if (R.bCritical)
                bOk = false;
            continue;
        }
        Row(R.Name, V, R.bCritical);
    }

    {
        const char* Pats[] = {
            "48 89 5C 24 ? 55 56 57 41 54 41 55 41 56 41 57 48 81 EC ? ? ? ? 4C 8B C1",
            "48 89 5C 24 ? 55 56 57 41 54 41 55 41 56 41 57 48 81 EC ? ? ? ? 48 8B D1",
            "48 89 5C 24 ? 55 56 57 41 54 41 55 41 56 41 57 48 81 EC ? ? ? ? 4C 8B D1",
            "48 89 5C 24 ? 48 89 6C 24 ? 48 89 74 24 ? 57 41 54 41 55 41 56 41 57 48 81 EC ? ? ? ? 4C 8B D1",
        };
        uint64 A = 0;
        int Which = -1;
        for (int i = 0; i < 4 && !A; i++)
        {
            A = Memcury::Scanner::FindPattern(Pats[i], false).Get();
            if (A)
                Which = i;
        }
        char Name[64];
        sprintf_s(Name, "AttemptDeriveFromURL (NetMode, pattern %d)", Which);
        Row(Name, A, true);
    }

    Log("preflight %s", bOk ? "OK" : "FAILED - refusing to patch; the client keeps running as a normal client");
    return bOk;
}

void DelMar::DiscoverGameFeatureApi()
{
    auto t0 = GetTickCount64();
    UEAllocatedVector<UEAllocatedString> GFClasses, DelMarClasses;
    bFunctionIndexBuilt = false;
    GameFeatureFunctions.clear();

    const int Num = TUObjectArray::Num();
    for (int i = 0; i < Num; i++)
    {
        auto Obj = TUObjectArray::GetObjectByIndex(i);
        if (!Obj || !Obj->Class)
            continue;
        auto Cast = Obj->Class->GetCastFlags();
        if (Cast & CASTCLASS_UFunction)
        {
            if (IContains(Obj->Name.ToString(), "GameFeature"))
                GameFeatureFunctions.push_back((UFunction*)Obj);
        }
        else if (Cast & CASTCLASS_UClass)
        {
            auto N = Obj->Name.ToString();
            if (IContains(N, "GameFeature"))
                GFClasses.push_back(N);
            else if (IContains(N, "DelMar"))
                DelMarClasses.push_back(N);
        }
    }
    bFunctionIndexBuilt = true;

    Log("discovery: scanned %d objects in %llu ms - %d *GameFeature* classes, %d *GameFeature* functions, %d *DelMar* classes", Num, GetTickCount64() - t0, (int)GFClasses.size(),
        (int)GameFeatureFunctions.size(), (int)DelMarClasses.size());
    for (auto& C : GFClasses)
        Log("  class %s", C.c_str());
    for (auto Fn : GameFeatureFunctions)
        Log("  fn    %s", Signature(Fn).c_str());

    const int MaxDelMar = 500;
    for (int i = 0; i < (int)DelMarClasses.size() && i < MaxDelMar; i++)
        Log("  delmar-class %s", DelMarClasses[i].c_str());
    if ((int)DelMarClasses.size() > MaxDelMar)
        Log("  ... %d more DelMar classes not listed", (int)DelMarClasses.size() - MaxDelMar);

    auto Sub = GFSubsystem();
    Log("GameFeaturesSubsystem instance %p : %s", Sub, Sub ? ClassChain(Sub).c_str() : "-");
    if (Sub)
        for (const UStruct* C = Sub->Class; C; C = C->GetSuper())
            for (auto F = C->GetChildren(); F; F = F->GetNext())
                if (F->Class && (F->Class->GetCastFlags() & CASTCLASS_UFunction))
                    Log("  subsystem fn %s", Signature((const UFunction*)F).c_str());

    ResolveActivationApi();
    DumpPluginStates("boot");
}

const wchar_t* DelMar::MapOpenCommand()
{
    static wchar_t Cmd[512];
    swprintf_s(Cmd, L"open %s", FConfiguration::DelMarMap);
    return Cmd;
}

void DelMar::OnMapOpenIssued()
{
    Log("map open issued: %ls", MapOpenCommand());
    RunOnGameThreadAfter(20.0, [] { DumpWorldSnapshot("map+20s"); });
    RunOnGameThreadAfter(60.0, [] { DumpWorldSnapshot("map+60s"); });
    RunOnGameThreadAfter(150.0, [] { DumpWorldSnapshot("map+150s"); });
}

// listen server
namespace
{
    bool PtrLooksBad(const void* p)
    {
        auto v = (uintptr_t)p;
        return p == nullptr || v < 0x10000 || v == ~0ull || IsBadReadPtr((void*)p);
    }

    void* DoSetupListenServer(bool* bListening)
    {
        *bListening = false;

        auto World = UWorld::GetWorld();
        auto Engine = UEngine::GetEngine();
        DelMar::Log("listen setup: World %p Engine %p GameMode %p", World, Engine, (World && World->HasAuthorityGameMode()) ? World->AuthorityGameMode : nullptr);
        if (PtrLooksBad(World) || PtrLooksBad(Engine))
        {
            DelMar::Log("  World/Engine invalid - skipping listen server");
            return nullptr;
        }

        auto GetWorldCtxFn = FindGetWorldContext();
        auto CreateNDFn = FindCreateNetDriverWorldContext();
        DelMar::Log("  GetWorldContext=%s CreateNetDriverWorldContext=%s", Addr(GetWorldCtxFn).c_str(), Addr(CreateNDFn).c_str());
        if (!GetWorldCtxFn || !CreateNDFn)
        {
            DelMar::Log("  a net-driver finder is missing - skipping listen server");
            return nullptr;
        }

        FName NetDriverName(L"GameNetDriver");

        void* WorldCtx = ((void* (*)(UEngine*, UWorld*))GetWorldCtxFn)(Engine, World);
        DelMar::Log("  WorldCtx %p", WorldCtx);
        if (PtrLooksBad(WorldCtx))
        {
            DelMar::Log("  world context invalid (finder mismatch for 30.40, or world not ready) - skipping listen server");
            return nullptr;
        }

        auto NetDriver = ((UNetDriver * (*)(UEngine*, void*, FName, int)) CreateNDFn)(Engine, WorldCtx, NetDriverName, 0);
        DelMar::Log("  CreateNetDriver -> %p", NetDriver);
        if (PtrLooksBad(NetDriver))
        {
            DelMar::Log("  net driver creation failed - skipping listen server Line:1531");
            return nullptr;
        }

        World->NetDriver = NetDriver;
        if (VersionInfo.FortniteVersion >= 20)
            NetDriver->NetServerMaxTickRate = 30;
        NetDriver->NetDriverName.ComparisonIndex = NetDriverName.ComparisonIndex;
        NetDriver->World = World;
        if (VersionInfo.EngineVersion >= 5.3 && FConfiguration::bEnableIris)
            *(bool*)(__int64(&NetDriver->ReplicationDriver) + 0x11) = true;

        for (int i = 0; i < World->LevelCollections.Num(); i++)
            World->LevelCollections.Get(i, FLevelCollection::Size()).NetDriver = NetDriver;

        auto InitListen = (bool (*)(UNetDriver*, UWorld*, FURL*, bool, FString&))FindInitListen();
        auto SetWorld = (void (*)(UNetDriver*, UWorld*))FindSetWorld();
        if (!InitListen || !SetWorld)
        {
            DelMar::Log("  InitListen/SetWorld finder missing - net driver created but not listening");
            return NetDriver;
        }

        auto URL = (FURL*)malloc(FURL::Size());
        memset((PBYTE)URL, 0, FURL::Size());
        URL->Port = FConfiguration::Port;

        SetWorld(NetDriver, World);
        FString Err;
        bool ok = InitListen(NetDriver, World, URL, false, Err);
        if (ok)
            SetWorld(NetDriver, World);
        else if (World && !IsBadReadPtr((const uint8*)World + 0x38, sizeof(void*)))
        {
            *(void**)((uint8*)World + 0x38) = nullptr;
            DelMar::Log("[listen] InitListen failed - detached the open driver from World->NetDriver");
        }
        free(URL);

        *bListening = ok;
        return NetDriver;
    }
}

void* DelMar::SetupListenServer(bool* bListening)
{
    if (bListening)
        *bListening = false;
    if (!FConfiguration::bStartListenServer)
    {
        Log("listen server disabled (bStartListenServer=false) - standalone bringup; map/mode/track still load, but no client can join and no player spawns until this is on or standalone is used");
        return nullptr;
    }
    bool local = false;
    void* result = nullptr;
    ++g_DelMarGuardDepth;
    __try
    {
        result = DoSetupListenServer(&local);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        DelMar::Log("!! listenserver setup FAULTED (exception 0x%08x) - continuing without a listen server; therefore alone on the track because no friends", GetExceptionCode());
        result = nullptr;
        local = false;
    }
    --g_DelMarGuardDepth;
    if (bListening)
        *bListening = local;
    return result;
}

//in match notifications
//PlayspaceManagerCached + OnRep_RootPlayspace);
//   S3 still in Setup -> DelMarStateMachine::RequestState(DelMar.Game.State.Loading);
//RequestLevelLoad(map tag);
namespace
{
    //helpers
    std::unordered_map<const void*, UEAllocatedString> ClassNameCache;

    const UEAllocatedString& CachedName(const UStruct* C)
    {
        auto it = ClassNameCache.find(C);
        if (it != ClassNameCache.end())
            return it->second;
        return ClassNameCache.emplace(C, C->Name.ToString()).first->second;
    }

    bool ChainContains(const UStruct* Class, const char* Needle)
    {
        for (const UStruct* C = Class; C; C = C->GetSuper())
            if (IContains(CachedName(C), Needle))
                return true;
        return false;
    }

    bool ChainHasExact(const UStruct* Class, const char* Name)
    {
        for (const UStruct* C = Class; C; C = C->GetSuper())
            if (IEquals(CachedName(C), Name))
                return true;
        return false;
    }

    bool MatchesAnyFilter(const UEAllocatedString& S, const char* Filters)
    {
        if (!Filters || !*Filters)
            return true;
        UEAllocatedString F(Filters);
        size_t Start = 0;
        for (;;)
        {
            auto Bar = F.find('|', Start);
            auto Tok = F.substr(Start, Bar == UEAllocatedString::npos ? UEAllocatedString::npos : Bar - Start);
            if (!Tok.empty() && IContains(S, Tok.c_str()))
                return true;
            if (Bar == UEAllocatedString::npos)
                return false;
            Start = Bar + 1;
        }
    }

    UEAllocatedString OuterChain(const UObject* Obj)
    {
        UEAllocatedString S;
        int Guard = 0;
        for (auto O = Obj ? Obj->Outer : nullptr; O && !IsBadReadPtr(O, 0x30) && Guard < 12; O = O->Outer, Guard++)
        {
            if (!S.empty())
                S += " <- ";
            S += ObjName(O);
        }
        return S.empty() ? UEAllocatedString("-") : S;
    }

    UEAllocatedString ObjRef(const UObject* O)
    {
        if (!O)
            return "null";
        if (IsBadReadPtr(O, 0x30) || !O->Class || IsBadReadPtr(O->Class, 0x30))
        {
            char b[40];
            _snprintf_s(b, sizeof(b), _TRUNCATE, "<bad %p>", O);
            return b;
        }
        return ObjName(O) + " (" + CachedName(O->Class) + ")";
    }

    bool IsTemplateObject(const UObject* O)
    {
        for (int g = 0; O && !IsBadReadPtr(O, 0x30) && g < 16; O = O->Outer, g++)
            if (O->ObjectFlags & 0x30)
                return true;
        return false;
    }

    bool IsInLiveWorld(const UObject* O)
    {
        auto World = (const UObject*)UWorld::GetWorld();
        for (int g = 0; O && World && !IsBadReadPtr(O, 0x30) && g < 16; O = O->Outer, g++)
            if (O == World)
                return true;
        return false;
    }

    const char* const BaseClassNames[] = { "Object", "Actor", "ActorComponent", "SceneComponent", "Info", "GameStateBase", "GameModeBase", "GameState", "GameMode", "Pawn", "Character", "Controller",
                                           "PlayerController", "BlueprintFunctionLibrary", "Subsystem", "WorldSubsystem", "GameInstanceSubsystem", "PrimaryDataAsset", "DataAsset" };

    bool IsBaseClassName(const UEAllocatedString& N)
    {
        for (auto B : BaseClassNames)
            if (N == B)
                return true;
        return false;
    }

    // GameplayTagContainer), AFortGameStateAthena::GamePhase (enum).
    uint32 PropSubOffset()
    {
        static uint32 Off = 0;
        if (Off)
            return Off;
        const UField* Arr = nullptr;
        if (auto W = UWorld::GetWorld())
            Arr = W->GetProperty("StreamingLevels");
        if (Arr)
            for (uint32 Cand : { 0x68u, 0x70u, 0x78u, 0x60u, 0x80u })
            {
                auto Inner = GetFromOffset<const UField*>(Arr, Cand);
                if (!Inner || IsBadReadPtr(Inner, 0x48))
                    continue;
                if (Inner->FField_GetName() == Arr->FField_GetName())
                {
                    Off = Cand;
                    break;
                }
            }
        if (Off)
            DelMar::Log("reflection: FArrayProperty::Inner at 0x%x (tried on UWorld::StreamingLevels)", Off);
        else
        {
            Off = 0x70;
            DelMar::Log("reflection: array inner probe %s assuming 0x70", Arr ? "matched no candidate" : "found no StreamingLevels property");
        }
        return Off;
    }

    bool ReadableObject(const UObject* P)
    {
        return P && !IsBadReadPtr(P, 0x30) && P->Class && !IsBadReadPtr(P->Class, 0x30);
    }

    uint32 StructSubOffset()
    {
        static uint32 Off = 0;
        if (Off)
            return Off;
        const UField* P = nullptr;
        if (auto C = FindClass("FortPlaylistAthena"))
            P = C->GetProperty("GameplayTagContainer");
        if (P)
            for (uint32 Cand : { PropSubOffset(), 0x68u, 0x70u, 0x78u, 0x80u, 0x60u, 0x88u })
            {
                auto S = GetFromOffset<const UObject*>(P, Cand);
                if (ReadableObject(S) && ObjName(S) == "GameplayTagContainer")
                {
                    Off = Cand;
                    break;
                }
            }
        if (Off)
            DelMar::Log("reflection: FStructProperty::Struct at 0x%x (tried on FortPlaylistAthena::GameplayTagContainer)", Off);
        else
        {
            Off = PropSubOffset();
            DelMar::Log("reflection: struct probe %s - assuming 0x%x (struct names will be missing anyway) so", P ? "matched no candidate" : "found no GameplayTagContainer property", Off);
        }
        return Off;
    }

    uint32 EnumSubOffset(bool bEnumProp)
    {
        static uint32 OffEnum = 0, OffByte = 0;
        uint32& Off = bEnumProp ? OffEnum : OffByte;
        if (Off)
            return Off;
        const UField* P = nullptr;
        if (auto C = FindClass("FortGameStateAthena"))
            P = C->GetProperty("GamePhase");
        bool bProbeKind = P && ((FieldTypeName(P) == "EnumProperty") == bEnumProp);
        if (bProbeKind)
            for (uint32 Cand : { PropSubOffset() + (bEnumProp ? 8u : 0u), PropSubOffset(), PropSubOffset() + 8u, 0x68u, 0x70u, 0x78u, 0x80u })
            {
                auto E = GetFromOffset<const UObject*>(P, Cand);
                if (ReadableObject(E) && IContains(CachedName(E->Class), "Enum"))
                {
                    Off = Cand;
                    break;
                }
            }
        if (Off)
            DelMar::Log("reflection: %s::Enum at 0x%x (trieed on FortGameStateAthena::GamePhase)", bEnumProp ? "FEnumProperty" : "FByteProperty", Off);
        else
        {
            Off = PropSubOffset() + (bEnumProp ? 8u : 0u);
            DelMar::Log("reflection: %s enum probe %s - assuming 0x%x", bEnumProp ? "FEnumProperty" : "FByteProperty", bProbeKind ? "matched no candidate" : "had no property of that kind to probe", Off);
        }
        return Off;
    }

    const UField* SubField(const UField* Prop, uint32 Extra)
    {
        auto P = GetFromOffset<const UField*>(Prop, PropSubOffset() + Extra);
        return (!P || IsBadReadPtr(P, 0x48)) ? nullptr : P;
    }

    const UObject* SubStruct(const UField* Prop)
    {
        auto S = GetFromOffset<const UObject*>(Prop, StructSubOffset());
        return (ReadableObject(S) && IContains(CachedName(S->Class), "Struct")) ? S : nullptr;
    }

    const UEnum* SubEnum(const UField* Prop, bool bEnumProp)
    {
        auto E = GetFromOffset<const UObject*>(Prop, EnumSubOffset(bEnumProp));
        return (ReadableObject(E) && IContains(CachedName(E->Class), "Enum")) ? (const UEnum*)E : nullptr;
    }

    // value formats
    UEAllocatedString NameAt(const void* A)
    {
        FName N(*(const int32*)A, 0);
        return N.ComparisonIndex > 0 ? N.ToString() : UEAllocatedString("None");
    }

    UEAllocatedString RawSoftPath(const void* A)
    {
        auto Pkg = NameAt(A);
        if (Pkg == "None")
            return Pkg;
        auto Asset = NameAt((const uint8*)A + 4);
        return Asset == "None" ? Pkg : Pkg + "." + Asset;
    }

    int SetCountAt(const void* A)
    {
        auto Num = *(const int32*)((const uint8*)A + 8);
        auto Free = *(const int32*)((const uint8*)A + 0x34);
        if (Num < 0 || Num > 100000 || Free < 0 || Free > Num)
            return -1;
        return Num - Free;
    }

    UEAllocatedString FormatValueAt(const UField* Prop, const void* V, int Depth);

    UEAllocatedString FormatStruct(const UObject* StructType, const void* V, uint32 Size, int Depth)
    {
        auto SN = StructType ? ObjName(StructType) : UEAllocatedString("?");
        auto A = (const uint8*)V;
        char b[128];
        if (SN == "GameplayTag" || (!StructType && Size == 4))
            return (StructType ? "" : "tag? ") + NameAt(A);
        if (SN == "GameplayTagContainer")
        {
            auto& Arr = *(const TArray<uint8>*)A;
            int n = Arr.Num();
            if (n < 0 || n > 4096 || (n > 0 && (!Arr.Data || IsBadReadPtr(Arr.Data, 4))))
                return "<bad tag container>";
            _snprintf_s(b, sizeof(b), _TRUNCATE, "[%d tags]", n);
            UEAllocatedString S = b;
            for (int i = 0; i < n && i < 16; i++)
                S += (i ? ", " : " ") + NameAt(Arr.Data + i * 4);
            return S;
        }
        if (SN == "SoftObjectPath" || SN == "SoftClassPath" || SN == "TopLevelAssetPath")
            return RawSoftPath(A);
        if (SN == "PrimaryAssetId")
            return NameAt(A) + ":" + NameAt(A + 4);
        if (SN == "PrimaryAssetType")
            return NameAt(A);
        if (SN == "Vector" || SN == "Rotator")
        {
            auto d = (const double*)A;
            _snprintf_s(b, sizeof(b), _TRUNCATE, "(%.1f, %.1f, %.1f)", d[0], d[1], d[2]);
            return b;
        }
        if (SN == "Transform")
        {
            auto d = (const double*)A;
            _snprintf_s(b, sizeof(b), _TRUNCATE, "T(%.0f, %.0f, %.0f) Q(%.2f, %.2f, %.2f, %.2f) S(%.1f, %.1f, %.1f)", d[4], d[5], d[6], d[0], d[1], d[2], d[3], d[8], d[9], d[10]);
            return b;
        }
        if (SN == "Guid")
        {
            auto g = (const uint32*)A;
            _snprintf_s(b, sizeof(b), _TRUNCATE, "%08X%08X%08X%08X", g[0], g[1], g[2], g[3]);
            return b;
        }
        if (!StructType || Depth >= 2)
        {
            _snprintf_s(b, sizeof(b), _TRUNCATE, "<struct %s/%u>", SN.c_str(), Size);
            return b;
        }
        UEAllocatedString S = "{ ";
        int Count = 0;
        for (auto P = ((const UStruct*)StructType)->GetChildProperties(); P && Count < 24; P = P->FField_GetNext(), Count++)
        {
            auto Off = GetFromOffset<uint32>(P, Offsets::Offset_Internal);
            if (Count)
                S += ", ";
            S += P->FField_GetName().ToString() + "=" + FormatValueAt(P, A + Off, Depth + 1);
        }
        return S + " }";
    }

    UEAllocatedString FormatValueAt(const UField* Prop, const void* V, int Depth)
    {
        if (!Prop || !V || IsBadReadPtr(V, 1))
            return "<unreadable>";
        auto T = FieldTypeName(Prop);
        auto A = (const uint8*)V;
        auto Size = GetFromOffset<uint32>(Prop, Offsets::ElementSize);
        char b[96];
        if (T == "BoolProperty")
        {
            auto M = Prop->GetFieldMask();
            if (!M)
                M = 1;
            return (*A & M) ? "true" : "false";
        }
        if (T == "IntProperty") { _snprintf_s(b, sizeof(b), _TRUNCATE, "%d", *(const int32*)A); return b; }
        if (T == "Int8Property") { _snprintf_s(b, sizeof(b), _TRUNCATE, "%d", (int)*(const int8*)A); return b; }
        if (T == "Int16Property") { _snprintf_s(b, sizeof(b), _TRUNCATE, "%d", (int)*(const int16*)A); return b; }
        if (T == "Int64Property") { _snprintf_s(b, sizeof(b), _TRUNCATE, "%lld", *(const int64*)A); return b; }
        if (T == "UInt16Property") { _snprintf_s(b, sizeof(b), _TRUNCATE, "%u", (unsigned)*(const uint16*)A); return b; }
        if (T == "UInt32Property") { _snprintf_s(b, sizeof(b), _TRUNCATE, "%u", *(const uint32*)A); return b; }
        if (T == "UInt64Property") { _snprintf_s(b, sizeof(b), _TRUNCATE, "%llu", *(const uint64*)A); return b; }
        if (T == "FloatProperty") { _snprintf_s(b, sizeof(b), _TRUNCATE, "%g", (double)*(const float*)A); return b; }
        if (T == "DoubleProperty") { _snprintf_s(b, sizeof(b), _TRUNCATE, "%g", *(const double*)A); return b; }
        if (T == "ByteProperty")
        {
            _snprintf_s(b, sizeof(b), _TRUNCATE, "%u", (unsigned)*A);
            UEAllocatedString S = b;
            if (auto E = SubEnum(Prop, false))
                S += " (" + EnumValueName(E, *A) + ")";
            return S;
        }
        if (T == "EnumProperty")
        {
            int64 Val = Size == 1 ? (int64)*A : Size == 2 ? (int64)*(const int16*)A : Size == 4 ? (int64)*(const int32*)A : *(const int64*)A;
            _snprintf_s(b, sizeof(b), _TRUNCATE, "%lld", Val);
            UEAllocatedString S = b;
            if (auto E = SubEnum(Prop, true))
                S += " (" + EnumValueName(E, Val) + ")";
            return S;
        }
        if (T == "NameProperty")
            return NameAt(A);
        if (T == "StrProperty")
        {
            auto& S = *(const FString*)A;
            if (!S.Data || S.Num() <= 0 || S.Num() > 4096 || IsBadReadPtr(S.Data, 2))
                return "\"\"";
            return "\"" + S.ToString() + "\"";
        }
        if (T == "TextProperty")
            return "<text>";
        if (T == "ObjectProperty" || T == "ClassProperty" || T == "ObjectPtrProperty" || T == "ClassPtrProperty" || T == "InterfaceProperty")
            return ObjRef(*(const UObject* const*)A);
        if (T == "WeakObjectProperty" || T == "LazyObjectProperty")
            return ObjRef(((const FWeakObjectPtr*)A)->Get());
        if (T == "SoftObjectProperty" || T == "SoftClassProperty")
            return SoftPathToString(A);
        if (T == "StructProperty")
            return FormatStruct(SubStruct(Prop), A, Size, Depth);
        if (T == "ArrayProperty")
        {
            auto Inner = SubField(Prop, 0);
            auto& Arr = *(const TArray<uint8>*)A;
            int n = Arr.Num();
            if (n < 0 || n > 200000 || (n > 0 && (!Arr.Data || IsBadReadPtr(Arr.Data, 1))))
                return "<bad array>";
            auto ISize = Inner ? GetFromOffset<uint32>(Inner, Offsets::ElementSize) : 0u;
            _snprintf_s(b, sizeof(b), _TRUNCATE, "[%d x %s]", n, Inner ? FieldTypeName(Inner).c_str() : "?");
            UEAllocatedString S = b;
            if (Inner && ISize && Depth < 2)
                for (int i = 0; i < n && i < 12; i++)
                    S += (i ? ", " : " ") + FormatValueAt(Inner, Arr.Data + i * ISize, Depth + 1);
            if (n > 12)
                S += ", ...";
            return S;
        }
        if (T == "MapProperty" || T == "SetProperty")
        {
            _snprintf_s(b, sizeof(b), _TRUNCATE, "<%s ~%d elements>", T.c_str(), SetCountAt(A));
            return b;
        }
        if (IContains(T, "Delegate"))
            return "<delegate>";
        return "<" + T + ">";
    }

    UEAllocatedString SignatureEx(const UFunction* Fn)
    {
        if (!Fn)
            return "null";
        UEAllocatedString Ret = "void", Args;
        for (auto& P : GetParams(Fn))
        {
            auto T = P.Type;
            if (T == "StructProperty")
            {
                auto S = SubStruct(P.Prop);
                char b[80];
                _snprintf_s(b, sizeof(b), _TRUNCATE, "(%s/%u)", S ? ObjName(S).c_str() : "?", P.Size);
                T += b;
            }
            else if (T == "EnumProperty" || T == "ByteProperty")
            {
                if (auto E = SubEnum(P.Prop, T == "EnumProperty"))
                    T += "(" + ObjName(E) + ")";
            }
            if (P.IsReturn())
            {
                Ret = T;
                continue;
            }
            if (!Args.empty())
                Args += ", ";
            Args += T + " " + (P.IsOut() ? "&" : "") + P.Name;
        }
        auto Flags = FunctionFlags(Fn);
        UEAllocatedString Prefix;
        if (Flags & FUNC_Static)
            Prefix += "static ";
        if (Flags & FUNC_Native)
            Prefix += "native ";
        if (Flags & FUNC_BlueprintCallable)
            Prefix += "bp ";
        return Prefix + Ret + " " + ObjName(Fn->Outer) + "::" + Fn->Name.ToString() + "(" + Args + ")";
    }

    //dumps
    void DumpObjectImpl(const UObject* Obj, const char* Tag, const char* Filters, int MaxLines)
    {
        if (!Obj || IsBadReadPtr(Obj, 0x30) || !Obj->Class)
        {
            DelMar::Log("[%s] object: %s", Tag, Obj ? "<bad pointer>" : "null");
            return;
        }
        DelMar::Log("[%s] object %s : %s  flags 0x%x  outer %s", Tag, ObjName(Obj).c_str(), DelMar::ClassChain(Obj).c_str(), (unsigned)Obj->ObjectFlags, OuterChain(Obj).c_str());
        int Lines = 0;
        for (const UStruct* C = Obj->Class; C && Lines < MaxLines; C = C->GetSuper())
        {
            auto& CN = CachedName(C);
            if (!Filters && IsBaseClassName(CN))
                break;
            for (auto P = C->GetChildProperties(); P && Lines < MaxLines; P = P->FField_GetNext())
            {
                auto PN = P->FField_GetName().ToString();
                if (!MatchesAnyFilter(PN, Filters))
                    continue;
                auto Off = GetFromOffset<uint32>(P, Offsets::Offset_Internal);
                auto Sz = GetFromOffset<uint32>(P, Offsets::ElementSize);
                DelMar::Log("[%s]   %s::%s (%s @0x%x/%u) = %s", Tag, CN.c_str(), PN.c_str(), FieldTypeName(P).c_str(), Off, Sz, FormatValueAt(P, (const uint8*)Obj + Off, 0).c_str());
                Lines++;
            }
        }
        if (Lines >= MaxLines)
            DelMar::Log("[%s]   ... property dump truncated at %d lines", Tag, MaxLines);
    }

    void DumpFunctionsImpl(const char* ClassName, const char* Tag)
    {
        auto C = FindClass(ClassName);
        if (!C)
        {
            DelMar::Log("[%s] class %s: not found", Tag, ClassName);
            return;
        }
        int n = 0;
        for (const UStruct* K = C; K; K = K->GetSuper())
        {
            if (IsBaseClassName(CachedName(K)))
                break;
            for (auto F = K->GetChildren(); F; F = F->GetNext())
                if (F->Class && (F->Class->GetCastFlags() & CASTCLASS_UFunction))
                {
                    DelMar::Log("[%s]   fn %s", Tag, SignatureEx((const UFunction*)F).c_str());
                    n++;
                }
        }
        DelMar::Log("[%s] class %s: %d functions", Tag, ClassName, n);
    }

    int DumpObjectsOfClassImpl(const char* Needle, const char* Tag, bool bProps, const char* Filters, int MaxHits)
    {
        std::unordered_map<const UClass*, bool> Match;
        int Hits = 0, Templates = 0;
        const int Num = TUObjectArray::Num();
        for (int i = 0; i < Num && Hits < MaxHits; i++)
        {
            auto Obj = TUObjectArray::GetObjectByIndex(i);
            if (!Obj || !Obj->Class)
                continue;
            auto it = Match.find(Obj->Class);
            bool bM = it != Match.end() ? it->second : (Match[Obj->Class] = ChainContains(Obj->Class, Needle));
            if (!bM)
                continue;
            if (IsTemplateObject(Obj))
            {
                Templates++;
                continue;
            }
            Hits++;
            if (bProps)
                DumpObjectImpl(Obj, Tag, Filters, 160);
            else
                DelMar::Log("[%s]   %s : %s  flags 0x%x  outer %s", Tag, ObjName(Obj).c_str(), DelMar::ClassChain(Obj).c_str(), (unsigned)Obj->ObjectFlags, OuterChain(Obj).c_str());
        }
        DelMar::Log("[%s] live objects with '%s' in the class chain: %d%s (%d CDO/archetype templates skipped)", Tag, Needle, Hits, Hits >= MaxHits ? " (capped)" : "", Templates);
        return Hits;
    }

    const UObject* FirstObjectOfClass(const char* Needle, const UObject* PreferOuter)
    {
        const UObject* Best = nullptr;
        const UObject* BestLive = nullptr;
        std::unordered_map<const UClass*, bool> Match;
        const int Num = TUObjectArray::Num();
        for (int i = 0; i < Num; i++)
        {
            auto Obj = TUObjectArray::GetObjectByIndex(i);
            if (!Obj || !Obj->Class)
                continue;
            auto it = Match.find(Obj->Class);
            bool bM = it != Match.end() ? it->second : (Match[Obj->Class] = ChainContains(Obj->Class, Needle));
            if (!bM || IsTemplateObject(Obj))
                continue;
            if (PreferOuter && Obj->Outer == PreferOuter)
                return Obj;
            if (!BestLive && IsInLiveWorld(Obj))
                BestLive = Obj;
            if (!Best)
                Best = Obj;
        }
        return BestLive ? BestLive : Best;
    }

    UEAllocatedVector<AActor*> ActorsWithClass(const char* Needle)
    {
        UEAllocatedVector<AActor*> Out;
        TArray<AActor*> Actors;
        Utils::GetAll(AActor::StaticClass(), Actors);
        std::unordered_map<const UClass*, bool> Match;
        for (int i = 0; i < Actors.Num(); i++)
        {
            auto A = Actors[i];
            if (!A || !A->Class)
                continue;
            auto it = Match.find(A->Class);
            bool bM = it != Match.end() ? it->second : (Match[A->Class] = ChainContains(A->Class, Needle));
            if (bM)
                Out.push_back(A);
        }
        Actors.Free();
        return Out;
    }

    bool IsPlayspaceActor(const AActor* A)
    {
        return A && A->Class && (ChainHasExact(A->Class, "DelMarPlayspace") || ChainHasExact(A->Class, "FortPlayspace") || ChainHasExact(A->Class, "Playspace"));
    }

    AActor* FindPlayspaceActor()
    {
        auto PS = ActorsWithClass("Playspace");
        AActor* Best = nullptr;
        for (auto A : PS)
        {
            if (!IsPlayspaceActor(A))
                continue;
            if (ChainContains(A->Class, "DelMarPlayspace"))
                return A;
            if (!Best)
                Best = A;
        }
        return Best;
    }

    bool StreamingLevelPathsContain(const char* Needle)
    {
        auto World = UWorld::GetWorld();
        if (!World)
            return false;
        auto SLOff = World->GetOffset("StreamingLevels");
        if (SLOff == (uint32)-1)
            return false;
        auto& Levels = GetFromOffset<TArray<UObject*>>(World, SLOff);
        for (int i = 0; i < Levels.Num(); i++)
        {
            auto L = Levels[i];
            if (!L)
                continue;
            auto WAOff = L->GetOffset("WorldAsset");
            if (WAOff != (uint32)-1 && IContains(SoftPathToString((const void*)(__int64(L) + WAOff)), Needle))
                return true;
        }
        return false;
    }

    bool AnyDelMarLevelStreaming()
    {
        for (auto W : FConfiguration::DelMarTrackWorlds)
            if (StreamingLevelPathsContain(Narrow(W).c_str()))
                return true;
        return StreamingLevelPathsContain("/DelMarSeamless_TutorialRun/") || StreamingLevelPathsContain("/DelMarIron/") || StreamingLevelPathsContain("/DelMarCruise/") || StreamingLevelPathsContain("/DelMarAqueduct/") ||
               StreamingLevelPathsContain("/DelMarLevels/");
    }

    // ObjectProperty or WeakObjectProperty by name
    const UObject* ReadObjAny(const UObject* Obj, const char* Name)
    {
        if (!Obj)
            return nullptr;
        auto P = Obj->GetProperty(Name);
        if (!P)
            return nullptr;
        auto Off = GetFromOffset<uint32>(P, Offsets::Offset_Internal);
        auto T = FieldTypeName(P);
        if (T == "WeakObjectProperty" || T == "LazyObjectProperty")
            return ((const FWeakObjectPtr*)((const uint8*)Obj + Off))->Get();
        if (T == "ObjectProperty" || T == "ClassProperty" || T == "ObjectPtrProperty")
            return *(const UObject* const*)((const uint8*)Obj + Off);
        return nullptr;
    }

    bool WriteObjProp(const UObject* Obj, const char* Name, const UObject* Value)
    {
        if (!Obj)
            return false;
        auto P = Obj->GetProperty(Name);
        if (!P)
            return false;
        auto T = FieldTypeName(P);
        auto Off = GetFromOffset<uint32>(P, Offsets::Offset_Internal);
        if (T == "ObjectProperty" || T == "ObjectPtrProperty")
        {
            *(const UObject**)((uint8*)Obj + Off) = Value;
            return true;
        }
        if (T == "WeakObjectProperty")
        {
            FWeakObjectPtr W(Value);
            memcpy((uint8*)Obj + Off, &W, sizeof(W));
            return true;
        }
        return false;
    }

    bool WriteFloatProp(const UObject* Obj, const char* Name, float Value)
    {
        auto P = Obj ? Obj->GetProperty(Name) : nullptr;
        if (!P)
            return false;
        auto T = FieldTypeName(P);
        auto Off = GetFromOffset<uint32>(P, Offsets::Offset_Internal);
        if (T == "FloatProperty")
            *(float*)((uint8*)Obj + Off) = Value;
        else if (T == "DoubleProperty")
            *(double*)((uint8*)Obj + Off) = Value;
        else
            return false;
        return true;
    }

    bool WriteStructBool(const UObject* Obj, const char* StructPropName, const char* FieldName, bool Value)
    {
        auto P = Obj ? Obj->GetProperty(StructPropName) : nullptr;
        if (!P || FieldTypeName(P) != "StructProperty")
            return false;
        auto Base = GetFromOffset<uint32>(P, Offsets::Offset_Internal);
        auto SS = (const UStruct*)SubStruct(P);
        if (!SS)
            return false;
        for (auto F = SS->GetChildProperties(); F; F = F->FField_GetNext())
        {
            if (F->FField_GetName().ToString() != FieldName)
                continue;
            if (FieldTypeName(F) != "BoolProperty")
                return false;
            auto Off = GetFromOffset<uint32>(F, Offsets::Offset_Internal);
            uint8 M = F->GetFieldMask();
            if (!M)
                M = 1;
            auto& B = *((uint8*)Obj + Base + Off);
            B = Value ? (B | M) : (B & ~M);
            return true;
        }
        return false;
    }

    bool WriteBoolProp(const UObject* Obj, const char* Name, bool Value)
    {
        auto P = Obj ? Obj->GetProperty(Name) : nullptr;
        if (!P || FieldTypeName(P) != "BoolProperty")
            return false;
        auto Off = GetFromOffset<uint32>(P, Offsets::Offset_Internal);
        uint8 M = P->GetFieldMask();
        if (!M)
            M = 1;
        auto& B = *((uint8*)Obj + Off);
        B = Value ? (B | M) : (B & ~M);
        return true;
    }

    UEAllocatedString PropText(const UObject* Obj, const char* Name);

    void SkipClientLoadWait(AActor* PS)
    {
        auto LS = PS ? ReadObjAny(PS, "LevelStreamComponent") : nullptr;
        if (!LS)
        {
            DelMar::Log("[playspace] no LevelStreamComponent on the playspace - cannot clear the client-load wait");
            return;
        }
        auto Before = PropText(LS, "bWaitForClientsInSessionBeforeConsideringComplete");
        bool ok = WriteBoolProp(LS, "bWaitForClientsInSessionBeforeConsideringComplete", false);
        DelMar::Log("[playspace] %s.bWaitForClientsInSessionBeforeConsideringComplete: %s -> %s (%s)", ObjRef(LS).c_str(), Before.c_str(),
                    PropText(LS, "bWaitForClientsInSessionBeforeConsideringComplete").c_str(), ok ? "cleared" : "!! property not found");
    }

    struct FReflCall
    {
        UFunction* Fn = nullptr;
        const UObject* Target = nullptr;
        UEAllocatedVector<FParamInfo> Params;
        uint8* Buf = nullptr;
        UEAllocatedVector<void*> Extra;

        bool Init(const UObject* InTarget, UFunction* InFn)
        {
            Target = InTarget;
            Fn = InFn;
            if (!Fn || !Target)
                return false;
            Params = GetParams(Fn);
            auto Size = Fn->GetPropertiesSize() + 16;
            Buf = (uint8*)FMemory::Malloc(Size);
            memset(Buf, 0, Size);
            return true;
        }
        ~FReflCall()
        {
            if (!Buf)
                return;
            for (auto& P : Params)
                if (P.Type == "StrProperty")
                    ((FString*)(Buf + P.Offset))->Free();
            for (auto E : Extra)
                FMemory::Free(E);
            FMemory::Free(Buf);
        }
        void SetTagContainer(FParamInfo* P, const int32* TagIdx, int Count)
        {
            if (!P || P->Size < 32 || Count <= 0)
                return;
            auto A = Buf + P->Offset;
            memset(A, 0, 32);
            auto Data = (int32*)FMemory::Malloc(4 * Count);
            memcpy(Data, TagIdx, 4 * Count);
            *(int32**)(A) = Data;
            *(int32*)(A + 8) = Count;
            *(int32*)(A + 12) = Count;
            Extra.push_back(Data);
            UEAllocatedVector<int32> Parents;
            for (int i = 0; i < Count; i++)
            {
                auto S = FName(TagIdx[i], 0).ToWString();
                for (auto Pos = S.rfind(L'.'); Pos != UEAllocatedWString::npos; Pos = S.rfind(L'.'))
                {
                    S.resize(Pos);
                    FName Pn(S.c_str());
                    if (Pn.ComparisonIndex > 0 && std::find(Parents.begin(), Parents.end(), Pn.ComparisonIndex) == Parents.end())
                        Parents.push_back(Pn.ComparisonIndex);
                }
            }
            if (!Parents.empty())
            {
                auto PData = (int32*)FMemory::Malloc(4 * Parents.size());
                memcpy(PData, Parents.data(), 4 * Parents.size());
                *(int32**)(A + 16) = PData;
                *(int32*)(A + 24) = (int32)Parents.size();
                *(int32*)(A + 28) = (int32)Parents.size();
                Extra.push_back(PData);
            }
        }
        void SetTagContainer(FParamInfo* P, FName Tag)
        {
            if (Tag.ComparisonIndex > 0)
                SetTagContainer(P, &Tag.ComparisonIndex, 1);
        }
        FParamInfo* Find(const char* Name)
        {
            for (auto& P : Params)
                if (IEquals(P.Name, Name))
                    return &P;
            return nullptr;
        }
        FParamInfo* Ret()
        {
            for (auto& P : Params)
                if (P.IsReturn())
                    return &P;
            return nullptr;
        }
        uint8* At(FParamInfo* P) { return P ? Buf + P->Offset : nullptr; }
        void SetPtr(FParamInfo* P, const void* V)
        {
            if (P)
                *(const void**)(Buf + P->Offset) = V;
        }
        void SetBool(FParamInfo* P, bool V)
        {
            if (!P)
                return;
            uint8 M = P->Prop ? P->Prop->GetFieldMask() : 0;
            if (!M)
                M = 1;
            Buf[P->Offset] = V ? (M == 0xFF ? 1 : M) : 0;
        }
        void SetNameIdx(FParamInfo* P, int32 Idx)
        {
            if (P)
                memcpy(Buf + P->Offset, &Idx, 4);
        }
        void SetInt(FParamInfo* P, int64 V)
        {
            if (P)
                memcpy(Buf + P->Offset, &V, P->Size >= 8 ? 8 : (P->Size >= 4 ? 4 : (P->Size >= 2 ? 2 : 1)));
        }
        void SetStr(FParamInfo* P, const wchar_t* S)
        {
            if (!P)
                return;
            auto Dst = (FString*)(Buf + P->Offset);
            Dst->Free();
            FString F(S);
            memcpy(Dst, &F, sizeof(FString));
        }
        void Invoke() { Target->ProcessEvent(Fn, Buf); }
        const UObject* GetObj(FParamInfo* P) { return P ? *(const UObject**)(Buf + P->Offset) : nullptr; }
        bool GetBool(FParamInfo* P) { return P && Buf[P->Offset] != 0; }
        UEAllocatedString RetString()
        {
            auto R = Ret();
            if (!R)
                return "void";
            if (R->Type == "BoolProperty")
                return GetBool(R) ? "true" : "false";
            if (R->Type == "ObjectProperty" || R->Type == "ClassProperty" || R->Type == "InterfaceProperty")
                return ObjRef(GetObj(R));
            if (R->Type == "StructProperty" && R->Size == 4)
                return NameAt(At(R));
            return FormatValueAt(R->Prop, At(R), 1);
        }
    };

    UEAllocatedString FillGenericInputs(FReflCall& C, const UClass* ClassArg, FName TagArg, bool bDeclineStrings, bool& bOutDeclined)
    {
        UEAllocatedString Desc;
        bOutDeclined = false;
        for (auto& P : C.Params)
        {
            if (!P.IsInput())
                continue;
            UEAllocatedString Set = "0";
            if (P.Type == "ClassProperty" || (P.Type == "ObjectProperty" && IContains(P.Name, "Class")))
            {
                C.SetPtr(&P, ClassArg);
                Set = ClassArg ? ObjName(ClassArg) : UEAllocatedString("null");
            }
            else if (P.Type == "SoftClassProperty" || P.Type == "SoftObjectProperty")
            {
                if (ClassArg && P.Size >= 0x20)
                {
                    auto Soft = C.At(&P);
                    FWeakObjectPtr W(ClassArg);
                    memcpy(Soft, &W, sizeof(W));
                    const UObject* Pkg = ClassArg->Outer;
                    for (int g = 0; Pkg && !IsBadReadPtr(Pkg, 0x30) && Pkg->Outer && g < 16; g++)
                        Pkg = Pkg->Outer;
                    *(int32*)(Soft + 0x8) = Pkg ? Pkg->Name.ComparisonIndex : 0;
                    *(int32*)(Soft + 0xC) = ClassArg->Name.ComparisonIndex;
                    Set = "soft(" + SoftPathToString(Soft) + ")";
                }
            }
            else if (P.Type == "StructProperty")
            {
                auto ST = SubStruct(P.Prop);
                auto SN = ST ? ObjName(ST) : UEAllocatedString("?");
                if ((SN == "GameplayTag" || (!ST && P.Size == 4)) && TagArg.ComparisonIndex > 0)
                {
                    C.SetNameIdx(&P, TagArg.ComparisonIndex);
                    Set = "tag " + TagArg.ToString();
                }
                else if ((SN == "GameplayTagContainer" || (!ST && P.Size == 32)) && TagArg.ComparisonIndex > 0)
                {
                    C.SetTagContainer(&P, TagArg);
                    Set = "tags{" + TagArg.ToString() + "}";
                }
                else if (SN == "Transform" && P.Size >= 96)
                {
                    auto d = (double*)C.At(&P);
                    d[3] = 1.0;
                    d[8] = d[9] = d[10] = 1.0;
                    Set = "identity Transform";
                }
                else if (SN == "Quat" && P.Size >= 32)
                {
                    ((double*)C.At(&P))[3] = 1.0;
                    Set = "identity Quat";
                }
                else
                {
                    char b[64];
                    _snprintf_s(b, sizeof(b), _TRUNCATE, "zero %s/%u", SN.c_str(), P.Size);
                    Set = b;
                }
            }
            else if (P.Type == "NameProperty" && TagArg.ComparisonIndex > 0)
            {
                C.SetNameIdx(&P, TagArg.ComparisonIndex);
                Set = "name " + TagArg.ToString();
            }
            else if ((P.Type == "ByteProperty" || P.Type == "EnumProperty") && IContains(P.Name, "CreationType"))
            {
                auto E = FindEnum("EPlayspaceCreationType");
                auto V = E ? E->GetValue("RootInserted") : -1;
                if (V >= 0)
                    C.SetInt(&P, V);
                char b[48];
                _snprintf_s(b, sizeof(b), _TRUNCATE, "creation-type %lld", V);
                Set = b;
            }
            else if (P.Type == "StrProperty")
            {
                if (bDeclineStrings)
                {
                    bOutDeclined = true;
                    return Desc;
                }
                C.SetStr(&P, L"");
                Set = "\"\"";
            }
            if (!Desc.empty())
                Desc += ", ";
            Desc += P.Name + "=" + Set;
        }
        return Desc;
    }

    bool CallNoArgs(const UObject* Obj, const char* FnName, const char* Tag)
    {
        auto Fn = Obj ? Obj->GetFunction(FnName) : nullptr;
        if (!Fn)
        {
            DelMar::Log("[%s] %s has no reflected %s", Tag, ObjRef(Obj).c_str(), FnName);
            return false;
        }
        FReflCall C;
        if (!C.Init(Obj, Fn))
            return false;
        DelMar::Log("[%s] calling %s on %s", Tag, SignatureEx(Fn).c_str(), ObjRef(Obj).c_str());
        C.Invoke();
        DelMar::Log("[%s]   -> %s", Tag, C.RetString().c_str());
        return true;
    }

    const UClass* LoadPlayspaceClass()
    {
        auto C = FindObject<UClass>(FConfiguration::DelMarPlayspaceClassPath);
        if (!C)
        {
            DelMar::Log("[playspace] !! could not find/load %ls", FConfiguration::DelMarPlayspaceClassPath);
            return nullptr;
        }
        UEAllocatedString Chain;
        for (const UStruct* K = C; K; K = K->GetSuper())
            Chain += (Chain.empty() ? "" : " : ") + CachedName(K);
        DelMar::Log("[playspace] class %ls -> %s", FConfiguration::DelMarPlayspaceClassPath, Chain.c_str());
        return C;
    }

    const UObject* StateMachineOf(const AActor* PS)
    {
        return PS ? ReadObjAny(PS, "PrimaryStateMachine") : nullptr;
    }

    UEAllocatedString StateTagOf(const AActor* PS)
    {
        auto SM = StateMachineOf(PS);
        if (!SM)
            return "no-state-machine";
        auto Fn = SM->GetFunction("GetCurrentStateTag");
        if (!Fn)
            return "no-GetCurrentStateTag";
        FReflCall C;
        if (!C.Init(SM, Fn))
            return "?";
        C.Invoke();
        auto R = C.Ret();
        return R ? NameAt(C.At(R)) : UEAllocatedString("?");
    }

    bool RequestStateTag(const AActor* PS, const wchar_t* Tag)
    {
        auto SM = StateMachineOf(PS);
        auto Fn = SM ? SM->GetFunction("RequestState") : nullptr;
        if (!Fn)
        {
            DelMar::Log("[playspace] no state machine / RequestState on %s", ObjRef(PS).c_str());
            return false;
        }
        FReflCall C;
        if (!C.Init(SM, Fn))
            return false;
        auto P = C.Find("StateTag");
        if (!P)
            P = C.Params.empty() ? nullptr : &C.Params[0];
        FName T(Tag);
        C.SetNameIdx(P, T.ComparisonIndex);
        DelMar::Log("[playspace] calling %s with StateTag=%ls (was %s)", SignatureEx(Fn).c_str(), Tag, StateTagOf(PS).c_str());
        C.Invoke();
        DelMar::Log("[playspace]   -> state now %s", StateTagOf(PS).c_str());
        return true;
    }

    //   struct FPlayspaceUserList : FFastArraySerializer
    //   {
    //       TArray<FPlayspaceUser> PlayspaceUsers;   // 0x0108
    //       bool                   bIsInitialized;   // 0x016C // this - 0x16C uint32 kUserListInited
    //   };
    constexpr uint32 kUserListArray  = 0x108;
    constexpr uint32 kUserListInited = 0x16C;
    constexpr uint32 kUserListOwner  = 0x170;

    const uint8* PlayspaceUserListAt(const AActor* PS)
    {
        auto P = PS ? PS->GetProperty("PlayspaceUsers") : nullptr;
        if (!P)
            return nullptr;
        return (const uint8*)PS + GetFromOffset<uint32>(P, Offsets::Offset_Internal);
    }

    int PlayspaceUserCount(const AActor* PS)
    {
        auto L = PlayspaceUserListAt(PS);
        if (!L)
            return -1;
        auto n = *(const int32*)(L + kUserListArray + 8);
        return (n >= 0 && n < 1000) ? n : -1;
    }

    bool InitPlayspaceUserList(AActor* PS)
    {
        auto L = (uint8*)PlayspaceUserListAt(PS);
        if (!L)
        {
            DelMar::Log("[users] playspace has no reflected PlayspaceUsers property");
            return false;
        }
        auto Owner  = (AActor**)(L + kUserListOwner);
        auto Inited = (bool*)(L + kUserListInited);
        DelMar::Log("[users] before: count=%d bIsInitialized=%s Playspace=%s",
                    PlayspaceUserCount(PS), *Inited ? "true" : "false", *Owner ? "set" : "null");
        if (*Owner == PS && *Inited)
        {
            DelMar::Log("[users] already bound - leaving it chill alone");
            return false;
        }
        *Owner = PS;
        *Inited = true;
        DelMar::Log("[users] bound the user list to %s and marked it initialised", ObjRef(PS).c_str());
        return true;
    }

    UEAllocatedString PropText(const UObject* Obj, const char* Name)
    {
        auto P = Obj ? Obj->GetProperty(Name) : nullptr;
        if (!P)
            return "-";
        return FormatValueAt(P, (const uint8*)Obj + GetFromOffset<uint32>(P, Offsets::Offset_Internal), 1);
    }

    void LogPlayspaceStatus(const char* Tag)
    {
        auto PS = FindPlayspaceActor();
        auto Vehicles = ActorsWithClass("DelMarVehicle");
        auto RaceManagers = ActorsWithClass("RaceManager");
        if (!PS)
        {
            DelMar::Log("[%s] playspace: NONE  vehicles %d  race managers %d", Tag, (int)Vehicles.size(), (int)RaceManagers.size());
            return;
        }
        auto LM = ReadObjAny(PS, "LevelManager");
        auto LS = ReadObjAny(PS, "LevelStreamComponent");
        DelMar::Log("[%s] playspace: %s  state %s  users %s  race-manager %s  manager-link %s  vehicles %d  race managers in world %d  track streaming %d  | level-manager desired %s current %s  | stream LevelsToLoad %s",
                    Tag, ObjRef(PS).c_str(), StateTagOf(PS).c_str(), PropText(PS, "PlayspaceUsers").c_str(), PropText(PS, "ActiveRaceManager").c_str(), PropText(PS, "PlayspaceManagerCached").c_str(),
                    (int)Vehicles.size(), (int)RaceManagers.size(), (int)AnyDelMarLevelStreaming(), PropText(LM, "DesiredMapDescription").c_str(), PropText(LM, "CurrentLevelData").c_str(),
                    PropText(LS, "LevelsToLoad").c_str());
    }

    const UObject* FindPlayspaceManager()
    {
        auto World = UWorld::GetWorld();
        auto GS = (World && World->HasGameState()) ? (const UObject*)World->GameState : nullptr;
        return FirstObjectOfClass("PlayspaceManagerComponent", GS);
    }

    void LinkPlayspaceToManager(AActor* PS)
    {
        auto Mgr = FindPlayspaceManager();
        if (!Mgr)
        {
            DelMar::Log("[playspace] no live PlayspaceManagerComponent on the game state - cannot link link");
            return;
        }
        DelMar::Log("[playspace] manager %s before: RootPlayspace=%s", ObjRef(Mgr).c_str(), ObjRef(ReadObjAny(Mgr, "RootPlayspace")).c_str());
        DumpObjectImpl(Mgr, "playspace", "RootPlayspace|Unassigned|PlayspaceRootType", 20);
        bool a = WriteObjProp(Mgr, "RootPlayspace", PS);
        bool b = WriteObjProp(PS, "PlayspaceManagerCached", Mgr);
        DelMar::Log("[playspace] linked: manager.RootPlayspace=%s (%d), playspace.PlayspaceManagerCached=%s (%d)", ObjRef(ReadObjAny(Mgr, "RootPlayspace")).c_str(), (int)a,
                    ObjRef(ReadObjAny(PS, "PlayspaceManagerCached")).c_str(), (int)b);
        CallNoArgs(Mgr, "OnRep_RootPlayspace", "playspace");

        if (InitPlayspaceUserList(PS))
            CallNoArgs(PS, "OnRep_PlayspaceUsers", "playspace");

        DumpObjectImpl(Mgr, "playspace", "RootPlayspace|Unassigned", 20);
    }

    AActor* SpawnPlayspace()
    {
        auto PSClass = LoadPlayspaceClass();
        if (!PSClass)
            return nullptr;
        DelMar::Log("[playspace] spawning %s with UWorld::SpawnActor at the origin but natively CreatePlayspacesFromConfig action never runs here). It ticks OUTSIDE the crash guard from this one.", ObjName(PSClass).c_str());
        auto A = UWorld::SpawnActor(PSClass, FVector(), FRotator());
        DelMar::Log("[playspace]   -> %s", ObjRef(A).c_str());
        return A;
    }

    //asking for level
    bool TryCallLevelLoad(const UObject* Obj, const char* Label, FName Tag)
    {
        if (!Obj)
            return false;
        const char* Exact[] = { "RequestLevelLoad", "ServerRequestLoadingLevel", "ServerRequestLevel", "RequestLoadingLevel", "RequestLevelLoadByTag", "LoadLevelByTag" };
        UEAllocatedVector<UFunction*> Cands;
        for (auto N : Exact)
            if (auto F = Obj->GetFunction(N))
                if (std::find(Cands.begin(), Cands.end(), F) == Cands.end())
                    Cands.push_back(F);
        if (Cands.empty())
        {
            DelMar::Log("[track] %s %s has no reflected level load function", Label, ObjRef(Obj).c_str());
            return false;
        }
        for (auto Fn : Cands)
        {
            FReflCall C;
            if (!C.Init(Obj, Fn))
                continue;
            bool bDeclined = false;
            auto Desc = FillGenericInputs(C, nullptr, Tag, true, bDeclined);
            if (bDeclined)
            {
                DelMar::Log("[track] skipping %s (takes a string - the LinkCode/UEFN variant)", SignatureEx(Fn).c_str());
                continue;
            }
            DelMar::Log("[track] calling %s on %s with (%s)", SignatureEx(Fn).c_str(), ObjRef(Obj).c_str(), Desc.c_str());
            C.Invoke();
            DelMar::Log("[track]   -> %s", C.RetString().c_str());
            return true;
        }
        return false;
    }

    bool CallWithTagContainer(const UObject* Obj, const char* FnName, const wchar_t* Tag, const char* Label)
    {
        auto Fn = Obj ? Obj->GetFunction(FnName) : nullptr;
        if (!Fn)
        {
            DelMar::Log("[track] %s %s has no reflected %s", Label, ObjRef(Obj).c_str(), FnName);
            return false;
        }
        FReflCall C;
        if (!C.Init(Obj, Fn))
            return false;
        bool bDeclined = false;
        auto Desc = FillGenericInputs(C, nullptr, FName(Tag), true, bDeclined);
        if (bDeclined)
            return false;
        UEAllocatedVector<int32> Idx;
        UEAllocatedString TagList;
        for (auto T : FConfiguration::DelMarTrackRequestTags)
        {
            FName N(T);
            if (N.ComparisonIndex > 0)
            {
                Idx.push_back(N.ComparisonIndex);
                TagList += (TagList.empty() ? "" : ", ") + Narrow(T);
            }
        }
        for (auto& P : C.Params)
            if (P.IsInput() && P.Type == "StructProperty" && P.Size == 32 && !Idx.empty())
            {
                C.SetTagContainer(&P, Idx.data(), (int)Idx.size());
                Desc = P.Name + "=tags{" + TagList + "}";
            }
        DelMar::Log("[track] calling %s on %s with (%s)", SignatureEx(Fn).c_str(), ObjRef(Obj).c_str(), Desc.c_str());
        C.Invoke();
        DelMar::Log("[track]   -> %s", C.RetString().c_str());
        return true;
    }

    void RequestLevelViaManager(AActor* PS)
    {
        auto LM = ReadObjAny(PS, "LevelManager");
        if (!LM)
            LM = FirstObjectOfClass("DelMarLevelManagerComponent", PS);
        DelMar::Log("[track] level manager: %s  desired %s", ObjRef(LM).c_str(), PropText(LM, "DesiredMapDescription").c_str());
        if (!CallWithTagContainer(LM, "RequestLevelLoad", FConfiguration::DelMarTrackMapTag, "level manager"))
            TryCallLevelLoad(LM, "level manager", FName(FConfiguration::DelMarTrackMapTag));
    }

    void RequestLevelViaPlayspace(AActor* PS)
    {
        if (!CallWithTagContainer(PS, "ServerRequestLoadingLevel", FConfiguration::DelMarTrackMapTag, "playspace"))
            RequestLevelViaManager(PS);
    }

    void DirectStreamLevelDataWorlds()
    {
        for (auto W : FConfiguration::DelMarTrackWorlds)
            DelMar::StreamTrack(W);
    }

    const UObject* FindRaceManager();
    void RetryPlace(int Left)
    {
        if (FindRaceManager() || !ActorsWithClass("DelMarVehicle").empty())
        {
            DelMar::Log("place: a race manager / DelMarVehicle exists - the race manager owns the player now, not teleporting the pawn");
            return;
        }
        bool bLast = Left <= 0;
        if (DelMar::PlacePlayerOnTrack(bLast) || bLast)
            return;
        DelMar::RunOnGameThreadAfter(5.0, [Left] { RetryPlace(Left - 1); });
    }

    // racer reg
    void RegisterRacerIfNeeded()
    {
        auto PS = FindPlayspaceActor();
        const UObject* RM = PS ? ReadObjAny(PS, "ActiveRaceManager") : nullptr;
        if (!RM)
        {
            auto RMs = ActorsWithClass("RaceManager");
            if (!RMs.empty())
                RM = RMs[0];
        }
        if (!RM)
        {
            DelMar::Log("[track] no race manager yet - nobody to register the racer with");
            return;
        }
        TArray<AFortPlayerControllerAthena*> PCs;
        Utils::GetAll<AFortPlayerControllerAthena>(PCs);
        AFortPlayerControllerAthena* PC = PCs.Num() > 0 ? PCs[0] : nullptr;
        PCs.Free();
        if (!PC)
        {
            DelMar::Log("[track] no player controller to register");
            return;
        }
        auto PState = PC->HasPlayerState() ? (const UObject*)PC->PlayerState : nullptr;
        bool bActive = false;
        if (auto Fn = RM->GetFunction("IsActiveRacer"))
        {
            FReflCall C;
            if (C.Init(RM, Fn))
            {
                C.SetPtr(C.Find("PlayerState"), PState);
                C.Invoke();
                bActive = C.GetBool(C.Ret());
            }
        }
        DelMar::Log("[track] race manager %s: local player %s is %san active racer", ObjRef(RM).c_str(), ObjRef(PC).c_str(), bActive ? "" : "NOT ");
        if (bActive || !FConfiguration::bDelMarRegisterRacer)
            return;
        if (auto Fn = RM->GetFunction("RegisterPlayerController"))
        {
            FReflCall C;
            if (C.Init(RM, Fn))
            {
                C.SetPtr(C.Find("InController"), PC);
                DelMar::Log("[track] calling %s", SignatureEx(Fn).c_str());
                C.Invoke();
            }
        }
        if (auto Fn = RM->GetFunction("RegisterPlayerState"))
        {
            FReflCall C;
            if (C.Init(RM, Fn))
            {
                C.SetPtr(C.Find("InPlayerState"), PState);
                DelMar::Log("[track] calling %s", SignatureEx(Fn).c_str());
                C.Invoke();
            }
        }
        if (auto Fn = RM->GetFunction("FinalizeRegisteredPlayerInitialization"))
        {
            FReflCall C;
            if (C.Init(RM, Fn))
            {
                C.SetPtr(C.Find("PlayerState"), PState);
                DelMar::Log("[track] calling %s", SignatureEx(Fn).c_str());
                C.Invoke();
            }
        }
        DumpObjectImpl(RM, "race-manager", "Racer|Player|State|Vehicle|Checkpoint|Lap|Start|Active|Registered|Config|Mode", 60);
    }

    const UObject* FindRaceManager()
    {
        auto PS = FindPlayspaceActor();
        const UObject* RM = PS ? ReadObjAny(PS, "ActiveRaceManager") : nullptr;
        if (!RM)
        {
            auto RMs = ActorsWithClass("RaceManager");
            if (!RMs.empty())
                RM = RMs[0];
        }
        return RM;
    }

    void RestartLocalPlayer()
    {
        auto World = UWorld::GetWorld();
        auto GM = (World && World->HasAuthorityGameMode()) ? (AFortGameMode*)World->AuthorityGameMode : nullptr;
        TArray<AFortPlayerControllerAthena*> PCs;
        Utils::GetAll<AFortPlayerControllerAthena>(PCs);
        AFortPlayerControllerAthena* PC = PCs.Num() > 0 ? PCs[0] : nullptr;
        PCs.Free();
        if (!GM || !PC)
        {
            DelMar::Log("[track] restart: no game mode / player controller");
            return;
        }
        AActor* Old = PC->HasPawn() ? (AActor*)PC->Pawn : nullptr;
        auto Start = GM->ChoosePlayerStart((AActor*)PC);
        DelMar::Log("[track] RestartPlayer(%s) on %s: pawn before %s, ChoosePlayerStart -> %s", ObjRef(PC).c_str(), ObjRef(GM).c_str(), ObjRef(Old).c_str(), ObjRef(Start).c_str());
        if (!Start)
        {
            DelMar::Log("[track]   no player start resolves - not restarting (the SpawnDefaultPawnFor hook would spin)");
            return;
        }
        if (Old)
            PC->UnPossess();
        GM->RestartPlayer((AActor*)PC);
        AActor* New = PC->HasPawn() ? (AActor*)PC->Pawn : nullptr;
        DelMar::Log("[track]   pawn after: %s%s", ObjRef(New).c_str(), New == Old ? "  (SAME pawn - nothing was spawned)" : "  (spawned by Erbium's SpawnDefaultPawnFor hook at the chosen start)");
        if (Old && New && New != Old)
            Old->K2_DestroyActor();
    }

    void RequestStartRaceIfIdle()
    {
        auto PS = FindPlayspaceActor();
        auto RM = FindRaceManager();
        if (!PS || !RM)
            return;
        auto State = StateTagOf(PS);
        if (!(IContains(State, "WaitingForPlayers") || IContains(State, "Lobby") || IContains(State, "LevelSetup") || IContains(State, "PreRace") || IContains(State, "Countdown")))
        {
            DelMar::Log("[track] state is %s - not requesting the race start", State.c_str());
            return;
        }
        if (auto Fn = RM->GetFunction("RequestStartRace"))
        {
            FReflCall C;
            if (C.Init(RM, Fn))
            {
                C.SetBool(C.Find("bSkipCountdown"), false);
                DelMar::Log("[track] calling %s (state %s)", SignatureEx(Fn).c_str(), State.c_str());
                C.Invoke();
            }
        }
    }

    // joining race
    AFortPlayerControllerAthena* LocalPC()
    {
        TArray<AFortPlayerControllerAthena*> PCs;
        Utils::GetAll<AFortPlayerControllerAthena>(PCs);
        AFortPlayerControllerAthena* PC = PCs.Num() > 0 ? PCs[0] : nullptr;
        PCs.Free();
        return PC;
    }

    const UObject* FindRequestComponent(AFortPlayerControllerAthena* PC)
    {
        const UObject* RC = PC ? FirstObjectOfClass("DelMarRequestComponent", PC) : nullptr;
        if (!RC && PC && PC->HasPlayerState())
            RC = FirstObjectOfClass("DelMarRequestComponent", (const UObject*)PC->PlayerState);
        if (!RC)
            RC = FirstObjectOfClass("DelMarRequestComponent", nullptr);
        return RC;
    }

    const UObject* FindWaitingState()
    {
        auto PS = FindPlayspaceActor();
        auto SM = PS ? StateMachineOf(PS) : nullptr;
        return FirstObjectOfClass("DelMarState_Gameplay_WaitingForPlayers", SM);
    }

    void DumpJoinDiagnostics()
    {
        auto PC = LocalPC();
        auto RC = FindRequestComponent(PC);
        if (RC)
        {
            DelMar::Log("[join] request component %s  outer %s", ObjRef(RC).c_str(), OuterChain(RC).c_str());
            DumpObjectImpl(RC, "join", nullptr, 60);
            DumpFunctionsImpl(CachedName(RC->Class).c_str(), "join-fns");
        }
        else
            DelMar::Log("[join] !! no live DelMarRequestComponent found");
        if (auto WS = FindWaitingState())
        {
            DumpObjectImpl(WS, "waiting", nullptr, 60);
            DumpFunctionsImpl(CachedName(WS->Class).c_str(), "waiting-fns");
        }
        else
            DelMar::Log("[join] no live DelMarState_Gameplay_WaitingForPlayers object");
        DumpObjectsOfClassImpl("DelMarPlayerStateComponent", "ps-comp", true, "Ready|Join|Loaded|Race|Racer|Vehicle|Manager|State|Spectat", 8);
        DumpFunctionsImpl("PlayspaceManagerComponent", "fns");
        if (PC)
            DelMar::Log("[join] PC %s: CheatManager %s, PlayerState %s", ObjRef(PC).c_str(), PropText(PC, "CheatManager").c_str(), PropText(PC, "PlayerState").c_str());
    }

    const UFunction* FindFunctionNamed(const char* Name)
    {
        const int Num = TUObjectArray::Num();
        for (int i = 0; i < Num; i++)
        {
            auto Obj = TUObjectArray::GetObjectByIndex(i);
            if (!Obj || !Obj->Class || !(Obj->Class->GetCastFlags() & CASTCLASS_UFunction))
                continue;
            if (ObjName(Obj) == Name)
                return (const UFunction*)Obj;
        }
        return nullptr;
    }

    void WhereIsFunction(const char* Name)
    {
        auto Fn = FindFunctionNamed(Name);
        if (Fn)
            DelMar::Log("[fn-where] %s runs in %s: %s", Name, OuterChain(Fn).c_str(), SignatureEx(Fn).c_str());
        else
            DelMar::Log("[fn-where] %s: no such UFunction", Name);
    }

    void ReadyUpLocalPlayer()
    {
        auto PC = LocalPC();
        auto RC = FindRequestComponent(PC);
        if (!RC)
        {
            DelMar::Log("[join] !! no DelMarRequestComponent to ready up with");
            return;
        }
        DelMar::Log("[join] before: bIsReadyToJoinRace %s, bIsReadyToStartRace %s", PropText(RC, "bIsReadyToJoinRace").c_str(), PropText(RC, "bIsReadyToStartRace").c_str());
        auto CallWithBool = [](const UObject* Obj, const char* FnName, const char* ParamName, bool Value)
        {
            auto Fn = Obj ? Obj->GetFunction(FnName) : nullptr;
            if (!Fn)
            {
                DelMar::Log("[join] !! %s has no %s", ObjRef(Obj).c_str(), FnName);
                return false;
            }
            FReflCall C;
            if (!C.Init(Obj, Fn))
                return false;
            auto P = C.Find(ParamName);
            if (!P)
            {
                DelMar::Log("[join] !! %s has no parameter %s - %s", FnName, ParamName, SignatureEx(Fn).c_str());
                return false;
            }
            C.SetBool(P, Value);
            DelMar::Log("[join] calling %s with %s=%s", SignatureEx(Fn).c_str(), ParamName, Value ? "true" : "false");
            C.Invoke();
            return true;
        };
        CallWithBool(RC, "ServerSetJoinNextRace", "bInReadyToJoinRace", true);
        CallWithBool(RC, "ServerReadyUp", "bInReadyUp", true);
        DelMar::Log("[join] after:  bIsReadyToJoinRace %s, bIsReadyToStartRace %s", PropText(RC, "bIsReadyToJoinRace").c_str(), PropText(RC, "bIsReadyToStartRace").c_str());
        LogPlayspaceStatus("ready-up");
        DelMar::RunOnGameThreadAfter(2.0, []
        {
            auto RC = FindRequestComponent(LocalPC());
            if (!RC)
                return;
            if (IContains(PropText(RC, "bIsReadyToStartRace"), "false"))
                CallNoArgs(RC, "ServerRequestRacerCountdown", "join");
            DelMar::Log("[join] +2s: bIsReadyToJoinRace %s, bIsReadyToStartRace %s", PropText(RC, "bIsReadyToJoinRace").c_str(), PropText(RC, "bIsReadyToStartRace").c_str());
        });
    }

    void TryCheatSkipWaiting()
    {
        auto PC = LocalPC();
        if (!PC)
            return;
        auto CM = ReadObjAny(PC, "CheatManager");
        if (!CM)
        {
            CallNoArgs(PC, "EnableCheats", "cheat");
            CM = ReadObjAny(PC, "CheatManager");
            DelMar::Log("[cheat] after EnableCheats: CheatManager %s", ObjRef(CM).c_str());
        }
        for (auto Name : { "DelMarReadyUpSelf", "DelMarSkipWaitingForPlayers" })
        {
            auto Fn = FindFunctionNamed(Name);
            if (!Fn)
            {
                DelMar::Log("[cheat] %s: no such UFunction", Name);
                continue;
            }
            auto Owner = (const UClass*)Fn->Outer;
            const UObject* Target = nullptr;
            if (CM && ChainContains(CM->Class, CachedName(Owner).c_str()))
                Target = CM;
            else if (CM)
                Target = FirstObjectOfClass(CachedName(Owner).c_str(), CM);
            if (!Target)
            {
                DelMar::Log("[cheat] %s runs in %s but there is no live instance to call it on (CheatManager %s)", Name, CachedName(Owner).c_str(), ObjRef(CM).c_str());
                continue;
            }
            CallNoArgs(Target, Name, "cheat");
        }
        LogPlayspaceStatus("cheat");
    }

    void WhereAreFunctions(const char* const* Names, int Count)
    {
        if (Count > 16)
            Count = 16;
        const UFunction* Hits[16] = {};
        const int Num = TUObjectArray::Num();
        for (int i = 0; i < Num; i++)
        {
            auto Obj = TUObjectArray::GetObjectByIndex(i);
            if (!Obj || !Obj->Class || !(Obj->Class->GetCastFlags() & CASTCLASS_UFunction))
                continue;
            auto N = ObjName(Obj);
            for (int k = 0; k < Count; k++)
                if (!Hits[k] && N == Names[k])
                    Hits[k] = (const UFunction*)Obj;
        }
        for (int k = 0; k < Count; k++)
        {
            if (Hits[k])
                DelMar::Log("[fn-where] %s runs in %s: %s", Names[k], OuterChain(Hits[k]).c_str(), SignatureEx(Hits[k]).c_str());
            else
                DelMar::Log("[fn-where] %s: no such UFunction", Names[k]);
        }
    }

    void CollectByClass(const char* const* Needles, int Count, const UObject** Out)
    {
        if (Count > 16)
            Count = 16;
        for (int k = 0; k < Count; k++)
            Out[k] = nullptr;
        std::unordered_map<const UClass*, int> Cache;
        const int Num = TUObjectArray::Num();
        for (int i = 0; i < Num; i++)
        {
            auto Obj = TUObjectArray::GetObjectByIndex(i);
            if (!Obj || !Obj->Class || IsTemplateObject(Obj))
                continue;
            auto it = Cache.find(Obj->Class);
            int Mask;
            if (it != Cache.end())
                Mask = it->second;
            else
            {
                Mask = 0;
                for (int k = 0; k < Count; k++)
                    if (ChainContains(Obj->Class, Needles[k]))
                        Mask |= (1 << k);
                Cache[Obj->Class] = Mask;
            }
            if (!Mask)
                continue;
            for (int k = 0; k < Count; k++)
                if ((Mask & (1 << k)) && !Out[k])
                    Out[k] = Obj;
        }
    }

    bool RaceIsLive()
    {
        auto PS = FindPlayspaceActor();
        if (PS && ReadObjAny(PS, "ActiveRaceManager"))
            return true;
        return !ActorsWithClass("RaceManager").empty();
    }

    void DumpDriveDiagnostics()
    {
        ClassNameCache.clear();
        DelMar::Log("[drive] ---- vehicle / controller / input state ----");

        auto PC = LocalPC();
        auto Vehicles = ActorsWithClass("DelMarVehicle");
        AActor* V = Vehicles.empty() ? nullptr : Vehicles[0];
        DelMar::Log("[drive] player controller %s  pawn %s  vehicles in world %d", ObjRef(PC).c_str(),
                    ObjRef(PC && PC->HasPawn() ? (const UObject*)PC->Pawn : nullptr).c_str(), (int)Vehicles.size());

        const char* Needles[] = { "DelMarPlayerInputManagerComponent", "EnhancedInputLocalPlayerSubsystem", "PlayerCameraManager",
                                  "DelMarVehicleCameraMode", "DelMarVehicleManager", "EnhancedPlayerInput",
                                  "DelMarNetworkInputComponent", "NetworkPhysicsComponent", "DelMarGlobalInputDisabler",
                                  "DelMarVehicleMovementSet", "DelMarVehicleAutoInputComponent", "DelMarVehicleMovementComponent" };
        const UObject* Found[12] = {};
        if (FConfiguration::bDelMarObjectScans)
        {
            CollectByClass(Needles, 12, Found);
            for (int k = 0; k < 12; k++)
                DelMar::Log("[drive] %-34s -> %s", Needles[k], ObjRef(Found[k]).c_str());
        }

        const char* PawnF = "Owner|Instigator|Controller|Role|Remote|Replicat|Input|Physic|Movement|Driver|Seat|Pawn|Player|Tag|Enable|Local|Archetype|Body|Cosmetic|Camera|Simulat|Sleep|Mass|Vehicle";
        if (V)
        {
            DelMar::Log("[drive] vehicle %s", ObjRef(V).c_str());
            DumpObjectImpl(V, "drive-vehicle", PawnF, 140);
            if (FConfiguration::bDelMarObjectScans)
                DumpFunctionsImpl(CachedName(V->Class).c_str(), "drive-vfns");
        }
        else
            DelMar::Log("[drive] !! no DelMarVehicle actor in the world");

        if (PC)
        {
            DumpObjectImpl(PC, "drive-pc", "Pawn|Acknowledge|Input|Camera|View|Player|Cheat|Enable|Ignore", 100);
            if (PC->HasPawn())
                DumpObjectImpl((const UObject*)PC->Pawn, "drive-pawn", PawnF, 100);
        }
        for (int k = 0; k < 12; k++)
            if (Found[k])
                DumpObjectImpl(Found[k], "drive-obj", nullptr, 60);

        if (FConfiguration::bDelMarObjectScans)
            DumpObjectsOfClassImpl("DelMarRacerState", "drive-racer", true, nullptr, 4);
        DelMar::Log("[drive] ---- end ----");
    }

    // missing ehicle body
    // EVehicleCosmeticsFailureReason::ActiveArchetypeNoBodyEquipped, /DelMarGame/Vehicle/DelMar_BodySetupMap
    int WriteTagPropsContaining(const UObject* Obj, const char* Needle, const wchar_t* TagText, const char* Tag)
    {
        if (!Obj || IsBadReadPtr(Obj, 0x30) || !Obj->Class)
            return 0;
        FName Wanted(TagText);
        if (Wanted.ComparisonIndex <= 0)
        {
            DelMar::Log("[%s] !! the tag %ls is not in this build's name pool", Tag, TagText);
            return 0;
        }
        int Written = 0;
        for (const UStruct* C = Obj->Class; C; C = C->GetSuper())
        {
            if (IsBaseClassName(CachedName(C)))
                break;
            for (auto P = C->GetChildProperties(); P; P = P->FField_GetNext())
            {
                auto PN = P->FField_GetName().ToString();
                if (!IContains(PN, Needle))
                    continue;
                auto Off = GetFromOffset<uint32>(P, Offsets::Offset_Internal);
                auto Sz = GetFromOffset<uint32>(P, Offsets::ElementSize);
                auto Type = FieldTypeName(P);
                UEAllocatedString StructName;
                if (Type == "StructProperty")
                    if (auto SS = SubStruct(P))
                        StructName = CachedName((const UStruct*)SS);
                DelMar::Log("[%s]   %s::%s (%s%s%s @0x%x/%u) = %s", Tag, CachedName(C).c_str(), PN.c_str(), Type.c_str(),
                            StructName.empty() ? "" : "/", StructName.c_str(), Off, Sz, FormatValueAt(P, (const uint8*)Obj + Off, 0).c_str());
                bool bTagLike = (Type == "StructProperty" && StructName == "GameplayTag" && Sz == 4) || Type == "NameProperty";
                if (!bTagLike)
                    continue;
                *(int32*)((uint8*)Obj + Off) = Wanted.ComparisonIndex;
                DelMar::Log("[%s]     -> wrote %ls, now %s", Tag, TagText, FormatValueAt(P, (const uint8*)Obj + Off, 0).c_str());
                Written++;
            }
        }
        return Written;
    }

    void EquipDefaultArchetype()
    {
        ClassNameCache.clear();
        auto Vehicles = ActorsWithClass("DelMarVehicle");
        if (Vehicles.empty())
        {
            DelMar::Log("[body] no DelMarVehicle yet - nothing to give a body to");
            return;
        }
        auto V = Vehicles[0];
        DelMar::Log("[body] vehicle %s: looking for the archetype / body setup", ObjRef(V).c_str());

        int Written = WriteTagPropsContaining(V, "Archetype", FConfiguration::DelMarVehicleArchetypeTag, "body");
        const UObject* Cos = ReadObjAny(V, "CosmeticComponent");
        if (Cos)
        {
            DelMar::Log("[body] cosmetic component %s", ObjRef(Cos).c_str());
            Written += WriteTagPropsContaining(Cos, "Archetype", FConfiguration::DelMarVehicleArchetypeTag, "body");
            DumpObjectImpl(Cos, "body-cos", "Archetype|Body|Setup|Loadout|Item|Active|Default", 60);
        }
        DumpObjectImpl(V, "body-veh", "Archetype|BodySetup|Body|Axle|Wheel|Suspension|Loadout|Cosmetic", 80);
        for (const UStruct* C = V->Class; C; C = C->GetSuper())
        {
            if (IsBaseClassName(CachedName(C)))
                break;
            for (auto F = C->GetChildren(); F; F = F->GetNext())
                if (F->Class && (F->Class->GetCastFlags() & CASTCLASS_UFunction))
                {
                    auto N = ObjName(F);
                    if (IContains(N, "Archetype") || IContains(N, "BodySetup") || IContains(N, "Cosmetic") || IContains(N, "Loadout"))
                        DelMar::Log("[body]   fn %s", SignatureEx((const UFunction*)F).c_str());
                }
        }
        DelMar::Log("[body] wrote %d archetypeish propert%s", Written, Written == 1 ? "y" : "ies");
        if (Written)
            DelMar::RunOnGameThreadAfter(2.0, []
            {
                auto Vs = ActorsWithClass("DelMarVehicle");
                if (!Vs.empty())
                    DumpObjectImpl(Vs[0], "body-after", "Archetype|BodySetup|Axle|Wheel|Suspension", 40);
            });
    }

    bool WriteStructFloat(const UObject* Obj, const char* StructPropName, const char* FieldName, float Value)
    {
        auto P = Obj ? Obj->GetProperty(StructPropName) : nullptr;
        if (!P || FieldTypeName(P) != "StructProperty")
            return false;
        auto Base = GetFromOffset<uint32>(P, Offsets::Offset_Internal);
        auto SS = (const UStruct*)SubStruct(P);
        if (!SS)
            return false;
        for (auto F = SS->GetChildProperties(); F; F = F->FField_GetNext())
        {
            if (F->FField_GetName().ToString() != FieldName)
                continue;
            if (FieldTypeName(F) != "FloatProperty")
                return false;
            auto Off = GetFromOffset<uint32>(F, Offsets::Offset_Internal);
            *(float*)((uint8*)Obj + Base + Off) = Value;
            return true;
        }
        return false;
    }

    bool ReadStructFloat(const UObject* Obj, const char* StructPropName, const char* FieldName, float* Out)
    {
        auto P = Obj ? Obj->GetProperty(StructPropName) : nullptr;
        if (!P || FieldTypeName(P) != "StructProperty")
            return false;
        auto Base = GetFromOffset<uint32>(P, Offsets::Offset_Internal);
        auto SS = (const UStruct*)SubStruct(P);
        if (!SS)
            return false;
        for (auto F = SS->GetChildProperties(); F; F = F->FField_GetNext())
        {
            if (F->FField_GetName().ToString() != FieldName)
                continue;
            if (FieldTypeName(F) != "FloatProperty")
                return false;
            auto Off = GetFromOffset<uint32>(F, Offsets::Offset_Internal);
            auto Addr = (const uint8*)Obj + Base + Off;
            if (IsBadReadPtr(Addr, 4))
                return false;
            *Out = *(const float*)Addr;
            return true;
        }
        return false;
    }

    bool bObserveStarted = false;
    float ObservePeakFwd = 0.f, ObservePeakSteer = 0.f, ObservePeakPitch = 0.f, ObservePeakRight = 0.f;
    void ObservePlayerInputTick(int Left)
    {
        auto Vs = ActorsWithClass("DelMarVehicle");
        if (Vs.empty())
        {
            if (Left > 0)
                DelMar::RunOnGameThreadAfter(0.2, [Left] { ObservePlayerInputTick(Left - 1); });
            return;
        }
        auto V = Vs[0];
        if (!bObserveStarted)
        {
            bObserveStarted = true;
            DelMar::Log("[playerinput] ================================================================");
            DelMar::Log("[playerinput] now need to test if input works. - reading");
            DelMar::Log("[playerinput] DelMarVehicle::PendingDriverInputState for the next ~%.0f seconds.", Left * 0.2);
            DelMar::Log("[playerinput] ================================================================");
        }
        auto Peak = [](float v, float& peak) { float a = v < 0 ? -v : v; if (a > peak) peak = a; };
        float f = 0.f;
        if (ReadStructFloat(V, "PendingDriverInputState", "ForwardAlpha", &f)) Peak(f, ObservePeakFwd);
        if (ReadStructFloat(V, "PendingDriverInputState", "SteerAlpha", &f)) Peak(f, ObservePeakSteer);
        if (ReadStructFloat(V, "PendingDriverInputState", "PitchAlpha", &f)) Peak(f, ObservePeakPitch);
        if (ReadStructFloat(V, "PendingDriverInputState", "RightAlpha", &f)) Peak(f, ObservePeakRight);
        if (Left % 5 == 0)
            DelMar::Log("[playerinput] peak so far: Forward=%.3f Steer=%.3f Pitch=%.3f Right=%.3f",
                        ObservePeakFwd, ObservePeakSteer, ObservePeakPitch, ObservePeakRight);
        if (Left > 0)
            DelMar::RunOnGameThreadAfter(0.2, [Left] { ObservePlayerInputTick(Left - 1); });
        else
        {
            bool any = ObservePeakFwd > 0.01f || ObservePeakSteer > 0.01f || ObservePeakPitch > 0.01f || ObservePeakRight > 0.01f;
            DelMar::Log("[playerinput] ==== VERDICT: peak Forward=%.3f Steer=%.3f Pitch=%.3f Right=%.3f. %s ====",
                        ObservePeakFwd, ObservePeakSteer, ObservePeakPitch, ObservePeakRight,
                        any ? "player its Enhanced Input IS populating PendingDriverInputState - input actually reaches vehicle, so the break is basically DOWNSTREAM (marshal gate / the forced test its own verdict tells the rest)"
                            : "PendingDriverInputState didnt move while holding throttle - Enhanced Input is therefore NOT populating it, so the break is UPSTREAM (input binding / possession); this dll will have to giv Pending or the marshal output itself instead");
        }
    }

    FVector ThrottleTestStart;
    bool bThrottleTestStarted = false;
    int PendingClearsSeen = 0;

    void ForceThrottleTick(int Left)
    {
        auto Vs = ActorsWithClass("DelMarVehicle");
        if (Vs.empty())
        {
            DelMar::Log("[throttle] no DelMarVehicle - aborting");
            return;
        }
        auto V = Vs[0];
        if (bThrottleTestStarted && !IsBadReadPtr((const uint8*)V + 0x2180, 4)
            && *(const float*)((const uint8*)V + 0x2180) == 0.0f)
            ++PendingClearsSeen;
        bool bWrote = WriteStructFloat(V, "PendingDriverInputState", "ForwardAlpha", 1.0f);
        WriteStructFloat(V, "PendingDriverInputState", "SteerAlpha", 0.0f);
        WriteStructFloat(V, "PendingDriverInputState", "RightAlpha", 0.0f);
        auto Loc = V->K2_GetActorLocation();
        if (!bThrottleTestStarted)
        {
            bThrottleTestStarted = true;
            ThrottleTestStart = Loc;
            DelMar::Log("[throttle] holding ForwardAlpha = 1 on %s for ~6 s (%s). start (%.0f, %.0f, %.0f)",
                        ObjRef(V).c_str(), bWrote ? "written" : "!! WRITE FAILED - property/field not found", Loc.X, Loc.Y, Loc.Z);
        }
        if (Left % 10 == 0)
        {
            auto dx = Loc.X - ThrottleTestStart.X, dy = Loc.Y - ThrottleTestStart.Y, dz = Loc.Z - ThrottleTestStart.Z;
            DelMar::Log("[throttle] t-%d: at (%.0f, %.0f, %.0f), moved %.1f cm from the start", Left / 10, Loc.X, Loc.Y, Loc.Z,
                        (float)sqrt(dx * dx + dy * dy + dz * dz));
        }
        if (Left > 0)
            DelMar::RunOnGameThreadAfter(0.1, [Left] { ForceThrottleTick(Left - 1); });
        else
        {
            auto dx = Loc.X - ThrottleTestStart.X, dy = Loc.Y - ThrottleTestStart.Y, dz = Loc.Z - ThrottleTestStart.Z;
            auto Dist = (float)sqrt(dx * dx + dy * dy + dz * dz);
            DelMar::Log("[throttle] ==== VERDICT: car moved %.1f cm on a forced throttle write %s ====", Dist,
                        Dist > 50.f ? "simzlation responses"
                                    : "sumulation is dead");
            DelMar::Log("[throttle] pending consumed %d time(s) during the test %s", PendingClearsSeen,
                        PendingClearsSeen > 0
                            ? "The producers local branch is sterted - means pending is being marshalled into the rollback sim, so the playerits keys should drive too it"
                            : "Pending was never cleared -rollback input producer is not consuming it; see the [rbinput] logs");
            DumpObjectImpl(V, "throttle-after", "PendingDriverInputState|Velocity|Speed|Simulat|Physic|Sleep|Awake|Throttle|Steer", 40);
        }
    }

    // FortVehicleSkelMeshComponent has PhysicsAssetOverride = SK_VEH_Octane_BoxPhysics,
    const UObject* FindComponentOn(const AActor* A, const char* ClassNeedle)
    {
        if (!A || IsBadReadPtr(A, 0x30) || !A->Class)
            return nullptr;
        for (const UStruct* C = A->Class; C; C = C->GetSuper())
        {
            if (IsBaseClassName(CachedName(C)))
                break;
            for (auto P = C->GetChildProperties(); P; P = P->FField_GetNext())
            {
                if (FieldTypeName(P) != "ObjectProperty")
                    continue;
                auto Off = GetFromOffset<uint32>(P, Offsets::Offset_Internal);
                auto Val = *(const UObject**)((const uint8*)A + Off);
                if (!Val || IsBadReadPtr(Val, 0x30) || !Val->Class)
                    continue;
                if (ChainContains(Val->Class, ClassNeedle))
                    return Val;
            }
        }
        return nullptr;
    }

    bool CallBoolNameArg(const UObject* Obj, const char* FnName, bool& bOut)
    {
        auto Fn = Obj ? Obj->GetFunction(FnName) : nullptr;
        if (!Fn)
            return false;
        FReflCall C;
        if (!C.Init(Obj, Fn))
            return false;
        if (auto P = C.Find("BoneName"))
            C.SetNameIdx(P, 0);
        C.Invoke();
        bOut = C.GetBool(C.Ret());
        return true;
    }

    void ReportPhysics(const UObject* Comp, const char* Tag)
    {
        if (!Comp)
        {
            DelMar::Log("[phys] %s: no component", Tag);
            return;
        }
        bool bSim = false, bAwake = false;
        bool bHasSim = CallBoolNameArg(Comp, "IsSimulatingPhysics", bSim);
        bool bHasAwake = CallBoolNameArg(Comp, "IsAnyRigidBodyAwake", bAwake);
        UEAllocatedString MassText = "?";
        if (auto Fn = Comp->GetFunction("GetMass"))
        {
            FReflCall C;
            if (C.Init(Comp, Fn))
            {
                C.Invoke();
                if (auto R = C.Ret())
                {
                    char b[64];
                    _snprintf_s(b, sizeof(b), _TRUNCATE, "%.1f", *(float*)C.At(R));
                    MassText = b;
                }
            }
        }
        DelMar::Log("[phys] %s %s: IsSimulatingPhysics=%s  IsAnyRigidBodyAwake=%s  Mass=%s", Tag, ObjRef(Comp).c_str(),
                    bHasSim ? (bSim ? "TRUE" : "false") : "<no such fn>", bHasAwake ? (bAwake ? "TRUE" : "false") : "<no such fn>", MassText.c_str());
    }

    void PhysicsProbe()
    {
        ClassNameCache.clear();
        auto Vs = ActorsWithClass("DelMarVehicle");
        if (Vs.empty())
        {
            DelMar::Log("[phys] no DelMarVehicle");
            return;
        }
        auto V = Vs[0];
        auto Mesh = FindComponentOn(V, "SkeletalMeshComponent");
        if (!Mesh)
            Mesh = FindComponentOn(V, "PrimitiveComponent");
        DelMar::Log("[phys] vehicle %s  body component %s", ObjRef(V).c_str(), ObjRef(Mesh).c_str());
        if (Mesh)
            DelMar::Log("[phys]   class chain: %s", DelMar::ClassChain(Mesh).c_str());
        DumpObjectImpl(V, "phys-sleep", "ShouldSleepAtSpawn|SpawnedSleepLock|ForceKinematic|Asleep|Sleep|Static", 30);
        if (auto Fn = V->GetFunction("IsAsleep"))
        {
            FReflCall C;
            if (C.Init(V, Fn))
            {
                C.Invoke();
                DelMar::Log("[phys] FortAthenaVehicle::IsAsleep() = %s", C.GetBool(C.Ret()) ? "TRUE" : "false");
            }
        }
        if (FConfiguration::bDelMarBreakSleepLock)
        {
            CallNoArgs(V, "BreakSpawnedSleepLock", "phys");
            if (auto Fn = V->GetFunction("SetStaticPhysics"))
            {
                FReflCall C;
                if (C.Init(V, Fn))
                {
                    if (auto P = C.Find("bStatic"))
                        C.SetBool(P, false);
                    DelMar::Log("[phys] calling %s with false", SignatureEx(Fn).c_str());
                    C.Invoke();
                }
            }
            if (auto Fn = V->GetFunction("SetShouldSleepAtSpawn"))
            {
                FReflCall C;
                if (C.Init(V, Fn))
                {
                    if (auto P = C.Find("bNewValue"))
                        C.SetBool(P, false);
                    C.Invoke();
                    DelMar::Log("[phys] SetShouldSleepAtSpawn(false) called");
                }
            }
            if (WriteFloatProp(V, "ForceKinematicOnClientCount", 0.f))
                DelMar::Log("[phys] ForceKinematicOnClientCount written to 0");
            DumpObjectImpl(V, "phys-sleep-after", "ShouldSleepAtSpawn|SpawnedSleepLock|ForceKinematic|Sleep|Static", 30);
        }

        ReportPhysics(Mesh, "before");
        if (!Mesh)
            return;

        auto Before = V->K2_GetActorLocation();

        // 1. wake it
        if (CallNoArgs(Mesh, "WakeAllRigidBodies", "phys"))
            ReportPhysics(Mesh, "after WakeAllRigidBodies");

        if (FConfiguration::bDelMarPhysicsProbeForce)
        {
            if (auto Fn = Mesh->GetFunction("SetSimulatePhysics"))
            {
                FReflCall C;
                if (C.Init(Mesh, Fn))
                {
                    if (auto P = C.Find("bSimulate"))
                        C.SetBool(P, true);
                    DelMar::Log("[phys] calling %s with bSimulate=true", SignatureEx(Fn).c_str());
                    C.Invoke();
                }
                ReportPhysics(Mesh, "after SetSimulatePhysics(true)");
            }

            if (auto Fn = Mesh->GetFunction("AddImpulse"))
            {
                FReflCall C;
                if (C.Init(Mesh, Fn))
                {
                    if (auto P = C.Find("Impulse"))
                    {
                        float* Vec = (float*)C.At(P);
                        auto Sz = 0u;
                        if (auto Prop = C.Find("Impulse"))
                            Sz = Prop->Size;
                        if (Sz >= 24)
                        {
                            double* D = (double*)C.At(P);
                            D[0] = 0.0; D[1] = 0.0; D[2] = FConfiguration::DelMarPhysicsProbeImpulse;
                        }
                        else
                        {
                            Vec[0] = 0.f; Vec[1] = 0.f; Vec[2] = (float)FConfiguration::DelMarPhysicsProbeImpulse;
                        }
                    }
                    if (auto P = C.Find("BoneName"))
                        C.SetNameIdx(P, 0);
                    if (auto P = C.Find("bVelChange"))
                        C.SetBool(P, true);
                    DelMar::Log("[phys] calling %s with Impulse=(0,0,%.0f) bVelChange=true", SignatureEx(Fn).c_str(), (double)FConfiguration::DelMarPhysicsProbeImpulse);
                    C.Invoke();
                }
            }
        }

        if (auto Fn = V->GetFunction("UpdateClientWithVehicleTestInput"))
        {
            FReflCall C;
            if (C.Init(V, Fn))
            {
                if (auto P = C.Find("LinearVelocity"))
                {
                    if (P->Size >= 24)
                    {
                        double* D = (double*)C.At(P);
                        D[0] = FConfiguration::DelMarPhysicsProbeImpulse * 2.0; D[1] = 0.0; D[2] = 0.0;
                    }
                    else
                    {
                        float* Fv = (float*)C.At(P);
                        Fv[0] = (float)FConfiguration::DelMarPhysicsProbeImpulse * 2.f; Fv[1] = 0.f; Fv[2] = 0.f;
                    }
                }
                DelMar::Log("[phys] calling %s with LinearVelocity=(%.0f,0,0)", SignatureEx(Fn).c_str(), FConfiguration::DelMarPhysicsProbeImpulse * 2.0);
                C.Invoke();
            }
        }
        else
            DelMar::Log("[phys] no UpdateClientWithVehicleTestInput on this vehicle");

        DelMar::RunOnGameThreadAfter(1.5, [Before]
        {
            auto Vs = ActorsWithClass("DelMarVehicle");
            if (Vs.empty())
                return;
            auto V = Vs[0];
            auto Now = V->K2_GetActorLocation();
            auto dx = Now.X - Before.X, dy = Now.Y - Before.Y, dz = Now.Z - Before.Z;
            auto Dist = (float)sqrt(dx * dx + dy * dy + dz * dz);
            auto Mesh = FindComponentOn(V, "SkeletalMeshComponent");
            ReportPhysics(Mesh, "1.5 s after the impulse");
            DelMar::Log("[phys] ==== body traveled %.1f cm after wake + impulse. %s ====", Dist,
                        Dist > 5.f ? "rigid body works and responds"
                                   : "rigid body does NOT respond at all - not simulating in this world");
        });
    }

    struct FMutatorSpec { const wchar_t* Path; const char* NativeClass; };
    const FMutatorSpec DelMarMutators[] = {
        { L"/DelMarGame/Mutators/DelMarAsyncPhysicsTickMutator_BP.DelMarAsyncPhysicsTickMutator_BP_C", "DelMarAsyncPhysicsTickMutator" },
        { nullptr,                                                                                     "DelMarNetworkPredictionMutator" },
        { L"/DelMarGame/Mutators/DelMarConsoleVariableMutator_CCD.DelMarConsoleVariableMutator_CCD_C",  "DelMarConsoleVariableMutator" },
        { L"/DelMarGame/Mutators/DM_DisableAthenaWheelSignificance.DM_DisableAthenaWheelSignificance_C", nullptr },
        { L"/DelMarGame/Mutators/DM_DisableClientRPCTimeoutMutator.DM_DisableClientRPCTimeoutMutator_C", nullptr },
    };

    void DumpMutators(const char* Tag)
    {
        int n = 0;
        auto World = UWorld::GetWorld();
        auto GM = (World && World->HasAuthorityGameMode()) ? World->AuthorityGameMode : nullptr;
        if (GM)
            DumpObjectImpl(GM, Tag, "Mutator", 30);
        for (auto A : ActorsWithClass("Mutator"))
        {
            DelMar::Log("[%s]   mutator actor %s : %s", Tag, ObjRef(A).c_str(), DelMar::ClassChain(A).c_str());
            n++;
        }
        DelMar::Log("[%s] %d mutator actor(s) in the world", Tag, n);
    }

    void SpawnDelMarMutators()
    {
        ClassNameCache.clear();
        DelMar::Log("[mutators] ---- DelMarGame GameFeatureData's AddMutators action never got a chance to get created here ----");
        DumpMutators("mut-before");

        int Spawned = 0;
        for (auto& M : DelMarMutators)
        {
            const UClass* Cls = nullptr;
            if (M.Path)
                Cls = FindObject<UClass>(M.Path);
            if (!Cls && M.NativeClass)
                Cls = FindClass(M.NativeClass);
            if (!Cls)
            {
                DelMar::Log("[mutators] !! could not resolve %ls / %s", M.Path ? M.Path : L"(no path)", M.NativeClass ? M.NativeClass : "(no native class)");
                continue;
            }
            // already there?
            bool bHave = false;
            for (auto A : ActorsWithClass(CachedName(Cls).c_str()))
                if (A) { bHave = true; break; }
            if (bHave)
            {
                DelMar::Log("[mutators] %s already exists - skipping", CachedName(Cls).c_str());
                continue;
            }
            auto A = UWorld::SpawnActor((UClass*)Cls, FVector(), FRotator());
            DelMar::Log("[mutators] spawned %s -> %s", CachedName(Cls).c_str(), ObjRef(A).c_str());
            if (A)
            {
                Spawned++;
                for (auto Fn : { "OnMutatorEnabled", "OnMutatorInitialized", "MutatorEnabled", "K2_OnMutatorEnabled" })
                    if (A->GetFunction(Fn))
                        CallNoArgs(A, Fn, "mutators");
                DumpObjectImpl(A, "mut-obj", nullptr, 40);
            }
        }
        DelMar::Log("[mutators] spawned %d mutator(s)", Spawned);
        DelMar::RunOnGameThreadAfter(3.0, [] { DumpMutators("mut-after"); });
    }

    // ChaosRollback
    const UObject* FindNetModelSubsystem()
    {
        return FirstObjectOfClass("DelMarNetModelSubsystem", nullptr);
    }

    void ProbeNetModel(const char* Tag, bool bAllowWrite)
    {
        ClassNameCache.clear();
        auto Sub = FindNetModelSubsystem();
        if (!Sub)
        {
            DelMar::Log("[netmodel] %s: !! no UDelMarNetModelSubsystem in this world - the AddWorldSubsystem action did not created (same failure class as the mutators)", Tag);
            return;
        }
        DelMar::Log("[netmodel] %s: %s : %s", Tag, ObjRef(Sub).c_str(), DelMar::ClassChain(Sub).c_str());
        DumpObjectImpl(Sub, "netmodel", nullptr, 40);

        if (auto Fn = Sub->GetFunction("GetNetModel"))
        {
            FReflCall C;
            if (C.Init(Sub, Fn))
            {
                C.Invoke();
                if (auto R = C.Ret())
                    DelMar::Log("[netmodel] %s: GetNetModel() = %d  (0 = ClientAuthoritative, 1 = ChaosRollback)", Tag, (int)*(uint8*)C.At(R));
            }
        }

        if (!IsBadReadPtr((const uint8*)Sub + 0x30, 1))
        {
            uint8 Raw = *((const uint8*)Sub + 0x30);
            DelMar::Log("[netmodel] %s: byte at +0x30 = %u -> %s", Tag, Raw,
                        Raw == 1 ? "ChaosRollback (the rollback callback SHOULD exist)" : "ClientAuthoritative (the rollback callback is NEVER built)");

            if (bAllowWrite && Raw != 1 && FConfiguration::bDelMarForceChaosRollback)
            {
                *((uint8*)Sub + 0x30) = 1;
                DelMar::Log("[netmodel] %s: wrote 1 (ChaosRollback). NOTE: the sim callbacks are created once per "
                            "world so if that already happened this write is too late to build callback B so the "
                            "reading above is the result that is the right one anyway", Tag);
            }
        }
        else
            DelMar::Log("[netmodel] %s: +0x30 is not readable", Tag);
    }

    void PushStateIfStillWaiting(const wchar_t* Tag, const char* Label)
    {
        auto PS = FindPlayspaceActor();
        if (!PS)
            return;
        auto State = StateTagOf(PS);
        if (!IContains(State, "WaitingForPlayers"))
        {
            DelMar::Log("[track] %s: state is %s - the race advanced on its own, not pushing", Label, State.c_str());
            return;
        }
        RequestStateTag(PS, Tag);
        LogPlayspaceStatus(Label);
    }

    void CheckPrimaryAssetIds(const wchar_t* TypeName)
    {
        DelMar::Log("[assets] checking primary asset type %ls", TypeName);
        auto KSL = UKismetSystemLibrary::GetDefaultObj();
        auto Fn = KSL ? KSL->GetFunction("GetPrimaryAssetIdList") : nullptr;
        if (!Fn)
        {
            DelMar::Log("[assets] UKismetSystemLibrary::GetPrimaryAssetIdList is not reflected (KSL %p)", KSL);
            return;
        }
        FReflCall C;
        if (!C.Init(KSL, Fn))
            return;
        auto Out = C.Find("OutPrimaryAssetIdList");
        auto TypeP = C.Find("PrimaryAssetType");
        if (!Out || !TypeP)
        {
            DelMar::Log("[assets] unexpected sig: %s", SignatureEx(Fn).c_str());
            return;
        }
        FName TN(TypeName);
        C.SetNameIdx(TypeP, TN.ComparisonIndex);
        C.Invoke();
        auto& Arr = *(TArray<uint8>*)C.At(Out);
        auto Inner = SubField(Out->Prop, 0);
        auto ISize = Inner ? GetFromOffset<uint32>(Inner, Offsets::ElementSize) : 8u;
        int n = Arr.Num();
        DelMar::Log("[assets] primary asset type %ls: %d id(s)", TypeName, n);
        for (int i = 0; i < n && i < 40 && Arr.Data && ISize; i++)
            DelMar::Log("[assets]   %s", FormatStruct(Inner ? SubStruct(Inner) : nullptr, Arr.Data + i * ISize, ISize, 1).c_str());
        Arr.Free();
    }

    // diag
    void DiagnosticsBefore()
    {
        PropSubOffset();
        StructSubOffset();
        EnumSubOffset(true);
        EnumSubOffset(false);
        auto World = UWorld::GetWorld();
        if (!World)
            return;
        const char* F = "Playspace|Island|Link|Level|Track|Hermes|Playlist|Race|Project|Config|Manager|Mnemonic";
        DumpObjectImpl(World->HasAuthorityGameMode() ? World->AuthorityGameMode : nullptr, "gm", F, 60);
        DumpObjectImpl(World->HasGameState() ? World->GameState : nullptr, "gs", F, 60);
        DumpObjectsOfClassImpl("CreatePlayspacesFromConfig", "gfa", true, nullptr, 20);
        auto LD = FindObject<UObject>(FConfiguration::DelMarTrackLevelDataPath);
        if (LD)
            DumpObjectImpl(LD, "leveldata", nullptr, 60);
        else
            DelMar::Log("[leveldata] !! %ls not found/loadable", FConfiguration::DelMarTrackLevelDataPath);
    }

    void DiagnosticsAfter()
    {
        auto PS = FindPlayspaceActor();
        if (PS)
        {
            DumpObjectImpl(PS, "playspace", nullptr, 120);
            if (auto SM = StateMachineOf(PS))
                DumpObjectImpl(SM, "state-machine", nullptr, 60);
            if (auto LM = ReadObjAny(PS, "LevelManager"))
            {
                DumpObjectImpl(LM, "level-manager", nullptr, 80);
                DumpFunctionsImpl(CachedName(LM->Class).c_str(), "fns");
            }
            if (auto LS = ReadObjAny(PS, "LevelStreamComponent"))
                DumpObjectImpl(LS, "level-stream", nullptr, 60);
        }
        DumpObjectsOfClassImpl("RaceManager", "objs", true, nullptr, 3);
        DumpObjectsOfClassImpl("DelMarVehicle", "objs", false, nullptr, 8);
        for (auto N : { "DelMarPlayspace", "DelMarStateMachine", "DelMarRaceManager", "DelMarVehicleManager", "DelMarState_Setup", "DelMarState_Loading", "DelMarState_LevelSetup", "DelMarState_Lobby" })
            DumpFunctionsImpl(N, "fns");
    }

    void WhenTrueImpl(std::shared_ptr<std::function<bool()>> Cond, std::shared_ptr<std::function<void()>> Then, double Remaining, double PollS, const char* Label);

    void WhenTrueImpl(std::shared_ptr<std::function<bool()>> Cond, std::shared_ptr<std::function<void()>> Then, double Remaining, double PollS, const char* Label)
    {
        bool bOk = false;
        bOk = (*Cond)();
        if (bOk || Remaining <= 0.0)
        {
            DelMar::Log("[track] %s: %s", Label, bOk ? "ready - continuing" : "timeout - continuing anyway");
            (*Then)();
            return;
        }
        DelMar::RunOnGameThreadAfter(PollS, [Cond, Then, Remaining, PollS, Label] { WhenTrueImpl(Cond, Then, Remaining - PollS, PollS, Label); });
    }

    void WhenTrue(std::function<bool()> Cond, std::function<void()> Then, double TimeoutS, const char* Label, double PollS = 1.0)
    {
        WhenTrueImpl(std::make_shared<std::function<bool()>>(std::move(Cond)), std::make_shared<std::function<void()>>(std::move(Then)), TimeoutS, PollS, Label);
    }

    bool bStepRan[12] = {};
    bool ClaimStep(int N, const char* Label)
    {
        if (bStepRan[N])
        {
            DelMar::Log("[track] %s already ran - skipping duplicate", Label);
            return false;
        }
        bStepRan[N] = true;
        return true;
    }

    //baby steps
    bool bTrackBringUpScheduled = false;
    void StepS7();
    void SpawnDelMarMutators();
    void ProbeNetModel(const char* Tag, bool bAllowWrite);

    void StepFallback()
    {
        ClassNameCache.clear();
        DelMar::Log("[track] fallback check (%.0f s after world-ready)", FConfiguration::DelMarStreamFallbackDelayS);
        DelMar::DumpWorldSnapshot("fallback");
        //   UDelMarLevelManagerComponent::GetDesiredBuiltinLevelDataAsset found 61 number of LevelDataAssets
        if (AnyDelMarLevelStreaming())
        {
            DelMar::Log("[track] a DelMar track level is streaming - no stream needed");
            DelMar::RunOnGameThreadAfter(10.0, [] { RetryPlace(6); });
            return;
        }

        const bool bHaveRaceManager = FindRaceManager() != nullptr;
        if (bHaveRaceManager)
            DelMar::Log("[track] a race manager exists but nothing streamed - race is set up on an empty world, so the stream fallback still has much to do work");
        if (!FConfiguration::bStreamTrack)
        {
            DelMar::Log("[track] nothing streamed and bStreamTrack is off - leaving the world as it is");
            return;
        }
        DelMar::RunOnGameThreadAfter(12.0, [] { DelMar::DumpWorldSnapshot("fallback+12s"); RetryPlace(8); });
        DelMar::Log("[track] nothing streamed - streaming DelMarTrackWorlds directly (fallback)");
        DelMar::RunOnGameThread([] { DirectStreamLevelDataWorlds(); });
    }

    void StepS8()
    {
        if (!ClaimStep(8, "S8"))
            return;
        ClassNameCache.clear();
        DelMar::Log("[track] S8: backups if WaitingForPlayers did not release");
        DelMar::RunOnGameThreadAfter(30.0, [] { DelMar::DumpWorldSnapshot("S8+30s"); });
        DelMar::RunOnGameThreadAfter(70.0, [] { DelMar::DumpWorldSnapshot("S8+70s"); });
        LogPlayspaceStatus("S8");
        auto PS = FindPlayspaceActor();
        if (!PS)
            return;
        auto State = StateTagOf(PS);
        if (!IContains(State, "WaitingForPlayers"))
        {
            DelMar::Log("[track] state is %s - readyup released wait, no backups needed", State.c_str());
            return;
        }
        if (FConfiguration::bDelMarCheatSkipWaiting)
            DelMar::RunOnGameThread(TryCheatSkipWaiting);
        if (FConfiguration::bDelMarPushStateMachine)
            DelMar::RunOnGameThreadAfter(3.0, [] { PushStateIfStillWaiting(L"DelMar.Game.State.Gameplay.PreRace", "S8+3s push PreRace"); });
        if (FConfiguration::bDelMarRequestStartRace)
            DelMar::RunOnGameThreadAfter(8.0, RequestStartRaceIfIdle);
        if (FConfiguration::bDelMarEquipArchetype)
            DelMar::RunOnGameThreadAfter(2.0, []
            {
                WhenTrue([] { return !ActorsWithClass("DelMarVehicle").empty(); }, [] { EquipDefaultArchetype(); }, 40.0, "waiting for the vehicle");
            });
        if (FConfiguration::bDelMarDriveDiagnostics)
            DelMar::RunOnGameThreadAfter(28.0, DumpDriveDiagnostics);
        if (FConfiguration::bDelMarNetModelProbe)
            DelMar::RunOnGameThreadAfter(30.0, [] { ProbeNetModel("race", false); });
            DelMar::RunOnGameThreadAfter(31.0, [] { DelMar::ProbeSolverRewind("race+31s"); });
            DelMar::RunOnGameThreadAfter(32.0, [] { DelMar::ArmRollbackInputChain("race+32s"); });
        if (FConfiguration::bDelMarObservePlayerInput)
            DelMar::RunOnGameThreadAfter(20.0, [] { ObservePlayerInputTick(50); });
        if (FConfiguration::bDelMarForceThrottleTest)
            DelMar::RunOnGameThreadAfter(32.0, [] { ForceThrottleTick(60); });
        if (FConfiguration::bDelMarPhysicsProbe)
            DelMar::RunOnGameThreadAfter(40.0, PhysicsProbe);
    }

    void StepS7()
    {
        if (!ClaimStep(7, "S7"))
            return;
        ClassNameCache.clear();
        DelMar::Log("[track] S7: join the race (readyup)");
        DelMar::RunOnGameThreadAfter(8.0, StepS8);
        DelMar::RunOnGameThread([] { DelMar::DumpWorldSnapshot("S7"); });
        DelMar::RunOnGameThread(RegisterRacerIfNeeded);
        if (FConfiguration::bDelMarDiagnostics)
        {
            DelMar::RunOnGameThread(DumpJoinDiagnostics);
            if (FConfiguration::bDelMarLocateFunctions)
                DelMar::RunOnGameThread([]
                {
                    static const char* const Names[] = { "DelMarReadyUpSelf", "DelMarSkipWaitingForPlayers", "DelMarForceEveryoneToReadyUp",
                                                         "DelMarStartRace", "SpawnRaceManager", "DelMarFinishRace", "EnableCheats" };
                    WhereAreFunctions(Names, 7);
                });
        }
        if (FConfiguration::bDelMarReadyUpLocalPlayer)
            DelMar::RunOnGameThreadAfter(1.0, ReadyUpLocalPlayer);
        DelMar::RunOnGameThreadAfter(5.0, [] { LogPlayspaceStatus("S7+5s"); });
    }

    void StepS6()
    {
        if (!ClaimStep(6, "S6"))
            return;
        ClassNameCache.clear();
        DelMar::Log("[track] S6: spawn through the playspace");
        DelMar::RunOnGameThreadAfter(14.0, StepS7);
        LogPlayspaceStatus("S6");
        if (!AnyDelMarLevelStreaming())
        {
            DelMar::Log("[track] the track is not streaming - not restarting the player");
            return;
        }
        if (!ActorsWithClass("DelMarVehicle").empty())
        {
            DelMar::Log("[track] a DelMarVehicle exists already - not restarting the player");
            return;
        }
        DelMar::RunOnGameThread(RegisterRacerIfNeeded);
        if (FindRaceManager())
        {
            DelMar::Log("[track] a race manager exists - leaving the spawn to it (no RestartPlayer)");
            return;
        }
        if (FConfiguration::bDelMarRestartPlayerAfterLoad)
            DelMar::RunOnGameThreadAfter(2.0, RestartLocalPlayer);
    }

    void StepS5()
    {
        if (!ClaimStep(5, "S5"))
            return;
        ClassNameCache.clear();
        DelMar::Log("[track] S5: post-load diagnostics + racer registration");
        DelMar::RunOnGameThreadAfter(10.0, StepS6);
        DelMar::RunOnGameThread([] { DelMar::DumpWorldSnapshot("S5"); });
        if (FConfiguration::bDelMarDiagnostics)
            DelMar::RunOnGameThread(DiagnosticsAfter);
        DelMar::RunOnGameThread(RegisterRacerIfNeeded);
    }

    void StepS4()
    {
        ClassNameCache.clear();
        DelMar::Log("[track] S4: backups if the native request did not take effect");
        DelMar::RunOnGameThreadAfter(12.0, StepS5);
        LogPlayspaceStatus("S4");
        auto PS = FindPlayspaceActor();
        if (!PS)
            return;
        auto State = StateTagOf(PS);
        if ((IContains(State, "Setup") || IContains(State, "None")) && FConfiguration::bDelMarPushStateMachine)
        {
            DelMar::RunOnGameThread([] { auto PS = FindPlayspaceActor(); if (PS) RequestStateTag(PS, L"DelMar.Game.State.Loading"); });
            DelMar::RunOnGameThreadAfter(2.0, [] { auto PS = FindPlayspaceActor(); if (PS) RequestLevelViaPlayspace(PS); });
        }
        DelMar::RunOnGameThreadAfter(4.0, []
        {
            LogPlayspaceStatus("S4+4s");
            auto PS = FindPlayspaceActor();
            if (!PS || !FConfiguration::bDelMarRequestLevelIfIdle)
                return;
            auto LM = ReadObjAny(PS, "LevelManager");
            if (!AnyDelMarLevelStreaming() && IContains(PropText(LM, "DesiredMapDescription"), "[0 tags]"))
                RequestLevelViaManager(PS);
        });
    }

    void StepS3()
    {
        ClassNameCache.clear();
        DelMar::Log("[track] S3: tagged level request from Setup");
        DelMar::RunOnGameThreadAfter(6.0, StepS4);
        LogPlayspaceStatus("S3");
        auto PS = FindPlayspaceActor();
        if (!PS || !FConfiguration::bDelMarRequestLevelIfIdle)
            return;
        DelMar::Log("[track] playspace MapSet %s, playlist map tags %s", PropText(PS, "MapSet").c_str(), PropText(PS, "PlaylistDefinedMapTags").c_str());
        DelMar::RunOnGameThread([] { auto PS = FindPlayspaceActor(); if (PS) RequestLevelViaPlayspace(PS); });
        DelMar::RunOnGameThreadAfter(2.0, []
        {
            WhenTrue([] { return FindRaceManager() != nullptr; }, [] { StepS7(); }, 55.0, "waiting for the race manager");
        });
    }

    void StepS2()
    {
        ClassNameCache.clear();
        DelMar::Log("[track] S2: match started");
        DelMar::RunOnGameThreadAfter(3.0, StepS3);
        LogPlayspaceStatus("S2");
        auto PS = FindPlayspaceActor();
        if (!PS || !FConfiguration::bDelMarCallHandleMatchStarted)
            return;
        DelMar::RunOnGameThread([] { auto PS = FindPlayspaceActor(); if (PS) { CallNoArgs(PS, "HandleMatchStarted", "playspace"); LogPlayspaceStatus("S2+HandleMatchStarted"); } });
    }

    void StepS1()
    {
        ClassNameCache.clear();
        DelMar::Log("[track] S1: playspace (world-ready + %.0f s)", FConfiguration::DelMarPlayspaceSpawnDelayS);
        DelMar::RunOnGameThreadAfter(3.0, StepS2);
        DelMar::RunOnGameThreadAfter(FConfiguration::DelMarStreamFallbackDelayS - FConfiguration::DelMarPlayspaceSpawnDelayS, StepFallback);
        if (FConfiguration::bDelMarDiagnostics)
            DelMar::RunOnGameThread(DiagnosticsBefore);
        DelMar::RunOnGameThread([] { CheckPrimaryAssetIds(L"DelMarMap"); CheckPrimaryAssetIds(L"GameplayModifier"); });
        DelMar::RunOnGameThread([]
        {
            auto PS = FindPlayspaceActor();
            if (PS)
                DelMar::Log("[playspace] a playspace actor already exists: %s", ObjRef(PS).c_str());
            else if (FConfiguration::bDelMarSpawnPlayspace)
                PS = SpawnPlayspace();
            else
                DelMar::Log("[playspace] no playspace actor and bDelMarSpawnPlayspace is off");
            if (PS && FConfiguration::bDelMarLinkPlayspaceToManager)
                LinkPlayspaceToManager(PS);
            if (PS && FConfiguration::bDelMarSkipClientLoadWait)
                SkipClientLoadWait(PS);
            LogPlayspaceStatus("S1");
        });
    }
}

void DelMar::OnListenStarted(bool bOk)
{
    if (bOk)
        Log("listen server on port %d: LISTENING", FConfiguration::Port);
    else if (!FConfiguration::bStartListenServer)
        Log("listen server: not started (bStartListenServer is off)");
    else
        Log("listen server on port %d: FAILED - see the [listen] lines abov", FConfiguration::Port);
}

void* DelMar::StreamTrack(const wchar_t* ObjectPath)
{
    auto World = UWorld::GetWorld();
    if (!World)
    {
        Log("stream track: no world");
        return nullptr;
    }
    auto CDO = ULevelStreamingDynamic::GetDefaultObj();
    auto Fn = CDO ? CDO->GetFunction("LoadLevelInstance") : nullptr;
    if (!Fn)
    {
        Log("stream track: ULevelStreamingDynamic::LoadLevelInstance is not reflected on this build");
        return nullptr;
    }
    UEAllocatedWString Pkg(ObjectPath ? ObjectPath : L"");
    auto Dot = Pkg.find(L'.');
    if (Dot != UEAllocatedWString::npos)
        Pkg.erase(Dot);
    if (Pkg.empty())
        return nullptr;
    FReflCall C;
    if (!C.Init(CDO, Fn))
        return nullptr;
    C.SetPtr(C.Find("WorldContextObject"), World);
    C.SetStr(C.Find("LevelName"), Pkg.c_str());
    C.SetStr(C.Find("OptionalLevelNameOverride"), L"");
    C.Invoke();
    auto Streamed = C.GetObj(C.Ret());
    Log("stream track: LoadLevelInstance('%ls') -> %s success=%d", Pkg.c_str(), ObjRef(Streamed).c_str(), (int)C.GetBool(C.Find("bOutSuccess")));
    return (void*)Streamed;
}

bool DelMar::PlacePlayerOnTrack(bool bFallbackToOrigin)
{
    auto World = UWorld::GetWorld();
    if (!World)
        return false;

    TArray<AFortPlayerControllerAthena*> PCs;
    Utils::GetAll<AFortPlayerControllerAthena>(PCs);
    AActor* Pawn = nullptr;
    for (auto& PC : PCs)
        if (PC && PC->Pawn)
        {
            Pawn = PC->Pawn;
            break;
        }
    PCs.Free();
    if (!Pawn)
    {
        Log("place: no possessed player pawn yet"); //cuz usually delmar uses AutoPosesPlayer0 and other
        return false;
    }

    TArray<AActor*> AllActors;
    Utils::GetAll<AActor>(AllActors);
    int delmarActors = 0;
    AActor* startActor = nullptr;
    for (auto& A : AllActors)
    {
        if (!A || !A->Class)
            continue;
        auto n = A->Class->Name.ToString();
        const char* c = n.c_str();
        if (strstr(c, "DelMar"))
            delmarActors++;
        if (!startActor && (strstr(c, "DelMarPlayerStart") || strstr(c, "PlayerStartRacing") || strstr(c, "DelMarStart") || strstr(c, "VehicleSpawner")))
            startActor = A;
    }
    AllActors.Free();

    FVector Target(0, 0, 3000);
    if (startActor)
    {
        Target = startActor->K2_GetActorLocation();
        Target.Z += 150;
        Log("place: found track spawn %s at (%.0f,%.0f,%.0f); %d DelMar actors in world", startActor->Name.ToString().c_str(), Target.X, Target.Y, Target.Z, delmarActors);
    }
    else if (!bFallbackToOrigin)
    {
        Log("place: no DelMar spawn actor yet (%d DelMar actors in world); will retry", delmarActors);
        return false;
    }
    else if (delmarActors <= 6)
    {
        Log("place: no DelMar spawn actor and no track actors in the world (%d DelMar actors) - not teleporting", delmarActors);
        return false;
    }
    else
    {
        Log("place: no DelMar spawn actor (%d DelMar actors in world) - NOT teleporting; leaving placement to the native path", delmarActors);
        return false;
    }

    FRotator Rot = startActor ? startActor->K2_GetActorRotation() : FRotator();
    bool ok = Pawn->K2_TeleportTo(Target, Rot);
    Log("place: teleport pawn '%s' -> (%.0f,%.0f,%.0f) ok=%d", Pawn->Name.ToString().c_str(), Target.X, Target.Y, Target.Z, (int)ok);
    return startActor != nullptr && ok;
}

void DelMar::OnPlaylistApplied(AFortGameMode* GameMode, AFortGameStateAthena* GameState, const UFortPlaylistAthena* Playlist)
{
    Log("playlist applied: %s", ObjName(Playlist).c_str());
    Log("  game mode  %s", ClassChain(GameMode).c_str());
    Log("  game state %s", ClassChain(GameState).c_str());

    if (Playlist && Playlist->HasGameplayTagContainer())
        for (int i = 0; i < Playlist->GameplayTagContainer.GameplayTags.Num(); i++)
            Log("  playlist tag %s", Playlist->GameplayTagContainer.GameplayTags.Get(i, FGameplayTag::Size()).TagName.ToString().c_str());

    if (GameState)
    {
        if (GameState->HasCurrentPlaylistInfo())
            Log("  GameState.CurrentPlaylistInfo.BasePlaylist = %s (key %d)", ObjName(GameState->CurrentPlaylistInfo.BasePlaylist).c_str(), GameState->CurrentPlaylistInfo.PlaylistReplicationKey);
        if (GameState->HasbPlaylistDataIsLoaded())
            Log("  GameState.bPlaylistDataIsLoaded = %d", (int)GameState->bPlaylistDataIsLoaded);
    }

    if (FConfiguration::bDelMarActivateGameFeatures)
        ActivateTrackPlugins();

    RunOnGameThreadAfter(5.0, [] { DumpPluginStates("playlist+5s"); });
    RunOnGameThreadAfter(25.0, [] { DumpWorldSnapshot("playlist+25s"); });
}

void DelMar::OnNewPlayer(AFortGameMode* GameMode, const UObject* PlayerController)
{
    Log("new player: %s (PlayerState %s)", ClassChain(PlayerController).c_str(), ClassChain(ReadObj(PlayerController, "PlayerState")).c_str());
    RunOnGameThreadAfter(15.0, [] { DumpWorldSnapshot("player+15s"); });
}

void DelMar::OnPawnSpawned(const UObject* PlayerController, const UObject* Pawn, const UObject* StartSpot)
{
    if (!Pawn)
    {
        Log("pawn spawn FAILED for %s at %s", ObjName(PlayerController).c_str(), ObjName(StartSpot).c_str());
        return;
    }
    auto Loc = ((AActor*)Pawn)->K2_GetActorLocation();
    Log("pawn spawned: %s at (%.0f, %.0f, %.0f) from start %s", ClassChain(Pawn).c_str(), (double)Loc.X, (double)Loc.Y, (double)Loc.Z, ObjName(StartSpot).c_str());
}

void DelMar::OnWorldReady(AFortGameMode* GameMode)
{
    Log("world ready (Erbium bWorldIsReady) - mode %s, match state %s", ClassChain(GameMode).c_str(), GameMode && GameMode->HasMatchState() ? GameMode->MatchState.ToString().c_str() : "?");
    if (!bTrackBringUpScheduled)
    {
        bTrackBringUpScheduled = true;
        if (FConfiguration::bDelMarNetModelProbe)
            RunOnGameThread([] { ProbeNetModel("world-ready", true); });
            RunOnGameThreadAfter(2.5, [] { DelMar::ProbeSolverRewind("world-ready+2.5s"); });
            RunOnGameThreadAfter(3.0, [] { DelMar::ArmRollbackInputChain("world-ready+3s"); });
        if (FConfiguration::bDelMarSpawnMutators)
            RunOnGameThread(SpawnDelMarMutators);
        RunOnGameThreadAfter(FConfiguration::DelMarPlayspaceSpawnDelayS, StepS1);
    }
    std::function<void()> Snap = [] { DumpWorldSnapshot("world-ready"); };
    RunGuarded(&Snap);
}

// console variable
//     call GetValueOnAnyThread
//     test al, al
//     je   skip
void DelMar::EnableNetPrediction()
{
    auto World = UWorld::GetWorld();
    Log("[netpred] console: DelMar.bUseNetPrediction 1");
    UKismetSystemLibrary::ExecuteConsoleCommand(World, FString(L"DelMar.bUseNetPrediction 1"), nullptr);

    auto Base = (uint64)GetModuleHandleA(nullptr);
    auto Slot = (uint8**)(Base + FConfiguration::DelMarNetPredCVarDataRva);
    if (IsBadReadPtr(Slot, sizeof(void*)))
    {
        Log("[netpred] !! cvar-data slot at base+0x%llx is not readable - relying on the console command alone", (unsigned long long)FConfiguration::DelMarNetPredCVarDataRva);
        return;
    }
    auto Data = *Slot;
    if (!Data || IsBadReadPtr(Data, 2))
    {
        Log("[netpred] !! cvar data pointer is null/unreadable (%p) - relying on the console command alone", Data);
        return;
    }
    Log("[netpred] shadow bytes after console command: [0]=%u [1]=%u", Data[0], Data[1]);
    if (FConfiguration::bDelMarForceNetPredShadow && (Data[0] != 1 || Data[1] != 1))
    {
        Data[0] = 1;
        Data[1] = 1;
        Log("[netpred] wrote shadow bytes directly -> [0]=%u [1]=%u", Data[0], Data[1]);
    }
    Log("[netpred] DelMar.bUseNetPrediction is now %s", Data[0] ? "TRUE (ChaosRollback + NetworkPhysicsComponent)" : "still FALSE");
}

void DelMar::EnableRollbackInputs()
{
    auto World = UWorld::GetWorld();
    Log("[np2] console: np2.bEnable 1  (arms UNetworkPhysicsManager at world creation - the rollback INPUT half)");
    UKismetSystemLibrary::ExecuteConsoleCommand(World, FString(L"np2.bEnable 1"), nullptr);
    UKismetSystemLibrary::ExecuteConsoleCommand(World, FString(L"p.RewindCaptureNumFrames 64"), nullptr);

    auto Base = (uint64)GetModuleHandleA(nullptr);
    auto Flag = (uint8*)(Base + FConfiguration::Np2EnableGlobalRva);
    if (IsBadReadPtr(Flag, 1))
    {
        Log("[np2] !! the bound bool at base+0x%llx is not readable - relying on the console command alone",
            (unsigned long long)FConfiguration::Np2EnableGlobalRva);
        return;
    }
    if (*Flag != 1)
    {
        *Flag = 1;
        Log("[np2] wrote the bound bool directly -> %u", *Flag);
    }
    Log("[np2] np2.bEnable is now %u On the next map load FortniteGame.log must gain "
        "\"UNetworkPhysicsManager EnablingRewindCapture\" - its absence in every previous log was not good result", *Flag);
}

namespace
{
    int CallRollbackRegistrarGuarded(void* Fn, void* Subsys)
    {
        __try
        {
            ((void(*)(void*))Fn)(Subsys);
            return 1;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
    }

    void* HopPtr(const void* BasePtr, uint64 Offset)
    {
        if (!BasePtr || IsBadReadPtr((const uint8*)BasePtr + Offset, sizeof(void*)))
            return nullptr;
        return *(void**)((const uint8*)BasePtr + Offset);
    }

    int ClearManagerSlot138Guarded(void* M)
    {
        __try { *(void**)((uint8*)M + 0x138) = nullptr; return 1; }
        __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
    }

    //no UObject API
    using MarshalFn = void(__fastcall*)(void*, void*);
    MarshalFn MarshalOG = nullptr;

    int LocalGateGuarded(void* Sub)
    {
        __try
        {
            void* Vt = *(void**)Sub;
            if (!Vt || IsBadReadPtr((const uint8*)Vt + 0x810, sizeof(void*))) return -1;
            void* Fn = *(void**)((const uint8*)Vt + 0x810);
            if (!Fn) return -1;
            return ((char(__fastcall*)(void*))Fn)(Sub) != 0 ? 1 : 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
    }

    void __fastcall Marshal_Hook(void* Ctx, void* Out)
    {
        bool  bOverride = false;
        uint8 Pending[0x40];

        if (FConfiguration::bDelMarForceMarshalInputFromPending && Ctx && Out
            && !IsBadReadPtr((const uint8*)Ctx + 0x20, sizeof(void*)))
        {
            void* Veh = *(void**)((const uint8*)Ctx + 0x20);
            if (Veh && !IsBadReadPtr((const uint8*)Veh + 0x2180, 0x40))
            {
                void* Sub = (uint8*)Veh + 0x510;
                if (LocalGateGuarded(Sub) == 1)
                {
                    memcpy(Pending, (const uint8*)Veh + 0x2180, 0x40);
                    bOverride = true;
                }
            }
        }

        MarshalOG(Ctx, Out);

        if (bOverride && !IsBadReadPtr((uint8*)Out + 0x20, 0x40))
            memcpy((uint8*)Out + 0x20, Pending, 0x40);
    }
}

void DelMar::HookInputMarshal()
{
    if (!FConfiguration::bDelMarForceMarshalInputFromPending) { Log("[marshalhook] disabled by config"); return; }
    if (VersionInfo.FortniteVersion != 30.40)
    {
        Log("[marshalhook] NOT installed - RVA hard for 30.40 and this is %.2f", VersionInfo.FortniteVersion);
        return;
    }

    const uintptr_t Marshal = ImageBase + FConfiguration::MarshalInputRva;
    Hooking::Hook(Marshal, Marshal_Hook, MarshalOG);
    Log("[marshalhook] hyp_PerVehicleInputMarshal @ 0x%llX hooked - asyncBuf+0x20 will carry PendingDriverInputState "
        "on the LOCAL branch (was bufferB, unwritten in standalone)", (unsigned long long)Marshal);
}

void DelMar::ArmRollbackInputChain(const char* Tag)
{
    auto World = (const UObject*)UWorld::GetWorld();

    if (auto NetSub = FindNetModelSubsystem())
        Log("[rbinput] %s: DelMarNetModelSubsystem %s  +0x30=%u  +0x130=%p  +0x138=%p (read-only, for the record - NOT the registrar's this)",
            Tag, ObjRef(NetSub).c_str(),
            IsBadReadPtr((const uint8*)NetSub + 0x30, 1) ? 255u : (unsigned)*((const uint8*)NetSub + 0x30),
            HopPtr(NetSub, 0x130), HopPtr(NetSub, 0x138));

    auto M = FirstObjectOfClass("NetworkPhysicsManager", World);
    if (!M)
    {
        Log("[rbinput] %s: no UNetworkPhysicsManager found by class - cannot arm safely anyway; bailing (NO call made)", Tag);
        return;
    }
    void* MSlot138 = HopPtr(M, 0x138);
    Log("[rbinput] %s: UNetworkPhysicsManager %s [%s]  +0x130=%p  +0x138=%p%s", Tag, ObjRef(M).c_str(),
        DelMar::ClassChain(M).c_str(), HopPtr(M, 0x130), MSlot138,
        MSlot138 ? "  (input consumer built already - registrar already ran, will not recall)" : "  (input consumer NOT built yet)");

    void* Solver = HopPtr(HopPtr(World, 0x248), 0x38);
    void* Rewind = HopPtr(Solver, 0x1C8);
    void* ArrData = (Rewind && !IsBadReadPtr((const uint8*)Rewind + 0x40, 16)) ? *(void**)((const uint8*)Rewind + 0x40) : (void*)~0ull;
    bool bArrayClean = Rewind && !IsBadReadPtr((const uint8*)Rewind + 0x40, 16)
        && (ArrData == nullptr || !IsBadReadPtr(ArrData, 1));
    Log("[rbinput] %s: solver rewind array Data=%p -> %s", Tag, ArrData, bArrayClean ? "clean (safe to append)" : "NOT ok - not calling");

    int RewindNum = (Rewind && !IsBadReadPtr((const uint8*)Rewind + 0x48, 4))
                  ? *(const int*)((const uint8*)Rewind + 0x48) : -1;
    Log("[rbinput] %s: producer array Num=%d (>=1 means the producer callback is appended and should tick)", Tag, RewindNum);

    if (MSlot138 && bArrayClean && RewindNum == 0 && FConfiguration::bDelMarRearmIfProducerMissing)
    {
        if (ClearManagerSlot138Guarded((void*)M))
        {
            MSlot138 = nullptr;
            Log("[rbinput] %s: halfarmed (consumer built, producer absent) - cleared +0x138 to force reappend", Tag);
        }
        else
            Log("[rbinput] %s: +0x138 clear faulted (SEH caught) - leaving as is", Tag);
    }

    if (!MSlot138 && bArrayClean && FConfiguration::bDelMarRollbackRegistrarBackstop)
    {
        auto Fn = (void*)((uint64)GetModuleHandleA(nullptr) + FConfiguration::DelMarRollbackRegistrarRva);
        Log("[rbinput] %s: calling registrar %p with the CLASS-VERIFIED UNetworkPhysicsManager (SEH-guarded)", Tag, Fn);
        int ok = CallRollbackRegistrarGuarded(Fn, (void*)M);
        Log("[rbinput] %s: registrar %s;  +0x138 now %p (non-null = input consumer built + producer bound)",
            Tag, ok ? "returned with no issues" : "!! FAULTED (SEH caught - chain unchanged)", HopPtr(M, 0x138));
    }
    else
        Log("[rbinput] %s: NOT calling registrar (consumer-already-built=%d array-clean=%d backstop=%d)", Tag,
            MSlot138 ? 1 : 0, bArrayClean ? 1 : 0, FConfiguration::bDelMarRollbackRegistrarBackstop ? 1 : 0);

    UKismetSystemLibrary::ExecuteConsoleCommand(UWorld::GetWorld(), FString(L"Log LogDelMarVehicleNetworkPhysics VeryVerbose"), nullptr);

    auto Vs = ActorsWithClass("DelMarVehicle");
    if (Vs.empty())
    {
        Log("[rbinput] %s: no DelMarVehicle yet - gate check deferred", Tag);
        return;
    }
    auto V = Vs[0];
    if (IsBadReadPtr((const uint8*)V + 0x879, 1) || IsBadReadPtr((const uint8*)V + 0x35C, 1))
    {
        Log("[rbinput] %s: vehicle gate bytes unreadable", Tag);
        return;
    }
    uint8 G879 = *((const uint8*)V + 0x879);
    uint8 G35C = *((const uint8*)V + 0x35C);
    void* Comp = HopPtr(V, 0x23B8);
    Log("[rbinput] %s: vehicle %s  +0x35C bit0=%u (rollback tick)  +0x879 bit5=%u (locally-viewed gate)  NetworkPhysicsComponent=%p",
        Tag, ObjRef(V).c_str(), (unsigned)(G35C & 1), (unsigned)((G879 >> 5) & 1), Comp);

    if (!((G879 >> 5) & 1) && FConfiguration::bDelMarMarkLocallyViewed)
    {
        *((uint8*)V + 0x879) = G879 | 0x20;
        Log("[rbinput] %s: set +0x879 bit5 - producer's LOCAL branch that consumes "
            "PendingDriverInputState and clears it is now eligible", Tag);
    }

    void* NetObj = HopPtr(Comp, 0xA0);
    void* Ring = HopPtr(NetObj, 0x20);
    if (Ring && !IsBadReadPtr((const uint8*)Ring + 0x28, 4))
    {
        int32 Step = *(const int32*)((const uint8*)Ring + 0x24);
        int32 Slot = *(const int32*)((const uint8*)Ring + 0x28);
        Log("[rbinput] %s: input ring: last stamped step=%d slot=%d  (compare across probes: growing = consumer alive)", Tag, Step, Slot);
    }
    else
        Log("[rbinput] %s: input ring not walkable (comp+0xA0=%p ring=%p) - consumer not armed", Tag, NetObj, Ring);
}

// solver rewind test (2026-09-09)
void DelMar::ProbeSolverRewind(const char* Tag)
{
    auto World = (const void*)UWorld::GetWorld();
    if (!World) { Log("[solverprobe] %s: no world", Tag); return; }

    void* NetDriver = HopPtr(World, 0x38);
    if (NetDriver)
    {
        void* ServerConn = HopPtr(NetDriver, 0xF0);
        Log("[solverprobe] %s: World->NetDriver=%p  ServerConnection=%p -> %s", Tag, NetDriver, ServerConn,
            ServerConn ? "CLIENT driver" : "LISTEN/SERVER driver (bound, no upstream connection)");
    }
    else
        Log("[solverprobe] %s: World->NetDriver = null (no net driver this run)", Tag);

    // The solver-rewind chain.
    void* PhysScene = HopPtr(World, 0x248);
    void* SolverOrMgr = HopPtr(PhysScene, 0x38);
    Log("[solverprobe] %s: PhysScene(World+0x248)=%p  +0x38=%p", Tag, PhysScene, SolverOrMgr);
    if (!SolverOrMgr) { Log("[solverprobe] %s: chain dead at +0x38 - nothing to read from", Tag); return; }

    ClassNameCache.clear();
    auto Chain = DelMar::ClassChain((const UObject*)SolverOrMgr);
    Log("[solverprobe] %s: +0x38 class chain: %s", Tag, Chain.empty() ? "(not a UObject - raw Chaos solver)" : Chain.c_str());

    void* Rewind = HopPtr(SolverOrMgr, 0x1C8);
    Log("[solverprobe] %s: rewind object (+0x1c8) = %p", Tag, Rewind);
    if (!Rewind) { Log("[solverprobe] %s: rewind object null - producer registry not built (mutator never ran? did it?)", Tag); return; }

    if (IsBadReadPtr((const uint8*)Rewind + 0x40, 16))
    {
        Log("[solverprobe] %s: TArray header at rewind+0x40 unreadable", Tag);
        return;
    }
    void* Data = *(void**)((const uint8*)Rewind + 0x40);
    int32 Num = *(const int32*)((const uint8*)Rewind + 0x48);
    int32 Max = *(const int32*)((const uint8*)Rewind + 0x4C);
    bool bDataBad = Data != nullptr && IsBadReadPtr(Data, 1);
    Log("[solverprobe] %s: producer TArray rewind+0x40: Data=%p Num=%d Max=%d -> %s", Tag, Data, Num, Max,
        Data == nullptr ? "EMPTY (clean - append would allocate fresh, np2 should NOT crash here)"
        : bDataBad      ? "GARBAGE (unmapped Data - THIS is the 0xffff0003 crash; zero-init before np2 is the fix)"
                        : "populated (Data mapped - registry already built, different problem)");
}

// is born as ChaosRollback
void DelMar::ConfigureNetModel()
{
    auto CDO = DefaultObjImpl("DelMarNetModelSubsystem");
    if (!CDO)
    {
        Log("[netmodel-cdo] !! UDelMarNetModelSubsystem CDO not found - cannot preset the net model");
        return;
    }
    Log("[netmodel-cdo] CDO %s", ObjName(CDO).c_str());
    DumpObjectImpl(CDO, "netmodel-cdo", nullptr, 40);

    if (!FConfiguration::bDelMarPresetChaosRollback)
        return;

    bool bWrote = false;
    for (const UStruct* C = CDO->Class; C && !bWrote; C = C->GetSuper())
    {
        if (IsBaseClassName(CachedName(C)))
            break;
        for (auto P = C->GetChildProperties(); P; P = P->FField_GetNext())
        {
            auto Off = GetFromOffset<uint32>(P, Offsets::Offset_Internal);
            if (Off != 0x30)
                continue;
            auto PN = P->FField_GetName().ToString();
            auto T = FieldTypeName(P);
            Log("[netmodel-cdo] property at +0x30 is %s::%s (%s) = %s", CachedName(C).c_str(), PN.c_str(), T.c_str(),
                FormatValueAt(P, (const uint8*)CDO + Off, 0).c_str());
            if (T == "EnumProperty" || T == "ByteProperty")
            {
                *((uint8*)CDO + Off) = (uint8)FConfiguration::DelMarNetModelValue;
                bWrote = true;
                Log("[netmodel-cdo]   -> wrote %d, now %s", FConfiguration::DelMarNetModelValue, FormatValueAt(P, (const uint8*)CDO + Off, 0).c_str());
            }
            break;
        }
    }
    if (!bWrote && !IsBadReadPtr((const uint8*)CDO + 0x30, 1))
    {
        Log("[netmodel-cdo] no reflected enum at +0x30 - writing the raw byte the thing reads (was %u)", *((const uint8*)CDO + 0x30));
        *((uint8*)CDO + 0x30) = (uint8)FConfiguration::DelMarNetModelValue;
    }
    Log("[netmodel-cdo] CDO byte at +0x30 is now %u (1 = ChaosRollback)", *((const uint8*)CDO + 0x30));
}

// before level is opened
void DelMar::ConfigurePhysics()
{
    ConfigureNetModel();

    auto CDO = DefaultObjImpl("PhysicsSettings");
    if (!CDO)
    {
        Log("[physcfg] !! UPhysicsSettings CDO not found - leaving physics as shipping it was");
        return;
    }
    Log("[physcfg] UPhysicsSettings CDO %s", ObjName(CDO).c_str());
    DumpObjectImpl(CDO, "physcfg-before", "Async|Tick|Substep|Predict|Delta|Frame|Physics", 60);

    if (FConfiguration::bDelMarEnablePhysicsPrediction)
    {
        bool p = WriteStructBool(CDO, "PhysicsPrediction", "bEnablePhysicsPrediction", true);
        bool r = WriteStructBool(CDO, "PhysicsPrediction", "bEnablePhysicsResimulation", true);
        Log("[physcfg] PhysicsPrediction: bEnablePhysicsPrediction=%s bEnablePhysicsResimulation=%s",
            p ? "set" : "!! NOT FOUND", r ? "set" : "!! NOT FOUND");
    }

    if (FConfiguration::bDelMarEnableAsyncPhysics)
    {
        float Step = 1.0f / (float)FConfiguration::DelMarPhysicsRateHz;
        bool a = WriteBoolProp(CDO, "bTickPhysicsAsync", true);
        bool b = WriteFloatProp(CDO, "AsyncFixedTimeStepSize", Step);
        bool c = WriteStructBool(CDO, "PhysicsPrediction", "bEnablePhysicsPrediction", true);
        bool d = WriteStructBool(CDO, "PhysicsPrediction", "bEnablePhysicsResimulation", true);
        Log("[physcfg] bTickPhysicsAsync=%s  AsyncFixedTimeStepSize=%.5f (%d Hz)=%s  bEnablePhysicsPrediction=%s  bEnablePhysicsResimulation=%s",
            a ? "set" : "!! NOT FOUND", Step, FConfiguration::DelMarPhysicsRateHz, b ? "set" : "!! NOT FOUND",
            c ? "set" : "!! NOT FOUND", d ? "set" : "!! NOT FOUND");
        DumpObjectImpl(CDO, "physcfg-after", "Async|Tick|Substep|Predict|Delta|Frame|Physics", 60);
    }

    auto World = UWorld::GetWorld();
    for (auto Cmd : { L"Fort.Rollback.DelMarDisableVehiclePhysicsBeforeLoadComplete 0",
                      L"p.ForceDisableAsyncPhysics 0",
                      L"log LogChaos Verbose", L"log LogPhysics Verbose", L"log LogPhysicsCore Verbose" })
    {
        Log("[physcfg] console: %ls", Cmd);
        UKismetSystemLibrary::ExecuteConsoleCommand(World, FString(Cmd), nullptr);
    }
    if (auto NetPred = FindCVar<int>(L"DelMar.bUseNetPrediction"))
        Log("[physcfg] DelMar.bUseNetPrediction = %d", *NetPred);
    else
        Log("[physcfg] DelMar.bUseNetPrediction: cvar not found");
}

static bool InFrontend()
{
    auto World = UWorld::GetWorld();
    if (!World)
        return true;
    return IContains(ObjName(World), "Frontend");
}

void DelMar::DumpWorldSnapshot(const char* Tag)
{
    ClassNameCache.clear();
    auto World = UWorld::GetWorld();
    if (!World)
    {
        Log("[%s] no world", Tag);
        return;
    }

    // "worst frame was 3327 ms"
    if (!FConfiguration::bDelMarHeavySnapshots && !InFrontend())
    {
        Log("[%s] (race is ok - heavy world snapshots ignoted, see the status line)", Tag);
        LogPlayspaceStatus(Tag);
        return;
    }

    Log("[%s] world %s", Tag, ObjName(World).c_str());
    auto Mode = World->HasAuthorityGameMode() ? World->AuthorityGameMode : nullptr;
    auto GS = World->HasGameState() ? World->GameState : nullptr;
    Log("[%s]   GameMode  %s", Tag, ClassChain(Mode).c_str());
    Log("[%s]   GameState %s", Tag, ClassChain(GS).c_str());
    if (Mode)
    {
        Log("[%s]   MatchState %s  DefaultPawnClass %s  PlayerControllerClass %s  GameStateClass %s  PlayerStateClass %s", Tag, ReadFName(Mode, "MatchState").c_str(),
            ObjName(ReadObj(Mode, "DefaultPawnClass")).c_str(), ObjName(ReadObj(Mode, "PlayerControllerClass")).c_str(), ObjName(ReadObj(Mode, "GameStateClass")).c_str(),
            ObjName(ReadObj(Mode, "PlayerStateClass")).c_str());
        auto Off = Mode->GetOffset("WarmupRequiredPlayerCount");
        if (Off != (uint32)-1)
            Log("[%s]   WarmupRequiredPlayerCount %d", Tag, GetFromOffset<int32>(Mode, Off));
    }

    auto ND = World->HasNetDriver() ? (UNetDriver*)World->NetDriver : nullptr;
    Log("[%s]   NetDriver %s, %d client connection(s)", Tag, ND ? ObjName(ND).c_str() : "none", ND && ND->HasClientConnections() ? ND->ClientConnections.Num() : 0);

    auto SLOff = World->GetOffset("StreamingLevels");
    if (SLOff != (uint32)-1)
    {
        auto& Levels = GetFromOffset<TArray<UObject*>>(World, SLOff);
        Log("[%s]   %d streaming level(s):", Tag, Levels.Num());
        for (int i = 0; i < Levels.Num() && i < 64; i++)
        {
            auto L = Levels[i];
            if (!L)
                continue;
            auto WAOff = L->GetOffset("WorldAsset");
            auto Path = WAOff == (uint32)-1 ? UEAllocatedString("?") : SoftPathToString((void*)(__int64(L) + WAOff));
            Log("[%s]     %-60s loaded=%s visible-req=%d load-req=%d pkg=%s (%s)", Tag, Path.c_str(), ReadObj(L, "LoadedLevel") ? "yes" : "no", ReadBool(L, "bShouldBeVisible"),
                ReadBool(L, "bShouldBeLoaded"), ReadFName(L, "PackageNameToLoad").c_str(), ObjName(L->Class).c_str());
        }
    }

    TArray<AActor*> Actors;
    Utils::GetAll(AActor::StaticClass(), Actors);
    int Total = Actors.Num(), Listed = 0, DelMarCount = 0;
    for (int i = 0; i < Total; i++)
    {
        auto A = Actors[i];
        if (!A || !A->Class || !(IsDelMarObject(A) || IsPlayspaceActor(A)))
            continue;
        DelMarCount++;
        if (Listed < 80)
        {
            Log("[%s]   actor %s  (%s)", Tag, ObjName(A).c_str(), ObjName(A->Class).c_str());
            Listed++;
        }
    }
    {
        std::map<UEAllocatedString, int> ClassHist;
        for (int i = 0; i < Total; i++)
            if (auto A = Actors[i]; A && A->Class)
                ClassHist[CachedName(A->Class)]++;
        UEAllocatedString Line;
        for (auto& [ClassName, Count] : ClassHist)
        {
            char b[160];
            _snprintf_s(b, sizeof(b), _TRUNCATE, "%s x%d, ", ClassName.c_str(), Count);
            Line += b;
            if (Line.size() > 900)
            {
                Log("[%s]   classes: %s", Tag, Line.c_str());
                Line.clear();
            }
        }
        if (!Line.empty())
            Log("[%s]   classes: %s", Tag, Line.c_str());
    }
    Actors.Free();
    Log("[%s]   %d actors in world, %d DelMar-classed (%d listed)", Tag, Total, DelMarCount, Listed);

    LogPlayspaceStatus(Tag);
    DumpPluginStates(Tag);
}

// --------------//-------------//---------////////////////////---------------
// ---------------//-----------//---------//----------------------------------
// ----------------//---------//---------//-----------------------------------
// -----------------//-------//---------////////////////////------------------ //got bored
// ------------------//-----//---------//-------------------------------------
// -------------------//---//---------//--------------------------------------
// --------------------//-//---------//---------------------------------------
// ---------------------//----------//----------------------------------------
//   LogGlobalStatus: UEngine::Browse Started Browse: "127.0.0.1/Game/Maps/Frontend?EncryptionToken=..."
//   LogNet: InitBase PendingNetDriver (NetDriverDefinition GameNetDriver)

namespace
{
    struct RawArray
    {
        FString* Data;
        int32_t  Num;
        int32_t  Max;
    };

    struct FURLRaw
    {
        FString Protocol;
        FString Host;
        int32_t Port;
        int32_t Valid;
        FString Map;
        FString RedirectURL;
        RawArray Op;
        FString Portal;
    };
    static_assert(sizeof(FURLRaw) == 0x68, "FURL layout does not match the disassembly");

    using BrowseFn = int(__fastcall*)(void*, void*, FURLRaw*, FString*);
    BrowseFn BrowseOG = nullptr;

    bool HostMatchesSentinel(const FURLRaw* URL)
    {
        if (!URL || !URL->Host.GetData() || URL->Host.Num() <= 1)
            return false;
        return wcscmp(URL->Host.GetData(), FConfiguration::DelMarTravelSentinelHost) == 0;
    }

    void AppendPlaylistOption(FURLRaw* URL)
    {
        const wchar_t* Full = FConfiguration::Playlist;
        const wchar_t* Dot = wcsrchr(Full, L'.');
        const wchar_t* Name = Dot ? Dot + 1 : Full;

        wchar_t Option[256];
        swprintf_s(Option, L"playlist=%ls", Name);

        const int32_t OldNum = URL->Op.Num;
        FString* Fresh = FMemory::MallocForType<FString>(OldNum + 1);
        if (!Fresh)
        {
            DelMar::Log("travel: could not allocate the option array - the track will not load");
            return;
        }
        memset(Fresh, 0, sizeof(FString) * (OldNum + 1));
        for (int32_t i = 0; i < OldNum; ++i)
            memcpy(&Fresh[i], &URL->Op.Data[i], sizeof(FString));
        Fresh[OldNum] = FString(Option);

        URL->Op.Data = Fresh;
        URL->Op.Num = OldNum + 1;
        URL->Op.Max = OldNum + 1;

        DelMar::Log("travel: added ?%ls (the level manager picks the track from thisshit)", Option);
    }

    int __fastcall Browse_Hook(void* Engine, void* WorldContext, FURLRaw* URL, FString* Error)
    {
        if (FConfiguration::bDelMarHookTravel && HostMatchesSentinel(URL))
        {
            DelMar::Log("travel: intercepted a connect to %ls:%d - rewriting to a local open of %ls",
                        URL->Host.GetData(), URL->Port, FConfiguration::DelMarMap);

            URL->Host = FString(L"");
            URL->Port = 0;
            URL->Map  = FString(FConfiguration::DelMarMap);
            URL->Valid = 1;
            AppendPlaylistOption(URL);

            //   UEngine::Browse Started Browse: "/DelMarCore/Playlists/DelMar_RootLevel"
        }

        return BrowseOG(Engine, WorldContext, URL, Error);
    }
}

void DelMar::HookTravel()
{
    if (!FConfiguration::bDelMarHookTravel)
    {
        Log("travel hook: disabled");
        return;
    }
    if (VersionInfo.FortniteVersion != 30.40)
    {
        Log("travel hook: NOT installed - the address is hardcoded for 30.40 and this is %.2f",
            VersionInfo.FortniteVersion);
        return;
    }

    const uintptr_t Browse = ImageBase + 0x026967F4;
    Hooking::Hook(Browse, Browse_Hook, BrowseOG);
    Log("travel hook: UEngine::Browse @ 0x%llX hooked - %ls:%d will open %ls locally",
        (unsigned long long)Browse, FConfiguration::DelMarTravelSentinelHost,
        FConfiguration::DelMarTravelSentinelPort, FConfiguration::DelMarMap);
}
