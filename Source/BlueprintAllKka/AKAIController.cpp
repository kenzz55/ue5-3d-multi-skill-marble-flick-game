// Fill out your copyright notice in the Description page of Project Settings.


#include "AKAIController.h"
#include "AKSingleGameMode.h"
#include "Kismet/GameplayStatics.h"
#include "AKStone.h"
#include "Kismet/KismetSystemLibrary.h"

// 프로토타입1 버전
void AAKAIController::DoAITurn(float BasePowerPerMeter, float MinPower, float MaxPower)
{
	AAKSingleGameMode* GM = Cast<AAKSingleGameMode>(UGameplayStatics::GetGameMode(this));
	if (!GM) {
		UE_LOG(LogTemp, Warning, TEXT("GM Casting failed"));
		return;
	}

	//우선 리스트에 있는 0번째 돌로만 테스트
	AAKStone* AIStone = GM->StonesT2[0];
	if (!IsValid(AIStone)) 
	{
		UE_LOG(LogTemp, Warning, TEXT("AI Stone is not valid"));
		return;
	}



	// AI stone 과 player stone 의 거리가 가장 가까운 player stone 찾기
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
	TArray<AActor*> Ignore; Ignore.Add(AIStone); Ignore.Add(TargetStone);
	const ECollisionChannel TraceChannel = ECC_Visibility;

	if (!FindClearDirection_Line(this, From, To, TraceChannel, Ignore, Dir))
	{
		// 전부 막혔다면: 그냥 직선
		UE_LOG(LogTemp, Warning, TEXT("LineTrace blocked"));
		Dir = (To - From).GetSafeNormal();
	}

	//파워 계산: 거리 비례 + 최소/최대 클램프
	const float Dist = FMath::Sqrt(BestD2);
	float Power = FMath::Clamp(Dist * BasePowerPerMeter, MinPower, MaxPower);

	//발사
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
		/*bTraceComplex=*/false,
		ActorsToIgnore,
		EDrawDebugTrace::None, //디버그 표시
		Hit,
		/*bIgnoreSelf=*/true
	);
	return !bHit; // 맞은 게 없으면 -> 길이 뚫려있다
}

bool AAKAIController::FindClearDirection_Line(UObject* WorldCtx, const FVector& From, const FVector& To, ECollisionChannel Channel, const TArray<AActor*>& ActorsToIgnore, FVector& OutDir)
{
	//직선벡터 및 정규화
	const FVector BaseDir = (To - From).GetSafeNormal();
	if (BaseDir.IsNearlyZero()) return false; // 겹쳐있으면 방향 무의미

	// 시도할 Offset 각
	static const float OffsetsDeg[] = { 0.f, 10.f, -10.f, 20.f, -20.f, 30.f, -30.f };

	//검사 거리
	const float RayLen = (To - From).Length();

	// 각도별 테스트, 직선 먼저
	for (float Deg : OffsetsDeg)
	{
		const FRotator Rot(0.f, Deg, 0.f);         //회전값        
		const FVector TestDir = Rot.RotateVector(BaseDir); // 직선 방향 좌우로 돌림 
		const FVector End = From + TestDir * RayLen;

		if (IsLineClear(WorldCtx, From, End, Channel, ActorsToIgnore))
		{
			OutDir = TestDir;
			return true; // 가장 먼저 통과한 방향 채택
		}
	}
	return false; // 전부 막힘
}

