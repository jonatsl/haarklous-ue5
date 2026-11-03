// Street parking (3 Nov 2026, the realism pass; not in the web game, so off unless the game starts with -HKParkedCars).
#pragma once

#include "CoreMinimal.h"
#include "HKVehicle.h"

/*
 * Street View shows Torshov's streets lined with parked cars; the web parks its 32 cars and motorcycles 26-42 m apart.
 * These cars stand nose to tail along the kerbs within 400 m of home (nearest streets first, at most 220), on the
 * streets and service lanes the web parks on, where the web parks (half a road width plus
 * 1.25 m from the road's middle) and with the web's checks (not within 10 m of another road, level, room for the car),
 * leaving gaps at footpath crossings and now and then. They are not in the vehicle list (online numbering stays the
 * web's) and cannot be driven; when on, they block walking and driving like the web's parked cars. A web player in the
 * same lobby does not have them, which is why they wait for the user's OK.
 */
namespace HKParking
{
	bool Enabled();
	// Parked cars that keep clear of the given vehicles (the web's list); seeded, the same on every start.
	void Create(const TArray<TUniquePtr<FHKVehicle>>& Existing, TArray<TUniquePtr<FHKVehicle>>& Out);
}
