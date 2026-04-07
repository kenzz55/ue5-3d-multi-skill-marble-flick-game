// Fill out your copyright notice in the Description page of Project Settings.

#include "AKAIController.h"

#include <cfloat>

#include "AKSingleGameMode.h"
#include "AKStone.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"

void AAKAIController::DoAITurn(float BasePowerPerMeter, float MinPower, float MaxPower)
{
	AAKSingleGameMode* GM = Cast<AAKSingleGameMode>(UGameplayStatics::GetGameMode(this));
	if (!GM)
	{
		UE_LOG(LogTemp, Warning, TEXT("GM Casting failed"));
		return;
	}

	AAKStone* AIStone = GM->StonesT2[0];
	if (!IsValid(AIStone))
	{
		UE_LOG(LogTemp, Warning, TEXT("AI Stone is not valid"));
		return;
	}

	AAKStone* TargetStone = nullptr;
	float BestD2 = TNumericLimits<float>::Max();
	const FVector From = AIStone->GetActorLocation();
	for (AAKStone* PlayerStone : GM->StonesT1)
	{
		if (!IsValid(PlayerStone))
		{
			UE_LOG(LogTemp, Warning, TEXT("Player Stone is not valid"));
			continue;
		}

		const float D2 = FVector::DistSquared(From, PlayerStone->GetActorLocation());
		if (D2 < BestD2)
		{
			BestD2 = D2;
			TargetStone = PlayerStone;
		}
	}

	if (!TargetStone)
	{
		UE_LOG(LogTemp, Warning, TEXT("Target Stone is not valid"));
		return;
	}

	const FVector To = TargetStone->GetActorLocation();

	FVector Dir;
	TArray<AActor*> Ignore;
	Ignore.Add(AIStone);
	Ignore.Add(TargetStone);
	const ECollisionChannel TraceChannel = ECC_Visibility;

	if (!FindClearDirection_Line(this, From, To, TraceChannel, Ignore, Dir))
	{
		UE_LOG(LogTemp, Warning, TEXT("LineTrace blocked"));
		Dir = (To - From).GetSafeNormal();
	}

	const float Dist = FMath::Sqrt(BestD2);
	const float Power = FMath::Clamp(Dist * BasePowerPerMeter, MinPower, MaxPower);
	AIStone->Shoot(Dir, Power);
}

bool AAKAIController::IsLineClear(UObject* WorldCtx, const FVector& Start, const FVector& End, ECollisionChannel Channel, const TArray<AActor*>& ActorsToIgnore)
{
	FHitResult Hit;
	const bool bHit = UKismetSystemLibrary::LineTraceSingle(
		WorldCtx,
		Start,
		End,
		UEngineTypes::ConvertToTraceType(Channel),
		false,
		ActorsToIgnore,
		EDrawDebugTrace::None,
		Hit,
		true);
	return !bHit;
}

bool AAKAIController::FindClearDirection_Line(UObject* WorldCtx, const FVector& From, const FVector& To, ECollisionChannel Channel, const TArray<AActor*>& ActorsToIgnore, FVector& OutDir)
{
	const FVector BaseDir = (To - From).GetSafeNormal();
	if (BaseDir.IsNearlyZero())
	{
		return false;
	}

	static const float OffsetsDeg[] = { 0.f, 10.f, -10.f, 20.f, -20.f, 30.f, -30.f };
	const float RayLen = (To - From).Length();

	for (float Deg : OffsetsDeg)
	{
		const FRotator Rot(0.f, Deg, 0.f);
		const FVector TestDir = Rot.RotateVector(BaseDir);
		const FVector End = From + TestDir * RayLen;

		if (IsLineClear(WorldCtx, From, End, Channel, ActorsToIgnore))
		{
			OutDir = TestDir;
			return true;
		}
	}

	return false;
}

void AAKAIController::BeginPlay()
{
	Super::BeginPlay();

	TArray<AActor*> BoundsActors;
	UGameplayStatics::GetAllActorsWithTag(this, FName("BoardBounds"), BoundsActors);

	if (BoundsActors.Num() > 0 && IsValid(BoundsActors[0]))
	{
		FVector Origin, Extent;
		BoundsActors[0]->GetActorBounds(false, Origin, Extent);

		BoardMin = FVector2D(Origin.X - Extent.X, Origin.Y - Extent.Y);
		BoardMax = FVector2D(Origin.X + Extent.X, Origin.Y + Extent.Y);
		BoardCenter = FVector((BoardMin.X + BoardMax.X) * 0.5f, (BoardMin.Y + BoardMax.Y) * 0.5f, 0.f);

		UE_LOG(LogTemp, Warning, TEXT("[AI] Board AABB: Min(%.1f,%.1f) ~ Max(%.1f,%.1f)"),
			BoardMin.X, BoardMin.Y, BoardMax.X, BoardMax.Y);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[AI] BoardBounds actor with Tag 'BoardBounds' not found. Using defaults."));
	}
}