void AAKAIController::BeginPlay()
{
	Super::BeginPlay();


	TArray<AActor*> BoundsActors;
	UGameplayStatics::GetAllActorsWithTag(this, FName("BoardBounds"), BoundsActors);

	if (BoundsActors.Num() > 0 && IsValid(BoundsActors[0]))
	{
		FVector Origin, Extent;
		BoundsActors[0]->GetActorBounds(/*bOnlyColliding=*/false, Origin, Extent);

		BoardMin = FVector2D(Origin.X - Extent.X, Origin.Y - Extent.Y);
		BoardMax = FVector2D(Origin.X + Extent.X, Origin.Y + Extent.Y);

		BoardCenter = FVector((BoardMin.X + BoardMax.X) * 0.5f,
			(BoardMin.Y + BoardMax.Y) * 0.5f,
			0.f);

		UE_LOG(LogTemp, Warning, TEXT("[AI] Board AABB: Min(%.1f,%.1f) ~ Max(%.1f,%.1f)"),
			BoardMin.X, BoardMin.Y, BoardMax.X, BoardMax.Y);
		UE_LOG(LogTemp, Warning, TEXT("[AI] origin x : %.1f, y : %.1f, extent x : %.1f, y : %.1f"),
			Origin.X, Origin.Y, Extent.X, Extent.Y);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[AI] BoardBounds actor with Tag 'BoardBounds' not found. Using defaults."));
	}
}

void AAKAIController::GenerateMyCandidates(const TArray<AAKStone*>& MyStones, const TArray<AAKStone*>& EnemyStones, TArray<FAKShot>& OutShots, float BasePowerPerMeter, float MinPower, float MaxPower) const
{
	OutShots.Reset();

	auto MakeFlatDir = [](const FVector& From, const FVector& To) {
		FVector d(To.X - From.X, To.Y - From.Y, 0.f); // Z 제거
		return d.GetSafeNormal();
	};

	for (AAKStone* Shooter : MyStones)
	{
		if (!IsValid(Shooter)) continue;
		const FVector From = Shooter->GetActorLocation();

		TArray<AAKStone*> Targets;
		KClosestEnemies_PathAware(EnemyStones, From, 2, Shooter, ECC_Visibility, Targets);

		// Fallback: 중앙 쏘기
		if (Targets.Num() == 0)
		{
			FAKShot S;
			S.Shooter = Shooter;
			S.Target = nullptr;
			S.Dir = MakeFlatDir(From, BoardCenter);
			const float dist = FVector::Dist2D(From, BoardCenter);
			const float scaled = dist * BasePowerPerMeter;

			const float boardRadius = FVector::Dist2D(BoardCenter, FVector(BoardMax, 0.f));
			const float scaleK = FMath::GetMappedRangeValueClamped(
				FVector2D(0.f, boardRadius),  // 거리 기준 
				FVector2D(0.6f, 1.f),    // 가까우면 40%, 멀면 100%
				dist
			);

			S.Power = FMath::Clamp(scaled * scaleK, MinPower,  MaxPower);

			OutShots.Add(S);
			continue;
		}

		// 타겟이 있으면 파워 샘플링
		for (AAKStone* Target : Targets)
		{
			const FVector To = Target->GetActorLocation();
			const FVector Dir = MakeFlatDir(From,To);

			for (float PScale : {0.7f, 1.0f, 1.3f})
			{
				FAKShot S;
				S.Shooter = Shooter;
				S.Target = Target;
				S.Dir = Dir;
				S.Power = FMath::Clamp(FVector::Dist(From, To) * BasePowerPerMeter * PScale, MinPower, MaxPower);

				OutShots.Add(S); // 점수는 아직 없음
			}
		}
	}
}

void AAKAIController::DoAITurn_Strategic(float BasePowerPerMeter, float MinPower, float MaxPower)
{
	AAKSingleGameMode* GM = Cast<AAKSingleGameMode>(UGameplayStatics::GetGameMode(this));
	if (!GM) return;

	const TArray<AAKStone*>& My = GM->StonesT2;
	const TArray<AAKStone*>& Opp = GM->StonesT1;

	const FAKShot best = PickBestShot_MinimaxD2(My, Opp, BasePowerPerMeter, MinPower, MaxPower,
		/*BeamMy=*/5, /*BeamOpp=*/5);

	if (IsValid(best.Shooter))
	{
		UE_LOG(LogTemp, Warning, TEXT("[AI:D2] dir=(%.2f,%.2f) power=%.1f score=%.1f"),
			best.Dir.X, best.Dir.Y, best.Power, best.Score);
		best.Shooter->Shoot(best.Dir, best.Power);
	}
}

