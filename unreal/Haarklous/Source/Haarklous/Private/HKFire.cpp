// Port of reference/web-game/game-fire.js (firepit, benches, woodpile, crate, thrown logs, flame/ember/smoke state) and
// AHKFireEffects, the Unreal side of what the web draws with a shader material and points.
#include "HKFire.h"
#include "HKData.h"
#include "Haarklous.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Math/RandomStream.h"
#include "Misc/App.h"
#include "TextureResource.h"
#include <cmath>

const double FHKFire::DECAY_S = 90;
const double FHKFire::MAX = 6;

FHKFireContext FHKFireContext::FromGame()
{
	FHKFireContext C;
	C.HeightAt = [](double X, double Z) { return FHKData::Get().HeightWorld(X, Z); };
	TSharedRef<FRandomStream> Stream = MakeShared<FRandomStream>((int32)((FPlatformTime::Cycles64() >> 3) & 0x7fffffff));
	C.Random = [Stream]() { return (double)Stream->GetFraction(); };
	C.RigDir = FHKData::DataDir() / TEXT("rigs");
	return C;
}

FHKObstacle FHKFire::Rect(double Cx, double Cz, double AlongA, double Len, double Wid, double MinY, double MaxY) const
{
	const double C = std::cos(AlongA), S = std::sin(AlongA);
	const double Pts[4][2] = {{Len / 2, Wid / 2}, {Len / 2, -Wid / 2}, {-Len / 2, -Wid / 2}, {-Len / 2, Wid / 2}};
	FHKObstacle O;
	for (const auto& P : Pts)
	{
		const double A = P[0], B = P[1];
		FHKPt Pt;
		Pt.X = X + (Cx + C * A - S * B);
		Pt.Z = Z + (Cz + S * A + C * B);
		O.Points.Add(Pt);
	}
	O.MinY = Y0 + MinY;
	O.MaxY = Y0 + MaxY;
	O.Kind = TEXT("firepit");
	O.Prepare();
	return O;
}

FHKFire::FHKFire(double InX, double InZ, const FHKFireContext& InCtx) : X(InX), Z(InZ), Ctx(InCtx)
{
	Y0 = Ctx.HeightAt(X, Z);
	FString Error;
	if (!Rig.LoadFromJson(Ctx.RigDir / TEXT("fire.json"), &Error)) UE_LOG(LogHaarklous, Warning, TEXT("Fire rig: %s"), *Error);
	Rig.Root->SetPos(X, Y0, Z);
	// Stone ring (its geometry is in the rig): 15 stones take four numbers each from the module's generator.
	for (int32 i = 0; i < 15; i++) { Rnd.Next(); Rnd.Next(); Rnd.Next(); Rnd.Next(); }
	Charred = Rig.Find(TEXT("charred"));
	// Flames: camera-facing cards with a noise shader; embers; smoke.
	for (int32 i = 0; i < 6; i++)
	{
		FFlame& F = Flames[i];
		F.Time = i * 3.1;
		F.K = .7 + Rnd.Next() * .5;
		F.Off[0] = (Rnd.Next() - .5) * .35;
		F.Off[1] = (Rnd.Next() - .5) * .35;
	}
	EmberPos.Init(0.f, EN * 3);
	Embers.SetNum(EN);
	for (int32 i = 0; i < EN; i++) Embers[i].Life = Rnd.Next() * 2;
	SmokePos.Init(0.f, SN * 3);
	SmokeLife.SetNum(SN);
	for (int32 i = 0; i < SN; i++) SmokeLife[i] = Rnd.Next() * 6;
	// Benches (five, facing the fire) with two seats each.
	double BenchAngles[5];
	for (int32 i = 0; i < 5; i++) BenchAngles[i] = i / 5.0 * HK::Pi * 2 + .3;
	for (const double A : BenchAngles)
	{
		const double R = 2.75, Bx = std::cos(A) * R, Bz = std::sin(A) * R;
		for (const double Off : {-.42, .42})
		{
			const double AlongB = -A + HK::Pi / 2, Px = Bx + std::cos(AlongB) * Off * 1, Pz = Bz - std::sin(AlongB) * Off;
			FHKFireSpot S;
			S.Id = Spots.Num();
			S.X = X + Px;
			S.Y = Y0 + .47;
			S.Z = Z + Pz;
			S.Yaw = std::atan2(Bx, Bz);
			Spots.Add(S);
		}
		Obstacles.Add(Rect(Bx, Bz, A - HK::Pi / 2, 1.75, .42, 0, .5));
	}
	Obstacles.Add(Rect(0, 0, 0, 1.9, 1.9, 0, .6));
	// Woodpile and beer crate between two benches, outside the ring.
	const double Wa = (BenchAngles[0] + BenchAngles[1]) / 2, Wx = std::cos(Wa) * 4.1, Wz = std::sin(Wa) * 4.1;
	Obstacles.Add(Rect(Wx, Wz, Wa, 1.0, .9, 0, .5));
	const double Ca = (BenchAngles[2] + BenchAngles[3]) / 2, Cx = std::cos(Ca) * 3.9, Cz = std::sin(Ca) * 3.9;
	Obstacles.Add(Rect(Cx, Cz, Ca, .5, .4, 0, .5));
	Center = FVector(X, Y0, Z);
	Woodpile = FVector2D(X + Wx, Z + Wz);
	Crate = FVector2D(X + Cx, Z + Cz);
	// Thrown logs: a few nodes made from the rig's "log" template (the web makes a new mesh per throw).
	const FHKNode* Template = Rig.Find(TEXT("log"));
	for (int32 i = 0; i < LogPool; i++)
	{
		FHKNode* N = Rig.Create(FString::Printf(TEXT("log%d"), i), Rig.Root);
		if (Template) N->MeshKey = Template->MeshKey;
		N->bVisible = false;
		LogNodes.Add(N);
	}
	Rig.Root->UpdateMatrixWorld();
}

