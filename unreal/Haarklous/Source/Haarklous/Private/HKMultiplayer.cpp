#include "HKMultiplayer.h"
#include "Haarklous.h"
#include "HKAvatars.h"
#include "HKMath.h"
#include "HKVehicle.h"

using namespace HKJsonWire;

namespace
{
	TSharedPtr<FJsonObject> Msg(const TCHAR* T)
	{
		TSharedPtr<FJsonObject> M = MakeShared<FJsonObject>();
		M->SetStringField(TEXT("t"), T);
		return M;
	}
	FHKChatLine LineOf(const TSharedPtr<FJsonObject>& O)
	{
		FHKChatLine L;
		L.Id = (int32)Num(O, TEXT("id")); L.Slot = (int32)Num(O, TEXT("slot")); L.Name = Str(O, TEXT("name")); L.Text = Str(O, TEXT("text"));
		L.At = Num(O, TEXT("at")); L.bSys = Truthy(O, TEXT("sys"));
		return L;
	}
	FHKChatLine SysLine(const FString& Name, const FString& Text, int32 Slot)
	{
		FHKChatLine L;
		L.Name = Name; L.Text = Text; L.Slot = Slot; L.bSys = true;
		return L;
	}
	const TSharedPtr<FJsonObject>* ObjField(const TSharedPtr<FJsonObject>& M, const TCHAR* K)
	{
		const TSharedPtr<FJsonObject>* O = nullptr;
		return M.IsValid() && M->TryGetObjectField(K, O) && O && O->IsValid() ? O : nullptr;
	}
}

// ---------------------------------------------------------------------------------------------------------------
// Player state
// ---------------------------------------------------------------------------------------------------------------
TSharedPtr<FJsonObject> FHKPlayerState::ToJson() const
{
	TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetField(TEXT("p"), NumArray({P.X, P.Y, P.Z}));
	O->SetNumberField(TEXT("y"), Y);
	O->SetStringField(TEXT("st"), St);
	O->SetNumberField(TEXT("b"), B);
	if (V >= 0) { O->SetNumberField(TEXT("v"), V); O->SetStringField(TEXT("s"), S); }
	if (Sit) O->SetNumberField(TEXT("sit"), Sit);
	if (bDr) O->SetField(TEXT("dr"), NumArray({Dr[0], Dr[1], Dr[2], Dr[3], Dr[4], Dr[5], Dr[6]}));
	if (Pb.IsValid()) O->SetObjectField(TEXT("pb"), Pb);
	return O;
}

bool FHKPlayerState::FromJson(const TSharedPtr<FJsonValue>& Val, FHKPlayerState& Out)
{
	Out = FHKPlayerState();
	if (!Val.IsValid() || Val->Type != EJson::Object) return false;
	const TSharedPtr<FJsonObject> O = Val->AsObject();
	const TArray<double> Pv = Nums(O, TEXT("p"));
	Out.P = FVector(Pv.IsValidIndex(0) ? Pv[0] : 0, Pv.IsValidIndex(1) ? Pv[1] : 0, Pv.IsValidIndex(2) ? Pv[2] : 0);
	Out.Y = Truthy(O, TEXT("y")) ? Num(O, TEXT("y")) : 0;                         // st.y || 0
	Out.St = Truthy(O, TEXT("st")) ? Str(O, TEXT("st")) : FString(TEXT("stand"));   // st.st || 'stand'
	Out.B = Truthy(O, TEXT("b")) ? 1 : 0;
	const TSharedPtr<FJsonValue> Vv = O->TryGetField(TEXT("v"));
	Out.V = Vv.IsValid() && Vv->Type == EJson::Number ? (int32)Vv->AsNumber() : -1;  // st.v ?? -1
	Out.S = Str(O, TEXT("s"));
	Out.Sit = Truthy(O, TEXT("sit")) ? (int32)Num(O, TEXT("sit")) : 0;
	if (Truthy(O, TEXT("dr")))
	{
		const TArray<double> D = Nums(O, TEXT("dr"));
		Out.bDr = true;
		for (int32 i = 0; i < 7; i++) Out.Dr[i] = D.IsValidIndex(i) ? D[i] : 0;
	}
	const TSharedPtr<FJsonObject>* PbO;
	if (O->TryGetObjectField(TEXT("pb"), PbO)) Out.Pb = *PbO;
	return true;
}