// 돌이 보드 밖으로 나갔는지 아웃 판정
bool AAKAIController::IsOutOfBoardRect(const FVector& P) const
{
	return (P.X < BoardMin.X || P.X > BoardMax.X || P.Y < BoardMin.Y || P.Y > BoardMax.Y);
}

float AAKAIController::EdgeMarginRect(const FVector& P) const
{
	float dxMin = P.X - BoardMin.X;
	float dxMax = BoardMax.X - P.X;
	float dyMin = P.Y - BoardMin.Y;
	float dyMax = BoardMax.Y - P.Y;
	return FMath::Min(FMath::Min(dxMin, dxMax), FMath::Min(dyMin, dyMax));
}

FVector2D AAKAIController::OutwardEdgeNormalAt(const FVector& P) const
{
	const float dLeft = P.X - BoardMin.X;
	const float dRight = BoardMax.X - P.X;
	const float dBottom = P.Y - BoardMin.Y;
	const float dTop = BoardMax.Y - P.Y;

	float m = dLeft;      
	FVector2D n(-1.f, 0.f);
	if (dRight < m) { m = dRight;  n = FVector2D(1.f, 0.f); }
	if (dBottom < m) { m = dBottom; n = FVector2D(0.f, -1.f); }
	if (dTop < m) { m = dTop;    n = FVector2D(0.f, 1.f); }
	return n;
}

float AAKAIController::PredictStopDistance(float Speed0, float DampingEff) const
{
	if (Speed0 <= KINDA_SMALL_NUMBER) return 0.f;
	const float k = FMath::Max(DampingEff, 0.1f);
	return (Speed0 * Speed0) / (2.f * k);
}

bool AAKAIController::SegmentHitsDisc2D(const FVector& From, const FVector& Dir, float Range, const FVector& Center, float Radius) const
{
	const FVector A(From.X, From.Y, 0.f);
	const FVector B(From.X + Dir.X * Range, From.Y + Dir.Y * Range, 0.f);
	const FVector C(Center.X, Center.Y, 0.f);

	const FVector AB = (B - A);
	const float AB2 = FMath::Max(AB.SizeSquared(), 1e-3f);
	const float t = FMath::Clamp(FVector::DotProduct(C - A, AB) / AB2, 0.f, 1.f);
	const FVector Closest = A + AB * t;
	return FVector::Dist(Closest, C) <= Radius;
}

float AAKAIController::AvailableTravelUntilHit(const FVector& Start, const FVector& Dir, float IntendedDist, ECollisionChannel Channel, const TArray<AActor*>& ActorsToIgnore) const
{
	const FVector End = Start + Dir * IntendedDist;

	FHitResult Hit;
	const bool bHit = UKismetSystemLibrary::LineTraceSingle(
		(UObject*)this,
		Start, End,
		UEngineTypes::ConvertToTraceType(Channel),
		/*bTraceComplex=*/false,
		ActorsToIgnore,
		EDrawDebugTrace::None,
		Hit,
		/*bIgnoreSelf=*/true
	);

	if (!bHit) return IntendedDist;
	return FVector::Dist(Start, Hit.ImpactPoint);
}

bool AAKAIController::ReflectOnce(const FHitResult& Hit, const FVector& InDir, FVector& OutReflectedDir) const
{
	const FVector N = Hit.ImpactNormal.GetSafeNormal();
	if (N.IsNearlyZero()) return false;

	// N에 대해 반사
	OutReflectedDir = InDir.GetSafeNormal().MirrorByVector(N).GetSafeNormal();
	return !OutReflectedDir.IsNearlyZero();
}