// A log flying into the fire.
void FHKFire::ThrowLog(const FVector& From, bool bHasY)
{
	FHKNode* Node = nullptr;
	for (FHKNode* N : LogNodes) if (!Flying.ContainsByPredicate([N](const FFlying& F) { return F.Node == N; })) { Node = N; break; }
	if (!Node) { Node = Flying[0].Node; Flying.RemoveAt(0); }
	Node->bVisible = true;
	Node->SetPos(0, 0, 0);
	Node->SetRotation(0, 0, 0);
	FFlying F;
	F.Node = Node;
	F.T = 0;
	F.Start = FVector(From.X - X, (bHasY ? From.Y : Y0 + 1.2) - Y0, From.Z - Z);
	const double Sx = Rnd.Next() * 6;
	const double Sy = Rnd.Next() * 6;
	const double Sz = Rnd.Next() * 6;
	F.Spin = FVector(Sx, Sy, Sz);
	Flying.Add(F);
}

void FHKFire::Update(double Dt, double Level, const FVector& CameraPos)
{
	Time += Dt;
	const double L = FMath::Max(0.0, FMath::Min(MAX, Level)), P = L <= 0 ? 0 : .25 + L / MAX * .95;
	Flick += ((.8 + Ctx.Random() * .4) - Flick) * FMath::Min(1.0, Dt * 12);
	LightIntensity = L <= 0 ? .4 : (3 + L * 4.2) * Flick;
	LightDistance = 9 + L * 3;
	CharredEmissiveIntensity = .6 + FMath::Min(1.0, L / 2) * 1.6;
	const double CamYaw = std::atan2(CameraPos.X - X, CameraPos.Z - Z);
	for (int32 i = 0; i < 6; i++)
	{
		FFlame& F = Flames[i];
		const double K = F.K * P;
		F.bVisible = K > .02;
		F.Scale = FVector(.65 * K + .2 * P, 1.2 * K + .3 * P, 1);
		F.Position = FVector(F.Off[0] * (1 + L * .08), .1 + F.Scale.Y * .78, F.Off[1] * (1 + L * .08));
		F.RotY = CamYaw;
		F.Time += Dt * (1 + i * .07);
		F.Power = .5 + P * .7;
	}
	// Float32Array positions: every store rounds to float like the web's typed arrays.
	for (int32 i = 0; i < EN; i++)
	{
		FEmber& E = Embers[i];
		E.Life -= Dt;
		if (E.Life <= 0)
		{
			E.Life = .8 + Ctx.Random() * 1.8;
			EmberPos[i * 3] = (float)((Ctx.Random() - .5) * .5);
			EmberPos[i * 3 + 1] = (float).3;
			EmberPos[i * 3 + 2] = (float)((Ctx.Random() - .5) * .5);
			const double V0 = (Ctx.Random() - .5) * .4;
			const double V1 = 1 + Ctx.Random() * 1.5 * (.4 + P);
			const double V2 = (Ctx.Random() - .5) * .4;
			E.V[0] = V0; E.V[1] = V1; E.V[2] = V2;
		}
		EmberPos[i * 3] = (float)((double)EmberPos[i * 3] + E.V[0] * Dt);
		EmberPos[i * 3 + 1] = (float)((double)EmberPos[i * 3 + 1] + E.V[1] * Dt);
		EmberPos[i * 3 + 2] = (float)((double)EmberPos[i * 3 + 2] + E.V[2] * Dt);
	}
	bEmbersVisible = L > .1;
	EmberSize = .03 + P * .03;
	for (int32 i = 0; i < SN; i++)
	{
		double& S = SmokeLife[i];
		S -= Dt;
		if (S <= 0)
		{
			S = 4 + Ctx.Random() * 3;
			SmokePos[i * 3] = (float)((Ctx.Random() - .5) * .4);
			SmokePos[i * 3 + 1] = (float)(.6 + P);
			SmokePos[i * 3 + 2] = (float)((Ctx.Random() - .5) * .4);
		}
		SmokePos[i * 3] = (float)((double)SmokePos[i * 3] + (.25 + std::sin(Time * .3 + i) * .15) * Dt);
		SmokePos[i * 3 + 1] = (float)((double)SmokePos[i * 3 + 1] + (.55 + P * .5) * Dt);
		SmokePos[i * 3 + 2] = (float)((double)SmokePos[i * 3 + 2] + .12 * Dt);
	}
	SmokeOpacity = .12 + P * .22;
	for (int32 i = Flying.Num() - 1; i >= 0; i--)
	{
		FFlying& F = Flying[i];
		F.T += Dt / .7;
		const double T = FMath::Min(1.0, F.T);
		F.Node->SetPos(F.Start.X * (1 - T), F.Start.Y * (1 - T) + .35 * T + std::sin(T * HK::Pi) * 1.1, F.Start.Z * (1 - T));
		F.Node->SetRotation(F.Spin.X * T, F.Spin.Y * T, F.Spin.Z * T);
		if (T >= 1) { F.Node->bVisible = false; Flying.RemoveAt(i); }
	}
}