void AAKAIController::GenerateMyCandidates(const TArray<AAKStone*>& MyStones, const TArray<AAKStone*>& EnemyStones, TArray<FAKShot>& OutShots, float BasePowerPerMeter, float MinPower, float MaxPower) const
{
	OutShots.Reset();

	auto MakeFlatDir = [](const FVector& From, const FVector& To)
	{
		FVector d(To.X - From.X, To.Y - From.Y, 0.f);
		return d.GetSafeNormal();
	};

	for (AAKStone* Shooter : MyStones)
	{
		if (!IsValid(Shooter))
		{
			continue;
		}

		const FVector From = Shooter->GetActorLocation();
		TArray<AAKStone*> Targets;
		KClosestEnemies_PathAware(EnemyStones, From, 2, Shooter, ECC_Visibility, Targets);

		if (Targets.Num() == 0)
		{
			FAKShot S;
			S.Shooter = Shooter;
			S.Target = nullptr;
			S.Dir = MakeFlatDir(From, BoardCenter);

			const float Dist = FVector::Dist2D(From, BoardCenter);
			const float Scaled = Dist * BasePowerPerMeter;
			const float BoardRadius = FVector::Dist2D(BoardCenter, FVector(BoardMax, 0.f));
			const float ScaleK = FMath::GetMappedRangeValueClamped(FVector2D(0.f, BoardRadius), FVector2D(0.6f, 1.f), Dist);
			S.Power = FMath::Clamp(Scaled * ScaleK, MinPower, MaxPower);
			OutShots.Add(S);
			continue;
		}

		for (AAKStone* Target : Targets)
		{
			const FVector To = Target->GetActorLocation();
			const FVector Dir = MakeFlatDir(From, To);

			for (float PScale : { 0.7f, 1.0f, 1.3f })
			{
				FAKShot S;
				S.Shooter = Shooter;
				S.Target = Target;
				S.Dir = Dir;
				S.Power = FMath::Clamp(FVector::Dist(From, To) * BasePowerPerMeter * PScale, MinPower, MaxPower);
				OutShots.Add(S);
			}
		}
	}
}

FAKBoardState AAKAIController::BuildBoardState(const TArray<AAKStone*>& MyStones, const TArray<AAKStone*>& EnemyStones, bool bAITurn) const
{
	FAKBoardState State;
	State.bAITurn = bAITurn;
	State.Stones.Reserve(MyStones.Num() + EnemyStones.Num());

	for (AAKStone* Stone : MyStones)
	{
		if (!IsValid(Stone))
		{
			continue;
		}

		FAKStoneState StoneState;
		StoneState.SourceStone = Stone;
		StoneState.Pos = Stone->GetActorLocation();
		StoneState.bOut = IsOutOfBoardRect(StoneState.Pos);
		StoneState.bIsAI = true;
		State.Stones.Add(StoneState);
	}

	for (AAKStone* Stone : EnemyStones)
	{
		if (!IsValid(Stone))
		{
			continue;
		}

		FAKStoneState StoneState;
		StoneState.SourceStone = Stone;
		StoneState.Pos = Stone->GetActorLocation();
		StoneState.bOut = IsOutOfBoardRect(StoneState.Pos);
		StoneState.bIsAI = false;
		State.Stones.Add(StoneState);
	}

	return State;
}

void AAKAIController::GenerateCandidatesForState(const FAKBoardState& State, TArray<FAKShot>& OutShots, float BasePowerPerMeter, float MinPower, float MaxPower) const
{
	OutShots.Reset();

	auto MakeFlatDir = [](const FVector& From, const FVector& To)
	{
		FVector d(To.X - From.X, To.Y - From.Y, 0.f);
		return d.GetSafeNormal();
	};

	for (int32 ShooterIndex = 0; ShooterIndex < State.Stones.Num(); ++ShooterIndex)
	{
		const FAKStoneState& Shooter = State.Stones[ShooterIndex];
		if (Shooter.bOut || Shooter.bIsAI != State.bAITurn)
		{
			continue;
		}

		TArray<int32> TargetIndices;
		KClosestEnemies_StateAware(State, ShooterIndex, 2, TargetIndices);

		if (TargetIndices.Num() == 0)
		{
			FAKShot Shot;
			Shot.Shooter = Shooter.SourceStone.Get();
			Shot.ShooterIndex = ShooterIndex;
			Shot.Target = nullptr;
			Shot.TargetIndex = INDEX_NONE;
			Shot.Dir = MakeFlatDir(Shooter.Pos, BoardCenter);

			const float Dist = FVector::Dist2D(Shooter.Pos, BoardCenter);
			const float Scaled = Dist * BasePowerPerMeter;
			const float BoardRadius = FVector::Dist2D(BoardCenter, FVector(BoardMax, 0.f));
			const float ScaleK = FMath::GetMappedRangeValueClamped(FVector2D(0.f, BoardRadius), FVector2D(0.6f, 1.f), Dist);
			Shot.Power = FMath::Clamp(Scaled * ScaleK, MinPower, MaxPower);
			OutShots.Add(Shot);
			continue;
		}

		for (int32 TargetIndex : TargetIndices)
		{
			const FAKStoneState& Target = State.Stones[TargetIndex];
			const FVector Dir = MakeFlatDir(Shooter.Pos, Target.Pos);

			for (float PScale : { 0.7f, 1.0f, 1.3f })
			{
				FAKShot Shot;
				Shot.Shooter = Shooter.SourceStone.Get();
				Shot.Target = Target.SourceStone.Get();
				Shot.ShooterIndex = ShooterIndex;
				Shot.TargetIndex = TargetIndex;
				Shot.Dir = Dir;
				Shot.Power = FMath::Clamp(FVector::Dist2D(Shooter.Pos, Target.Pos) * BasePowerPerMeter * PScale, MinPower, MaxPower);
				OutShots.Add(Shot);
			}
		}
	}
}