FStoneSimResult AAKAIController::SimulateShotApprox(const FVector& ShooterStart, float ShooterMass, float ShooterDamping, const FVector& TargetStart, float TargetMass, float TargetDamping, const FVector& Dir, float Power, float ShooterRadius, float TargetRadius, float Restitution, ECollisionChannel TraceChannel, bool bEnableOneBounce) const
{
	FStoneSimResult R;

	const FVector NDir = Dir.GetSafeNormal();
	if (NDir.IsNearlyZero() || ShooterMass <= 0.f) { R.FinalPos = ShooterStart; return R; }

	// 초기 속도
	const FVector v0 = (Power / ShooterMass) * NDir;
	const float   speed0 = v0.Size();
	if (speed0 <= KINDA_SMALL_NUMBER) { R.FinalPos = ShooterStart; return R; }

	// 장애물 무시 리스트: 슈터/타깃
	TArray<AActor*> IgnoreActors;

	// 무충돌 정지거리
	float preStopDist = PredictStopDistance(speed0, ShooterDamping);

	// 타깃과의 1회 충돌 여부 
	const float hitRadius = ShooterRadius + TargetRadius;
	const bool  bHitTarget = SegmentHitsDisc2D(ShooterStart, NDir, preStopDist, TargetStart, hitRadius);


	if (!bHitTarget)
	{
		// 장애물로 클램프
		const float sAvail = AvailableTravelUntilHit(ShooterStart, NDir, preStopDist, TraceChannel, IgnoreActors);

		//  1회 반사
		if (bEnableOneBounce && sAvail + 1.f < preStopDist) // 1.f 여유: 히트가 있었음을 대충 감지
		{
			// 첫 히트 포인트
			FHitResult Hit;
			UKismetSystemLibrary::LineTraceSingle(
				(UObject*)this,
				ShooterStart, ShooterStart + NDir * preStopDist,
				UEngineTypes::ConvertToTraceType(TraceChannel),
				false, IgnoreActors, EDrawDebugTrace::None, Hit, true
			);

			FVector rDir;
			if (ReflectOnce(Hit, NDir, rDir))
			{
				const float left = preStopDist - FVector::Dist(ShooterStart, Hit.ImpactPoint);
				const float leftClamped = AvailableTravelUntilHit(Hit.ImpactPoint + rDir * 1.f, rDir, left, TraceChannel, IgnoreActors);
				R.FinalPos = Hit.ImpactPoint + rDir * leftClamped;
			}
			else
			{
				R.FinalPos = ShooterStart + NDir * sAvail;
			}
		}
		else
		{
			R.FinalPos = ShooterStart + NDir * sAvail;
		}

		R.FinalVel = FVector::ZeroVector;
		R.bShooterOut = IsOutOfBoardRect(R.FinalPos);
		R.bTargetOut = IsOutOfBoardRect(TargetStart);
		return R;
	}


	// 히트 포인트 근사
	const FVector S0 = ShooterStart;
	const FVector T0 = TargetStart;
	const FVector ST = T0 - S0;
	const float   h = FVector::DotProduct(ST, NDir);
	const float   travelToHit = FMath::Clamp(h - hitRadius, 0.f, preStopDist);
	const FVector HitPoint = S0 + NDir * travelToHit;

	// 충돌 전속도/후속도 (법선 교환 + e)
	const float m1 = ShooterMass, m2 = TargetMass, e = FMath::Clamp(Restitution, 0.f, 1.f);
	const FVector v1_before = v0, v2_before = FVector::ZeroVector;

	FVector n = (T0 - HitPoint); n.Z = 0.f; n = n.GetSafeNormal();
	if (n.IsNearlyZero()) n = NDir;

	const float v1n = FVector::DotProduct(v1_before, n);
	const float v2n = FVector::DotProduct(v2_before, n);
	const float v1n_after = ((m1 - e * m2) * v1n + (1 + e) * m2 * v2n) / (m1 + m2);
	const float v2n_after = ((m2 - e * m1) * v2n + (1 + e) * m1 * v1n) / (m1 + m2);

	const FVector v1t = v1_before - v1n * n;
	const FVector v2t = v2_before - v2n * n;

	const FVector v1_after = (v1t + v1n_after * n);
	const FVector v2_after = (v2t + v2n_after * n);

	// 충돌 후 정지거리
	float s1 = PredictStopDistance(v1_after.Size(), ShooterDamping);
	float s2 = PredictStopDistance(v2_after.Size(), TargetDamping);

	const FVector d1 = v1_after.GetSafeNormal();
	const FVector d2 = v2_after.GetSafeNormal();

	// ── 슈터 이동: 장애물 클램프 + 반사 1회
	{
		// 먼저 HitPoint까지 가는 구간에서의 장애물은 이미 없다고 가정
		// Hit 이후 잔여 거리 이동을 클램프
		float s1Avail = AvailableTravelUntilHit(HitPoint, d1, s1, TraceChannel, IgnoreActors);

		FVector shooterEnd;
		if (bEnableOneBounce && s1Avail + 1.f < s1)
		{
			// 첫 장애물 히트
			FHitResult HitObs;
			UKismetSystemLibrary::LineTraceSingle(
				(UObject*)this,
				HitPoint, HitPoint + d1 * s1,
				UEngineTypes::ConvertToTraceType(TraceChannel),
				false, IgnoreActors, EDrawDebugTrace::None, HitObs, true
			);

			FVector rDir;
			if (ReflectOnce(HitObs, d1, rDir))
			{
				const float left = s1 - FVector::Dist(HitPoint, HitObs.ImpactPoint);
				const float leftClamped = AvailableTravelUntilHit(HitObs.ImpactPoint + rDir * 1.f, rDir, left, TraceChannel, IgnoreActors);
				shooterEnd = HitObs.ImpactPoint + rDir * leftClamped;
			}
			else
			{
				shooterEnd = HitPoint + d1 * s1Avail;
			}
		}
		else
		{
			shooterEnd = HitPoint + d1 * s1Avail;
		}

		R.FinalPos = shooterEnd;
		R.bShooterOut = IsOutOfBoardRect(shooterEnd);
	}

	// ── 타깃 이동: 장애물 클램프 +  반사 1회
	{
		float s2Avail = AvailableTravelUntilHit(T0, d2, s2, TraceChannel, IgnoreActors);

		FVector targetEnd;
		if (bEnableOneBounce && s2Avail + 1.f < s2)
		{
			FHitResult HitObs;
			UKismetSystemLibrary::LineTraceSingle(
				(UObject*)this,
				T0, T0 + d2 * s2,
				UEngineTypes::ConvertToTraceType(TraceChannel),
				false, IgnoreActors, EDrawDebugTrace::None, HitObs, true
			);

			FVector rDir;
			if (ReflectOnce(HitObs, d2, rDir))
			{
				const float left = s2 - FVector::Dist(T0, HitObs.ImpactPoint);
				const float leftClamped = AvailableTravelUntilHit(HitObs.ImpactPoint + rDir * 1.f, rDir, left, TraceChannel, IgnoreActors);
				targetEnd = HitObs.ImpactPoint + rDir * leftClamped;
			}
			else
			{
				targetEnd = T0 + d2 * s2Avail;
			}
		}
		else
		{
			targetEnd = T0 + d2 * s2Avail;
		}

		R.bTargetOut = IsOutOfBoardRect(targetEnd);
		// (R.FinalVel은 이번 근사에서 0 유지)
	}

	return R;
}