/* ======================= AHKFireEffects ======================= */

namespace
{
	constexpr int32 FlameW = 32;
	constexpr int32 FlameH = 52;
	// /Engine/BasicShapes/Plane: 100 x 100 cm in its XY plane, normal +Z. Moving along PlaneUp goes up the texture
	// (towards V = 0); checked by the test Haarklous.Fire.EffectsAssets.
	const FVector PlaneUp(0, -1, 0);

	double Fract(double X) { return X - FMath::FloorToDouble(X); }
	double Hash(double X, double Y) { return Fract(std::sin(X * 127.1 + Y * 311.7) * 43758.5453); }
	double Noise(double X, double Y)
	{
		const double Ix = FMath::FloorToDouble(X), Iy = FMath::FloorToDouble(Y);
		double Fx = X - Ix, Fy = Y - Iy;
		Fx = Fx * Fx * (3.0 - 2.0 * Fx);
		Fy = Fy * Fy * (3.0 - 2.0 * Fy);
		const double A = FMath::Lerp(Hash(Ix, Iy), Hash(Ix + 1, Iy), Fx), B = FMath::Lerp(Hash(Ix, Iy + 1), Hash(Ix + 1, Iy + 1), Fx);
		return FMath::Lerp(A, B, Fy);
	}
	double SmoothStep(double E0, double E1, double X)
	{
		const double T = FMath::Clamp((X - E0) / (E1 - E0), 0.0, 1.0);
		return T * T * (3.0 - 2.0 * T);
	}
	UTexture2D* MakeTexture(UObject* Outer, int32 W, int32 H, const TArray<FColor>& Pixels)
	{
		UTexture2D* T = UTexture2D::CreateTransient(W, H, PF_B8G8R8A8);
		if (!T) return nullptr;
		T->SRGB = true;
		T->Filter = TF_Bilinear;
		T->AddressX = TA_Clamp;
		T->AddressY = TA_Clamp;
		FTexturePlatformData* Data = T->GetPlatformData();
		if (Data && Data->Mips.Num() > 0)
		{
			void* Dst = Data->Mips[0].BulkData.Lock(LOCK_READ_WRITE);
			FMemory::Memcpy(Dst, Pixels.GetData(), W * H * sizeof(FColor));
			Data->Mips[0].BulkData.Unlock();
		}
		T->UpdateResource();
		return T;
	}
	void FindParams(UMaterialInterface* M, FName& Color, FName& Texture)
	{
		if (!M) return;
		TArray<FMaterialParameterInfo> Infos;
		TArray<FGuid> Ids;
		M->GetAllVectorParameterInfo(Infos, Ids);
		// Widget3DPassThrough has BackColor (back faces) and TintColorAndOpacity (front): prefer the tint.
		for (const FMaterialParameterInfo& I : Infos) if (Color.IsNone() || I.Name.ToString().Contains(TEXT("Tint"))) Color = I.Name;
		Infos.Reset();
		Ids.Reset();
		M->GetAllTextureParameterInfo(Infos, Ids);
		if (Infos.Num()) Texture = Infos[0].Name;
	}
	FQuat CardQuat(const FVector& NormalUE, const FVector& UpUE)
	{
		const FVector Up = PlaneUp.X != 0 ? UpUE * PlaneUp.X : UpUE * PlaneUp.Y;
		return (PlaneUp.X != 0 ? FRotationMatrix::MakeFromXZ(Up, NormalUE) : FRotationMatrix::MakeFromYZ(Up, NormalUE)).ToQuat();
	}
}

