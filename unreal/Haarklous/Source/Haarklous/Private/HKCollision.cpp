#include "HKCollision.h"

// Port of walk-collision.js. Comments in quotes are the web version's own.
const double FHKWalkWorld::INF = std::numeric_limits<double>::infinity();
const double FHKWalkWorld::EPS = 1e-8, FHKWalkWorld::STEP = .36, FHKWalkWorld::JOIN = .05, FHKWalkWorld::CELL = 16;

namespace
{
	inline FHKPt Sub(const FHKPt& A, const FHKPt& B) { return FHKPt(A.X - B.X, A.Z - B.Z); }
	inline double Dot(const FHKPt& A, const FHKPt& B) { return A.X * B.X + A.Z * B.Z; }
	inline double Cross(const FHKPt& A, const FHKPt& B) { return A.X * B.Z - A.Z * B.X; }
	inline double Len(const FHKPt& V) { return FMath::Sqrt(V.X * V.X + V.Z * V.Z); }
	inline FHKPt LerpP(const FHKPt& A, const FHKPt& B, double T) { return FHKPt(A.X + (B.X - A.X) * T, A.Z + (B.Z - A.Z) * T); }
	inline bool Finite(double V) { return FMath::IsFinite(V); }
}

void FHKObstacle::Prepare()
{
	Bounds = FHKWalkWorld::BoundsOf(Points);
}

FHKBounds FHKWalkWorld::BoundsOf(const TArray<FHKPt>& Points)
{
	FHKBounds B;
	B.MinX = B.MinZ = INF;
	B.MaxX = B.MaxZ = -INF;
	for (const FHKPt& P : Points)
	{
		B.MinX = FMath::Min(B.MinX, P.X); B.MaxX = FMath::Max(B.MaxX, P.X);
		B.MinZ = FMath::Min(B.MinZ, P.Z); B.MaxZ = FMath::Max(B.MaxZ, P.Z);
	}
	return B;
}

FHKPt FHKWalkWorld::ClosestOnSegment(const FHKPt& P, const FHKPt& A, const FHKPt& B)
{
	const FHKPt D = Sub(B, A);
	const double Squared = Dot(D, D);
	return LerpP(A, B, Squared ? FMath::Max(0.0, FMath::Min(1.0, Dot(Sub(P, A), D) / Squared)) : 0.0);
}

double FHKWalkWorld::DistanceToSegment(const FHKPt& P, const FHKPt& A, const FHKPt& B)
{
	return Len(Sub(P, ClosestOnSegment(P, A, B)));
}

bool FHKWalkWorld::Inside(const FHKPt& P, const TArray<FHKPt>& Points)
{
	bool Result = false;
	const int32 N = Points.Num();
	for (int32 i = 0; i < N; i++)
	{
		const FHKPt& A = Points[i];
		const FHKPt& B = Points[(i + 1) % N];
		if (DistanceToSegment(P, A, B) < EPS) return true;
		if ((A.Z > P.Z) != (B.Z > P.Z) && P.X < (B.X - A.X) * (P.Z - A.Z) / (B.Z - A.Z) + A.X) Result = !Result;
	}
	return Result;
}

double FHKWalkWorld::DistanceToPolygon(const FHKPt& P, const TArray<FHKPt>& Polygon)
{
	if (Inside(P, Polygon)) return 0;
	double Best = INF;
	const int32 N = Polygon.Num();
	for (int32 i = 0; i < N; i++) Best = FMath::Min(Best, DistanceToSegment(P, Polygon[i], Polygon[(i + 1) % N]));
	return Best;
}

