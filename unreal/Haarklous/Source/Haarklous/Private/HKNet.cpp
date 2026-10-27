#include "HKNet.h"
#include "Haarklous.h"
#include "HKMath.h"
#include "IWebSocket.h"
#include "WebSocketsModule.h"
#include "Misc/CommandLine.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "HAL/PlatformTime.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include <cstdio>
#include <cstdlib>

// ---------------------------------------------------------------------------------------------------------------
// localStorage stand-in
// ---------------------------------------------------------------------------------------------------------------
namespace
{
	FString GStorePath;
	TMap<FString, FString> GStore;
	FString GStoreLoadedFrom;
	bool GStoreLoaded = false;

	void LoadStore()
	{
		const FString P = FHKStore::Path();
		if (GStoreLoaded && GStoreLoadedFrom == P) return;
		GStoreLoaded = true; GStoreLoadedFrom = P; GStore.Reset();
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *P)) return;
		TSharedPtr<FJsonObject> J;
		if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), J) || !J.IsValid()) return;
		for (const auto& KV : J->Values) { FString V; if (KV.Value.IsValid() && KV.Value->TryGetString(V)) GStore.Add(FString(*KV.Key), V); }
	}
}

FString FHKStore::Path()
{
	return GStorePath.IsEmpty() ? FPaths::ProjectSavedDir() / TEXT("LocalStorage.json") : GStorePath;
}

void FHKStore::SetPathForTests(const FString& InPath) { GStorePath = InPath; GStoreLoaded = false; }

FString FHKStore::Get(const FString& Key)
{
	LoadStore();
	const FString* V = GStore.Find(Key);
	return V ? *V : FString();
}

void FHKStore::Set(const FString& Key, const FString& Value)
{
	LoadStore();
	GStore.Add(Key, Value);
	TSharedPtr<FJsonObject> J = MakeShared<FJsonObject>();
	for (const auto& KV : GStore) J->SetStringField(KV.Key, KV.Value);
	FFileHelper::SaveStringToFile(HKJsonWire::Write(J), *Path(), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}

// ---------------------------------------------------------------------------------------------------------------
// JSON like the browser
// ---------------------------------------------------------------------------------------------------------------
FString HKJsonWire::Number(double V)
{
	if (!FMath::IsFinite(V)) return TEXT("null");
	if (V == 0) return TEXT("0");
	const bool bNeg = V < 0;
	const double A = bNeg ? -V : V;
	// Shortest decimal that reads back as the same double (ECMAScript Number::toString).
	char Buf[64];
	for (int32 P = 1; P <= 17; P++)
	{
		snprintf(Buf, sizeof(Buf), "%.*e", P - 1, A);
		if (strtod(Buf, nullptr) == A) break;
	}
	FString Digits;
	int32 i = 0;
	for (; Buf[i] && Buf[i] != 'e'; i++) if (Buf[i] >= '0' && Buf[i] <= '9') Digits.AppendChar(TCHAR(Buf[i]));
	const int32 Exp = Buf[i] == 'e' ? atoi(Buf + i + 1) : 0;
	while (Digits.Len() > 1 && Digits.EndsWith(TEXT("0"))) Digits.LeftChopInline(1);
	const int32 K = Digits.Len(), N = Exp + 1;
	FString Out = bNeg ? TEXT("-") : TEXT("");
	if (K <= N && N <= 21) { Out += Digits; for (int32 z = 0; z < N - K; z++) Out.AppendChar('0'); }
	else if (0 < N && N <= 21) Out += Digits.Left(N) + TEXT(".") + Digits.Mid(N);
	else if (-6 < N && N <= 0) { Out += TEXT("0."); for (int32 z = 0; z < -N; z++) Out.AppendChar('0'); Out += Digits; }
	else
	{
		const int32 E = N - 1;
		Out += Digits.Left(1);
		if (K > 1) Out += TEXT(".") + Digits.Mid(1);
		Out += FString::Printf(TEXT("e%s%d"), E < 0 ? TEXT("-") : TEXT("+"), FMath::Abs(E));
	}
	return Out;
}

FString HKJsonWire::Quote(const FString& S)
{
	FString Out = TEXT("\"");
	const int32 L = S.Len();
	for (int32 i = 0; i < L; i++)
	{
		const TCHAR C = S[i];
		switch (C)
		{
		case '"': Out += TEXT("\\\""); break;
		case '\\': Out += TEXT("\\\\"); break;
		case '\b': Out += TEXT("\\b"); break;
		case '\f': Out += TEXT("\\f"); break;
		case '\n': Out += TEXT("\\n"); break;
		case '\r': Out += TEXT("\\r"); break;
		case '\t': Out += TEXT("\\t"); break;
		default:
			if (C < 0x20) Out += FString::Printf(TEXT("\\u%04x"), (uint32)C);
			else if (C >= 0xD800 && C <= 0xDBFF && i + 1 < L && S[i + 1] >= 0xDC00 && S[i + 1] <= 0xDFFF) { Out.AppendChar(C); Out.AppendChar(S[++i]); }
			else if (C >= 0xD800 && C <= 0xDFFF) Out += FString::Printf(TEXT("\\u%04x"), (uint32)C);   // lone surrogate
			else Out.AppendChar(C);
		}
	}
	Out += TEXT("\"");
	return Out;
}

FString HKJsonWire::WriteValue(const TSharedPtr<FJsonValue>& V)
{
	if (!V.IsValid()) return TEXT("null");
	switch (V->Type)
	{
	case EJson::String: return Quote(V->AsString());
	case EJson::Number: return Number(V->AsNumber());
	case EJson::Boolean: return V->AsBool() ? TEXT("true") : TEXT("false");
	case EJson::Array:
	{
		FString Out = TEXT("[");
		const TArray<TSharedPtr<FJsonValue>>& A = V->AsArray();
		for (int32 i = 0; i < A.Num(); i++) { if (i) Out += TEXT(","); Out += (A[i].IsValid() && A[i]->Type != EJson::None) ? WriteValue(A[i]) : TEXT("null"); }
		return Out + TEXT("]");
	}
	case EJson::Object: return Write(V->AsObject());
	default: return TEXT("null");
	}
}

FString HKJsonWire::Write(const TSharedPtr<FJsonObject>& O)
{
	if (!O.IsValid()) return TEXT("null");
	FString Out = TEXT("{");
	bool bFirst = true;
	for (const auto& KV : O->Values)
	{
		if (!KV.Value.IsValid() || KV.Value->Type == EJson::None) continue;   // undefined is left out
		if (!bFirst) Out += TEXT(",");
		bFirst = false;
		Out += Quote(FString(*KV.Key)) + TEXT(":") + WriteValue(KV.Value);
	}
	return Out + TEXT("}");
}

TSharedPtr<FJsonObject> HKJsonWire::Parse(const FString& S)
{
	TSharedPtr<FJsonObject> J;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(S), J) || !J.IsValid()) return nullptr;
	return J;
}

