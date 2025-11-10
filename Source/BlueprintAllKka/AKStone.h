// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "AKStone.generated.h"

class UStaticMeshComponent;

UCLASS()
class BLUEPRINTALLKKA_API AAKStone : public APawn
{
	GENERATED_BODY()

public:
	

	UFUNCTION(BlueprintCallable, Category = "Alkkagi")
	void Shoot(const FVector& Dir, float Power);

};
