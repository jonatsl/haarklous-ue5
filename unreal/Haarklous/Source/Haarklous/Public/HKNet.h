#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Containers/Ticker.h"

/*
 * Multiplayer connection: port of the web's game-net.js. One WebSocket to the lobby's Durable Object on Cloudflare
 * (protocol: reference/web-game/cloud/src/index.js, JSON text messages {t: 'hello' | 'welcome' | ...}).
 * Every player has an own lobby code (4 characters, kept in Saved/LocalStorage.json like the web's localStorage) and
 * starts in it; others join with that code. Up to five players per lobby. Reconnects with back-off; a full or
 * unreachable lobby falls back to your own.
 *
 * Online is switched off unless [Haarklous.Online] bOnlineEnabled=True (DefaultGame.ini) or -HKOnline is given.
 * ServerBase comes from the same section (only the separate test server is configured); -HKServer=<ws(s)://host>
 * overrides it like the web's ?server=, -HKLobby=<CODE> is the web's ?lobby=. The live game server is refused.
 * While online is off the client behaves like the web game without WebSocket support: always 'offline'.
 */

/* The web's localStorage: a small key/value file Saved/LocalStorage.json. */
struct HAARKLOUS_API FHKStore
{
	static FString Get(const FString& Key);                 // empty = not stored (localStorage returns null)
	static void Set(const FString& Key, const FString& Value);
	static FString Path();
	static void SetPathForTests(const FString& InPath);     // empty = the normal file
};

/* JSON exactly like the browser writes it (JSON.stringify): compact, shortest round-trip numbers, same escaping. */
namespace HKJsonWire
{
	HAARKLOUS_API FString Write(const TSharedPtr<FJsonObject>& O);
	HAARKLOUS_API FString WriteValue(const TSharedPtr<FJsonValue>& V);
	HAARKLOUS_API FString Number(double V);                  // Number.prototype.toString (NaN/Infinity -> "null")
	HAARKLOUS_API FString Quote(const FString& S);           // JSON string literal
	HAARKLOUS_API TSharedPtr<FJsonObject> Parse(const FString& S);   // null on bad JSON or a non-object
	// Field access with JavaScript truthiness (missing / null / false / 0 / "" are falsy).
	HAARKLOUS_API bool Truthy(const TSharedPtr<FJsonObject>& O, const FString& K);
	HAARKLOUS_API double Num(const TSharedPtr<FJsonObject>& O, const FString& K, double Default = 0);
	HAARKLOUS_API FString Str(const TSharedPtr<FJsonObject>& O, const FString& K, const FString& Default = FString());
	HAARKLOUS_API TArray<double> Nums(const TSharedPtr<FJsonObject>& O, const FString& K);
	HAARKLOUS_API TSharedPtr<FJsonValue> NumArray(const TArray<double>& A);
}

/* setTimeout / setInterval stand-ins (milliseconds on the clock given). Tick() fires what is due. */
class HAARKLOUS_API FHKTimers
{
public:
	explicit FHKTimers(TFunction<double()> InNow) : NowFn(MoveTemp(InNow)) {}
	int32 SetTimeout(double Ms, TFunction<void()> Fn);
	int32 SetInterval(double Ms, TFunction<void()> Fn);
	void Clear(int32 Id);
	void Tick();
	double Now() const { return NowFn(); }
private:
	struct FTimer { int32 Id; double Due; double Every; TFunction<void()> Fn; };
	TFunction<double()> NowFn;
	TArray<FTimer> List;
	int32 NextId = 1;
};

/* One WebSocket (the browser's WebSocket object): events arrive on the game thread. */
class HAARKLOUS_API IHKSocket
{
public:
	virtual ~IHKSocket() {}
	virtual void Connect() = 0;
	virtual void Send(const FString& Text) = 0;
	virtual void Close(int32 Code) = 0;
	virtual bool IsOpen() const = 0;          // readyState === 1
	TFunction<void()> OnOpen;
	TFunction<void(const FString&)> OnMessage;
	TFunction<void(int32 /*Code*/)> OnClose; // also after a failed connection (code 1006)
};

struct HAARKLOUS_API FHKNetOptions
{
	bool bEnabled = false;
	FString Base;                                                 // wss://host (no trailing slash)
	FString Lobby;                                                // -HKLobby= (the web's ?lobby=)
	TFunction<TSharedPtr<IHKSocket>(const FString& Url)> MakeSocket;   // default: Unreal's WebSockets module
	TFunction<double()> DateNow;                                  // Date.now(): ms since 1970
	TFunction<double()> PerfNow;                                  // performance.now(): monotonic ms (timers)
	TFunction<double()> Random;                                   // Math.random()
	bool bOwnTicker = true;                                       // run the timers from the engine's core ticker
};

struct FHKNetPlayer
{
	int32 Id = 0;
	int32 Slot = 0;
	FString Name;
	bool bMic = false;
};