// "Split every floor edge at other edges and keep the exposed pieces. A piece is open when the floor just across it
// continues at the same height (touching rooms, a stair meeting its landing); floors stacked on other storeys do not
// hide each other's edges."
void FHKWalkWorld::ExposedBoundary()
{
	struct FSeg { FHKPt C, E; double X0, X1, Z0, Z1; };
	TArray<FSeg> Segments;
	for (const FHKFloor& F : Floors)
		for (int32 i = 0; i < F.Points.Num(); i++)
		{
			const FHKPt& C = F.Points[i];
			const FHKPt& E = F.Points[(i + 1) % F.Points.Num()];
			Segments.Add({C, E, FMath::Min(C.X, E.X), FMath::Max(C.X, E.X), FMath::Min(C.Z, E.Z), FMath::Max(C.Z, E.Z)});
		}
	auto Covered = [this](const FHKPt& P, double Height, int32 Except)
	{
		for (int32 k = 0; k < Floors.Num(); k++)
		{
			const FHKFloor& F = Floors[k];
			if (k != Except && NearBounds(P, F.Bounds, EPS) && Inside(P, F.Points) && FMath::Abs(F.Surface(P) - Height) < JOIN) return true;
		}
		return false;
	};
	Boundary.Reset();
	for (int32 fi = 0; fi < Floors.Num(); fi++)
	{
		const FHKFloor& Floor = Floors[fi];
		for (int32 ei = 0; ei < Floor.Points.Num(); ei++)
		{
			const FHKPt A = Floor.Points[ei], B = Floor.Points[(ei + 1) % Floor.Points.Num()];
			const FHKPt D = Sub(B, A);
			const double DD = Dot(D, D), Span = Len(D);
			if (Span < EPS) continue;
			TArray<double> Cuts = {0.0, 1.0};
			const double Ex0 = FMath::Min(A.X, B.X) - 1e-6, Ex1 = FMath::Max(A.X, B.X) + 1e-6, Ez0 = FMath::Min(A.Z, B.Z) - 1e-6, Ez1 = FMath::Max(A.Z, B.Z) + 1e-6;
			for (const FSeg& S : Segments)
			{
				if (S.X1 < Ex0 || S.X0 > Ex1 || S.Z1 < Ez0 || S.Z0 > Ez1) continue;
				const FHKPt V = Sub(S.E, S.C), AC = Sub(S.C, A);
				const double Den = Cross(D, V);
				if (FMath::Abs(Den) > EPS)
				{
					const double T = Cross(AC, V) / Den, U = Cross(AC, D) / Den;
					if (T > EPS && T < 1 - EPS && U >= -EPS && U <= 1 + EPS) Cuts.Add(T);
				}
				else if (FMath::Abs(Cross(AC, D)) < EPS)
				{
					for (const FHKPt& Q : {S.C, S.E})
					{
						const double T = Dot(Sub(Q, A), D) / DD;
						if (T > EPS && T < 1 - EPS) Cuts.Add(T);
					}
				}
			}
			Cuts.Sort();
			for (int32 i = 1; i < Cuts.Num(); i++)
			{
				if (Cuts[i] - Cuts[i - 1] < EPS) continue;
				const FHKPt Start = LerpP(A, B, Cuts[i - 1]), End = LerpP(A, B, Cuts[i]);
				const FHKPt Middle = LerpP(Start, End, .5), Normal(-D.Z / Span * 1e-6, D.X / Span * 1e-6);
				const double Height = Floor.Surface(Middle);
				const FHKPt Sides[2] = {FHKPt(Middle.X + Normal.X, Middle.Z + Normal.Z), FHKPt(Middle.X - Normal.X, Middle.Z - Normal.Z)};
				// "Exactly one side lies inside this floor; the edge is a barrier unless the other side continues it."
				const bool In0 = Inside(Sides[0], Floor.Points), In1 = Inside(Sides[1], Floor.Points);
				const FHKPt* Outside = (In0 && !In1) ? &Sides[1] : (In1 && !In0) ? &Sides[0] : nullptr;
				if (Outside && Covered(*Outside, Height, fi)) continue;
				if (!Outside)
				{
					bool bOpen = false;
					for (int32 k = 0; k < Floors.Num() && !bOpen; k++)
					{
						const FHKFloor& F = Floors[k];
						if (k != fi && Inside(Middle, F.Points) && FMath::Abs(F.Surface(Middle) - Height) < JOIN && Inside(Sides[0], F.Points) && Inside(Sides[1], F.Points)) bOpen = true;
					}
					if (bOpen) continue;
				}
				FHKBoundaryPiece Piece;
				Piece.A = Start; Piece.B = End; Piece.Floor = fi;
				Boundary.Add(Piece);
			}
		}
	}
}