void AAKAIController::DoAITurn_Strategic(float BasePowerPerMeter, float MinPower, float MaxPower)
{
	AAKSingleGameMode* GM = Cast<AAKSingleGameMode>(UGameplayStatics::GetGameMode(this));
	if (!GM)
	{
		return;
	}

	const TArray<AAKStone*>& My = GM->StonesT2;
	const TArray<AAKStone*>& Opp = GM->StonesT1;
	const FAKShot Best = PickBestShot_MinimaxD2(My, Opp, BasePowerPerMeter, MinPower, MaxPower, 6, 5);

	if (IsValid(Best.Shooter))
	{
		UE_LOG(LogTemp, Warning, TEXT("[AI:Minimax] dir=(%.2f,%.2f) power=%.1f score=%.1f shooter=%d target=%d"),
			Best.Dir.X, Best.Dir.Y, Best.Power, Best.Score, Best.ShooterIndex, Best.TargetIndex);
		Best.Shooter->Shoot(Best.Dir, Best.Power);
	}
}

bool AAKAIController::IsOutOfBoardRect(const FVector& P) const
{
	return (P.X < BoardMin.X || P.X > BoardMax.X || P.Y < BoardMin.Y || P.Y > BoardMax.Y);
}

float AAKAIController::EdgeMarginRect(const FVector& P) const
{
	const float DxMin = P.X - BoardMin.X;
	const float DxMax = BoardMax.X - P.X;
	const float DyMin = P.Y - BoardMin.Y;
	const float DyMax = BoardMax.Y - P.Y;
	return FMath::Min(FMath::Min(DxMin, DxMax), FMath::Min(DyMin, DyMax));
}

FVector2D AAKAIController::OutwardEdgeNormalAt(const FVector& P) const
{
	const float DLeft = P.X - BoardMin.X;
	const float DRight = BoardMax.X - P.X;
	const float DBottom = P.Y - BoardMin.Y;
	const float DTop = BoardMax.Y - P.Y;

	float MinDist = DLeft;
	FVector2D Normal(-1.f, 0.f);
	if (DRight < MinDist) { MinDist = DRight; Normal = FVector2D(1.f, 0.f); }
	if (DBottom < MinDist) { MinDist = DBottom; Normal = FVector2D(0.f, -1.f); }
	if (DTop < MinDist) { Normal = FVector2D(0.f, 1.f); }
	return Normal;
}

float AAKAIController::PredictStopDistance(float Speed0, float DampingEff) const
{
	if (Speed0 <= KINDA_SMALL_NUMBER)
	{
		return 0.f;
	}

	const float K = FMath::Max(DampingEff, 0.1f);
	return (Speed0 * Speed0) / (2.f * K);
}

bool AAKAIController::SegmentHitsDisc2D(const FVector& From, const FVector& Dir, float Range, const FVector& Center, float Radius) const
{
	const FVector A(From.X, From.Y, 0.f);
	const FVector B(From.X + Dir.X * Range, From.Y + Dir.Y * Range, 0.f);
	const FVector C(Center.X, Center.Y, 0.f);

	const FVector AB = B - A;
	const float AB2 = FMath::Max(AB.SizeSquared(), 1e-3f);
	const float T = FMath::Clamp(FVector::DotProduct(C - A, AB) / AB2, 0.f, 1.f);
	const FVector Closest = A + AB * T;
	return FVector::Dist(Closest, C) <= Radius;
}

float AAKAIController::AvailableTravelUntilHit(const FVector& Start, const FVector& Dir, float IntendedDist, ECollisionChannel Channel, const TArray<AActor*>& ActorsToIgnore) const
{
	const FVector End = Start + Dir * IntendedDist;

	FHitResult Hit;
	const bool bHit = UKismetSystemLibrary::LineTraceSingle(
		(UObject*)this,
		Start,
		End,
		UEngineTypes::ConvertToTraceType(Channel),
		false,
		ActorsToIgnore,
		EDrawDebugTrace::None,
		Hit,
		true);

	if (!bHit)
	{
		return IntendedDist;
	}

	return FVector::Dist(Start, Hit.ImpactPoint);
}