FHKRemote::~FHKRemote() {}

// ---------------------------------------------------------------------------------------------------------------
// Multiplayer
// ---------------------------------------------------------------------------------------------------------------
FHKMultiplayer::FHKMultiplayer(FHKMultiplayerContext InCtx, FHKNetOptions NetOptions, TUniquePtr<IHKVoice> InVoice)
	: Ctx(MoveTemp(InCtx)), VoicePtr(InVoice ? MoveTemp(InVoice) : MakeUnique<IHKVoice>())
{
	NetPtr = MakeUnique<FHKNet>([this](const TSharedPtr<FJsonObject>& M) { OnMessage(M); }, [this]() { if (Ctx.OnStatus) Ctx.OnStatus(); OnLobbyChange(); }, MoveTemp(NetOptions));
	NetPtr->AfterTask = [this]() { EndFrame(); };
	Fire.T = NetPtr->DateNow();
	VoicePtr->SendSignal = [this](int32 To, const TSharedPtr<FJsonValue>& D)
	{
		TSharedPtr<FJsonObject> M = Msg(TEXT("rtc"));
		M->SetNumberField(TEXT("to"), To);
		M->SetField(TEXT("d"), D.IsValid() ? D : MakeShared<FJsonValueNull>());
		NetPtr->Send(M);
	};
	VoicePtr->You = [this]() { return NetPtr->You(); };
}

FHKMultiplayer::~FHKMultiplayer()
{
	NetPtr.Reset();
	VoicePtr->Reset();
	for (TUniquePtr<FHKRemote>& R : RemotesList)
	{
		if (Ctx.RemoveFromScene && R->Av) Ctx.RemoveFromScene(R->Av->Rig);
		if (Ctx.RemoveFromScene && R->Drone) Ctx.RemoveFromScene(*R->Drone);
	}
	RemotesList.Reset();
}

FHKVehicle* FHKMultiplayer::VehicleAt(int32 I) const { return I >= 0 && I < VehicleCount() && Ctx.Vehicle ? Ctx.Vehicle(I) : nullptr; }
int32 FHKMultiplayer::VehicleCount() const { return Ctx.NumVehicles ? Ctx.NumVehicles() : 0; }

FHKRemote* FHKMultiplayer::FindRemote(int32 Id)
{
	for (TUniquePtr<FHKRemote>& R : RemotesList) if (R->Id == Id) return R.Get();
	return nullptr;
}

const FHKRemote* FHKMultiplayer::Remote(int32 Id) const
{
	for (const TUniquePtr<FHKRemote>& R : RemotesList) if (R->Id == Id) return R.Get();
	return nullptr;
}

FHKRemote& FHKMultiplayer::RemoteOf(int32 Id)
{
	if (FHKRemote* R = FindRemote(Id)) return *R;
	const FHKNetPlayer* P = NetPtr->Player(Id);
	TUniquePtr<FHKRemote> R = MakeUnique<FHKRemote>();
	R->Id = Id;
	R->Slot = P ? P->Slot : 0;
	R->Name = P ? P->Name : FString(TEXT("Player"));
	R->Av = HKAvatars::Avatar(R->Slot, R->Name);
	R->Av->Group()->bVisible = false;
	if (Ctx.AddToScene) Ctx.AddToScene(R->Av->Rig, R->Av->RigName);
	RemotesList.Add(MoveTemp(R));
	return *RemotesList.Last();
}

