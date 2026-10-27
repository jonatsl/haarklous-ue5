#include "HKViewmodel.h"
#include "HKData.h"
#include "Haarklous.h"
#include "Misc/Paths.h"

const FVector FHKViewmodel::HipP(.12, -.075, -.44);
const FVector FHKViewmodel::HipR(.02, .08, 0);
const FVector FHKViewmodel::AdsP(0, -.029, -.36);
const FVector FHKViewmodel::AdsR(0, 0, 0);

FHKViewmodel::FHKViewmodel()
{
	FString Error;
	bLoaded = Rig.LoadFromJson(FPaths::Combine(FHKData::DataDir(), TEXT("rigs"), TEXT("mission_viewmodel.json")), &Error);
	Gun = Rig.Find(TEXT("gun"));
	Slide = Rig.Find(TEXT("slide"));
	Flash = Rig.Find(TEXT("flash"));
	if (!Gun || !Slide || !Flash)
	{
		if (bLoaded) UE_LOG(LogHaarklous, Warning, TEXT("mission_viewmodel rig lacks gun/slide/flash; using bare nodes"));
		bLoaded = false;
		// case-game.js: gun = new Group(), slide = new Group(); gun.add(slide); flash cone at (0, 0, -.215), rotation.x = -PI / 2.
		if (!Gun) Gun = Rig.Create(TEXT("gun"), Rig.Root);
		if (!Slide) Slide = Rig.Create(TEXT("slide"), Gun);
		if (!Flash)
		{
			Flash = Rig.Create(TEXT("flash"), Gun);
			Flash->SetPos(0, 0, -.215);
			Flash->SetRotation(-HK::Pi / 2, 0, 0);
			Flash->bVisible = false;
		}
	}
	// The rig file stores quaternions; the code sets Euler angles (order XYZ), so keep the Euler form in sync.
	Flash->SetRotation(-HK::Pi / 2, 0, Flash->Rot().Z);
}

FVector FHKViewmodel::RootScale(double WorldFovYDeg, double Shrink)
{
	// A camera-space point (x, y, z) projects to (x / -z, y / -z) / tan(fov / 2): scaling x and y by
	// tan(world / 2) / tan(62 / 2) gives the same screen position through the world camera. A uniform scale toward
	// the eye keeps the projection.
	const double K = FMath::Tan(FMath::DegreesToRadians(WorldFovYDeg) / 2) / FMath::Tan(FMath::DegreesToRadians(FovY) / 2);
	return FVector(K * Shrink, K * Shrink, Shrink);
}

void FHKViewmodel::PlaceAtCamera(const FVector& CamPos, const FQuat& CamQuat, double WorldFovYDeg, bool bVisible, double Shrink)
{
	Rig.Root->Position = CamPos;
	Rig.Root->SetQuat(CamQuat);
	Rig.Root->Scale = RootScale(WorldFovYDeg, Shrink);
	Rig.Root->bVisible = bVisible;
}