FVector AHKFireEffects::PlaneUpAxis() { return PlaneUp; }

FLinearColor AHKFireEffects::FlameShader(double U, double V, double Time)
{
	const double Nz = Noise(U * 4.0, V * 3.0 - Time * 2.6) * .6 + Noise(U * 9.0, V * 7.0 - Time * 4.1) * .4;
	const double Wd = (1.0 - V) * .5 + .08;
	double Shape = SmoothStep(Wd, Wd * .35, FMath::Abs(U - .5 + (Nz - .5) * .25 * V));
	Shape *= SmoothStep(1.0, .25, V + Nz * .45);
	Shape *= SmoothStep(0.0, .08, V);
	FLinearColor Col = FMath::Lerp(FLinearColor(1.0f, .92f, .55f), FLinearColor(1.0f, .42f, .08f), (float)SmoothStep(.05, .6, V));
	Col = FMath::Lerp(Col, FLinearColor(.8f, .15f, .02f), (float)SmoothStep(.55, 1.0, V));
	return FLinearColor(Col.R * (float)Shape, Col.G * (float)Shape, Col.B * (float)Shape, (float)Shape);
}

AHKFireEffects::AHKFireEffects()
{
	PrimaryActorTick.bCanEverTick = false;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent->SetMobility(EComponentMobility::Movable);
}