void FHKMultiplayer::Drop(int32 Id)
{
	const int32 Index = RemotesList.IndexOfByPredicate([Id](const TUniquePtr<FHKRemote>& R) { return R->Id == Id; });
	if (Index == INDEX_NONE) return;
	TUniquePtr<FHKRemote> R = MoveTemp(RemotesList[Index]);
	RemotesList.RemoveAt(Index);
	if (Ctx.RemoveFromScene) { Ctx.RemoveFromScene(R->Av->Rig); if (R->Drone) Ctx.RemoveFromScene(*R->Drone); }
	VoicePtr->PeerLeft(Id);
}

TMap<int32, FHKOcc> FHKMultiplayer::ParseOcc(const TSharedPtr<FJsonObject>& M)
{
	TMap<int32, FHKOcc> Out;
	const TSharedPtr<FJsonObject>* O = ObjField(M, TEXT("occ"));
	if (!O) return Out;
	for (const auto& KV : (*O)->Values)
	{
		if (!FString(*KV.Key).IsNumeric() || !KV.Value.IsValid() || KV.Value->Type != EJson::Object) continue;
		const TSharedPtr<FJsonObject> S = KV.Value->AsObject();
		FHKOcc Occ;
		Occ.D = (int32)Num(S, TEXT("d")); Occ.P = (int32)Num(S, TEXT("p"));
		Out.Add(FCString::Atoi(*KV.Key), Occ);
	}
	return Out;
}

bool FHKMultiplayer::ParseFire(const TSharedPtr<FJsonObject>& M, FFire& Out)
{
	const TSharedPtr<FJsonObject>* O = ObjField(M, TEXT("fire"));
	if (!O) return false;
	Out.Level = Num(*O, TEXT("level")); Out.T = Num(*O, TEXT("t"));
	return true;
}