bool AAKAIController::ReflectOnce(const FHitResult& Hit, const FVector& InDir, FVector& OutReflectedDir) const
{
	const FVector N = Hit.ImpactNormal.GetSafeNormal();
	if (N.IsNearlyZero())
	{
		return false;
	}

	OutReflectedDir = InDir.GetSafeNormal().MirrorByVector(N).GetSafeNormal();
	return !OutReflectedDir.IsNearlyZero();
}

FStoneSimResult AAKAIController::SimulateShotApprox(const FVector& ShooterStart, float ShooterMass, float ShooterDamping, const FVector& TargetStart, float TargetMass, float TargetDamping, const FVector& Dir, float Power, float ShooterRadius, float TargetRadius, float Restitution, ECollisionChannel TraceChannel, bool bEnableOneBounce, const TArray<AActor*>* ActorsToIgnore) const
{
	FStoneSimResult Result;
	Result.FinalPos = ShooterStart;
	Result.TargetFinalPos = TargetStart;

	const FVector NDir = Dir.GetSafeNormal();
	if (NDir.IsNearlyZero() || ShooterMass <= 0.f)
	{
		return Result;
	}

	const FVector V0 = (Power / ShooterMass) * NDir;
	const float Speed0 = V0.Size();
	if (Speed0 <= KINDA_SMALL_NUMBER)
	{
		return Result;
	}

	TArray<AActor*> IgnoreActors;
	if (ActorsToIgnore)
	{
		IgnoreActors = *ActorsToIgnore;
	}

	const float PreStopDist = PredictStopDistance(Speed0, ShooterDamping);
	const float HitRadius = ShooterRadius + TargetRadius;
	const bool bHitTarget = SegmentHitsDisc2D(ShooterStart, NDir, PreStopDist, TargetStart, HitRadius);

	if (!bHitTarget)
	{
		const float SAvail = AvailableTravelUntilHit(ShooterStart, NDir, PreStopDist, TraceChannel, IgnoreActors);
		if (bEnableOneBounce && SAvail + 1.f < PreStopDist)
		{
			FHitResult Hit;
			UKismetSystemLibrary::LineTraceSingle(
				(UObject*)this,
				ShooterStart,
				ShooterStart + NDir * PreStopDist,
				UEngineTypes::ConvertToTraceType(TraceChannel),
				false,
				IgnoreActors,
				EDrawDebugTrace::None,
				Hit,
				true);

			FVector RDir;
			if (ReflectOnce(Hit, NDir, RDir))
			{
				const float Left = PreStopDist - FVector::Dist(ShooterStart, Hit.ImpactPoint);
				const float LeftClamped = AvailableTravelUntilHit(Hit.ImpactPoint + RDir, RDir, Left, TraceChannel, IgnoreActors);
				Result.FinalPos = Hit.ImpactPoint + RDir * LeftClamped;
			}
			else
			{
				Result.FinalPos = ShooterStart + NDir * SAvail;
			}
		}
		else
		{
			Result.FinalPos = ShooterStart + NDir * SAvail;
		}

		Result.bShooterOut = IsOutOfBoardRect(Result.FinalPos);
		Result.bTargetOut = IsOutOfBoardRect(TargetStart);
		return Result;
	}

	const FVector ST = TargetStart - ShooterStart;
	const float H = FVector::DotProduct(ST, NDir);
	const float TravelToHit = FMath::Clamp(H - HitRadius, 0.f, PreStopDist);
	const FVector HitPoint = ShooterStart + NDir * TravelToHit;

	const float M1 = ShooterMass;
	const float M2 = TargetMass;
	const float E = FMath::Clamp(Restitution, 0.f, 1.f);
	const FVector V1Before = V0;
	const FVector V2Before = FVector::ZeroVector;

	FVector N = TargetStart - HitPoint;
	N.Z = 0.f;
	N = N.GetSafeNormal();
	if (N.IsNearlyZero())
	{
		N = NDir;
	}

	const float V1N = FVector::DotProduct(V1Before, N);
	const float V2N = FVector::DotProduct(V2Before, N);
	const float V1NAfter = ((M1 - E * M2) * V1N + (1 + E) * M2 * V2N) / (M1 + M2);
	const float V2NAfter = ((M2 - E * M1) * V2N + (1 + E) * M1 * V1N) / (M1 + M2);

	const FVector V1T = V1Before - V1N * N;
	const FVector V2T = V2Before - V2N * N;
	const FVector V1After = V1T + V1NAfter * N;
	const FVector V2After = V2T + V2NAfter * N;

	const float S1 = PredictStopDistance(V1After.Size(), ShooterDamping);
	const float S2 = PredictStopDistance(V2After.Size(), TargetDamping);
	const FVector D1 = V1After.GetSafeNormal();
	const FVector D2 = V2After.GetSafeNormal();

	float S1Avail = AvailableTravelUntilHit(HitPoint, D1, S1, TraceChannel, IgnoreActors);
	if (bEnableOneBounce && S1Avail + 1.f < S1)
	{
		FHitResult HitObs;
		UKismetSystemLibrary::LineTraceSingle(
			(UObject*)this,
			HitPoint,
			HitPoint + D1 * S1,
			UEngineTypes::ConvertToTraceType(TraceChannel),
			false,
			IgnoreActors,
			EDrawDebugTrace::None,
			HitObs,
			true);

		FVector RDir;
		if (ReflectOnce(HitObs, D1, RDir))
		{
			const float Left = S1 - FVector::Dist(HitPoint, HitObs.ImpactPoint);
			const float LeftClamped = AvailableTravelUntilHit(HitObs.ImpactPoint + RDir, RDir, Left, TraceChannel, IgnoreActors);
			Result.FinalPos = HitObs.ImpactPoint + RDir * LeftClamped;
		}
		else
		{
			Result.FinalPos = HitPoint + D1 * S1Avail;
		}
	}
	else
	{
		Result.FinalPos = HitPoint + D1 * S1Avail;
	}

	float S2Avail = AvailableTravelUntilHit(TargetStart, D2, S2, TraceChannel, IgnoreActors);
	if (bEnableOneBounce && S2Avail + 1.f < S2)
	{
		FHitResult HitObs;
		UKismetSystemLibrary::LineTraceSingle(
			(UObject*)this,
			TargetStart,
			TargetStart + D2 * S2,
			UEngineTypes::ConvertToTraceType(TraceChannel),
			false,
			IgnoreActors,
			EDrawDebugTrace::None,
			HitObs,
			true);

		FVector RDir;
		if (ReflectOnce(HitObs, D2, RDir))
		{
			const float Left = S2 - FVector::Dist(TargetStart, HitObs.ImpactPoint);
			const float LeftClamped = AvailableTravelUntilHit(HitObs.ImpactPoint + RDir, RDir, Left, TraceChannel, IgnoreActors);
			Result.TargetFinalPos = HitObs.ImpactPoint + RDir * LeftClamped;
		}
		else
		{
			Result.TargetFinalPos = TargetStart + D2 * S2Avail;
		}
	}
	else
	{
		Result.TargetFinalPos = TargetStart + D2 * S2Avail;
	}

	Result.bShooterOut = IsOutOfBoardRect(Result.FinalPos);
	Result.bTargetOut = IsOutOfBoardRect(Result.TargetFinalPos);
	return Result;
}

