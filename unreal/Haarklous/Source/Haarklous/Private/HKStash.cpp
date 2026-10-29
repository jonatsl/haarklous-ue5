// Port of reference/web-game/game-stash.js (the hidden stashes). Keep it line by line with the JavaScript.
#include "HKStash.h"
#include "HKData.h"
#include "Haarklous.h"

FHKStash::FHKStash(const TSharedPtr<FJsonObject>& J, const FString& RigPath)
{
	FString Error;
	if (!Rig.LoadFromJson(RigPath, &Error)) UE_LOG(LogHaarklous, Warning, TEXT("Stash rig: %s"), *Error);
	for (const TSharedPtr<FJsonValue>& V : HKJson::Arr(J, TEXT("places")))
	{
		const TSharedPtr<FJsonObject> O = V->AsObject();
		Places.Add({HKJson::Str(O, TEXT("name")), HKJson::Num(O, TEXT("x")), HKJson::Num(O, TEXT("y")), HKJson::Num(O, TEXT("z")), HKJson::Num(O, TEXT("yaw"))});
	}
	const TArray<TSharedPtr<FJsonValue>>& List = HKJson::Arr(J, TEXT("items"));
	for (int32 i = 0; i < List.Num(); i++)
	{
		const TSharedPtr<FJsonObject> O = List[i]->AsObject();
		FHKStashItem It;
		It.Kind = HKJson::Str(O, TEXT("kind"));
		It.Place = (int32)HKJson::Num(O, TEXT("place"));
		It.Pos = FVector(HKJson::Num(O, TEXT("x")), HKJson::Num(O, TEXT("y")), HKJson::Num(O, TEXT("z")));
		It.Hit = FHKHitBox::FromJson(HKJson::Obj(O, TEXT("hit")));
		for (int32 j = 0; FHKNode* N = Rig.Find(FString::Printf(TEXT("item%d.part%d"), i, j)); j++) It.Parts.Add(N);
		Items.Add(MoveTemp(It));
	}
}

void FHKStash::Use(int32 Item)
{
	if (!Items.IsValidIndex(Item)) return;
	FHKStashItem& It = Items[Item];
	It.bReady = false;
	It.T = COOLDOWN;
	for (FHKNode* P : It.Parts) P->bVisible = false;
}

void FHKStash::Update(double Dt)
{
	for (FHKStashItem& It : Items)
		if (!It.bReady && (It.T -= Dt) <= 0) { It.bReady = true; for (FHKNode* P : It.Parts) P->bVisible = true; }
}

int32 FHKStash::Nearest(const FVector& P, double Feet) const
{
	int32 Best = -1;
	double Bd = 1.5;
	for (int32 i = 0; i < Items.Num(); i++)
	{
		const FHKStashItem& It = Items[i];
		const double D = HK::Hypot(It.Pos.X - P.X, It.Pos.Z - P.Z) - (It.bReady ? .3 : 0);
		if (D < Bd && FMath::Abs(Places[It.Place].Y - Feet) < 1.2) { Bd = D; Best = i; }
	}
	return Best;
}

int32 FHKStash::Aimed(const FVector& From, const FVector& Dir, double Far, double& OutT) const
{
	int32 Best = -1;
	OutT = Far;
	for (int32 i = 0; i < Items.Num(); i++)
	{
		double T;
		if (Items[i].Hit.Ray(From, Dir, OutT, T) && T < OutT) { OutT = T; Best = i; }
	}
	return Best;
}