void FHKMultiplayer::OnMessage(const TSharedPtr<FJsonObject>& M)
{
	const FString T = Str(M, TEXT("t"));
	FHKNet& Net = *NetPtr;
	if (T == TEXT("welcome"))
	{
		TArray<int32> Ids;
		for (const TUniquePtr<FHKRemote>& R : RemotesList) Ids.Add(R->Id);
		for (int32 Id : Ids) Drop(Id);
		const int32 You = (int32)Num(M, TEXT("you"));
		const TArray<TSharedPtr<FJsonValue>>* Players = nullptr;
		int32 NumPlayers = 0;
		if (M->TryGetArrayField(TEXT("players"), Players))
		{
			NumPlayers = Players->Num();
			for (const TSharedPtr<FJsonValue>& P : *Players)
			{
				if (!P.IsValid() || P->Type != EJson::Object) continue;
				const int32 Id = (int32)Num(P->AsObject(), TEXT("id"));
				if (Id != You) { RemoteOf(Id); VoicePtr->PeerJoined(Id); }
			}
		}
		if (const TSharedPtr<FJsonObject>* States = ObjField(M, TEXT("states")))
			for (const auto& KV : (*States)->Values) { FHKRemote& R = RemoteOf(FCString::Atoi(*KV.Key)); R.bHasSt = FHKPlayerState::FromJson(KV.Value, R.St); }
		OccMap = ParseOcc(M);
		ParseFire(M, Fire);
		VehTargets.Reset();
		if (const TSharedPtr<FJsonObject>* Vs = ObjField(M, TEXT("vehicles")))
			for (const auto& KV : (*Vs)->Values)
			{
				FHKVehicle* V = FString(*KV.Key).IsNumeric() ? VehicleAt(FCString::Atoi(*KV.Key)) : nullptr;
				if (V && KV.Value.IsValid() && KV.Value->Type == EJson::Array)
				{
					TArray<double> S;
					for (const TSharedPtr<FJsonValue>& E : KV.Value->AsArray()) S.Add(E.IsValid() && E->Type == EJson::Number ? E->AsNumber() : 0);
					V->NetSet(S);
				}
			}
		if (const TSharedPtr<FJsonObject>* Doors = ObjField(M, TEXT("doors")))
			for (const auto& KV : (*Doors)->Values) if (Ctx.ApplyDoor) Ctx.ApplyDoor(FString(*KV.Key), KV.Value.IsValid() && KV.Value->Type == EJson::Boolean && KV.Value->AsBool(), true);
		const TArray<TSharedPtr<FJsonValue>>* Chat = nullptr;
		if (M->TryGetArrayField(TEXT("chat"), Chat))
			for (const TSharedPtr<FJsonValue>& L : *Chat) if (L.IsValid() && L->Type == EJson::Object && Ctx.OnChat) Ctx.OnChat(LineOf(L->AsObject()), false, true);
		if (VoicePtr->Mic()) { TSharedPtr<FJsonObject> Mic = Msg(TEXT("mic")); Mic->SetBoolField(TEXT("on"), true); Net.Send(Mic); }
		const FString Code = Str(M, TEXT("code"));
		if (Ctx.Notice) Ctx.Notice(NumPlayers > 1 ? FString::Printf(TEXT("Joined lobby %s · %d players"), *Code, NumPlayers) : FString::Printf(TEXT("Online · your lobby code is %s"), *Code), 4);
	}
	else if (T == TEXT("join"))
	{
		const int32 Id = (int32)Num(M, TEXT("id"));
		const FString Name = Str(M, TEXT("name"));
		RemoteOf(Id).Av->SetName(Name);
		VoicePtr->PeerJoined(Id);
		if (Ctx.Notice) Ctx.Notice(FString::Printf(TEXT("%s joined"), *Name), 3);
		if (Ctx.OnChat) Ctx.OnChat(SysLine(Name, TEXT("joined the lobby"), (int32)Num(M, TEXT("slot"))), false, false);
	}
	else if (T == TEXT("leave"))
	{
		const int32 Id = (int32)Num(M, TEXT("id"));
		if (const FHKRemote* R = FindRemote(Id)) if (Ctx.OnChat) Ctx.OnChat(SysLine(R->Name, TEXT("left"), R->Slot), false, false);
		Drop(Id);
	}
	else if (T == TEXT("name"))
	{
		if (FHKRemote* R = FindRemote((int32)Num(M, TEXT("id")))) { R->Name = Str(M, TEXT("name")); R->Av->SetName(R->Name); }
	}
	else if (T == TEXT("st"))
	{
		FHKRemote& R = RemoteOf((int32)Num(M, TEXT("id")));
		R.bHasSt = FHKPlayerState::FromJson(M->TryGetField(TEXT("s")), R.St);
	}
	else if (T == TEXT("vs"))
	{
		const int32 I = (int32)Num(M, TEXT("v"), -1);
		FHKVehicle* V = VehicleAt(I);
		if (!V) return;
		const TArray<double> S = Nums(M, TEXT("s"));
		const FHKOcc* O = OccMap.Find(I);
		if (O && O->D && O->D != Net.You())
		{
			const int32 Index = VehTargets.IndexOfByPredicate([I](const TPair<int32, FVehTarget>& P) { return P.Key == I; });
			FVehTarget Tgt;
			Tgt.A = S;
			Tgt.Prev = Index != INDEX_NONE ? VehTargets[Index].Value.A : S;
			Tgt.At = Net.PerfNow();
			Tgt.PrevAt = Index != INDEX_NONE ? VehTargets[Index].Value.At : Net.PerfNow() - 100;
			if (Index != INDEX_NONE) VehTargets[Index].Value = MoveTemp(Tgt); else VehTargets.Add(TPair<int32, FVehTarget>(I, MoveTemp(Tgt)));
		}
		else if (!Ctx.DrivingIndex || Ctx.DrivingIndex() != I) V->NetSet(S);
	}
	else if (T == TEXT("occ"))
	{
		const TMap<int32, FHKOcc> Before = OccMap;
		OccMap = ParseOcc(M);
		for (const auto& KV : Before)
		{
			const FHKOcc* Now = OccMap.Find(KV.Key);
			if (KV.Value.D && KV.Value.D != Net.You() && !(Now && Now->D == KV.Value.D))
				VehTargets.RemoveAll([&KV](const TPair<int32, FVehTarget>& P) { return P.Key == KV.Key; });
		}
	}
	else if (T == TEXT("claim"))
	{
		const int32 I = (int32)Num(M, TEXT("v"), -1);
		if (FPending* P = Pending.Find(I))
		{
			TFunction<void(const FHKClaimResult&)> Done = MoveTemp(P->Done);
			Pending.Remove(I);
			FHKClaimResult Res;
			Res.bOk = Truthy(M, TEXT("ok")); Res.By = (int32)Num(M, TEXT("by"));
			if (Done) Deferred.Add([Done, Res]() { Done(Res); });
		}
	}
	else if (T == TEXT("chat"))
	{
		const FHKChatLine Line = LineOf(M);
		const bool bMine = Line.Id == Net.You();
		if (Ctx.OnChat) Ctx.OnChat(Line, bMine, false);
		if (!bMine) if (FHKRemote* R = FindRemote(Line.Id)) R->Av->Say(Line.Text);
	}
	else if (T == TEXT("door"))
	{
		if (Ctx.ApplyDoor) Ctx.ApplyDoor(Str(M, TEXT("id")), Truthy(M, TEXT("open")), false);
	}
	else if (T == TEXT("fire"))
	{
		ParseFire(M, Fire);
		const int32 By = (int32)Num(M, TEXT("by"));
		if (By && By != Net.You()) if (const FHKRemote* R = FindRemote(By)) if (Ctx.OnRemoteWood) Ctx.OnRemoteWood(*R);
	}
	else if (T == TEXT("rtc"))
	{
		VoicePtr->OnSignal((int32)Num(M, TEXT("from")), M->TryGetField(TEXT("d")));
	}
	else if (T == TEXT("full"))
	{
		if (Ctx.Notice) Ctx.Notice(FString::Printf(TEXT("Lobby %s is full (%d players). Back in your own lobby."), *Str(M, TEXT("code")), NetPtr->Max()), 5);
	}
	else if (T == TEXT("reset"))
	{
		TArray<int32> Ids;
		for (const TUniquePtr<FHKRemote>& R : RemotesList) Ids.Add(R->Id);
		for (int32 Id : Ids) Drop(Id);
		OccMap.Reset();
		VehTargets.Reset();
		for (auto& KV : Pending)
		{
			TFunction<void(const FHKClaimResult&)> Done = MoveTemp(KV.Value.Done);
			FHKClaimResult Res;
			Res.bOk = true; Res.bOffline = true;
			if (Done) Deferred.Add([Done, Res]() { Done(Res); });
		}
		Pending.Reset();
	}
}