bool HKJsonWire::Truthy(const TSharedPtr<FJsonObject>& O, const FString& K)
{
	if (!O.IsValid()) return false;
	const TSharedPtr<FJsonValue> V = O->TryGetField(K);
	if (!V.IsValid()) return false;
	switch (V->Type)
	{
	case EJson::Boolean: return V->AsBool();
	case EJson::Number: { const double D = V->AsNumber(); return D != 0 && !FMath::IsNaN(D); }
	case EJson::String: return !V->AsString().IsEmpty();
	case EJson::Array: case EJson::Object: return true;
	default: return false;
	}
}

double HKJsonWire::Num(const TSharedPtr<FJsonObject>& O, const FString& K, double Default)
{
	if (!O.IsValid()) return Default;
	const TSharedPtr<FJsonValue> V = O->TryGetField(K);
	if (!V.IsValid()) return Default;
	if (V->Type == EJson::Number) return V->AsNumber();
	if (V->Type == EJson::String) { const FString S = V->AsString(); return S.IsNumeric() ? FCString::Atod(*S) : Default; }   // +m.v
	if (V->Type == EJson::Boolean) return V->AsBool() ? 1 : 0;
	return Default;
}

FString HKJsonWire::Str(const TSharedPtr<FJsonObject>& O, const FString& K, const FString& Default)
{
	if (!O.IsValid()) return Default;
	const TSharedPtr<FJsonValue> V = O->TryGetField(K);
	if (!V.IsValid()) return Default;
	if (V->Type == EJson::String) return V->AsString();
	if (V->Type == EJson::Number) return Number(V->AsNumber());
	if (V->Type == EJson::Boolean) return V->AsBool() ? TEXT("true") : TEXT("false");
	return Default;
}