void FHKWalkWorld::Create(TArray<FHKFloor> InFloors, TArray<FHKObstacle> InObstacles, double InRadius, double InClearance)
{
	Floors = MoveTemp(InFloors);
	Obstacles = MoveTemp(InObstacles);
	Radius = InRadius;
	Clearance = InClearance;
	for (FHKFloor& F : Floors) F.Bounds = BoundsOf(F.Points);
	for (FHKObstacle& O : Obstacles) O.Prepare();
	ExposedBoundary();
	Dynamic.Reset();
	// "Spatial hash (CELL m) so each query only tests nearby obstacles and boundary pieces."
	Cells.Reset();
	const double Pad = Radius + .8;
	auto AddTo = [this, Pad](int32 Item, const FHKBounds& B, bool bObstacle)
	{
		for (int32 i = FMath::FloorToInt32((B.MinX - Pad) / CELL); i <= FMath::FloorToInt32((B.MaxX + Pad) / CELL); i++)
			for (int32 j = FMath::FloorToInt32((B.MinZ - Pad) / CELL); j <= FMath::FloorToInt32((B.MaxZ + Pad) / CELL); j++)
			{
				FCell& C = Cells.FindOrAdd(FIntPoint(i, j));
				(bObstacle ? C.Obstacles : C.Boundary).Add(Item);
			}
	};
	for (int32 i = 0; i < Obstacles.Num(); i++) AddTo(i, Obstacles[i].Bounds, true);
	for (int32 i = 0; i < Boundary.Num(); i++) AddTo(i, BoundsOf({Boundary[i].A, Boundary[i].B}), false);
}

void FHKWalkWorld::SetDynamic(TArray<FHKObstacle> List)
{
	Dynamic = MoveTemp(List);
	for (FHKObstacle& O : Dynamic) O.Prepare();
}

const FHKWalkWorld::FCell* FHKWalkWorld::CellAt(const FHKPt& P) const
{
	if (!Finite(P.X) || !Finite(P.Z)) return nullptr;
	return Cells.Find(FIntPoint(FMath::FloorToInt32(P.X / CELL), FMath::FloorToInt32(P.Z / CELL)));
}

FHKSupport FHKWalkWorld::SupportAt(const FHKPt& P, double Y) const
{
	FHKSupport Best;
	double Gap = -INF;
	for (int32 i = 0; i < Floors.Num(); i++)
	{
		const FHKFloor& F = Floors[i];
		if (!NearBounds(P, F.Bounds, EPS) || !Inside(P, F.Points)) continue;
		// "Highest walkable surface within a step: walking into a stair or ramp climbs onto it."
		const double H = F.Surface(P);
		if (FMath::Abs(H - Y) <= STEP && H > Gap) { Best.bFound = true; Best.Floor = i; Best.Y = H; Gap = H; }
	}
	return Best;
}

FHKPt FHKWalkWorld::ClosestInside(const FHKPt& P, const FHKFloor& F) const
{
	if (Inside(P, F.Points)) return P;
	FHKPt Best = P;
	double Distance = INF;
	for (int32 i = 0; i < F.Points.Num(); i++)
	{
		const FHKPt Q = ClosestOnSegment(P, F.Points[i], F.Points[(i + 1) % F.Points.Num()]);
		const double D = Len(Sub(P, Q));
		if (D < Distance) { Distance = D; Best = Q; }
	}
	return Best;
}