UStaticMeshComponent* AHKFireEffects::MakeMesh(UStaticMesh* Mesh, UMaterialInterface* Mat, const TCHAR* Name)
{
	UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this, MakeUniqueObjectName(this, UStaticMeshComponent::StaticClass(), FName(Name)));
	C->SetStaticMesh(Mesh);
	C->SetMobility(EComponentMobility::Movable);
	C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	C->SetGenerateOverlapEvents(false);
	C->SetCastShadow(false);
	C->bReceivesDecals = false;
	if (Mat) C->SetMaterial(0, Mat);
	C->SetupAttachment(RootComponent);
	C->RegisterComponent();
	return C;
}

void AHKFireEffects::Init(UStaticMeshComponent* InCharred)
{
	if (!bBuilt) Build();
	if (InCharred && InCharred->GetNumMaterials() > 0)
	{
		CharredMid = InCharred->CreateDynamicMaterialInstance(0);
		for (int32 s = 1; s < InCharred->GetNumMaterials(); s++) InCharred->SetMaterial(s, CharredMid);
	}
}

void AHKFireEffects::Build()
{
	bBuilt = true;
	UStaticMesh* Plane = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Plane.Plane"));
	UStaticMesh* Sphere = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	UMaterialInterface* Additive = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/EngineMaterials/EmissiveMeshMaterial.EmissiveMeshMaterial"));
	UMaterialInterface* Translucent = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/EngineMaterials/Widget3DPassThrough_Translucent.Widget3DPassThrough_Translucent"));
	if (!Plane || !Sphere || !Additive || !Translucent) UE_LOG(LogHaarklous, Warning, TEXT("Fire effects: engine assets missing (plane %d sphere %d additive %d translucent %d)"), Plane != nullptr, Sphere != nullptr, Additive != nullptr, Translucent != nullptr);
	FindParams(Additive, AdditiveColorParam, AdditiveTextureParam);
	FindParams(Translucent, TranslucentColorParam, TranslucentTextureParam);
	UE_LOG(LogHaarklous, Log, TEXT("Fire effects: additive %s/%s, translucent %s/%s"), *AdditiveColorParam.ToString(), *AdditiveTextureParam.ToString(), *TranslucentColorParam.ToString(), *TranslucentTextureParam.ToString());

	Light = NewObject<UPointLightComponent>(this, TEXT("FireLight"));
	Light->SetMobility(EComponentMobility::Movable);
	Light->SetCastShadows(false);
	Light->SetUseInverseSquaredFalloff(true);
	Light->SetIntensityUnits(ELightUnits::Candelas);
	Light->SetLightColor(FLinearColor::FromSRGBColor(FColor(0xff, 0x8a, 0x3a)));
	Light->SetIntensity(0);
	Light->SetAttenuationRadius(1600);
	Light->SetRelativeLocation(HK::ToUE(FVector(0, .9, 0)));
	Light->SetupAttachment(RootComponent);
	Light->RegisterComponent();

	// Textures: white, the smoke puff (the web's radial gradient canvas) and one animated texture per flame.
	{
		TArray<FColor> White;
		White.Init(FColor::White, 16);
		WhiteTexture = MakeTexture(this, 4, 4, White);
		TArray<FColor> Puff;
		Puff.SetNum(64 * 64);
		for (int32 y = 0; y < 64; y++)
			for (int32 x = 0; x < 64; x++)
			{
				const double D = HK::Hypot(x + .5 - 32, y + .5 - 32), A = D <= 2 ? .55 : D >= 32 ? 0 : .55 * (1 - (D - 2) / 30);
				Puff[y * 64 + x] = FColor(255, 255, 255, (uint8)FMath::RoundToInt(A * 255));
			}
		SmokeTexture = MakeTexture(this, 64, 64, Puff);
	}
	for (int32 i = 0; i < 6; i++)
	{
		TArray<FColor> Px;
		Px.Init(FColor::Black, FlameW * FlameH);
		FlameTextures.Add(MakeTexture(this, FlameW, FlameH, Px));
		UMaterialInstanceDynamic* Mid = Additive ? UMaterialInstanceDynamic::Create(Additive, this) : nullptr;
		if (Mid && !AdditiveTextureParam.IsNone()) Mid->SetTextureParameterValue(AdditiveTextureParam, FlameTextures.Last());
		FlameMids.Add(Mid);
		FlameCards.Add(MakeMesh(Plane, Mid, TEXT("Flame")));
		PaintFlame(i, i * 3.1);
	}
	EmberMid = Additive ? UMaterialInstanceDynamic::Create(Additive, this) : nullptr;
	if (EmberMid)
	{
		if (!AdditiveTextureParam.IsNone()) EmberMid->SetTextureParameterValue(AdditiveTextureParam, WhiteTexture);
		// PointsMaterial({color: 0xffa040, opacity: .9, blending: Additive}): adds colour * opacity.
		if (!AdditiveColorParam.IsNone()) EmberMid->SetVectorParameterValue(AdditiveColorParam, FLinearColor::FromSRGBColor(FColor(0xff, 0xa0, 0x40)) * .9f);
	}
	for (int32 i = 0; i < FHKFire::EN; i++) EmberMeshes.Add(MakeMesh(Sphere, EmberMid, TEXT("Ember")));
	SmokeMid = Translucent ? UMaterialInstanceDynamic::Create(Translucent, this) : nullptr;
	if (SmokeMid)
	{
		if (!TranslucentTextureParam.IsNone()) SmokeMid->SetTextureParameterValue(TranslucentTextureParam, SmokeTexture);
		SmokeMid->SetScalarParameterValue(TEXT("OpacityFromTexture"), 1.f);
	}
	for (int32 i = 0; i < FHKFire::SN; i++) SmokeMeshes.Add(MakeMesh(Plane, SmokeMid, TEXT("Smoke")));
}