float AAKAIController::EvaluateShotHeuristic(const FAKShot& Shot) const
{
	if (!Shot.Shooter)
	{
		return -1e9f;
	}

	const FVector ShooterStart = Shot.Shooter->GetActorLocation();
	const FVector TargetStart = Shot.Target ? Shot.Target->GetActorLocation() : ShooterStart;

	FVector FlatDir = Shot.Dir;
	FlatDir.Z = 0.f;
	FlatDir.Normalize();

	TArray<AActor*> Ignore;
	if (Shot.Shooter) Ignore.Add(Shot.Shooter);
	if (Shot.Target) Ignore.Add(Shot.Target);

	const FStoneSimResult Sim = SimulateShotApprox(
		ShooterStart, MassStone, LinDamp,
		TargetStart, MassStone, LinDamp,
		FlatDir, Shot.Power,
		StoneRadius, StoneRadius,
		RestCoef,
		ECC_Visibility,
		true,
		&Ignore);

	float Score = 0.f;
	if (Shot.Target && Sim.bTargetOut) Score += 100.f;
	if (Sim.bShooterOut) Score -= 80.f;

	if (Shot.Target)
	{
		const float DBefore = FVector::Dist2D(ShooterStart, TargetStart);
		const float DAfter = FVector::Dist2D(Sim.FinalPos, TargetStart);
		Score += (DBefore - DAfter) * 0.05f;
	}

	Score += EdgeMarginRect(Sim.FinalPos) * 0.01f;

	if (Shot.Target)
	{
		const FVector2D NOut = OutwardEdgeNormalAt(TargetStart);
		const FVector2D Shot2(FlatDir.X, FlatDir.Y);
		const float AlignOut = FMath::Max(0.f, FVector2D::DotProduct(NOut.GetSafeNormal(), Shot2.GetSafeNormal()));
		const float ClearFrac = OutwardClearFraction(TargetStart, NOut, ECC_Visibility, Ignore);
		const float OutPotential = AlignOut * ClearFrac;
		Score += 100.f * OutPotential;
		if (AlignOut > 0.5f && ClearFrac < 0.3f)
		{
			Score -= 15.f;
		}
	}

	return Score;
}

float AAKAIController::EvaluateShotForSide(const FAKShot& Shot, bool bShooterIsAI) const
{
	const float S = EvaluateShotHeuristic(Shot);
	return bShooterIsAI ? S : -S;
}

