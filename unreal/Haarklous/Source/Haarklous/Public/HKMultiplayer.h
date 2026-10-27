#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "HKMath.h"
#include "HKNet.h"
#include "HKNode.h"
#include "HKAvatars.h"

class FHKVehicle;
class UCanvas;

/*
 * Multiplayer glue between the game and the lobby: port of the web's game-multiplayer.js (and onLobbyChange of
 * game-main.js). Remote players as avatars (on foot, on the firepit benches, in vehicle seats, flying a drone),
 * vehicles driven by other players (smoothed between their updates), seat claims (one driver and one passenger per
 * vehicle), chat, doors and the shared firepit level. Everything is in the web frame, so nothing is converted on the wire.
 */

/* What other players see of us ('st' messages, game-main.js localState()). Built by the main loop every 0.1 s. */
struct HAARKLOUS_API FHKPlayerState
{
	FVector P = FVector::ZeroVector;   // [x, feet + jumpY, z], rounded with R()
	double Y = 0;                       // yaw, R()
	FString St = TEXT("stand");         // player.stance; 'drone' while flying the drone
	int32 B = 0;                        // 1 while drinking a beer (beerT > 0)
	int32 V = -1;                       // vehicle index while driving or riding along
	FString S;                          // seat 'd' or 'p' (only with V >= 0)
	int32 Sit = 0;                      // firepit spot id + 1 while sitting (0 = not sitting)
	bool bDr = false;                   // drone pose: position R(), quaternion Q()
	double Dr[7] = {0, 0, 0, 0, 0, 0, 1};

	static double R(double V) { return HK::JsRound(V * 100) / 100; }    // Math.round(v * 100) / 100
	static double Q(double V) { return HK::JsRound(V * 1000) / 1000; }  // Math.round(v * 1000) / 1000
	TSharedPtr<FJsonObject> ToJson() const;
	// A remote state; false when it is not an object (the web then hides the avatar).
	static bool FromJson(const TSharedPtr<FJsonValue>& V, FHKPlayerState& Out);
};

struct FHKChatLine
{
	int32 Id = 0;
	int32 Slot = 0;
	FString Name;
	FString Text;
	double At = 0;
	bool bSys = false;                  // 'joined the lobby', 'left'
};

struct FHKClaimResult
{
	bool bOk = false;
	int32 By = 0;                       // who has the seat (0 = nobody / null)
	bool bTimeout = false;              // no answer within 4 s
	bool bOffline = false;              // the connection dropped while waiting
};

struct FHKOcc { int32 D = 0; int32 P = 0; };   // occupants of one vehicle (0 = free)

/* A player in the lobby other than us. */
struct HAARKLOUS_API FHKRemote
{
	int32 Id = 0;
	int32 Slot = 0;
	FString Name;
	bool bHasSt = false;
	FHKPlayerState St;
	TUniquePtr<FHKAvatar> Av;
	FVector Pos = FVector::ZeroVector;
	double Yaw = 0;
	double Speed = 0;
	TUniquePtr<FHKRig> Drone;
	bool bSeen = false;
	~FHKRemote();
};

/* Lobby voice chat (game-voice.js); the default does nothing (no microphone, nobody heard). See HKVoice.h. */
class HAARKLOUS_API IHKVoice
{
public:
	virtual ~IHKVoice() {}
	// Called by FHKMultiplayer: send(to, data) through the lobby, and our own id.
	TFunction<void(int32 /*To*/, const TSharedPtr<FJsonValue>& /*D*/)> SendSignal;
	TFunction<int32()> You;
	virtual void SetMic(bool bOn, TFunction<void(bool)> Done) { if (Done) Done(false); }
	virtual bool Mic() const { return false; }
	virtual void OnSignal(int32 From, const TSharedPtr<FJsonValue>& D) {}
	virtual void PeerJoined(int32 Id) {}
	virtual void PeerLeft(int32 Id) {}
	virtual void SetVolume(int32 Id, double V) {}
	virtual void Reset() {}
	// Received level (RMS 0..1) and connection state of one peer (tests and diagnostics; 0 when unknown).
	virtual double PeerLevel(int32 Id, FString* OutState = nullptr, double* OutBytesIn = nullptr) const { return 0; }
};

