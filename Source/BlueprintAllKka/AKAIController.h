// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "AKAIController.generated.h"

class AAKStone;

USTRUCT()
struct FAKShot
{
	GENERATED_BODY()

	UPROPERTY() AAKStone* Shooter = nullptr;
	UPROPERTY() AAKStone* Target = nullptr;
	UPROPERTY() int32 ShooterIndex = INDEX_NONE;
	UPROPERTY() int32 TargetIndex = INDEX_NONE;

	FVector Dir = FVector::ZeroVector;
	float   Power = 0.f;
	float   Score = 0.f;
};

USTRUCT()
struct FStoneSimResult
{
	GENERATED_BODY()

	FVector FinalPos = FVector::ZeroVector;
	FVector TargetFinalPos = FVector::ZeroVector;
	FVector FinalVel = FVector::ZeroVector;
	bool    bShooterOut = false;
	bool    bTargetOut = false;
};

USTRUCT()
struct FAKStoneState
{
	GENERATED_BODY()

	UPROPERTY() TWeakObjectPtr<AAKStone> SourceStone;
	FVector Pos = FVector::ZeroVector;
	bool bOut = false;
	bool bIsAI = false;
};

USTRUCT()
struct FAKBoardState
{
	GENERATED_BODY()

	UPROPERTY() TArray<FAKStoneState> Stones;
	bool bAITurn = true;
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
	bool     IsOutOfBoardRect(const FVector& P) const;
	float    EdgeMarginRect(const FVector& P) const;
	FVector2D RectCenter() const { return (BoardMin + BoardMax) * 0.5f; }
	FVector2D OutwardEdgeNormalAt(const FVector& P) const;

	float PredictStopDistance(float Speed0, float DampingEff) const;
	bool  SegmentHitsDisc2D(const FVector& From, const FVector& Dir, float Range,
		const FVector& Center, float Radius) const;

	float AvailableTravelUntilHit(
		const FVector& Start, const FVector& Dir, float IntendedDist,
		ECollisionChannel Channel,
		const TArray<AActor*>& ActorsToIgnore
	) const;

	bool ReflectOnce(const FHitResult& Hit, const FVector& InDir, FVector& OutReflectedDir) const;

	FStoneSimResult SimulateShotApprox(
		const FVector& ShooterStart, float ShooterMass, float ShooterDamping,
		const FVector& TargetStart, float TargetMass, float TargetDamping,
		const FVector& Dir, float Power,
		float ShooterRadius, float TargetRadius,
		float Restitution,
		ECollisionChannel TraceChannel = ECC_Visibility,
		bool bEnableOneBounce = false,
		const TArray<AActor*>* ActorsToIgnore = nullptr
	) const;

	float EvaluateShotHeuristic(const FAKShot& Shot) const;
	float EvaluateShotForSide(const FAKShot& Shot, bool bShooterIsAI) const;

	FAKBoardState BuildBoardState(const TArray<AAKStone*>& MyStones,
		const TArray<AAKStone*>& EnemyStones,
		bool bAITurn) const;

	void GenerateCandidatesForState(const FAKBoardState& State,
		TArray<FAKShot>& OutShots,
		float BasePowerPerMeter,
		float MinPower,
		float MaxPower) const;

	FAKBoardState ApplyShotToState(const FAKBoardState& State, const FAKShot& Shot) const;
	float EvaluateBoardState(const FAKBoardState& State) const;
	float EvaluateActionImmediate(const FAKBoardState& State, const FAKShot& Shot) const;
	bool IsTerminalState(const FAKBoardState& State) const;
	float MinimaxScore(const FAKBoardState& State,
		int32 Depth,
		float Alpha,
		float Beta,
		float BasePowerPerMeter,
		float MinPower,
		float MaxPower,
		int32 BeamWidth) const;

	FAKShot PickBestShot_MinimaxD2(const TArray<AAKStone*>& MyStones,
		const TArray<AAKStone*>& EnemyStones, float BasePowerPerMeter,
		float MinPower, float MaxPower,
		int32 BeamMy, int32 BeamOpp) const;

	float DistanceToEdgeAlongNormal(const FVector& P, const FVector2D& nOut) const;
	float OutwardClearFraction(const FVector& Start, const FVector2D& nOut, ECollisionChannel Channel,
		const TArray<AActor*>& ActorsToIgnore) const;
	bool SegmentHitsStone2D(const FVector& From, const FVector& To, const FVector& Center, float Radius) const;
	float OutwardClearFractionForState(const FAKBoardState& State, int32 StoneIndex, const FVector2D& nOut) const;
	void KClosestEnemies_StateAware(const FAKBoardState& State,
		int32 ShooterIndex,
		int32 K,
		TArray<int32>& OutTargetIndices) const;

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

	float StoneRadius = 35.f;
	float MassStone = 60.f;
	float LinDamp = 0.4f;
	float RestCoef = 0.5f;
};