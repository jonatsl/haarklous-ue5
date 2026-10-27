// Automation tests for the online port: the C++ must answer exactly like the web's own game-net.js,
// game-multiplayer.js and game-avatars.js. Reference answers: tools/web-export/reference-net.js
// (Content/Data/tests/net.json). Run: tools\run-tests.ps1 -Filter Haarklous.Net
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "HKAvatars.h"
#include "HKData.h"
#include "HKMultiplayer.h"
#include "HKNet.h"
#include "HKVehicle.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	TSharedPtr<FJsonObject> LoadNetRef(FAutomationTestBase& Test)
	{
		FString Error;
		TSharedPtr<FJsonObject> J = HKJson::LoadFile(FHKData::DataDir() / TEXT("tests") / TEXT("net.json"), &Error);
		if (!J) Test.AddError(Error);
		return J;
	}

	// ---- Deep comparison of JSON values (numbers with a small tolerance) ----
	bool NearNum(double A, double B) { return FMath::Abs(A - B) <= 1e-9 + 1e-14 * FMath::Max(FMath::Abs(A), FMath::Abs(B)); }
	FString Show(const TSharedPtr<FJsonValue>& V) { const FString S = HKJsonWire::WriteValue(V); return S.Len() > 220 ? S.Left(220) + TEXT("...") : S; }
	bool Same(const TSharedPtr<FJsonValue>& A, const TSharedPtr<FJsonValue>& B, const FString& Path, FString& Why)
	{
		const bool bANull = !A.IsValid() || A->Type == EJson::Null || A->Type == EJson::None, bBNull = !B.IsValid() || B->Type == EJson::Null || B->Type == EJson::None;
		if (bANull || bBNull) { if (bANull == bBNull) return true; Why = FString::Printf(TEXT("%s: C++ %s, web %s"), *Path, *Show(A), *Show(B)); return false; }
		if (A->Type != B->Type) { Why = FString::Printf(TEXT("%s: C++ %s, web %s"), *Path, *Show(A), *Show(B)); return false; }
		switch (A->Type)
		{
		case EJson::Number: if (NearNum(A->AsNumber(), B->AsNumber())) return true; break;
		case EJson::String: if (A->AsString() == B->AsString()) return true; break;
		case EJson::Boolean: if (A->AsBool() == B->AsBool()) return true; break;
		case EJson::Array:
		{
			const TArray<TSharedPtr<FJsonValue>>& X = A->AsArray(); const TArray<TSharedPtr<FJsonValue>>& Y = B->AsArray();
			if (X.Num() != Y.Num()) break;
			for (int32 i = 0; i < X.Num(); i++) if (!Same(X[i], Y[i], FString::Printf(TEXT("%s[%d]"), *Path, i), Why)) return false;
			return true;
		}
		case EJson::Object:
		{
			const TSharedPtr<FJsonObject> X = A->AsObject(), Y = B->AsObject();
			for (const auto& KV : Y->Values) if (!Same(X->TryGetField(FString(*KV.Key)), KV.Value, Path + TEXT(".") + FString(*KV.Key), Why)) return false;
			for (const auto& KV : X->Values) if (!Y->HasField(FString(*KV.Key)) && !(KV.Value.IsValid() && (KV.Value->Type == EJson::Null || KV.Value->Type == EJson::None))) { Why = Path + TEXT(".") + FString(*KV.Key) + TEXT(": only in C++"); return false; }
			return true;
		}
		default: return true;
		}
		Why = FString::Printf(TEXT("%s: C++ %s, web %s"), *Path, *Show(A), *Show(B));
		return false;
	}

	TSharedPtr<FJsonValue> J(const FString& S) { return MakeShared<FJsonValueString>(S); }
	TSharedPtr<FJsonValue> J(const TCHAR* S) { return MakeShared<FJsonValueString>(S); }
	TSharedPtr<FJsonValue> J(double D) { return MakeShared<FJsonValueNumber>(D); }
	TSharedPtr<FJsonValue> J(int32 D) { return MakeShared<FJsonValueNumber>(D); }
	TSharedPtr<FJsonValue> J(bool B) { return MakeShared<FJsonValueBoolean>(B); }
	TSharedPtr<FJsonValue> J(const TArray<double>& A) { return HKJsonWire::NumArray(A); }
	TSharedPtr<FJsonValue> J(const FVector& V) { return HKJsonWire::NumArray({V.X, V.Y, V.Z}); }
	TSharedPtr<FJsonValue> J(const FQuat& Q) { return HKJsonWire::NumArray({Q.X, Q.Y, Q.Z, Q.W}); }
	TSharedPtr<FJsonValue> J(const TSharedPtr<FJsonObject>& O) { return O.IsValid() ? TSharedPtr<FJsonValue>(MakeShared<FJsonValueObject>(O)) : TSharedPtr<FJsonValue>(MakeShared<FJsonValueNull>()); }
	TSharedPtr<FJsonValue> J(const TSharedPtr<FJsonValue>& V) { return V.IsValid() ? V : TSharedPtr<FJsonValue>(MakeShared<FJsonValueNull>()); }

	struct FRec
	{
		TArray<TSharedPtr<FJsonValue>> Events;
		template <typename... TArgs> void Ev(const TCHAR* Kind, TArgs&&... Args)
		{
			TArray<TSharedPtr<FJsonValue>> A;
			A.Add(J(Kind));
			(A.Add(J(Args)), ...);
			Events.Add(MakeShared<FJsonValueArray>(A));
		}
	};

	TSharedPtr<FJsonObject> AvatarSnap(const FHKAvatar& Av)
	{
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>(), Nodes = MakeShared<FJsonObject>();
		const TPair<const TCHAR*, FHKNode*> Parts[] = {{TEXT("hips"), Av.Hips}, {TEXT("torso"), Av.Torso}, {TEXT("neck"), Av.Neck}, {TEXT("head"), Av.Head},
			{TEXT("L.arm.sh"), Av.L.Arm.Sh}, {TEXT("L.arm.el"), Av.L.Arm.El}, {TEXT("R.arm.sh"), Av.R.Arm.Sh}, {TEXT("R.arm.el"), Av.R.Arm.El},
			{TEXT("L.leg.hip"), Av.L.Leg.Hip}, {TEXT("L.leg.kn"), Av.L.Leg.Kn}, {TEXT("R.leg.hip"), Av.R.Leg.Hip}, {TEXT("R.leg.kn"), Av.R.Leg.Kn}, {TEXT("bottle"), Av.Bottle}};
		for (const auto& P : Parts) Nodes->SetField(P.Key, J(P.Value->Rot()));
		O->SetObjectField(TEXT("nodes"), Nodes);
		O->SetField(TEXT("hips"), J(Av.Hips->Position));
		O->SetBoolField(TEXT("bottle"), Av.Bottle->bVisible);
		O->SetNumberField(TEXT("tagY"), Av.TagY); O->SetNumberField(TEXT("bubbleY"), Av.BubbleY);
		O->SetBoolField(TEXT("tagVisible"), Av.bTagVisible); O->SetNumberField(TEXT("tagK"), Av.TagK);
		O->SetBoolField(TEXT("bubbleVisible"), Av.bBubbleVisible); O->SetNumberField(TEXT("bubbleK"), Av.BubbleK);
		return O;
	}

	// The reference session's fake vehicles (posed groups; netSet/netAnimate/reset are recorded).
	class FFakeVehicle : public FHKVehicle
	{
	public:
		FRec* Rec = nullptr;
		int32 Index = 0;
		FHKNode* SeatNode = nullptr;
		TArray<double> Get;
		struct FA { bool bValid = false; FVector Pos; FString Stance; } AnchorD, AnchorP;
		virtual void Update(double Dt, const FHKVehicleInput& In) override {}
		virtual FHKSeatPose Seat() override { return FHKSeatPose(); }
		virtual void Enter() override {}
		virtual FVector2D Exit() override { return FVector2D::ZeroVector; }
		virtual TArray<FHKObstacle> Footprint() const override { return {}; }
		virtual TArray<double> NetGet() const override { return Get; }
		virtual void NetSet(const TArray<double>& A) override { Rec->Ev(TEXT("netSet"), Index, A); }
		virtual void NetAnimate(double Dt) override { Rec->Ev(TEXT("netAnimate"), Index, Dt); }
		virtual void Reset() override { Rec->Ev(TEXT("reset"), Index); }
		virtual FHKHipAnchor HipAnchor(const FString& S) override
		{
			FHKHipAnchor H;
			const FA& A = S == TEXT("d") ? AnchorD : AnchorP;
			if ((S == TEXT("d") || S == TEXT("p")) && A.bValid) { H.bValid = true; H.Obj = SeatNode; H.Pos = A.Pos; H.Stance = A.Stance; }
			return H;
		}
	};

	class FStubVoice : public IHKVoice
	{
	public:
		FRec* Rec = nullptr;
		bool bMic = false;
		virtual void SetMic(bool bOn, TFunction<void(bool)> Done) override { bMic = bOn; Rec->Ev(TEXT("voiceMic"), bOn); if (Done) Done(true); }
		virtual bool Mic() const override { return bMic; }
		virtual void OnSignal(int32 From, const TSharedPtr<FJsonValue>& D) override { Rec->Ev(TEXT("rtcIn"), From, D); }
		virtual void PeerJoined(int32 Id) override { Rec->Ev(TEXT("peerJoined"), Id); }
		virtual void PeerLeft(int32 Id) override { Rec->Ev(TEXT("peerLeft"), Id); }
	};

	class FFakeSocket : public IHKSocket
	{
	public:
		FRec* Rec = nullptr;
		bool bOpen = false;
		virtual void Connect() override {}
		virtual void Send(const FString& Text) override { Rec->Ev(TEXT("send"), Text); }
		virtual void Close(int32 Code) override { bOpen = false; Rec->Ev(TEXT("wsclose"), Code); }
		virtual bool IsOpen() const override { return bOpen; }
	};

	FVector Vec3(const TArray<double>& A) { return FVector(A.IsValidIndex(0) ? A[0] : 0, A.IsValidIndex(1) ? A[1] : 0, A.IsValidIndex(2) ? A[2] : 0); }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHKNetCodesTest, "Haarklous.Net.Codes", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FHKNetCodesTest::RunTest(const FString& Parameters)
{
	const TSharedPtr<FJsonObject> R = LoadNetRef(*this);
	if (!R) return false;
	TestEqual(TEXT("alphabet"), FString(FHKNet::Alphabet), HKJson::Str(R, TEXT("alphabet")));
	int32 Bad = 0, N = 0;
	for (const TSharedPtr<FJsonValue>& V : HKJson::Arr(R, TEXT("codes")))
	{
		const TArray<TSharedPtr<FJsonValue>>& A = V->AsArray();
		N++;
		if (FHKNet::ValidCode(A[0]->AsString()) != A[1]->AsBool()) { Bad++; AddError(FString::Printf(TEXT("validCode('%s') should be %d"), *A[0]->AsString(), A[1]->AsBool())); }
	}
	const TArray<double> Backoff = HKJson::Nums(R, TEXT("backoff"));
	for (int32 i = 0; i < Backoff.Num(); i++) { N++; if (FHKNet::RetryDelayMs(i) != Backoff[i]) { Bad++; AddError(FString::Printf(TEXT("back-off %d: %f vs web %f"), i, FHKNet::RetryDelayMs(i), Backoff[i])); } }
	// Random codes are valid; the pause-menu code pad steps through the alphabet like codePadInput().
	for (int32 i = 0; i < 200; i++) { N++; if (!FHKNet::ValidCode(FHKNet::RandomCode([]() { return (double)FMath::FRand(); }))) Bad++; }
	FHKCodePad Pad;
	Pad.Chars = {'A', 'A', 'A', 'A'};
	Pad.Step(-1); Pad.Right(); Pad.Step(1); Pad.Right(); Pad.Right(); Pad.Right(); Pad.Left(); Pad.Step(1); Pad.Step(1);
	TestEqual(TEXT("code pad"), Pad.Code(), FString(TEXT("9BAC")));
	AddInfo(FString::Printf(TEXT("codes and back-off: %d / %d match the web version"), N - Bad, N));
	return Bad == 0;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHKNetJsonTest, "Haarklous.Net.Json", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FHKNetJsonTest::RunTest(const FString& Parameters)
{
	const TSharedPtr<FJsonObject> R = LoadNetRef(*this);
	if (!R) return false;
	int32 Bad = 0, N = 0;
	for (const TSharedPtr<FJsonValue>& V : HKJson::Arr(R, TEXT("numbers")))
	{
		const TArray<TSharedPtr<FJsonValue>>& A = V->AsArray();
		const FString Mine = HKJsonWire::Number(A[0]->AsNumber());
		N++;
		if (Mine != A[1]->AsString()) { Bad++; AddError(FString::Printf(TEXT("number: C++ %s, JSON.stringify %s"), *Mine, *A[1]->AsString())); }
	}
	for (const TSharedPtr<FJsonValue>& V : HKJson::Arr(R, TEXT("strings")))
	{
		const TArray<TSharedPtr<FJsonValue>>& A = V->AsArray();
		const FString Mine = HKJsonWire::Quote(A[0]->AsString());
		N++;
		if (Mine != A[1]->AsString()) { Bad++; AddError(FString::Printf(TEXT("string: C++ %s, JSON.stringify %s"), *Mine, *A[1]->AsString())); }
	}
	for (const TSharedPtr<FJsonValue>& V : HKJson::Arr(R, TEXT("messages")))
	{
		const TArray<TSharedPtr<FJsonValue>>& A = V->AsArray();
		const FString Mine = HKJsonWire::Write(A[0]->AsObject());
		N++;
		if (Mine != A[1]->AsString()) { Bad++; AddError(FString::Printf(TEXT("message: C++ %s, JSON.stringify %s"), *Mine, *A[1]->AsString())); }
		// and back
		const TSharedPtr<FJsonObject> Back = HKJsonWire::Parse(A[1]->AsString());
		FString Why;
		N++;
		if (!Back || !Same(MakeShared<FJsonValueObject>(Back), A[0], TEXT("parse"), Why)) { Bad++; AddError(Why); }
	}
	// Player state round trip (game-main.js localState() keys and order; Math.round(-10.005 * 100) / 100 is -10.01 in JS too).
	{
		FHKPlayerState S;
		S.P = FVector(FHKPlayerState::R(1.234567), FHKPlayerState::R(-10.005), FHKPlayerState::R(2.5)); S.Y = FHKPlayerState::R(-.7549); S.St = TEXT("crouch"); S.B = 1; S.V = 3; S.S = TEXT("p");
		N++;
		const FString W = HKJsonWire::Write(S.ToJson());
		if (W != TEXT("{\"p\":[1.23,-10.01,2.5],\"y\":-0.75,\"st\":\"crouch\",\"b\":1,\"v\":3,\"s\":\"p\"}")) { Bad++; AddError(TEXT("player state: ") + W); }
		FHKPlayerState Back;
		N++;
		if (!FHKPlayerState::FromJson(MakeShared<FJsonValueObject>(HKJsonWire::Parse(W)), Back) || Back.V != 3 || Back.S != TEXT("p") || Back.St != TEXT("crouch") || Back.B != 1 || !Back.P.Equals(S.P, 1e-12)) { Bad++; AddError(TEXT("player state parse")); }
	}
	AddInfo(FString::Printf(TEXT("wire JSON: %d / %d match the web version"), N - Bad, N));
	return Bad == 0;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHKNetAvatarPosesTest, "Haarklous.Net.AvatarPoses", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FHKNetAvatarPosesTest::RunTest(const FString& Parameters)
{
	const TSharedPtr<FJsonObject> R = LoadNetRef(*this);
	if (!R) return false;
	int32 Bad = 0, N = 0;
	for (const TSharedPtr<FJsonValue>& PV : HKJson::Arr(R, TEXT("poses")))
	{
		const TSharedPtr<FJsonObject> P = PV->AsObject();
		const FString Stance = HKJson::Str(P, TEXT("stance"));
		const bool bBeer = HKJson::Bool(P, TEXT("beer"));
		FHKAvatar Av(1, TEXT("P"));
		if (N == 0 && !Av.bFromRigFile) AddWarning(TEXT("avatar_1 rig file missing: the skeleton was built in code"));
		const TArray<TSharedPtr<FJsonValue>>& Args = HKJson::Arr(P, TEXT("args"));
		const TArray<TSharedPtr<FJsonValue>>& Seq = HKJson::Arr(P, TEXT("seq"));
		for (int32 i = 0; i < Args.Num() && i < Seq.Num(); i++)
		{
			const TArray<double> A = HKJson::Nums(Args[i]);
			FHKPoseArgs PA; PA.Stance = Stance; PA.Speed = A[0]; PA.Dt = A[1]; PA.bBeer = bBeer;
			Av.Pose(PA);
			FString Why;
			N++;
			if (!Same(MakeShared<FJsonValueObject>(AvatarSnap(Av)), Seq[i], FString::Printf(TEXT("%s%s[%d]"), *Stance, bBeer ? TEXT("+beer") : TEXT(""), i), Why)) { if (Bad < 8) AddError(Why); Bad++; }
		}
	}
	AddInfo(FString::Printf(TEXT("avatar poses: %d / %d match the web version"), N - Bad, N));
	return Bad == 0;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHKNetSessionTest, "Haarklous.Net.Session", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FHKNetSessionTest::RunTest(const FString& Parameters)
{
	const TSharedPtr<FJsonObject> Ref = LoadNetRef(*this);
	if (!Ref) return false;
	const TSharedPtr<FJsonObject> Session = HKJson::Obj(Ref, TEXT("session"));
	const TSharedPtr<FJsonObject> Init = HKJson::Obj(Session, TEXT("init"));
	FRec Rec;
	double DateNow = HKJson::Num(Init, TEXT("dateNow")), PerfNow = HKJson::Num(Init, TEXT("perfNow"));
	// Own lobby code and name as the web's localStorage had them.
	const FString StorePath = FPaths::ProjectSavedDir() / TEXT("Tests") / TEXT("LocalStorage-net-test.json");
	IFileManager::Get().Delete(*StorePath);
	FHKStore::SetPathForTests(StorePath);
	FHKStore::Set(FHKNet::CodeKey, HKJson::Str(Init, TEXT("code")));
	FHKStore::Set(FHKNet::NameKey, HKJson::Str(Init, TEXT("name")));
	// Fake vehicles.
	TArray<TUniquePtr<FFakeVehicle>> Vehicles;
	for (const TSharedPtr<FJsonValue>& DV : HKJson::Arr(Session, TEXT("vehicles")))
	{
		const TSharedPtr<FJsonObject> D = DV->AsObject();
		TUniquePtr<FFakeVehicle> V = MakeUnique<FFakeVehicle>();
		V->Rec = &Rec; V->Index = Vehicles.Num(); V->bAircraft = HKJson::Bool(D, TEXT("aircraft"));
		const TArray<double> Rot = HKJson::Nums(D, TEXT("rot")), SeatRot = HKJson::Nums(D, TEXT("seatRot"));
		V->Rig.Root->Position = Vec3(HKJson::Nums(D, TEXT("pos"))); V->Rig.Root->SetRotation(Rot[0], Rot[1], Rot[2]);
		V->SeatNode = V->Rig.Create(TEXT("seat"), V->Rig.Root);
		V->SeatNode->Position = Vec3(HKJson::Nums(D, TEXT("seatPos"))); V->SeatNode->SetRotation(SeatRot[0], SeatRot[1], SeatRot[2]);
		V->Rig.Root->UpdateMatrixWorld();
		V->Spawn = HKJson::Nums(D, TEXT("spawn")); V->Get = HKJson::Nums(D, TEXT("get"));
		const TSharedPtr<FJsonObject> Anchors = HKJson::Obj(D, TEXT("anchors"));
		for (const TCHAR* S : {TEXT("d"), TEXT("p")})
		{
			FFakeVehicle::FA& A = FString(S) == TEXT("d") ? V->AnchorD : V->AnchorP;
			const TSharedPtr<FJsonObject> AO = HKJson::Obj(Anchors, S);
			if (AO.IsValid()) { A.bValid = true; A.Pos = Vec3(HKJson::Nums(AO, TEXT("pos"))); A.Stance = HKJson::Str(AO, TEXT("stance")); }
		}
		Vehicles.Add(MoveTemp(V));
	}
	// Context like the web harness.
	FHKPlayerState Local;
	FHKPlayerState::FromJson(MakeShared<FJsonValueObject>(HKJsonWire::Parse(TEXT("{\"p\":[0,-10,0],\"y\":0,\"st\":\"stand\",\"b\":0}"))), Local);
	int32 Driving = -1, PlayerVehicle = -1;
	bool bPlayerDriving = false;
	FVector Camera = FVector::ZeroVector;
	TArray<TSharedPtr<FFakeSocket>> Sockets;
	FHKMultiplayer* MpPtr = nullptr;
	FHKMultiplayerContext Ctx;
	Ctx.NumVehicles = [&]() { return Vehicles.Num(); };
	Ctx.Vehicle = [&](int32 I) -> FHKVehicle* { return Vehicles.IsValidIndex(I) ? Vehicles[I].Get() : nullptr; };
	Ctx.LocalState = [&]() { return Local; };
	Ctx.DrivingIndex = [&]() { return Driving; };
	Ctx.CameraPosition = [&]() { return Camera; };
	Ctx.Notice = [&](const FString& T, double S) { Rec.Ev(TEXT("notice"), T, S); };
	Ctx.ApplyDoor = [&](const FString& Id, bool bOpen, bool bInstant) { Rec.Ev(TEXT("door"), Id, bOpen, bInstant); };
	Ctx.OnChat = [&](const FHKChatLine& L, bool bMine, bool bOld)
	{
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetNumberField(TEXT("id"), L.Id); O->SetNumberField(TEXT("slot"), L.Slot); O->SetStringField(TEXT("name"), L.Name); O->SetStringField(TEXT("text"), L.Text);
		O->SetNumberField(TEXT("at"), L.At); O->SetBoolField(TEXT("sys"), L.bSys);
		Rec.Ev(TEXT("chat"), O, bMine, bOld);
	};
	Ctx.OnStatus = [&]() { Rec.Ev(TEXT("status"), MpPtr ? MpPtr->Net().GetStatus() : FString()); };
	Ctx.OnRemoteWood = [&](const FHKRemote& R) { Rec.Ev(TEXT("wood"), R.Id); };
	Ctx.PlayerVehicleIndex = [&]() { return PlayerVehicle; };
	Ctx.PlayerDriving = [&]() { return bPlayerDriving; };
	FHKNetOptions O;
	O.bEnabled = true;
	O.Base = HKJson::Str(Init, TEXT("base"));
	O.DateNow = [&]() { return DateNow; };
	O.PerfNow = [&]() { return PerfNow; };
	O.Random = []() { return .5; };
	O.bOwnTicker = false;
	O.MakeSocket = [&](const FString& Url) -> TSharedPtr<IHKSocket>
	{
		TSharedPtr<FFakeSocket> S = MakeShared<FFakeSocket>();
		S->Rec = &Rec;
		Sockets.Add(S);
		Rec.Ev(TEXT("socket"), Url);
		return S;
	};
	TUniquePtr<FStubVoice> Voice = MakeUnique<FStubVoice>();
	Voice->Rec = &Rec;
	FHKMultiplayer Mp(Ctx, O, MoveTemp(Voice));
	MpPtr = &Mp;
	TestEqual(TEXT("own code"), Mp.Net().GetOwnCode(), HKJson::Str(Init, TEXT("ownCode")));
	TestEqual(TEXT("name"), Mp.Net().GetName(), HKJson::Str(Init, TEXT("startName")));

	auto Snap = [&]()
	{
		const FHKNet& N = Mp.Net();
		TSharedPtr<FJsonObject> S = MakeShared<FJsonObject>();
		S->SetStringField(TEXT("status"), N.GetStatus()); S->SetStringField(TEXT("code"), N.GetCode()); S->SetStringField(TEXT("own"), N.GetOwnCode());
		S->SetNumberField(TEXT("you"), N.You()); S->SetBoolField(TEXT("online"), N.Online()); S->SetNumberField(TEXT("others"), N.Others());
		S->SetStringField(TEXT("name"), N.GetName()); S->SetNumberField(TEXT("slot"), N.GetSlot()); S->SetNumberField(TEXT("fire"), Mp.FireLevel()); S->SetNumberField(TEXT("serverNow"), N.ServerNow());
		TArray<TSharedPtr<FJsonValue>> Occ, Names, Sit, Players, Remotes;
		for (int32 i = 0; i < 3; i++) Occ.Add(MakeShared<FJsonValueArray>(TArray<TSharedPtr<FJsonValue>>{J(Mp.Occupant(i, TEXT("d"))), J(Mp.Occupant(i, TEXT("p"))), J(Mp.RemoteDriven(i))}));
		for (int32 Id : {0, 1, 2, 3, 4, 99}) Names.Add(J(Mp.NameOf(Id)));
		for (int32 Id = 0; Id < 4; Id++) Sit.Add(J(Mp.SitTaken(Id)));
		for (const FHKNetPlayer& P : N.GetPlayers()) Players.Add(MakeShared<FJsonValueArray>(TArray<TSharedPtr<FJsonValue>>{J(P.Id), J(P.Slot), J(P.Name), J(P.bMic)}));
		for (const TUniquePtr<FHKRemote>& R : Mp.Remotes())
		{
			TSharedPtr<FJsonObject> RO = AvatarSnap(*R->Av);
			RO->SetNumberField(TEXT("id"), R->Id); RO->SetNumberField(TEXT("slot"), R->Slot); RO->SetStringField(TEXT("name"), R->Name); RO->SetBoolField(TEXT("seen"), R->bSeen);
			RO->SetBoolField(TEXT("visible"), R->Av->Group()->bVisible); RO->SetField(TEXT("pos"), J(R->Av->Group()->Position)); RO->SetField(TEXT("quat"), J(R->Av->Group()->Quat()));
			RO->SetField(TEXT("rpos"), J(R->Pos)); RO->SetNumberField(TEXT("yaw"), R->Yaw); RO->SetNumberField(TEXT("speed"), R->Speed);
			if (R->Drone)
			{
				TSharedPtr<FJsonObject> D = MakeShared<FJsonObject>();
				D->SetBoolField(TEXT("visible"), R->Drone->Root->bVisible); D->SetField(TEXT("pos"), J(R->Drone->Root->Position)); D->SetField(TEXT("quat"), J(R->Drone->Root->Quat()));
				RO->SetObjectField(TEXT("drone"), D);
			}
			else RO->SetField(TEXT("drone"), MakeShared<FJsonValueNull>());
			Remotes.Add(MakeShared<FJsonValueObject>(RO));
		}
		S->SetArrayField(TEXT("occ"), Occ); S->SetArrayField(TEXT("names"), Names); S->SetArrayField(TEXT("sit"), Sit); S->SetArrayField(TEXT("players"), Players);
		S->SetBoolField(TEXT("mic"), Mp.Mic()); S->SetArrayField(TEXT("remotes"), Remotes);
		return S;
	};

	const TArray<TSharedPtr<FJsonValue>>& Steps = HKJson::Arr(Session, TEXT("steps"));
	const TArray<TSharedPtr<FJsonValue>>& Results = HKJson::Arr(Session, TEXT("results"));
	int32 Bad = 0, Events = 0;
	for (int32 i = 0; i < Steps.Num() && i < Results.Num(); i++)
	{
		const TSharedPtr<FJsonObject> St = Steps[i]->AsObject();
		const FString Op = HKJson::Str(St, TEXT("op"));
		Rec.Events.Reset();
		TSharedPtr<FFakeSocket> Ws = Sockets.Num() ? Sockets.Last() : nullptr;
		if (Op == TEXT("begin")) { const FString Q = HKJson::Str(St, TEXT("lobby")).ToUpper(); if (!(!Q.IsEmpty() && Mp.Net().Connect(Q))) Mp.Net().Home(); }
		else if (Op == TEXT("open")) { Ws->bOpen = true; if (Ws->OnOpen) Ws->OnOpen(); }
		else if (Op == TEXT("msg")) { if (Ws->OnMessage) Ws->OnMessage(HKJsonWire::Write(HKJson::Obj(St, TEXT("m")))); }
		else if (Op == TEXT("raw")) { if (Ws->OnMessage) Ws->OnMessage(HKJson::Str(St, TEXT("text"))); }
		else if (Op == TEXT("close")) { Ws->bOpen = false; if (Ws->OnClose) Ws->OnClose((int32)HKJson::Num(St, TEXT("code"))); }
		else if (Op == TEXT("time")) { const double Ms = HKJson::Num(St, TEXT("ms")); DateNow += Ms; PerfNow += Ms; Mp.Net().Tick(); }
		else if (Op == TEXT("update")) Mp.Update(HKJson::Num(St, TEXT("dt")));
		else if (Op == TEXT("local")) FHKPlayerState::FromJson(St->TryGetField(TEXT("s")), Local);
		else if (Op == TEXT("driving")) Driving = (int32)HKJson::Num(St, TEXT("i"));
		else if (Op == TEXT("camera")) Camera = Vec3(HKJson::Nums(St, TEXT("p")));
		else if (Op == TEXT("player")) { PlayerVehicle = (int32)HKJson::Num(St, TEXT("i")); bPlayerDriving = HKJson::Bool(St, TEXT("driving")); }
		else if (Op == TEXT("claim"))
		{
			const int32 I = (int32)HKJson::Num(St, TEXT("i"));
			Mp.Claim(I, HKJson::Str(St, TEXT("seat")), [&Rec, I](const FHKClaimResult& Res)
			{
				TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
				O->SetBoolField(TEXT("ok"), Res.bOk); O->SetNumberField(TEXT("by"), Res.By); O->SetBoolField(TEXT("timeout"), Res.bTimeout); O->SetBoolField(TEXT("offline"), Res.bOffline);
				Rec.Ev(TEXT("claimResult"), I, O);
			});
		}
		else if (Op == TEXT("release"))
		{
			const TArray<double> S = HKJson::Nums(St, TEXT("s"));
			Mp.Release((int32)HKJson::Num(St, TEXT("i")), HKJson::Str(St, TEXT("seat")), St->HasField(TEXT("s")) ? &S : nullptr);
		}
		else if (Op == TEXT("chat")) Rec.Ev(TEXT("chatReturn"), Mp.SendChat(HKJson::Str(St, TEXT("text"))));
		else if (Op == TEXT("door")) Mp.SendDoor(HKJson::Str(St, TEXT("id")), HKJson::Bool(St, TEXT("open")));
		else if (Op == TEXT("wood")) Mp.AddWood();
		else if (Op == TEXT("mic")) Mp.SetMic(HKJson::Bool(St, TEXT("on")), [&Rec](bool bOk) { Rec.Ev(TEXT("micReturn"), bOk); });
		else if (Op == TEXT("name")) Mp.Net().SetName(HKJson::Str(St, TEXT("n")));
		else if (Op == TEXT("connect")) Rec.Ev(TEXT("connectReturn"), Mp.Net().Connect(HKJson::Str(St, TEXT("code"))));
		else if (Op == TEXT("home")) Mp.Net().Home();
		else if (Op == TEXT("rtcOut")) Mp.Voice().SendSignal((int32)HKJson::Num(St, TEXT("to")), St->TryGetField(TEXT("d")));
		Mp.EndFrame();   // the web harness awaits the microtasks after every step
		const TSharedPtr<FJsonObject> Want = Results[i]->AsObject();
		FString Why;
		const bool bEv = Same(MakeShared<FJsonValueArray>(Rec.Events), Want->TryGetField(TEXT("events")), TEXT("events"), Why);
		const bool bSn = bEv && Same(MakeShared<FJsonValueObject>(Snap()), Want->TryGetField(TEXT("snap")), TEXT("snap"), Why);
		Events += Rec.Events.Num();
		if (!bEv || !bSn) { if (Bad < 10) AddError(FString::Printf(TEXT("step %d (%s): %s"), i, *Op, *Why)); Bad++; }
	}
	MpPtr = nullptr;
	FHKStore::SetPathForTests(FString());
	IFileManager::Get().Delete(*StorePath);
	AddInfo(FString::Printf(TEXT("lobby session: %d / %d steps (%d events) match the web version"), Steps.Num() - Bad, Steps.Num(), Events));
	return Bad == 0;
}

#endif
