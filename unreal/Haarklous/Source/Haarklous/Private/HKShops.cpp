// Port of reference/web-game/game-shops.js (the shops' running parts and the wallet). Keep it line by line with the
// JavaScript; the shop buildings themselves are level geometry made from the web's export.
#include "HKShops.h"
#include "HKData.h"
#include "Haarklous.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

// ---------- Wallet ----------
void FHKWallet::Load()
{
	Reset(500);
	FString Text;
	if (SavePath.IsEmpty() || !FFileHelper::LoadFileToString(Text, *SavePath)) return;
	TSharedPtr<FJsonObject> J;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), J) || !J) return;
	double M = 0;
	if (!J->TryGetNumberField(TEXT("money"), M) || !FMath::IsFinite(M)) return;
	Money = M;
	Pant = (int32)HKJson::Num(J, TEXT("pant"));
	const TSharedPtr<FJsonObject>* B = nullptr;
	if (J->TryGetObjectField(TEXT("bag"), B))
		for (const TPair<FString, TSharedPtr<FJsonValue>>& E : (*B)->Values) { const int32 N = (int32)E.Value->AsNumber(); if (N > 0) Bag.Add({E.Key, N}); }
}

void FHKWallet::Save() const
{
	if (SavePath.IsEmpty()) return;
	TSharedRef<FJsonObject> J = MakeShared<FJsonObject>(), B = MakeShared<FJsonObject>();
	J->SetNumberField(TEXT("money"), Money);
	J->SetNumberField(TEXT("pant"), Pant);
	for (const TPair<FString, int32>& E : Bag) B->SetNumberField(E.Key, E.Value);
	J->SetObjectField(TEXT("bag"), B);
	FString Text;
	FJsonSerializer::Serialize(J, TJsonWriterFactory<>::Create(&Text));
	FFileHelper::SaveStringToFile(Text, *SavePath);
}

void FHKWallet::Earn(double V) { if (!(V > 0)) return; Money = Round2(Money + V); Save(); }
bool FHKWallet::Pay(double V) { if (!(V >= 0) || Money + 1e-9 < V) return false; Money = Round2(Money - V); Save(); return true; }
void FHKWallet::AddPant(int32 N) { Pant += N; Save(); }
int32 FHKWallet::CashPant() { const int32 N = Pant; Pant = 0; Money = Round2(Money + N * FHKShops::PANT); Save(); return N; }
void FHKWallet::Add(const FString& Id, int32 N)
{
	if (TPair<FString, int32>* E = Bag.FindByPredicate([&](const TPair<FString, int32>& X) { return X.Key == Id; })) E->Value += N;
	else Bag.Add({Id, N});
	Save();
}
bool FHKWallet::Take(const FString& Id)
{
	const int32 I = Bag.IndexOfByPredicate([&](const TPair<FString, int32>& X) { return X.Key == Id; });
	if (I == INDEX_NONE || Bag[I].Value <= 0) return false;
	if (--Bag[I].Value <= 0) Bag.RemoveAt(I);
	Save();
	return true;
}
void FHKWallet::Reset(double InMoney) { Money = InMoney; Pant = 0; Bag.Reset(); }
int32 FHKWallet::Count(const FString& Id) const
{
	const TPair<FString, int32>* E = Bag.FindByPredicate([&](const TPair<FString, int32>& X) { return X.Key == Id; });
	return E ? E->Value : 0;
}

// ---------- Shops ----------
double FHKShops::PriceOf(const FHKShop& S, const FHKProduct& P)
{
	return S.PriceFactor == 1 ? P.Price : HK::JsRound(P.Price * S.PriceFactor - .9) + .9;
}

bool FHKShops::BeerAllowed(const FDateTime& T)
{
	const int32 Day = (int32)T.GetDayOfWeek();   // EDayOfWeek: Monday = 0 ... Saturday = 5, Sunday = 6
	const double H = T.GetHour() + T.GetMinute() / 60.0;
	if (Day == 6) return false;
	return H >= 8 && H < (Day == 5 ? 18 : 20);
}

FString FHKShops::Kr(double V)
{
	return FString::Printf(TEXT("%.2f kr"), V).Replace(TEXT("."), TEXT(","));
}