class HAARKLOUS_API FHKNet
{
public:
	static const TCHAR* const Alphabet;                           // 'ABCDEFGHJKLMNPQRSTUVWXYZ23456789'
	static const TCHAR* const CodeKey;                            // 'haarklous-lobby-code-v1'
	static const TCHAR* const NameKey;                            // 'haarklous-name-v1'
	static bool ValidCode(const FString& C);                      // /^[A-HJ-NP-Z2-9]{4}$/
	static FString RandomCode(const TFunction<double()>& Random);
	static double RetryDelayMs(int32 Retry);                      // Math.min(30000, 1500 * 2 ** retry)
	static bool IsLiveServer(const FString& Base);                // the live game's Worker: never connect
	static FHKNetOptions DefaultOptions();                        // config + command line (see top)
	static TSharedPtr<IHKSocket> MakeUnrealSocket(const FString& Url);

	FHKNet(TFunction<void(const TSharedPtr<FJsonObject>&)> InOnMessage, TFunction<void()> InOnStatus, FHKNetOptions InOptions = DefaultOptions());
	~FHKNet();
	FHKNet(const FHKNet&) = delete;
	FHKNet& operator=(const FHKNet&) = delete;

	bool Connect(const FString& Code);                            // false: not a valid code
	bool Home() { return Connect(OwnCode); }
	// game-main.js begin(): the -HKLobby= lobby if valid, else your own.
	void Begin();
	bool Send(const TSharedPtr<FJsonObject>& M);                  // false unless connected and welcomed
	void SetName(const FString& N);
	void Tick();                                                  // runs due timers (by itself unless bOwnTicker is false)
	// Called after every network event and timer (the browser's microtask checkpoint): FHKMultiplayer answers claims here.
	TFunction<void()> AfterTask;

	const FString& GetName() const { return Name; }
	const FString& GetStatus() const { return Status; }           // 'offline' | 'connecting' | 'online' | 'full'
	const FString& GetCode() const { return Code; }               // lobby of the current/last connection ('' = none yet)
	const FString& GetOwnCode() const { return OwnCode; }
	int32 You() const { return YouId; }                           // 0 = not welcomed (the web's null)
	int32 GetSlot() const { return Slot; }
	bool Online() const { return Status == TEXT("online") && YouId != 0; }
	int32 Others() const { return FMath::Max(0, Players.Num() - 1); }
	const TArray<FHKNetPlayer>& GetPlayers() const { return Players; }   // in join order (the web's Map)
	int32 Max() const { return MaxPlayers; }                          // players a lobby holds (the server's 'max'; 5 live)
	const FHKNetPlayer* Player(int32 Id) const;
	double ServerNow() const { return Options.DateNow() + Offset; }
	double DateNow() const { return Options.DateNow(); }
	double PerfNow() const { return Options.PerfNow(); }
	const FString& GetBase() const { return Options.Base; }
	bool Enabled() const { return Options.bEnabled; }
	FHKTimers& GetTimers() { return Timers; }

	// Pause-menu texts of game-main.js renderMp() (mpStatus, mpWhere, mpLink, mpHome hidden).
	FString StatusText() const;
	FString WhereText() const;
	FString Link() const;
	bool HomeHidden() const { return Code.IsEmpty() || Code == OwnCode; }
	TArray<FHKNetPlayer> PlayersBySlot() const;

private:
	void Open();
	void Later();
	void Shut();
	void SetStatus(const FString& S);
	void HandleMessage(IHKSocket* Sock, const FString& Text);
	void HandleClose(IHKSocket* Sock, int32 CloseCode);
	void Detach(const TSharedPtr<IHKSocket>& S);

	TFunction<void(const TSharedPtr<FJsonObject>&)> OnMessage;
	TFunction<void()> OnStatus;
	FHKNetOptions Options;
	FHKTimers Timers;
	FTSTicker::FDelegateHandle TickHandle;
	TSharedPtr<bool> AliveToken = MakeShared<bool>(true);   // socket callbacks check it (no calls after destruction)
	TSharedPtr<IHKSocket> Ws;
	TArray<TSharedPtr<IHKSocket>> Closing;
	FString OwnCode, Name, Code, Want, Status = TEXT("offline");
	int32 YouId = 0, Slot = 0, Retry = 0, Timer = 0, PingTimer = 0;
	int32 MaxPlayers = 5;
	double Offset = 0;
	bool bAlive = false;
	TArray<FHKNetPlayer> Players;
};

/* The controller code pad of the pause menu (game-main.js openCodePad / codePadInput). */
struct HAARKLOUS_API FHKCodePad
{
	TArray<TCHAR> Chars;
	int32 Pos = 0;
	static FHKCodePad Open(const FHKNet& Net);    // the current lobby's code, or AAAA in your own
	void Left() { Pos = (Pos + 3) % 4; }
	void Right() { Pos = (Pos + 1) % 4; }
	void Step(int32 Dir);                          // up = +1, down = -1
	FString Code() const;
};