TArray<double> HKJsonWire::Nums(const TSharedPtr<FJsonObject>& O, const FString& K)
{
	TArray<double> Out;
	const TArray<TSharedPtr<FJsonValue>>* A;
	if (O.IsValid() && O->TryGetArrayField(K, A)) for (const TSharedPtr<FJsonValue>& V : *A) Out.Add(V.IsValid() && V->Type == EJson::Number ? V->AsNumber() : 0);
	return Out;
}

TSharedPtr<FJsonValue> HKJsonWire::NumArray(const TArray<double>& A)
{
	TArray<TSharedPtr<FJsonValue>> Out;
	for (double D : A) Out.Add(MakeShared<FJsonValueNumber>(D));
	return MakeShared<FJsonValueArray>(Out);
}

// ---------------------------------------------------------------------------------------------------------------
// Timers
// ---------------------------------------------------------------------------------------------------------------
int32 FHKTimers::SetTimeout(double Ms, TFunction<void()> Fn)
{
	const int32 Id = NextId++;
	List.Add({Id, NowFn() + FMath::Max(0.0, Ms), 0, MoveTemp(Fn)});
	return Id;
}

int32 FHKTimers::SetInterval(double Ms, TFunction<void()> Fn)
{
	const int32 Id = NextId++;
	List.Add({Id, NowFn() + FMath::Max(1.0, Ms), FMath::Max(1.0, Ms), MoveTemp(Fn)});
	return Id;
}

void FHKTimers::Clear(int32 Id)
{
	if (Id) List.RemoveAll([Id](const FTimer& T) { return T.Id == Id; });
}

void FHKTimers::Tick()
{
	const double Now = NowFn();
	// Only timers that exist now fire in this tick (a timeout set inside a callback waits for the next one).
	const int32 Limit = NextId;
	for (;;)
	{
		int32 Best = INDEX_NONE;
		for (int32 i = 0; i < List.Num(); i++)
			if (List[i].Id < Limit && List[i].Due <= Now && (Best == INDEX_NONE || List[i].Due < List[Best].Due || (List[i].Due == List[Best].Due && List[i].Id < List[Best].Id))) Best = i;
		if (Best == INDEX_NONE) break;
		if (List[Best].Every > 0)
		{
			List[Best].Due = Now + List[Best].Every;
			if (List[Best].Due <= Now) List[Best].Due = Now + 1;
			TFunction<void()> Fn = List[Best].Fn;
			Fn();
		}
		else
		{
			TFunction<void()> Fn = MoveTemp(List[Best].Fn);
			List.RemoveAt(Best);
			Fn();
		}
	}
}

// ---------------------------------------------------------------------------------------------------------------
// Unreal WebSocket
// ---------------------------------------------------------------------------------------------------------------
namespace
{
	class FHKUnrealSocket : public IHKSocket, public TSharedFromThis<FHKUnrealSocket>
	{
	public:
		explicit FHKUnrealSocket(const FString& InUrl) : Url(InUrl) {}
		virtual ~FHKUnrealSocket() override
		{
			if (!Ws.IsValid()) return;
			Ws->OnConnected().Clear(); Ws->OnMessage().Clear(); Ws->OnClosed().Clear(); Ws->OnConnectionError().Clear();
			if (Ws->IsConnected()) Ws->Close(1000);
		}
		virtual void Connect() override
		{
			if (!FModuleManager::Get().IsModuleLoaded(TEXT("WebSockets"))) FModuleManager::LoadModuleChecked<FWebSocketsModule>(TEXT("WebSockets"));
			Ws = FWebSocketsModule::Get().CreateWebSocket(Url, FString());
			Ws->SetTextMessageMemoryLimit(1 << 20);
			TWeakPtr<FHKUnrealSocket> Weak = AsShared();
			Ws->OnConnected().AddLambda([Weak]() { if (TSharedPtr<FHKUnrealSocket> S = Weak.Pin()) { S->bOpen = true; if (S->OnOpen) S->OnOpen(); } });
			Ws->OnMessage().AddLambda([Weak](const FString& M) { if (TSharedPtr<FHKUnrealSocket> S = Weak.Pin()) if (S->OnMessage) S->OnMessage(M); });
			Ws->OnClosed().AddLambda([Weak](int32 Code, const FString& Reason, bool) { if (TSharedPtr<FHKUnrealSocket> S = Weak.Pin()) S->Closed(Code); });
			Ws->OnConnectionError().AddLambda([Weak](const FString& Error)
			{
				UE_LOG(LogHaarklous, Log, TEXT("Lobby connection error: %s"), *Error);
				if (TSharedPtr<FHKUnrealSocket> S = Weak.Pin()) S->Closed(1006);
			});
			Ws->Connect();
		}
		virtual void Send(const FString& Text) override { if (Ws.IsValid() && bOpen) Ws->Send(Text); }
		virtual void Close(int32 Code) override { if (Ws.IsValid() && !bClosed) Ws->Close(Code); }
		virtual bool IsOpen() const override { return bOpen && !bClosed; }
	private:
		void Closed(int32 Code)
		{
			if (bClosed) return;
			bClosed = true; bOpen = false;
			if (OnClose) OnClose(Code);
		}
		FString Url;
		TSharedPtr<IWebSocket> Ws;
		bool bOpen = false, bClosed = false;
	};
}