double FHKWalkWorld::CeilingAt(const FHKPt& P, double Y) const
{
	double Lowest = INF;
	for (const FHKFloor& F : Floors)
	{
		if (!NearBounds(P, F.Bounds, Radius + EPS) || DistanceToPolygon(P, F.Points) > Radius + EPS) continue;
		const double H = F.Surface(ClosestInside(P, F));
		if (FMath::Abs(H - Y) > STEP * 1.7) continue;
		Lowest = FMath::Min(Lowest, H + F.CeilingHeight - Y);
	}
	return Lowest;
}

bool FHKWalkWorld::ClampEyeHeight(const FHKPt& P, double Requested, double& Out, double Minimum, double Y) const
{
	double Maximum = CeilingAt(P, Y) - FMath::Max(.10, Clearance);
	ForEachSolidNear(P, [&](const FHKObstacle& O)
	{
		if (O.MinY > Y + .04 && NearBounds(P, O.Bounds, Radius) && DistanceToPolygon(P, O.Points) < Radius - EPS) Maximum = FMath::Min(Maximum, O.MinY - Y - .10);
	});
	if (Maximum < Minimum) return false;
	Out = FMath::Min(Maximum, FMath::Max(Minimum, Requested));
	return true;
}

bool FHKWalkWorld::BarrierActive(const FHKBoundaryPiece& Piece, const FHKPt& P, double Y) const
{
	return FMath::Abs(Floors[Piece.Floor].Surface(ClosestOnSegment(P, Piece.A, Piece.B)) - Y) < STEP * 1.7;
}

bool FHKWalkWorld::CanStand(const FHKPt& P, double EyeHeight, double Y) const
{
	if (!Finite(P.X) || !Finite(P.Z) || !Finite(EyeHeight) || EyeHeight <= 0) return false;
	const FHKSupport S = SupportAt(P, Y);
	if (!S.bFound) return false;
	const double Feet = S.Y;
	if (EyeHeight > CeilingAt(P, Feet) - FMath::Max(.10, Clearance) + EPS) return false;
	if (const FCell* C = CellAt(P))
		for (int32 i : C->Boundary)
		{
			const FHKBoundaryPiece& Piece = Boundary[i];
			if (DistanceToSegment(P, Piece.A, Piece.B) < Radius - EPS && BarrierActive(Piece, P, Feet)) return false;
		}
	return !AnySolidNear(P, [&](const FHKObstacle& O)
	{
		return Overhead(O, Feet, EyeHeight) && NearBounds(P, O.Bounds, Radius) && DistanceToPolygon(P, O.Points) < Radius - EPS;
	});
}

// "Flying objects: does any solid (building, tree, wall, vehicle) come within the world's radius of p between heights y0 and y1?"
bool FHKWalkWorld::HitsSolid(const FHKPt& P, double Y0, double Y1) const
{
	if (!Finite(P.X) || !Finite(P.Z)) return true;
	return AnySolidNear(P, [&](const FHKObstacle& O)
	{
		return O.MaxY > Y0 && O.MinY < Y1 && NearBounds(P, O.Bounds, Radius) && (Inside(P, O.Points) || DistanceToPolygon(P, O.Points) < Radius - EPS);
	});
}