void FHKMultiplayer::OnLobbyChange()
{
	// A new lobby: vehicles go back to where everybody else has them (the lobby then sends the ones that were moved).
	const FHKNet& N = *NetPtr;
	if (!N.Online() || (bHasLastLobby && N.GetCode() == LastLobby)) return;
	LastLobby = N.GetCode(); bHasLastLobby = true;
	const int32 Mine = Ctx.PlayerVehicleIndex ? Ctx.PlayerVehicleIndex() : -1;
	for (int32 i = 0; i < VehicleCount(); i++)
	{
		FHKVehicle* V = VehicleAt(i);
		if (!V) continue;
		if (i == Mine) { Claim(i, Ctx.PlayerDriving && Ctx.PlayerDriving() ? TEXT("d") : TEXT("p"), nullptr); continue; }
		if (V->Spawn.Num()) V->NetSet(V->Spawn);
		if (V->bAircraft) V->Reset();
	}
}

void FHKMultiplayer::Claim(int32 I, const FString& Seat, TFunction<void(const FHKClaimResult&)> Done)
{
	if (!NetPtr->Online())
	{
		FHKClaimResult Res;
		Res.bOk = true;
		if (Done) Deferred.Add([Done, Res]() { Done(Res); });
		return;
	}
	const int32 Token = NextToken++;
	Pending.Add(I, FPending{Token, MoveTemp(Done)});
	TSharedPtr<FJsonObject> M = Msg(TEXT("claim"));
	M->SetNumberField(TEXT("v"), I);
	M->SetStringField(TEXT("seat"), Seat);
	NetPtr->Send(M);
	NetPtr->GetTimers().SetTimeout(4000, [this, I, Token]()
	{
		FPending* P = Pending.Find(I);
		if (!P || P->Token != Token) return;
		TFunction<void(const FHKClaimResult&)> D = MoveTemp(P->Done);
		Pending.Remove(I);
		FHKClaimResult Res;
		Res.bOk = false; Res.bTimeout = true;
		if (D) Deferred.Add([D, Res]() { D(Res); });
	});
}