TSharedPtr<IHKSocket> FHKNet::MakeUnrealSocket(const FString& Url)
{
	if (!Url.StartsWith(TEXT("ws://")) && !Url.StartsWith(TEXT("wss://"))) return nullptr;
	if (IsLiveServer(Url)) { UE_LOG(LogHaarklous, Error, TEXT("Refusing to connect to the live game server (%s)"), *Url); return nullptr; }
	return MakeShared<FHKUnrealSocket>(Url);
}

// ---------------------------------------------------------------------------------------------------------------
// The lobby connection (game-net.js)
// ---------------------------------------------------------------------------------------------------------------
const TCHAR* const FHKNet::Alphabet = TEXT("ABCDEFGHJKLMNPQRSTUVWXYZ23456789");
const TCHAR* const FHKNet::CodeKey = TEXT("haarklous-lobby-code-v1");
const TCHAR* const FHKNet::NameKey = TEXT("haarklous-name-v1");

bool FHKNet::ValidCode(const FString& C)
{
	if (C.Len() != 4) return false;
	for (TCHAR Ch : C) if (!FCString::Strchr(Alphabet, Ch) || Ch == 0) return false;
	return true;
}

FString FHKNet::RandomCode(const TFunction<double()>& Random)
{
	FString S;
	for (int32 i = 0; i < 4; i++) S.AppendChar(Alphabet[FMath::Clamp((int32)FMath::FloorToDouble(Random() * 32), 0, 31)]);
	return S;
}

double FHKNet::RetryDelayMs(int32 Retry) { return FMath::Min(30000.0, 1500.0 * FMath::Pow(2.0, (double)Retry)); }

bool FHKNet::IsLiveServer(const FString& Base)
{
	return Base.Contains(TEXT("haarklous-online"), ESearchCase::IgnoreCase) || Base.Contains(TEXT("haarklous-plass"), ESearchCase::IgnoreCase);
}

FHKNetOptions FHKNet::DefaultOptions()
{
	FHKNetOptions O;
	if (GConfig)
	{
		GConfig->GetBool(TEXT("Haarklous.Online"), TEXT("bOnlineEnabled"), O.bEnabled, GGameIni);
		GConfig->GetString(TEXT("Haarklous.Online"), TEXT("ServerBase"), O.Base, GGameIni);
	}
	const TCHAR* Cmd = FCommandLine::Get();
	if (FParse::Param(Cmd, TEXT("HKOnline"))) O.bEnabled = true;
	if (FParse::Param(Cmd, TEXT("HKOffline"))) O.bEnabled = false;
	FString V;
	if (FParse::Value(Cmd, TEXT("HKServer="), V, false)) O.Base = V;
	O.Base.RemoveFromEnd(TEXT("/"));
	if (FParse::Value(Cmd, TEXT("HKLobby="), V, false)) O.Lobby = V.ToUpper();
	if (IsLiveServer(O.Base))
	{
		UE_LOG(LogHaarklous, Error, TEXT("The configured lobby server %s is the live game; online stays off."), *O.Base);
		O.bEnabled = false; O.Base.Reset();
	}
	O.MakeSocket = [](const FString& Url) { return FHKNet::MakeUnrealSocket(Url); };
	O.DateNow = []() { return (FDateTime::UtcNow() - FDateTime(1970, 1, 1)).GetTotalMilliseconds(); };
	O.PerfNow = []() { return FPlatformTime::Seconds() * 1000.0; };
	O.Random = []() { return (double)FMath::FRand(); };
	return O;
}