FAKBoardState AAKAIController::ApplyShotToState(const FAKBoardState& State, const FAKShot& Shot) const
{
	FAKBoardState Next = State;
	Next.bAITurn = !State.bAITurn;

	if (!Next.Stones.IsValidIndex(Shot.ShooterIndex))
	{
		return Next;
	}

	FAKStoneState& ShooterState = Next.Stones[Shot.ShooterIndex];
	if (ShooterState.bOut)
	{
		return Next;
	}

	TArray<AActor*> IgnoreActors;
	for (const FAKStoneState& StoneState : State.Stones)
	{
		if (AAKStone* Actor = StoneState.SourceStone.Get())
		{
			IgnoreActors.Add(Actor);
		}
	}

	FVector FlatDir = Shot.Dir;
	FlatDir.Z = 0.f;
	FlatDir = FlatDir.GetSafeNormal();
	if (FlatDir.IsNearlyZero())
	{
		return Next;
	}

	if (Next.Stones.IsValidIndex(Shot.TargetIndex) && !Next.Stones[Shot.TargetIndex].bOut)
	{
		FAKStoneState& TargetState = Next.Stones[Shot.TargetIndex];
		const FStoneSimResult Sim = SimulateShotApprox(
			ShooterState.Pos, MassStone, LinDamp,
			TargetState.Pos, MassStone, LinDamp,
			FlatDir, Shot.Power,
			StoneRadius, StoneRadius,
			RestCoef,
			ECC_Visibility,
			true,
			&IgnoreActors);

		ShooterState.Pos = Sim.FinalPos;
		ShooterState.bOut = ShooterState.bOut || Sim.bShooterOut;
		TargetState.Pos = Sim.TargetFinalPos;
		TargetState.bOut = TargetState.bOut || Sim.bTargetOut;
		return Next;
	}

	const float Speed0 = Shot.Power / FMath::Max(MassStone, 1.f);
	const float IntendedDist = PredictStopDistance(Speed0, LinDamp);
	const float MoveDist = AvailableTravelUntilHit(ShooterState.Pos, FlatDir, IntendedDist, ECC_Visibility, IgnoreActors);
	ShooterState.Pos += FlatDir * MoveDist;
	ShooterState.bOut = ShooterState.bOut || IsOutOfBoardRect(ShooterState.Pos);
	return Next;
}

float AAKAIController::EvaluateBoardState(const FAKBoardState& State) const
{
	int32 AIAlive = 0;
	int32 PlayerAlive = 0;
	float Score = 0.f;

	for (const FAKStoneState& Stone : State.Stones)
	{
		if (Stone.bOut)
		{
			continue;
		}

		const float EdgeMargin = EdgeMarginRect(Stone.Pos);
		const float EdgeRisk = FMath::Clamp(140.f - EdgeMargin, 0.f, 140.f) * 0.25f;
		const float CenterDist = FVector::Dist2D(Stone.Pos, BoardCenter);
		const float CenterBias = FMath::Clamp((500.f - CenterDist) * 0.02f, -10.f, 10.f);

		if (Stone.bIsAI)
		{
			++AIAlive;
			Score += 120.f;
			Score += FMath::Clamp(EdgeMargin, -200.f, 300.f) * 0.04f;
			Score += CenterBias;
			Score -= EdgeRisk;
		}
		else
		{
			++PlayerAlive;
			Score -= 120.f;
			Score -= FMath::Clamp(EdgeMargin, -200.f, 300.f) * 0.04f;
			Score -= CenterBias;
			Score += EdgeRisk;
		}
	}

	if (PlayerAlive == 0)
	{
		return 100000.f;
	}

	if (AIAlive == 0)
	{
		return -100000.f;
	}

	Score += (AIAlive - PlayerAlive) * 200.f;
	return Score;
}

float AAKAIController::EvaluateActionImmediate(const FAKBoardState& State, const FAKShot& Shot) const
{
	return EvaluateBoardState(ApplyShotToState(State, Shot));
}

bool AAKAIController::IsTerminalState(const FAKBoardState& State) const
{
	bool bHasAI = false;
	bool bHasPlayer = false;

	for (const FAKStoneState& Stone : State.Stones)
	{
		if (Stone.bOut)
		{
			continue;
		}

		if (Stone.bIsAI)
		{
			bHasAI = true;
		}
		else
		{
			bHasPlayer = true;
		}
	}

	return !bHasAI || !bHasPlayer;
}