void FHKMultiplayer::Release(int32 I, const FString& Seat, const TArray<double>* Final)
{
	if (NetPtr->Online())
	{
		TSharedPtr<FJsonObject> M = Msg(TEXT("release"));
		M->SetNumberField(TEXT("v"), I);
		M->SetStringField(TEXT("seat"), Seat);
		if (Final) M->SetField(TEXT("s"), NumArray(*Final));
		NetPtr->Send(M);
	}
	if (FHKOcc* O = OccMap.Find(I))
	{
		if (Seat == TEXT("d") && O->D == NetPtr->You()) O->D = 0;
		if (Seat == TEXT("p") && O->P == NetPtr->You()) O->P = 0;
	}
}

int32 FHKMultiplayer::Occupant(int32 I, const FString& Seat) const
{
	const FHKOcc* O = OccMap.Find(I);
	return O ? SeatOf(*O, Seat) : 0;
}

bool FHKMultiplayer::RemoteDriven(int32 I) const
{
	const int32 D = Occupant(I, TEXT("d"));
	return D && D != NetPtr->You() && Remote(D) != nullptr;
}

FString FHKMultiplayer::NameOf(int32 Id) const
{
	if (Id == NetPtr->You()) return NetPtr->GetName();
	const FHKRemote* R = Remote(Id);
	return R && !R->Name.IsEmpty() ? R->Name : FString(TEXT("someone"));
}

bool FHKMultiplayer::SitTaken(int32 SpotId) const
{
	for (const TUniquePtr<FHKRemote>& R : RemotesList) if (R->bHasSt && R->St.Sit == SpotId + 1) return true;
	return false;
}

double FHKMultiplayer::FireLevel() const
{
	const double Now = NetPtr->Online() ? NetPtr->ServerNow() : NetPtr->DateNow();
	return FMath::Max(0.0, Fire.Level - (Now - Fire.T) / (Ctx.FireDecayS * 1000));
}

void FHKMultiplayer::AddWood()
{
	const double Now = NetPtr->Online() ? NetPtr->ServerNow() : NetPtr->DateNow();
	const double Level = FMath::Min(Ctx.FireMax, FireLevel() + 1);
	Fire.Level = Level; Fire.T = Now;
	if (NetPtr->Online()) NetPtr->Send(Msg(TEXT("fire")));
}

bool FHKMultiplayer::SendChat(const FString& InText)
{
	const FString Text = InText.TrimStartAndEnd().Left(200);
	if (Text.IsEmpty()) return false;
	if (NetPtr->Online())
	{
		TSharedPtr<FJsonObject> M = Msg(TEXT("chat"));
		M->SetStringField(TEXT("text"), Text);
		NetPtr->Send(M);
		return true;
	}
	FHKChatLine L;
	L.Name = NetPtr->GetName(); L.Text = Text; L.Slot = 0;
	if (Ctx.OnChat) Ctx.OnChat(L, true, false);
	return false;
}

void FHKMultiplayer::SendDoor(const FString& Id, bool bOpen)
{
	if (!NetPtr->Online()) return;
	TSharedPtr<FJsonObject> M = Msg(TEXT("door"));
	M->SetStringField(TEXT("id"), Id);
	M->SetBoolField(TEXT("open"), bOpen);
	NetPtr->Send(M);
}