FVector FHKWalkWorld::Move(const FHKPt& Position, const FHKPt& Delta, double EyeHeight, double Y) const
{
	const FHKSupport Start = SupportAt(Position, Y);
	if (!Start.bFound || !CanStand(Position, EyeHeight, Y)) return FVector(Position.X, Position.Z, Y);
	FHKPt P = Position;
	double Feet = Start.Y;
	const double Distance = Len(Delta);
	if (!Finite(Distance)) return FVector(P.X, P.Z, Feet);
	// "Small enough for thin obstacles: circle dilation makes each solid at least 2*radius wide,
	// so an entire collision cannot be skipped in one substep."
	const int32 Count = FMath::Max(1, (int32)FMath::CeilToDouble(Distance / FMath::Min(.04, Radius * .25)));
	const FHKPt Step(Delta.X / Count, Delta.Z / Count);
	auto Advance = [&](const FHKPt& Q) { P = Q; Feet = SupportAt(Q, Feet).Y; };
	TArray<TPair<FHKPt, FHKPt>> Edges;
	for (int32 i = 0; i < Count; i++)
	{
		const FHKPt Next(P.X + Step.X, P.Z + Step.Z);
		if (CanStand(Next, EyeHeight, Feet)) { Advance(Next); continue; }
		// "Approach contact without leaving the footprint embedded in the wall."
		double Low = 0, High = 1;
		for (int32 j = 0; j < 12; j++)
		{
			const double Mid = (Low + High) / 2;
			if (CanStand(LerpP(P, Next, Mid), EyeHeight, Feet)) Low = Mid; else High = Mid;
		}
		const FHKPt Contact = LerpP(P, Next, Low);
		const FHKPt Rest(Step.X * (1 - Low), Step.Z * (1 - Low));
		TArray<FHKPt, TInlineAllocator<16>> Candidates;
		Candidates.Add(FHKPt(Rest.X, 0));
		Candidates.Add(FHKPt(0, Rest.Z));
		// activeEdges(): barrier pieces near p, then the edges of overhead solids near p.
		Edges.Reset();
		if (const FCell* C = CellAt(P))
			for (int32 k : C->Boundary)
				if (BarrierActive(Boundary[k], P, Feet)) Edges.Add(TPair<FHKPt, FHKPt>(Boundary[k].A, Boundary[k].B));
		ForEachSolidNear(P, [&](const FHKObstacle& O)
		{
			if (!Overhead(O, Feet, EyeHeight)) return;
			for (int32 k = 0; k < O.Points.Num(); k++) Edges.Add(TPair<FHKPt, FHKPt>(O.Points[k], O.Points[(k + 1) % O.Points.Num()]));
		});
		const double RestLen = Len(Rest);
		for (const TPair<FHKPt, FHKPt>& E : Edges)
		{
			if (DistanceToSegment(Contact, E.Key, E.Value) > Radius + RestLen + .001) continue;
			const FHKPt Edge = Sub(E.Value, E.Key);
			const double Squared = Dot(Edge, Edge);
			if (Squared < EPS) continue;
			const double T = Dot(Rest, Edge) / Squared;
			Candidates.Add(FHKPt(Edge.X * T, Edge.Z * T));
		}
		FHKPt Best = Contact;
		double Score = 0;
		for (const FHKPt& Cand : Candidates)
		{
			const FHKPt Q(Contact.X + Cand.X, Contact.Z + Cand.Z);
			const double Progress = Dot(Cand, Rest);
			if (Progress > Score && CanStand(Q, EyeHeight, Feet)) { Best = Q; Score = Progress; }
		}
		Advance(Best);
	}
	return FVector(P.X, P.Z, Feet);
}

bool FHKWalkWorld::FindSafeSpawn(const FHKPt& Requested, double EyeHeight, FVector& Out, double MaxDistance, double Step, double Y) const
{
	auto Ok = [&](const FHKPt& P) { return CanStand(P, EyeHeight, Y); };
	auto Result = [&](const FHKPt& P) { Out = FVector(P.X, P.Z, SupportAt(P, Y).Y); return true; };
	bool bAny = false;
	for (const FHKFloor& F : Floors) if (EyeHeight <= F.CeilingHeight - FMath::Max(.10, Clearance) + EPS) { bAny = true; break; }
	if (!bAny) return false;
	if (Ok(Requested)) return Result(Requested);
	// "Expanding radial search returns a nearby safe point; angular spacing is bounded by step so even a narrow
	// doorway remains discoverable."
	for (double R = Step; R <= MaxDistance; R += Step)
	{
		const int32 Count = FMath::Max(12, (int32)FMath::CeilToDouble(2 * UE_DOUBLE_PI * R / Step));
		for (int32 i = 0; i < Count; i++)
		{
			const double Angle = 2 * UE_DOUBLE_PI * i / Count;
			const FHKPt P(Requested.X + R * FMath::Cos(Angle), Requested.Z + R * FMath::Sin(Angle));
			if (Ok(P)) return Result(P);
		}
	}
	return false;
}
