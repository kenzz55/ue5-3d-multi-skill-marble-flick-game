// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameMode.h"
#include "AKSingleGameMode.generated.h"

/**
 * 
 */

class AAKStone;

UCLASS()
class BLUEPRINTALLKKA_API AAKSingleGameMode : public AGameMode
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintReadWrite, Category = "Alkkagi")
	TArray<TObjectPtr<AAKStone>> StonesT1;

	UPROPERTY(BlueprintReadWrite, Category = "Alkkagi")
	TArray<TObjectPtr<AAKStone>> StonesT2;
};