FHKShops::FHKShops(const TSharedPtr<FJsonObject>& J, const FString& RigPath)
{
	FString Error;
	if (!RigPath.IsEmpty() && !Rig.LoadFromJson(RigPath, &Error)) UE_LOG(LogHaarklous, Warning, TEXT("Shops rig: %s"), *Error);
	for (const TSharedPtr<FJsonValue>& V : HKJson::Arr(J, TEXT("products")))
	{
		const TSharedPtr<FJsonObject> O = V->AsObject();
		FHKProduct P;
		P.Id = HKJson::Str(O, TEXT("id")); P.Name = HKJson::Str(O, TEXT("name")); P.Where = HKJson::Str(O, TEXT("where")); P.Use = HKJson::Str(O, TEXT("use"));
		P.Price = HKJson::Num(O, TEXT("price")); P.Sober = HKJson::Num(O, TEXT("sober")); P.bBeer = HKJson::Bool(O, TEXT("beer"), false); P.Color = (uint32)HKJson::Num(O, TEXT("color"));
		Products.Add(P);
	}
	const TArray<TSharedPtr<FJsonValue>>& List = HKJson::Arr(J, TEXT("shops"));
	for (int32 i = 0; i < List.Num(); i++)
	{
		const TSharedPtr<FJsonObject> O = List[i]->AsObject();
		FHKShop S;
		S.Id = HKJson::Str(O, TEXT("id")); S.Name = HKJson::Str(O, TEXT("name")); S.Brand = HKJson::Str(O, TEXT("brand")); S.Hours = HKJson::Str(O, TEXT("hours"));
		S.Shirt = (uint32)HKJson::Num(O, TEXT("shirt")); S.PriceFactor = HKJson::Num(O, TEXT("priceFactor"), 1);
		S.FloorY = HKJson::Num(O, TEXT("floorY")); S.W = HKJson::Num(O, TEXT("W")); S.D = HKJson::Num(O, TEXT("D")); S.UDoor = HKJson::Num(O, TEXT("uDoor"));
		const TArray<double> Or = HKJson::Nums(O, TEXT("origin")), Uv = HKJson::Nums(O, TEXT("u")), Vv = HKJson::Nums(O, TEXT("v"));
		if (Or.Num() == 2) S.Origin = FVector2D(Or[0], Or[1]);
		if (Uv.Num() == 2) S.U = FVector2D(Uv[0], Uv[1]);
		if (Vv.Num() == 2) S.V = FVector2D(Vv[0], Vv[1]);
		S.Prices = HKJson::Nums(O, TEXT("prices"));
		for (int32 k = 0; k < 2; k++) S.Doors[k] = Rig.Find(FString::Printf(TEXT("shop%d.door%d"), i, k));
		Shops.Add(S);
	}
	for (const TSharedPtr<FJsonValue>& V : HKJson::Arr(J, TEXT("hitboxes")))
	{
		const TSharedPtr<FJsonObject> O = V->AsObject();
		FHKShopBox B;
		B.Kind = HKJson::Str(O, TEXT("kind"));
		B.Shop = (int32)HKJson::Num(O, TEXT("shop"));
		B.Product = ProductIndex(HKJson::Str(O, TEXT("product")));
		B.Box = FHKHitBox::FromJson(O);
		Boxes.Add(B);
	}
}

int32 FHKShops::ShopAt(double X, double Z, double Feet) const
{
	for (int32 i = 0; i < Shops.Num(); i++)
	{
		const FHKShop& S = Shops[i];
		const FVector2D L = S.ToLocal(X, Z);
		if (L.X > 0 && L.X < S.W && L.Y > 0 && L.Y < S.D && FMath::Abs(Feet - S.FloorY) < 1.4) return i;
	}
	return -1;
}

bool FHKShops::AtTill(int32 Shop, double X, double Z) const
{
	const FVector2D L = Shops[Shop].ToLocal(X, Z);
	return HK::Hypot(L.X - 2.4, L.Y - 2.25) < 1.3;
}

int32 FHKShops::Aimed(const FVector& From, const FVector& Dir, double Far, double& OutT) const
{
	int32 Best = -1;
	OutT = Far;
	for (int32 i = 0; i < Boxes.Num(); i++)
	{
		double T;
		if (Boxes[i].Box.Ray(From, Dir, OutT, T) && T < OutT) { OutT = T; Best = i; }
	}
	return Best;
}

bool FHKShops::Update(double Dt, double X, double Z, double Feet)
{
	bool bOpened = false;
	for (FHKShop& B : Shops)
	{
		const FVector2D L = B.ToLocal(X, Z);
		// Sliding doors open when someone is within 2.5 m of them.
		const double Want = HK::Hypot(L.X - B.UDoor, L.Y) < 2.5 && FMath::Abs(Feet - B.FloorY) < 1.6 ? 1 : 0;
		if (Want && !B.Open) bOpened = true;
		B.Open += (Want - B.Open) * FMath::Min(1.0, Dt * 5);
		for (int32 k = 0; k < 2; k++)
			if (FHKNode* D = B.Doors[k]) { const double Sd = k == 0 ? -1 : 1; D->Position.X = B.UDoor + Sd * (.475 + B.Open * .9); }
		// The cashier turns toward a player inside (at most 1.1 rad from facing the door side).
		const bool bInside = L.X > 0 && L.X < B.W && L.Y > 0 && L.Y < B.D;
		const double Face = bInside ? std::atan2(-(L.X - 2.4), -(L.Y - .5)) : HK::Pi;
		double Dd = Face - HK::Pi;
		Dd = std::atan2(std::sin(Dd), std::cos(Dd));
		const double Yaw = HK::Pi + FMath::Clamp(Dd, -1.1, 1.1);
		B.CashierYaw += (Yaw - B.CashierYaw) * FMath::Min(1.0, Dt * 3);
	}
	return bOpened;
}
