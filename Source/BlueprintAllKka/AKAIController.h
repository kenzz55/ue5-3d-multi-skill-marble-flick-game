// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "AKAIController.generated.h"

/**
 * 
 */

class AAKStone;

USTRUCT()
struct FAKShot
{
	GENERATED_BODY()

	UPROPERTY() AAKStone* Shooter = nullptr;
	UPROPERTY() AAKStone* Target = nullptr;

	FVector Dir = FVector::ZeroVector;  // 발사 방향
	float   Power = 0.f;    
	float   Score = 0;                // 휴리스틱 평가
};

USTRUCT()
struct FStoneSimResult
{
	GENERATED_BODY()
	FVector FinalPos = FVector::ZeroVector;
	FVector FinalVel = FVector::ZeroVector;
	bool    bShooterOut = false;
	bool    bTargetOut = false;
};

UCLASS()
class BLUEPRINTALLKKA_API AAKAIController : public APlayerController
{
	GENERATED_BODY()

//////////////////////////////////////////////////////////////////
	// === 프로토타입 1 ===
public:
	UFUNCTION(BlueprintCallable, Category = "Alkkagi|AI")
	void DoAITurn(float BasePowerPerMeter, float MinPower, float MaxPower); 
	
private:

	// 라인트레이스로 장애물 충돌 판정 여부 검사 함수
	static bool IsLineClear(UObject* WorldCtx,
		const FVector& Start,
		const FVector& End,
		ECollisionChannel Channel,
		const TArray<AActor*>& ActorsToIgnore);

	static bool FindClearDirection_Line(UObject* WorldCtx,
		const FVector& From,
		const FVector& To,
		ECollisionChannel Channel,
		const TArray<AActor*>& ActorsToIgnore,
		FVector& OutDir);
//////////////////////////////////////////////////////////////////

public:
	virtual void BeginPlay() override;

	void GenerateMyCandidates(
		const TArray<AAKStone*>& MyStones,
		const TArray<AAKStone*>& EnemyStones,
		TArray<FAKShot>& OutShots,
		float BasePowerPerMeter,
		float MinPower,
		float MaxPower
	) const;

	UFUNCTION(BlueprintCallable, Category = "Alkkagi|AI")
	void DoAITurn_Strategic(float BasePowerPerMeter, float MinPower, float MaxPower);

private:
	// === 보드 유틸함수 ===
	bool     IsOutOfBoardRect(const FVector& P) const;
	float    EdgeMarginRect(const FVector& P) const; // +이면 안쪽 여유, -면 이미 밖
	FVector2D RectCenter() const { return (BoardMin + BoardMax) * 0.5f; }
	FVector2D OutwardEdgeNormalAt(const FVector& P) const;

	// === 수학적 근사 시뮬 =================
	float PredictStopDistance(float Speed0, float DampingEff) const;
	bool  SegmentHitsDisc2D(const FVector& From, const FVector& Dir, float Range,
		const FVector& Center, float Radius) const;

	float AvailableTravelUntilHit(
		const FVector& Start, const FVector& Dir, float IntendedDist,
		ECollisionChannel Channel,
		const TArray<AActor*>& ActorsToIgnore
	) const;

	// 1회 반사: 충돌 정보와 입사 방향으로 반사 방향 계산
	bool ReflectOnce(const FHitResult& Hit, const FVector& InDir, FVector& OutReflectedDir) const;

	FStoneSimResult SimulateShotApprox(
		const FVector& ShooterStart, float ShooterMass, float ShooterDamping,
		const FVector& TargetStart, float TargetMass, float TargetDamping,
		const FVector& Dir, float Power,
		float ShooterRadius, float TargetRadius,
		float Restitution,
		ECollisionChannel TraceChannel = ECC_Visibility,
		bool bEnableOneBounce = false   
	) const;
	// ====================================

	float EvaluateShotHeuristic(const FAKShot& Shot) const;

	// 미니맥스용 - 팀 구분 점수
	float EvaluateShotForSide(const FAKShot& Shot, bool bShooterIsAI) const;

	// 상대 응수(최악의 경우)
	float OpponentBestReplyScore(const TArray<AAKStone*>& OpponentStones,
		const TArray<AAKStone*>& MyStones, float BasePowerPerMeter,
		float MinPower, float MaxPower,
		int32 BeamOpp) const;

	// 깊이 2 미니맥스
	FAKShot PickBestShot_MinimaxD2(const TArray<AAKStone*>& MyStones,
		const TArray<AAKStone*>& EnemyStones, float BasePowerPerMeter,
		float MinPower, float MaxPower,
		int32 BeamMy, int32 BeamOpp) const;


	// ===== 휴리스틱 보정, 장애물 충돌 수학적 시뮬 고려안했을 때 사용, 테스트용 ====
	float DistanceToEdgeAlongNormal(const FVector& P, const FVector2D& nOut) const;
	float OutwardClearFraction(const FVector& Start, const FVector2D& nOut, ECollisionChannel Channel,
		const TArray<AActor*>& ActorsToIgnore) const;


	void KClosestEnemies_PathAware(
		const TArray<class AAKStone*>& Enemies,
		const FVector& From,
		int32 K,
		class AActor* ShooterToIgnore,
		ECollisionChannel Channel,
		TArray<class AAKStone*>& Out
	) const;

	FVector2D BoardMin = FVector2D(0.f, 0.f);
	FVector2D BoardMax = FVector2D(0.f, 0.f);
	FVector BoardCenter;

	float StoneRadius = 35.f;  // cm
	float MassStone = 60.f;  // kg
	float LinDamp = 0.4f;  // 감쇠 근사 계수
	float RestCoef = 0.5f;  // 탄성계수 e (0~1)
};