float AAKAIController::EvaluateShotHeuristic(const FAKShot& Shot) const
{
	if (!Shot.Shooter) return -1e9f;

	const FVector shooterStart = Shot.Shooter->GetActorLocation();
	const FVector targetStart = Shot.Target ? Shot.Target->GetActorLocation() : shooterStart;

	FVector flatDir = Shot.Dir;
	flatDir.Z = 0.f;
	flatDir.Normalize();


	// 근사 시뮬: (장애물/반사까지 쓰려면 true로)
	const FStoneSimResult Sim = SimulateShotApprox(
		shooterStart, MassStone, LinDamp,
		targetStart, MassStone, LinDamp,
		flatDir, Shot.Power,
		StoneRadius, StoneRadius,
		RestCoef,
		ECC_Visibility,
		/*bEnableOneBounce=*/true   // ← 우선 끄고, 아래 휴리스틱으로 보정
	);

	float score = 0.f;

	// 1) 아웃/생존
	if (Shot.Target && Sim.bTargetOut)  score += 100.f;
	if (Sim.bShooterOut)                score -= 80.f;

	// 2) 타깃 접근도
	if (Shot.Target)
	{
		const float dBefore = FVector::Dist2D(shooterStart, targetStart);
		const float dAfter = FVector::Dist2D(Sim.FinalPos, targetStart);
		score += (dBefore - dAfter) * 0.05f;
	}

	// 3) 가장자리 여유
	score += EdgeMarginRect(Sim.FinalPos) * 0.01f;

	// 4) “바깥으로 밀기” 가능성(타깃 뒤 장애물 고려)
	if (Shot.Target)
	{
		const FVector2D nOut = OutwardEdgeNormalAt(targetStart);
		const FVector2D shot2(flatDir.X, flatDir.Y);
		const float alignOut = FMath::Max(0.f, FVector2D::DotProduct(nOut.GetSafeNormal(), shot2.GetSafeNormal()));

		// 타깃 뒤로(바깥 방향) 보드 끝까지 뚫렸는지
		TArray<AActor*> Ignore = { Shot.Shooter, Shot.Target };
		const float clearFrac = OutwardClearFraction(targetStart, nOut, ECC_Visibility, Ignore); // 0~1

		const float outPotential = alignOut * clearFrac;

		// 아웃 보상 스케일
		score += 100.f * outPotential;

		// 뒤가 막혀 있으면(=clearFrac 낮음) 추가 감점으로
		if (alignOut > 0.5f && clearFrac < 0.3f) score -= 15.f;
	}

	// 파워 안정성
	/*if (Shot.Power < 21000.f)  score -= 10.f;
	if (Shot.Power > 39000.f) score -= 10.f;*/

	return score;
}