float AAKAIController::MinimaxScore(const FAKBoardState& State, int32 Depth, float Alpha, float Beta, float BasePowerPerMeter, float MinPower, float MaxPower, int32 BeamWidth) const
{
	if (Depth == 0 || IsTerminalState(State))
	{
		return EvaluateBoardState(State);
	}

	TArray<FAKShot> Candidates;
	GenerateCandidatesForState(State, Candidates, BasePowerPerMeter, MinPower, MaxPower);
	if (Candidates.Num() == 0)
	{
		return EvaluateBoardState(State);
	}

	for (FAKShot& Candidate : Candidates)
	{
		Candidate.Score = EvaluateActionImmediate(State, Candidate);
	}

	Candidates.Sort([bMaximizing = State.bAITurn](const FAKShot& A, const FAKShot& B)
	{
		return bMaximizing ? (A.Score > B.Score) : (A.Score < B.Score);
	});

	if (Candidates.Num() > BeamWidth)
	{
		Candidates.SetNum(BeamWidth);
	}

	if (State.bAITurn)
	{
		float Value = -FLT_MAX;
		for (const FAKShot& Candidate : Candidates)
		{
			const FAKBoardState Next = ApplyShotToState(State, Candidate);
			Value = FMath::Max(Value, MinimaxScore(Next, Depth - 1, Alpha, Beta, BasePowerPerMeter, MinPower, MaxPower, BeamWidth));
			Alpha = FMath::Max(Alpha, Value);
			if (Alpha >= Beta)
			{
				break;
			}
		}
		return Value;
	}

	float Value = FLT_MAX;
	for (const FAKShot& Candidate : Candidates)
	{
		const FAKBoardState Next = ApplyShotToState(State, Candidate);
		Value = FMath::Min(Value, MinimaxScore(Next, Depth - 1, Alpha, Beta, BasePowerPerMeter, MinPower, MaxPower, BeamWidth));
		Beta = FMath::Min(Beta, Value);
		if (Alpha >= Beta)
		{
			break;
		}
	}
	return Value;
}

FAKShot AAKAIController::PickBestShot_MinimaxD2(const TArray<AAKStone*>& MyStones, const TArray<AAKStone*>& EnemyStones, float BasePowerPerMeter, float MinPower, float MaxPower, int32 BeamMy, int32 BeamOpp) const
{
	const FAKBoardState Root = BuildBoardState(MyStones, EnemyStones, true);

	TArray<FAKShot> Candidates;
	GenerateCandidatesForState(Root, Candidates, BasePowerPerMeter, MinPower, MaxPower);
	if (Candidates.Num() == 0)
	{
		return FAKShot{};
	}

	for (FAKShot& Candidate : Candidates)
	{
		Candidate.Score = EvaluateActionImmediate(Root, Candidate);
	}

	Candidates.Sort([](const FAKShot& A, const FAKShot& B)
	{
		return A.Score > B.Score;
	});

	if (Candidates.Num() > BeamMy)
	{
		Candidates.SetNum(BeamMy);
	}

	float Alpha = -FLT_MAX;
	float Beta = FLT_MAX;
	FAKShot Best = Candidates[0];
	float BestValue = -FLT_MAX;

	for (FAKShot& Candidate : Candidates)
	{
		const FAKBoardState Next = ApplyShotToState(Root, Candidate);
		const float Value = MinimaxScore(Next, 1, Alpha, Beta, BasePowerPerMeter, MinPower, MaxPower, BeamOpp);
		Candidate.Score = Value;
		if (Value > BestValue)
		{
			BestValue = Value;
			Best = Candidate;
		}
		Alpha = FMath::Max(Alpha, BestValue);
	}

	Best.Score = BestValue;
	return Best;
}

float AAKAIController::DistanceToEdgeAlongNormal(const FVector& P, const FVector2D& NOut) const
{
	if (FMath::Abs(NOut.X) > 0.5f)
	{
		return (NOut.X > 0.f) ? (BoardMax.X - P.X) : (P.X - BoardMin.X);
	}

	return (NOut.Y > 0.f) ? (BoardMax.Y - P.Y) : (P.Y - BoardMin.Y);
}

float AAKAIController::OutwardClearFraction(const FVector& Start, const FVector2D& NOut, ECollisionChannel Channel, const TArray<AActor*>& ActorsToIgnore) const
{
	const float ToEdge = DistanceToEdgeAlongNormal(Start, NOut);
	if (ToEdge <= 1.f)
	{
		return 0.f;
	}

	const FVector Dir3(NOut.X, NOut.Y, 0.f);
	const FVector End = Start + Dir3 * ToEdge;

	FHitResult Hit;
	const bool bHit = UKismetSystemLibrary::LineTraceSingle(
		(UObject*)this,
		Start,
		End,
		UEngineTypes::ConvertToTraceType(Channel),
		false,
		ActorsToIgnore,
		EDrawDebugTrace::None,
		Hit,
		true);

	if (!bHit)
	{
		return 1.f;
	}

	const float ClearDist = FVector::Dist(Start, Hit.ImpactPoint);
	return FMath::Clamp(ClearDist / ToEdge, 0.f, 1.f);
}

bool AAKAIController::SegmentHitsStone2D(const FVector& From, const FVector& To, const FVector& Center, float Radius) const
{
	const FVector Segment = To - From;
	const float SegmentLenSq = FMath::Max(FVector::DotProduct(Segment, Segment), 1e-3f);
	const float T = FMath::Clamp(FVector::DotProduct(Center - From, Segment) / SegmentLenSq, 0.f, 1.f);
	const FVector Closest = From + Segment * T;
	return FVector::Dist2D(Closest, Center) <= Radius;
}