FHKNet::FHKNet(TFunction<void(const TSharedPtr<FJsonObject>&)> InOnMessage, TFunction<void()> InOnStatus, FHKNetOptions InOptions)
	: OnMessage(MoveTemp(InOnMessage)), OnStatus(MoveTemp(InOnStatus)), Options(MoveTemp(InOptions)),
	  Timers([this]() { return Options.PerfNow(); })
{
	if (!Options.DateNow) Options.DateNow = []() { return (FDateTime::UtcNow() - FDateTime(1970, 1, 1)).GetTotalMilliseconds(); };
	if (!Options.PerfNow) Options.PerfNow = []() { return FPlatformTime::Seconds() * 1000.0; };
	if (!Options.Random) Options.Random = []() { return (double)FMath::FRand(); };
	if (IsLiveServer(Options.Base)) { Options.bEnabled = false; Options.Base.Reset(); }
	OwnCode = FHKStore::Get(CodeKey);
	if (!ValidCode(OwnCode)) { OwnCode = RandomCode(Options.Random); FHKStore::Set(CodeKey, OwnCode); }
	Name = FHKStore::Get(NameKey);
	if (Name.IsEmpty()) Name = FString::Printf(TEXT("Player %d"), (int32)FMath::FloorToDouble(100 + Options.Random() * 900));
	// Keep-alive and clock sync (also keeps idle proxies from dropping the socket).
	PingTimer = Timers.SetInterval(20000, [this]()
	{
		TSharedPtr<FJsonObject> M = MakeShared<FJsonObject>();
		M->SetStringField(TEXT("t"), TEXT("ping"));
		M->SetNumberField(TEXT("c"), Options.DateNow());
		Send(M);
	});
	if (Options.bOwnTicker) TickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([this](float) { Tick(); return true; }));
}

FHKNet::~FHKNet()
{
	if (TickHandle.IsValid()) FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);
	AliveToken.Reset();
	if (Ws.IsValid()) Ws->Close(1000);
	Ws.Reset();
	Closing.Reset();
}

void FHKNet::Tick()
{
	Timers.Tick();
	if (AfterTask) AfterTask();
}

const FHKNetPlayer* FHKNet::Player(int32 Id) const
{
	return Players.FindByPredicate([Id](const FHKNetPlayer& P) { return P.Id == Id; });
}

void FHKNet::SetStatus(const FString& S)
{
	Status = S;
	if (OnStatus) OnStatus();
}

bool FHKNet::Connect(const FString& InCode)
{
	FString C;
	for (TCHAR Ch : InCode.ToUpper()) if ((Ch >= 'A' && Ch <= 'Z') || (Ch >= '0' && Ch <= '9')) C.AppendChar(Ch);
	if (!ValidCode(C)) return false;
	Want = C; Retry = 0;
	Open();
	return true;
}

void FHKNet::Begin()
{
	const FString Q = Options.Lobby.ToUpper();
	if (!(!Q.IsEmpty() && Connect(Q))) Home();
}

void FHKNet::Open()
{
	Shut();
	if (Want.IsEmpty() || !Options.bEnabled || !Options.MakeSocket) return;   // the web without WebSocket support
	Code = Want;
	SetStatus(TEXT("connecting"));
	TSharedPtr<IHKSocket> Sock = Options.MakeSocket(Options.Base + TEXT("/lobby/") + Code);
	if (!Sock.IsValid()) { SetStatus(TEXT("offline")); Later(); return; }
	Ws = Sock; bAlive = true;
	IHKSocket* Raw = Sock.Get();
	const TWeakPtr<bool> Token = AliveToken;
	Sock->OnOpen = [this, Raw, Token]()
	{
		if (!Token.IsValid() || Ws.Get() != Raw) return;
		Retry = 0;
		TSharedPtr<FJsonObject> M = MakeShared<FJsonObject>();
		M->SetStringField(TEXT("t"), TEXT("hello"));
		M->SetStringField(TEXT("name"), Name);
		Raw->Send(HKJsonWire::Write(M));
		if (AfterTask) AfterTask();
	};
	Sock->OnMessage = [this, Raw, Token](const FString& Text) { if (!Token.IsValid()) return; HandleMessage(Raw, Text); if (Token.IsValid() && AfterTask) AfterTask(); };
	Sock->OnClose = [this, Raw, Token](int32 CloseCode) { if (!Token.IsValid()) return; HandleClose(Raw, CloseCode); if (Token.IsValid() && AfterTask) AfterTask(); };
	Sock->Connect();
}