float AAKAIController::EvaluateShotForSide(const FAKShot& Shot, bool bShooterIsAI) const
{
	const float s = EvaluateShotHeuristic(Shot);
	return bShooterIsAI ? s : -s;
}

float AAKAIController::OpponentBestReplyScore(const TArray<AAKStone*>& OpponentStones, const TArray<AAKStone*>& MyStones, float BasePowerPerMeter, float MinPower, float MaxPower, int32 BeamOpp) const
{
	TArray<FAKShot> oppCands;
	GenerateMyCandidates(OpponentStones, MyStones, oppCands, BasePowerPerMeter, MinPower, MaxPower);

	if (oppCands.Num() == 0) return 0.f;

	for (FAKShot& s : oppCands)
		s.Score = EvaluateShotForSide(s, /*bShooterIsAI=*/false);

	oppCands.Sort([](const FAKShot& A, const FAKShot& B) { return A.Score > B.Score; });
	if (oppCands.Num() > BeamOpp) oppCands.SetNum(BeamOpp);

	return oppCands[0].Score;
}

FAKShot AAKAIController::PickBestShot_MinimaxD2(const TArray<AAKStone*>& MyStones, const TArray<AAKStone*>& EnemyStones, float BasePowerPerMeter, float MinPower, float MaxPower, int32 BeamMy, int32 BeamOpp) const
{
	TArray<FAKShot> myCands;
	GenerateMyCandidates(MyStones, EnemyStones, myCands, BasePowerPerMeter, MinPower, MaxPower);
	if (myCands.Num() == 0) return FAKShot{};

	for (FAKShot& s : myCands)
		s.Score = EvaluateShotForSide(s, /*bShooterIsAI=*/true);

	myCands.Sort([](const FAKShot& A, const FAKShot& B) { return A.Score > B.Score; });
	if (myCands.Num() > BeamMy) myCands.SetNum(BeamMy);

	float alpha = -FLT_MAX;
	float beta = FLT_MAX;
	FAKShot best = myCands[0];

	for (FAKShot& me : myCands)
	{
		const float opp = OpponentBestReplyScore(EnemyStones, MyStones, BasePowerPerMeter, MinPower, MaxPower, BeamOpp);
		const float total = me.Score + opp;

		if (total > alpha)
		{
			alpha = total;
			best = me;
		}

		if (alpha >= beta) break; // 알파–베타 컷
	}
	return best;
}