void AHKFireEffects::PaintFlame(int32 Index, double Time)
{
	UTexture2D* T = FlameTextures.IsValidIndex(Index) ? FlameTextures[Index].Get() : nullptr;
	if (!T || !FApp::CanEverRender() || !T->GetResource()) return;
	FColor* Px = new FColor[FlameW * FlameH];
	for (int32 r = 0; r < FlameH; r++)
	{
		const double V = 1.0 - (r + .5) / FlameH;   // row 0 = top of the flame (uv.y = 1)
		for (int32 c = 0; c < FlameW; c++)
		{
			const FLinearColor S = FlameShader((c + .5) / FlameW, V, Time);
			// Additive blending of vec4(col * shape * power * 1.6, shape * power): adds col * shape^2 * power^2 * 1.6.
			// The texture holds col * shape^2, the material colour power^2 * 1.6.
			Px[r * FlameW + c] = FLinearColor(S.R * S.A, S.G * S.A, S.B * S.A, S.A).ToFColor(false);
		}
	}
	FUpdateTextureRegion2D* Region = new FUpdateTextureRegion2D(0, 0, 0, 0, FlameW, FlameH);
	T->UpdateTextureRegions(0, 1, Region, FlameW * sizeof(FColor), sizeof(FColor), (uint8*)Px, [](uint8* Data, const FUpdateTextureRegion2D* Regions) { delete[] (FColor*)Data; delete Regions; });
}