void FHKNet::HandleMessage(IHKSocket* Sock, const FString& Text)
{
	if (Ws.Get() != Sock) return;
	const TSharedPtr<FJsonObject> M = HKJsonWire::Parse(Text);
	if (!M.IsValid()) return;
	const FString T = HKJsonWire::Str(M, TEXT("t"));
	auto PlayerOf = [](const TSharedPtr<FJsonObject>& P)
	{
		FHKNetPlayer Out;
		Out.Id = (int32)HKJsonWire::Num(P, TEXT("id")); Out.Slot = (int32)HKJsonWire::Num(P, TEXT("slot"));
		Out.Name = HKJsonWire::Str(P, TEXT("name")); Out.bMic = HKJsonWire::Truthy(P, TEXT("mic"));
		return Out;
	};
	auto SetPlayer = [this](const FHKNetPlayer& P)
	{
		if (FHKNetPlayer* Old = Players.FindByPredicate([&P](const FHKNetPlayer& Q) { return Q.Id == P.Id; })) *Old = P; else Players.Add(P);
	};
	if (T == TEXT("welcome"))
	{
		YouId = (int32)HKJsonWire::Num(M, TEXT("you")); Slot = (int32)HKJsonWire::Num(M, TEXT("slot"));
		Offset = HKJsonWire::Num(M, TEXT("now")) - Options.DateNow();
		Players.Reset();
		const TArray<TSharedPtr<FJsonValue>>* List;
		if (M->TryGetArrayField(TEXT("players"), List)) for (const TSharedPtr<FJsonValue>& V : *List) if (V.IsValid() && V->Type == EJson::Object) SetPlayer(PlayerOf(V->AsObject()));
		// A name changed while the hello was on its way is sent again.
		FHKNetPlayer* Me = Players.FindByPredicate([this](const FHKNetPlayer& P) { return P.Id == YouId; });
		if (Me && Me->Name != Name)
		{
			Me->Name = Name;
			TSharedPtr<FJsonObject> N = MakeShared<FJsonObject>();
			N->SetStringField(TEXT("t"), TEXT("name")); N->SetStringField(TEXT("name"), Name);
			Sock->Send(HKJsonWire::Write(N));
		}
		SetStatus(TEXT("online"));
	}
	else if (T == TEXT("join")) SetPlayer(PlayerOf(M));
	else if (T == TEXT("leave")) { const int32 Id = (int32)HKJsonWire::Num(M, TEXT("id")); Players.RemoveAll([Id](const FHKNetPlayer& P) { return P.Id == Id; }); }
	else if (T == TEXT("name")) { const int32 Id = (int32)HKJsonWire::Num(M, TEXT("id")); for (FHKNetPlayer& P : Players) if (P.Id == Id) P.Name = HKJsonWire::Str(M, TEXT("name")); }
	else if (T == TEXT("mic")) { const int32 Id = (int32)HKJsonWire::Num(M, TEXT("id")); for (FHKNetPlayer& P : Players) if (P.Id == Id) P.bMic = HKJsonWire::Truthy(M, TEXT("on")); }
	else if (T == TEXT("full")) SetStatus(TEXT("full"));
	else if (T == TEXT("pong"))
	{
		const double Now = Options.DateNow();
		Offset = HKJsonWire::Num(M, TEXT("now")) - (Now + (HKJsonWire::Truthy(M, TEXT("c")) ? HKJsonWire::Num(M, TEXT("c")) : Options.DateNow())) / 2;
	}
	if (OnMessage) OnMessage(M);
	if ((T == TEXT("join") || T == TEXT("leave") || T == TEXT("name")) && OnStatus) OnStatus();
}