void FHKMultiplayer::SetMic(bool bOn, TFunction<void(bool)> Done)
{
	VoicePtr->SetMic(bOn, [this, Done](bool bOk)
	{
		if (bOk && NetPtr && NetPtr->Online())
		{
			TSharedPtr<FJsonObject> M = Msg(TEXT("mic"));
			M->SetBoolField(TEXT("on"), VoicePtr->Mic());
			NetPtr->Send(M);
		}
		if (Done) Done(bOk);
	});
}

void FHKMultiplayer::EndFrame()
{
	// Like the browser's microtask queue: callbacks queued while running also run now.
	for (int32 Guard = 0; Deferred.Num() && Guard < 100; Guard++)
	{
		TArray<TFunction<void()>> Now = MoveTemp(Deferred);
		Deferred.Reset();
		for (TFunction<void()>& F : Now) F();
	}
}

void FHKMultiplayer::SmoothVehicle(int32 I, FHKVehicle* V, const FVehTarget& Tgt, double Dt)
{
	// Remote-driven vehicle: extrapolate the latest update a little and ease toward it.
	const double Age = FMath::Min(.25, (NetPtr->PerfNow() - Tgt.At) / 1000), Span = FMath::Max(.03, (Tgt.At - Tgt.PrevAt) / 1000);
	TArray<double> A = Tgt.A;
	const double K = 1 - FMath::Exp(-Dt * 14);
	for (int32 j = 0; j < 3 && j < A.Num(); j++) A[j] += (Tgt.A[j] - (Tgt.Prev.IsValidIndex(j) ? Tgt.Prev[j] : Tgt.A[j])) / Span * Age;
	if (!V->bHasNetCur) { V->NetCur = Tgt.A; V->bHasNetCur = true; }
	TArray<double>& Out = V->NetCur;
	while (Out.Num() < A.Num()) Out.Add(A[Out.Num()]);
	if (V->bAircraft && A.Num() >= 7)
	{
		for (int32 j = 0; j < 3; j++) Out[j] += (A[j] - Out[j]) * K;
		const FQuat Q = HK::Slerp(FQuat(Out[3], Out[4], Out[5], Out[6]), FQuat(A[3], A[4], A[5], A[6]), K);
		Out[3] = Q.X; Out[4] = Q.Y; Out[5] = Q.Z; Out[6] = Q.W;
		for (int32 j = 7; j < A.Num(); j++) Out[j] += (A[j] - Out[j]) * K;
	}
	else
	{
		for (int32 j = 0; j < A.Num(); j++) Out[j] = j >= 3 && j <= 5 ? HK::AngLerp(Out[j], A[j], K) : Out[j] + (A[j] - Out[j]) * K;
	}
	V->NetSet(Out);
	V->NetAnimate(Dt);
}

