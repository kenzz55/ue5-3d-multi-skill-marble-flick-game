// Fill out your copyright notice in the Description page of Project Settings.


#include "AKStone.h"

void AAKStone::Shoot(const FVector& Dir, float Power)
{
    if (UPrimitiveComponent* Prim = Cast<UPrimitiveComponent>(GetRootComponent()))
    {
      
        FVector Impulse = FVector(Dir.X, Dir.Y, 0.f).GetSafeNormal() * Power; // XY Æò¸é¸¸
        Prim->AddImpulse(Impulse, NAME_None, false);

        UE_LOG(LogTemp, Warning, TEXT("Prim class: %s"), *Prim->GetClass()->GetName());

       
    }

    else {
        UE_LOG(LogTemp, Warning, TEXT("Stone shot err : not found cylinder"));
        return;
    }
}