void AHKFireEffects::Sync(const FHKFire& Fire, const FVector& CameraWeb)
{
	if (!bBuilt) Build();
	const FVector Origin = HK::ToUE(Fire.Center);
	if (!GetActorLocation().Equals(Origin, .1)) SetActorLocation(Origin);
	const FVector Cam = HK::ToUE(CameraWeb);
	const bool bNear = HK::Hypot(CameraWeb.X - Fire.X, CameraWeb.Y - Fire.Y0, CameraWeb.Z - Fire.Z) < DetailDistance;
	// Light: PointLight(0xff8a3a, intensity, distance, decay 1.6) at (0, .9, 0).
	if (Light)
	{
		Light->SetIntensity(Fire.LightIntensity * LightScale);
		Light->SetAttenuationRadius(Fire.LightDistance * 100);
	}
	// Flames: cards PlaneGeometry(1, 1.6) scaled (sx, sy), turned to the camera's yaw.
	const bool bRepaint = bNear && (LastTime < 0 || Fire.Time - TextureClock >= 1.0 / 20 || Fire.Time < TextureClock);
	if (bRepaint) TextureClock = Fire.Time;
	LastTime = Fire.Time;
	for (int32 i = 0; i < 6 && i < FlameCards.Num(); i++)
	{
		const FHKFire::FFlame& F = Fire.Flames[i];
		UStaticMeshComponent* C = FlameCards[i];
		if (!C) continue;
		C->SetVisibility(F.bVisible);
		if (!F.bVisible) continue;
		const FVector Normal = HK::DirToUE(FVector(std::sin(F.RotY), 0, std::cos(F.RotY)));
		const FQuat Q = CardQuat(Normal, FVector(0, 0, 1));
		const double Width = F.Scale.X * 1.0, Height = F.Scale.Y * 1.6;
		const FVector S = PlaneUp.X != 0 ? FVector(Height, Width, 1) : FVector(Width, Height, 1);
		C->SetRelativeTransform(FTransform(Q, HK::ToUE(F.Position), S));
		if (UMaterialInstanceDynamic* Mid = FlameMids.IsValidIndex(i) ? FlameMids[i].Get() : nullptr)
			if (!AdditiveColorParam.IsNone()) { const float G = (float)(F.Power * F.Power * 1.6 * FlameGain); Mid->SetVectorParameterValue(AdditiveColorParam, FLinearColor(G, G, G, 1)); }
		if (bRepaint) PaintFlame(i, F.Time);
	}
	// Embers: Points size .03-.06 with size attenuation = about size * tan(fov / 2) metres at the 70 degree view.
	const double EmberWorld = Fire.EmberSize * std::tan(FMath::DegreesToRadians(35.0));
	for (int32 i = 0; i < EmberMeshes.Num(); i++)
	{
		UStaticMeshComponent* C = EmberMeshes[i];
		const bool bVis = Fire.bEmbersVisible && bNear;
		C->SetVisibility(bVis);
		if (!bVis) continue;
		const FVector P(Fire.EmberPos[i * 3], Fire.EmberPos[i * 3 + 1], Fire.EmberPos[i * 3 + 2]);
		C->SetRelativeTransform(FTransform(FQuat::Identity, HK::ToUE(P), FVector(EmberWorld)));
	}
	// Smoke: 1.6 size points -> camera-facing puffs of about 1.1 m.
	const double SmokeWorld = 1.6 * std::tan(FMath::DegreesToRadians(35.0));
	if (SmokeMid && !TranslucentColorParam.IsNone())
	{
		const FLinearColor Grey = FLinearColor::FromSRGBColor(FColor(0x9a, 0x9a, 0x9a));
		const FLinearColor Tint(Grey.R, Grey.G, Grey.B, (float)Fire.SmokeOpacity);
		SmokeMid->SetVectorParameterValue(TranslucentColorParam, Tint);
		SmokeMid->SetVectorParameterValue(TEXT("BackColor"), Tint);   // the same puff seen from behind
	}
	for (int32 i = 0; i < SmokeMeshes.Num(); i++)
	{
		UStaticMeshComponent* C = SmokeMeshes[i];
		C->SetVisibility(bNear);
		if (!bNear) continue;
		const FVector Local = HK::ToUE(FVector(Fire.SmokePos[i * 3], Fire.SmokePos[i * 3 + 1], Fire.SmokePos[i * 3 + 2]));
		const FVector ToCam = (Cam - (Origin + Local)).GetSafeNormal();
		C->SetRelativeTransform(FTransform(CardQuat(ToCam.IsNearlyZero() ? FVector(1, 0, 0) : ToCam, FMath::Abs(ToCam.Z) > .99 ? FVector(1, 0, 0) : FVector(0, 0, 1)), Local, FVector(SmokeWorld)));
	}
	if (CharredMid) CharredMid->SetScalarParameterValue(TEXT("EmissiveIntensity"), (float)Fire.CharredEmissiveIntensity);
}
