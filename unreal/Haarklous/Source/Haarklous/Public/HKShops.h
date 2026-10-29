#pragma once

#include "CoreMinimal.h"
#include <cmath>
#include "Dom/JsonObject.h"
#include "HKMath.h"
#include "HKNode.h"

/*
 * Port of game-shops.js (Unreal issue #5): Joker Torshov and Kiwi Torshov at their OpenStreetMap places, each cut into the
 * ground floor of its building (glass front with sliding doors and the brand sign, shelves, fridges, a till with a cashier,
 * a bottle-return machine), and the wallet: money in kroner, empty bottles and the bag of bought items, saved between
 * sessions per player (not shared online). Prompts, paying, the bag and earning are in AHKGame (game-main.js "Shops").
 *
 * Data: game.json "shops" (each shop's frame, the products and prices, the hit boxes of the items, the till and the bottle
 * machine); the floors, entrance slabs, glass fronts, shelves and the notched building outlines are in the walk world
 * already. Rig "shops" (level geometry, tools/web-export/exporter-places.js): "shop<i>.door<k>" are the sliding doors.
 */
struct FHKProduct
{
	FString Id, Name, Where, Use;
	double Price = 0, Sober = 0;
	bool bBeer = false;
	uint32 Color = 0;
};

struct FHKShop
{
	FString Id, Name, Brand, Hours;
	uint32 Shirt = 0;
	double PriceFactor = 1, FloorY = 0, W = 0, D = 0, UDoor = 0;
	FVector2D Origin = FVector2D::ZeroVector, U = FVector2D::ZeroVector, V = FVector2D::ZeroVector;   // shop frame: a along U, b along V
	TArray<double> Prices;           // priceOf(shop, product) for each product
	// Running state (create().built[]).
	double Open = 0;                 // the sliding doors, 0 closed ... 1 open
	FHKNode* Doors[2] = {nullptr, nullptr};
	double CashierYaw = HK::Pi;      // the cashier's group.rotation.y (in the shop frame)
	FVector2D ToLocal(double X, double Z) const { const double Dx = X - Origin.X, Dz = Z - Origin.Y; return FVector2D(Dx * U.X + Dz * U.Y, Dx * V.X + Dz * V.Y); }
	FVector2D ToWorld(double A, double B) const { return FVector2D(Origin.X + U.X * A + V.X * B, Origin.Y + U.Y * A + V.Y * B); }
	double Yaw() const { return std::atan2(-U.Y, U.X); }   // the shop group's rotation.y
};

struct FHKShopBox
{
	FString Kind;                    // 'item' | 'till' | 'pant'
	int32 Shop = 0;
	int32 Product = -1;              // index into Products for 'item'
	FHKHitBox Box;
};

/* makeWallet(): money (kr), empty bottles, the bag (product id -> count, in the order first bought). */
class HAARKLOUS_API FHKWallet
{
public:
	double Money = 500;
	int32 Pant = 0;
	TArray<TPair<FString, int32>> Bag;
	FString SavePath;                // empty: not saved (tests)

	void Load();                     // a broken or missing save starts over at 500 kr
	void Save() const;
	void Earn(double V);
	bool Pay(double V);
	void AddPant(int32 N = 1);
	int32 CashPant();
	void Add(const FString& Id, int32 N = 1);
	bool Take(const FString& Id);
	void Reset(double InMoney = 500);
	int32 Count(const FString& Id) const;
	static double Round2(double V) { return HK::JsRound(V * 100) / 100; }
};

class HAARKLOUS_API FHKShops
{
public:
	static constexpr double PANT = 2;   // kroner per empty bottle or can

	// J = game.json "shops"; RigPath = Content/Data/rigs/shops.json (empty: no rig, e.g. in tests).
	FHKShops(const TSharedPtr<FJsonObject>& J, const FString& RigPath);
	FHKShops(const FHKShops&) = delete;
	FHKShops& operator=(const FHKShops&) = delete;

	FHKRig Rig;
	TArray<FHKProduct> Products;
	TArray<FHKShop> Shops;
	TArray<FHKShopBox> Boxes;
	FHKWallet Wallet;

	int32 ProductIndex(const FString& Id) const { return Products.IndexOfByPredicate([&](const FHKProduct& P) { return P.Id == Id; }); }
	static double PriceOf(const FHKShop& S, const FHKProduct& P);
	// Norwegian law: grocery shops may sell beer Mon-Fri 08-20 and Sat 08-18, not on Sundays (the PC clock).
	static bool BeerAllowed(const FDateTime& T);
	static FString Kr(double V);     // "39,90 kr"
	// Which shop is the player standing in (on its floor)? -1 if none.
	int32 ShopAt(double X, double Z, double Feet) const;
	bool AtTill(int32 Shop, double X, double Z) const;
	// The first box the ray meets within Far; -1 if none.
	int32 Aimed(const FVector& From, const FVector& Dir, double Far, double& OutT) const;
	// update(): the sliding doors open when someone is within 2.5 m (returns true when a door starts to open, for the
	// 'shopDoor' sound) and the cashier turns toward a player inside.
	bool Update(double Dt, double X, double Z, double Feet);
};