/* The game's side (the JS ctx). Every function may be left empty where the game has nothing to do. */
struct FHKMultiplayerContext
{
	TFunction<int32()> NumVehicles;                         // vehicles.length
	TFunction<FHKVehicle*(int32)> Vehicle;                  // vehicles[i] (same list and order as the web)
	TFunction<FHKPlayerState()> LocalState;                 // localState()
	TFunction<int32()> DrivingIndex;                        // player.mode === 'vehicle' ? vehicles.indexOf(player.vehicle) : -1
	TFunction<FVector()> Listener;                          // {x, feet + 1.5, z} (voice has no distance fall-off)
	TFunction<FVector()> CameraPosition;                    // camera.position (name tag and bubble size)
	TFunction<void(const FString& /*Id*/, bool /*bOpen*/, bool /*bInstant*/)> ApplyDoor;
	TFunction<void(const FHKChatLine& /*Line*/, bool /*bMine*/, bool /*bOld*/)> OnChat;
	TFunction<void()> OnStatus;                             // renderMp() (onLobbyChange() runs inside FHKMultiplayer)
	TFunction<void(const FString& /*Text*/, double /*Seconds*/)> Notice;
	TFunction<void(const FHKRemote&)> OnRemoteWood;         // fire.throwLog from r.pos + 1.3 m
	TFunction<void(FHKRig& /*Rig*/, const FString& /*RigName*/)> AddToScene;   // scene.add (an AHKRigActor for the rig)
	TFunction<void(FHKRig&)> RemoveFromScene;               // scene.remove
	// onLobbyChange (game-main.js): which vehicle the player is in (-1 none) and whether as the driver.
	TFunction<int32()> PlayerVehicleIndex;
	TFunction<bool()> PlayerDriving;
	double FireDecayS = 90;                                 // fire.DECAY_S
	double FireMax = 6;                                     // fire.MAX
};

class HAARKLOUS_API FHKMultiplayer
{
public:
	FHKMultiplayer(FHKMultiplayerContext InCtx, FHKNetOptions NetOptions = FHKNet::DefaultOptions(), TUniquePtr<IHKVoice> InVoice = nullptr);
	~FHKMultiplayer();
	FHKMultiplayer(const FHKMultiplayer&) = delete;
	FHKMultiplayer& operator=(const FHKMultiplayer&) = delete;

	FHKNet& Net() { return *NetPtr; }
	const FHKNet& Net() const { return *NetPtr; }
	IHKVoice& Voice() { return *VoicePtr; }

	// Once per frame where game-main.js calls mp.update(dt).
	void Update(double Dt);
	// End of the frame: answers of offline seat claims arrive here (the web's resolved promises).
	void EndFrame();
	// Seats: online the lobby decides who gets a seat ('d' driver, 'p' passenger); offline every seat is yours.
	void Claim(int32 I, const FString& Seat, TFunction<void(const FHKClaimResult&)> Done);
	void Release(int32 I, const FString& Seat, const TArray<double>* Final = nullptr);
	int32 Occupant(int32 I, const FString& Seat) const;     // 0 = free
	bool RemoteDriven(int32 I) const;
	FString NameOf(int32 Id) const;
	bool SitTaken(int32 SpotId) const;
	double FireLevel() const;
	void AddWood();
	bool SendChat(const FString& Text);                     // true when sent to the lobby
	void SendDoor(const FString& Id, bool bOpen);
	void SetMic(bool bOn, TFunction<void(bool)> Done = nullptr);
	bool Mic() const { return VoicePtr->Mic(); }
	const TArray<TUniquePtr<FHKRemote>>& Remotes() const { return RemotesList; }
	const FHKRemote* Remote(int32 Id) const;
	const TMap<int32, FHKOcc>& Occ() const { return OccMap; }
	// Name tags and chat bubbles of all remote players (from AHKHud).
	void DrawHud(UCanvas* C) const;

private:
	struct FVehTarget { TArray<double> A, Prev; double At = 0, PrevAt = 0; };
	struct FPending { int32 Token = 0; TFunction<void(const FHKClaimResult&)> Done; };
	struct FFire { double Level = 2; double T = 0; };

	void OnMessage(const TSharedPtr<FJsonObject>& M);
	void OnLobbyChange();
	FHKRemote& RemoteOf(int32 Id);
	FHKRemote* FindRemote(int32 Id);
	void Drop(int32 Id);
	FHKVehicle* VehicleAt(int32 I) const;
	int32 VehicleCount() const;
	void SmoothVehicle(int32 I, FHKVehicle* V, const FVehTarget& Tgt, double Dt);
	static TMap<int32, FHKOcc> ParseOcc(const TSharedPtr<FJsonObject>& M);
	static bool ParseFire(const TSharedPtr<FJsonObject>& M, FFire& Out);
	static int32 SeatOf(const FHKOcc& O, const FString& Seat) { return Seat == TEXT("d") ? O.D : Seat == TEXT("p") ? O.P : 0; }

	FHKMultiplayerContext Ctx;
	TUniquePtr<IHKVoice> VoicePtr;
	TUniquePtr<FHKNet> NetPtr;
	TArray<TUniquePtr<FHKRemote>> RemotesList;    // insertion order like the web's Map
	TMap<int32, FHKOcc> OccMap;
	FFire Fire;
	double SendT = 0, VehT = 0;
	FHKPlayerState LastLocal;
	TMap<int32, FPending> Pending;
	int32 NextToken = 1;
	TArray<TPair<int32, FVehTarget>> VehTargets;   // insertion order like the web's Map
	TArray<TFunction<void()>> Deferred;
	FString LastLobby;
	bool bHasLastLobby = false;
};