float AAKAIController::OutwardClearFractionForState(const FAKBoardState& State, int32 StoneIndex, const FVector2D& NOut) const
{
	if (!State.Stones.IsValidIndex(StoneIndex) || State.Stones[StoneIndex].bOut)
	{
		return 0.f;
	}

	const FVector Start = State.Stones[StoneIndex].Pos;
	const float ToEdge = DistanceToEdgeAlongNormal(Start, NOut);
	if (ToEdge <= 1.f)
	{
		return 0.f;
	}

	const FVector2D Start2(Start.X, Start.Y);
	const FVector2D Dir = NOut.GetSafeNormal();
	float BestHit = ToEdge;
	const float Radius = StoneRadius * 2.f;

	for (int32 Index = 0; Index < State.Stones.Num(); ++Index)
	{
		if (Index == StoneIndex || State.Stones[Index].bOut)
		{
			continue;
		}

		const FVector2D Center(State.Stones[Index].Pos.X, State.Stones[Index].Pos.Y);
		const FVector2D M = Start2 - Center;
		const float B = FVector2D::DotProduct(M, Dir);
		const float C = FVector2D::DotProduct(M, M) - Radius * Radius;
		const float Disc = B * B - C;
		if (Disc < 0.f)
		{
			continue;
		}

		const float T = -B - FMath::Sqrt(Disc);
		if (T >= 0.f && T < BestHit)
		{
			BestHit = T;
		}
	}

	return FMath::Clamp(BestHit / ToEdge, 0.f, 1.f);
}

void AAKAIController::KClosestEnemies_StateAware(const FAKBoardState& State, int32 ShooterIndex, int32 K, TArray<int32>& OutTargetIndices) const
{
	struct FCandidate
	{
		int32 StoneIndex = INDEX_NONE;
		float DistSq = 0.f;
		bool bClear = false;
	};

	OutTargetIndices.Reset();
	if (!State.Stones.IsValidIndex(ShooterIndex) || State.Stones[ShooterIndex].bOut)
	{
		return;
	}

	const FAKStoneState& Shooter = State.Stones[ShooterIndex];
	TArray<FCandidate> Candidates;
	Candidates.Reserve(State.Stones.Num());

	for (int32 TargetIndex = 0; TargetIndex < State.Stones.Num(); ++TargetIndex)
	{
		if (TargetIndex == ShooterIndex)
		{
			continue;
		}

		const FAKStoneState& Target = State.Stones[TargetIndex];
		if (Target.bOut || Target.bIsAI == Shooter.bIsAI)
		{
			continue;
		}

		FCandidate Candidate;
		Candidate.StoneIndex = TargetIndex;
		Candidate.DistSq = FVector::DistSquared2D(Shooter.Pos, Target.Pos);
		Candidate.bClear = true;

		for (int32 BlockerIndex = 0; BlockerIndex < State.Stones.Num(); ++BlockerIndex)
		{
			if (BlockerIndex == ShooterIndex || BlockerIndex == TargetIndex || State.Stones[BlockerIndex].bOut)
			{
				continue;
			}

			if (SegmentHitsStone2D(Shooter.Pos, Target.Pos, State.Stones[BlockerIndex].Pos, StoneRadius * 2.f))
			{
				Candidate.bClear = false;
				break;
			}
		}

		Candidates.Add(Candidate);
	}

	Candidates.Sort([](const FCandidate& A, const FCandidate& B)
	{
		if (A.bClear != B.bClear)
		{
			return A.bClear > B.bClear;
		}
		return A.DistSq < B.DistSq;
	});

	for (const FCandidate& Candidate : Candidates)
	{
		if (OutTargetIndices.Num() >= K)
		{
			break;
		}
		if (Candidate.bClear)
		{
			OutTargetIndices.Add(Candidate.StoneIndex);
		}
	}
}

void AAKAIController::KClosestEnemies_PathAware(const TArray<class AAKStone*>& Enemies, const FVector& From, int32 K, AActor* ShooterToIgnore, ECollisionChannel Channel, TArray<class AAKStone*>& Out) const
{
	struct FCandidate
	{
		AAKStone* Enemy = nullptr;
		float D2 = 0.f;
		bool bClear = false;
	};

	TArray<FCandidate> Candidates;
	Candidates.Reserve(Enemies.Num());

	for (AAKStone* Enemy : Enemies)
	{
		if (!IsValid(Enemy))
		{
			continue;
		}

		FCandidate Candidate;
		Candidate.Enemy = Enemy;
		Candidate.D2 = FVector::DistSquared(From, Enemy->GetActorLocation());

		TArray<AActor*> Ignore;
		if (ShooterToIgnore) Ignore.Add(ShooterToIgnore);
		Ignore.Add(Enemy);

		Candidate.bClear = IsLineClear((UObject*)this, From, Enemy->GetActorLocation(), Channel, Ignore);
		Candidates.Add(Candidate);
	}

	Candidates.Sort([](const FCandidate& A, const FCandidate& B)
	{
		if (A.bClear != B.bClear)
		{
			return A.bClear > B.bClear;
		}
		return A.D2 < B.D2;
	});

	Out.Reset();
	for (const FCandidate& Candidate : Candidates)
	{
		if (Out.Num() >= K)
		{
			break;
		}
		if (Candidate.bClear)
		{
			Out.Add(Candidate.Enemy);
		}
	}
}