void FHKMultiplayer::Update(double Dt)
{
	EndFrame();
	FHKNet& Net = *NetPtr;
	// Send our own state ~10 Hz (only when somebody else is in the lobby).
	SendT -= Dt; VehT -= Dt;
	if (Net.Online() && Net.Others() > 0)
	{
		if (SendT <= 0 && Ctx.LocalState)
		{
			SendT = .1;
			LastLocal = Ctx.LocalState();
			TSharedPtr<FJsonObject> M = Msg(TEXT("st"));
			M->SetObjectField(TEXT("s"), LastLocal.ToJson());
			Net.Send(M);
		}
		const int32 Di = Ctx.DrivingIndex ? Ctx.DrivingIndex() : -1;
		FHKVehicle* Dv = VehicleAt(Di);
		if (Di >= 0 && Dv && VehT <= 0)
		{
			VehT = 1.0 / 12;
			TSharedPtr<FJsonObject> M = Msg(TEXT("vs"));
			M->SetNumberField(TEXT("v"), Di);
			M->SetField(TEXT("s"), NumArray(Dv->NetGet()));
			Net.Send(M);
		}
	}
	// Vehicles driven by others.
	for (int32 n = 0; n < VehTargets.Num();)
	{
		const int32 I = VehTargets[n].Key;
		FHKVehicle* V = VehicleAt(I);
		if (!V || !RemoteDriven(I))
		{
			VehTargets.RemoveAt(n);
			if (V) { V->NetCur.Reset(); V->bHasNetCur = false; }
			continue;
		}
		SmoothVehicle(I, V, VehTargets[n].Value, Dt);
		n++;
	}
	// Avatars.
	const FVector Cam = Ctx.CameraPosition ? Ctx.CameraPosition() : FVector::ZeroVector;
	for (TUniquePtr<FHKRemote>& Rp : RemotesList)
	{
		FHKRemote& R = *Rp;
		if (!R.bHasSt) { R.Av->Group()->bVisible = false; continue; }
		const FHKPlayerState& St = R.St;
		FHKNode* G = R.Av->Group();
		G->bVisible = true;
		FString Stance = St.St.IsEmpty() ? FString(TEXT("stand")) : St.St;
		FHKHipAnchor Anchor;
		if (FHKVehicle* V = St.V >= 0 ? VehicleAt(St.V) : nullptr) Anchor = V->HipAnchor(St.S);
		if (Anchor.bValid && Anchor.Obj)
		{
			Anchor.Obj->UpdateWorldMatrix(true, false);
			G->Position = Anchor.Obj->LocalToWorld(Anchor.Pos);
			G->SetQuat(Anchor.Obj->GetWorldQuaternion());
			Stance = Anchor.Stance;
			R.Pos = G->Position;
			R.Speed = 0;
		}
		else
		{
			const FVector Target = St.P;
			if (!R.bSeen || FVector::Distance(R.Pos, Target) > 12) R.Pos = Target;
			const FVector Before = R.Pos;
			const double K = 1 - FMath::Exp(-Dt * 10);
			R.Pos.X += (Target.X - R.Pos.X) * K; R.Pos.Y += (Target.Y - R.Pos.Y) * K; R.Pos.Z += (Target.Z - R.Pos.Z) * K;
			R.Yaw = HK::AngLerp(R.Yaw, St.Y, K);
			R.Speed += ((Dt > 0 ? FVector::Distance(Before, R.Pos) / Dt : 0) - R.Speed) * FMath::Min(1.0, Dt * 6);
			G->Position = R.Pos;
			G->SetRotation(0, R.Yaw, 0);
			if (St.Sit) Stance = TEXT("sit");
		}
		R.bSeen = true;
		FHKPoseArgs Args;
		Args.Stance = Stance;
		Args.Speed = Stance == TEXT("stand") || Stance == TEXT("crouch") || Stance == TEXT("prone") ? R.Speed : 0;
		Args.Dt = Dt;
		Args.bBeer = St.B != 0;
		Args.bMarker = Ctx.HoldsMarker && Ctx.HoldsMarker(R.Id);
		R.Av->Pose(Args);
		R.Av->Update(Dt, Cam);
		// Their drone.
		if (St.bDr)
		{
			if (!R.Drone) { R.Drone = HKAvatars::Drone(); if (Ctx.AddToScene) Ctx.AddToScene(*R.Drone, TEXT("avatar_drone")); }
			R.Drone->Root->bVisible = true;
			R.Drone->Root->SetPos(St.Dr[0], St.Dr[1], St.Dr[2]);
			R.Drone->Root->SetQuat(FQuat(St.Dr[3], St.Dr[4], St.Dr[5], St.Dr[6]));
			R.Drone->Root->UpdateMatrixWorld();
		}
		else if (R.Drone) R.Drone->Root->bVisible = false;
	}
}

void FHKMultiplayer::DrawHud(UCanvas* C) const
{
	for (const TUniquePtr<FHKRemote>& R : RemotesList) if (R->Av) R->Av->DrawHud(C);
}