void FHKNet::HandleClose(IHKSocket* Sock, int32 CloseCode)
{
	if (Ws.Get() != Sock)
	{
		Closing.RemoveAll([Sock](const TSharedPtr<IHKSocket>& S) { return S.Get() == Sock; });
		return;
	}
	TSharedPtr<IHKSocket> Keep = Ws;
	Ws.Reset(); YouId = 0; Players.Reset();
	{
		TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
		R->SetStringField(TEXT("t"), TEXT("reset"));
		if (OnMessage) OnMessage(R);
	}
	if (CloseCode == 4001 || Status == TEXT("full"))
	{
		TSharedPtr<FJsonObject> F = MakeShared<FJsonObject>();
		F->SetStringField(TEXT("t"), TEXT("full"));
		F->SetStringField(TEXT("code"), Code);
		if (OnMessage) OnMessage(F);
		if (Want != OwnCode) { Want = OwnCode; Open(); return; }
	}
	SetStatus(TEXT("offline"));
	Later();
}

void FHKNet::Later()
{
	Timers.Clear(Timer); Timer = 0;
	if (!bAlive) return;
	Timer = Timers.SetTimeout(RetryDelayMs(Retry++), [this]() { Timer = 0; Open(); });
}

void FHKNet::Shut()
{
	Timers.Clear(Timer); Timer = 0;
	if (Ws.IsValid())
	{
		TSharedPtr<IHKSocket> S = Ws;
		Ws.Reset();
		Closing.Add(S);
		if (Closing.Num() > 8) Closing.RemoveAt(0);
		S->Close(1000);
	}
	YouId = 0;
	Players.Reset();
}

bool FHKNet::Send(const TSharedPtr<FJsonObject>& M)
{
	if (Ws.IsValid() && Ws->IsOpen() && YouId) { Ws->Send(HKJsonWire::Write(M)); return true; }
	return false;
}

void FHKNet::SetName(const FString& InName)
{
	const FString N = InName.TrimStartAndEnd().Left(24);
	if (N.IsEmpty()) return;
	Name = N;
	FHKStore::Set(NameKey, N);
	TSharedPtr<FJsonObject> M = MakeShared<FJsonObject>();
	M->SetStringField(TEXT("t"), TEXT("name")); M->SetStringField(TEXT("name"), N);
	Send(M);
}

FString FHKNet::StatusText() const
{
	if (Online()) return FString::Printf(TEXT("Online · lobby %s · %d/5 players"), *Code, Players.Num());
	if (Status == TEXT("connecting")) return FString::Printf(TEXT("Connecting to lobby %s…"), *Code);
	if (Status == TEXT("full")) return TEXT("That lobby is full (5 players).");
	return TEXT("Offline – single player (no connection to the lobby server).");
}

FString FHKNet::WhereText() const
{
	return !Code.IsEmpty() && Code != OwnCode ? FString::Printf(TEXT("You are in lobby %s."), *Code) : FString(TEXT("You are in your own lobby – friends join with your code."));
}

FString FHKNet::Link() const
{
	FString Host = Options.Base;
	if (Host.StartsWith(TEXT("wss://"))) Host.RightChopInline(6); else if (Host.StartsWith(TEXT("ws://"))) Host.RightChopInline(5);
	return FString::Printf(TEXT("https://%s/?lobby=%s"), *Host, Code.IsEmpty() ? *OwnCode : *Code);
}

TArray<FHKNetPlayer> FHKNet::PlayersBySlot() const
{
	TArray<FHKNetPlayer> Out = Players;
	Out.StableSort([](const FHKNetPlayer& A, const FHKNetPlayer& B) { return A.Slot < B.Slot; });
	return Out;
}

FHKCodePad FHKCodePad::Open(const FHKNet& Net)
{
	FHKCodePad P;
	const FString C = !Net.GetCode().IsEmpty() && Net.GetCode() != Net.GetOwnCode() ? Net.GetCode() : FString(TEXT("AAAA"));
	for (TCHAR Ch : C) P.Chars.Add(Ch);
	while (P.Chars.Num() < 4) P.Chars.Add('A');
	return P;
}

void FHKCodePad::Step(int32 Dir)
{
	if (!Dir) return;
	const int32 N = FCString::Strlen(FHKNet::Alphabet);
	const TCHAR* Found = FCString::Strchr(FHKNet::Alphabet, Chars[Pos]);
	const int32 Index = Found ? (int32)(Found - FHKNet::Alphabet) : -1;
	Chars[Pos] = FHKNet::Alphabet[((Index + Dir) % N + N) % N];
}

FString FHKCodePad::Code() const
{
	FString S;
	for (TCHAR C : Chars) S.AppendChar(C);
	return S;
}