float AAKAIController::DistanceToEdgeAlongNormal(const FVector& P, const FVector2D& nOut) const
{
	if (FMath::Abs(nOut.X) > 0.5f) // 좌/우
	{
		return (nOut.X > 0.f) ? (BoardMax.X - P.X) : (P.X - BoardMin.X);
	}
	// 상/하
	return (nOut.Y > 0.f) ? (BoardMax.Y - P.Y) : (P.Y - BoardMin.Y);
}

float AAKAIController::OutwardClearFraction(const FVector& Start, const FVector2D& nOut, ECollisionChannel Channel, const TArray<AActor*>& ActorsToIgnore) const
{
	const float toEdge = DistanceToEdgeAlongNormal(Start, nOut);
	if (toEdge <= 1.f) return 0.f;

	const FVector dir3(nOut.X, nOut.Y, 0.f);
	const FVector End = Start + dir3 * toEdge;

	FHitResult Hit;
	const bool bHit = UKismetSystemLibrary::LineTraceSingle(
		(UObject*)this, Start, End,
		UEngineTypes::ConvertToTraceType(Channel),
		/*bTraceComplex=*/false,
		ActorsToIgnore,
		EDrawDebugTrace::None,
		Hit,
		/*bIgnoreSelf=*/true
	);

	if (!bHit) return 1.f; // 끝까지 뚫림

	const float clearDist = FVector::Dist(Start, Hit.ImpactPoint);
	return FMath::Clamp(clearDist / toEdge, 0.f, 1.f);
}

/*
input: 적 돌 리스트 + 우리 돌 위치(From)
output : 경로가 뚫려 있는 적 K 개 
직선 라인트레이스가 뚫려 있는 적 중 가까운 K 개 반환
*/
void AAKAIController::KClosestEnemies_PathAware(const TArray<class AAKStone*>& Enemies, const FVector& From, int32 K, AActor* ShooterToIgnore, ECollisionChannel Channel, TArray<class AAKStone*>& Out) const
{
	struct FCandidate
	{
		AAKStone* Enemy = nullptr;
		float     D2 = 0.f;         //적돌 사이 거리^2
		bool      bClear = false;
	};

	TArray<FCandidate> Cands;
	Cands.Reserve(Enemies.Num());

	for (AAKStone* E : Enemies)
	{
		if (!IsValid(E)) continue;

		FCandidate C;
		C.Enemy = E;
		C.D2 = FVector::DistSquared(From, E->GetActorLocation());

		// 라인트레이스 무시 리스트
		TArray<AActor*> Ignore;
		if (ShooterToIgnore) Ignore.Add(ShooterToIgnore);
		if (E) Ignore.Add(E);

		C.bClear = IsLineClear(
			(UObject*)this,
			From,
			E->GetActorLocation(),
			Channel,
			Ignore
		);

		/*UKismetSystemLibrary::PrintString(
			this,
			FString::Printf(TEXT("[PathAware] Enemy=%s d2=%.1f clear=%d"),
				*E->GetName(), C.D2, C.bClear ? 1 : 0),
			true,  
			true,  
			FLinearColor::Red,
			5.f
		);*/


		Cands.Add(C);
	}

	// 뚫린 경로만 정렬
	// 뚫려 있고 가장 가까운 순 정렬
	Cands.Sort([](const FCandidate& A, const FCandidate& B)
		{
			if (A.bClear != B.bClear) return A.bClear > B.bClear;
			return A.D2 < B.D2;
		});

	Out.Reset();
	for (const FCandidate& C : Cands)
	{
		if (Out.Num() >= K) break;
		if (C.bClear) Out.Add(C.Enemy);
	}
}
