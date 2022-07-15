// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "AIDamageComponent.generated.h"


UCLASS( ClassGroup=(Custom), meta=(BlueprintSpawnableComponent) )
class EALOND_API UAIDamageComponent : public UActorComponent
{
	GENERATED_BODY()

public:	
	// Sets default values for this component's properties
	UAIDamageComponent();

	void InitComp();

	UFUNCTION(BlueprintCallable, Server, reliable)
	void DiagonalTrace(bool InvertForward, bool InvertRight, bool InvertUp);
	UFUNCTION(BlueprintCallable, Server, reliable)
	void OverheadTrace();
	UFUNCTION(BlueprintCallable, Server, reliable)
	void ThrustTrace();

	float ArmLength;
	float WeaponLength;
	float ArmExtension;
	float TraceRadius;
	float BaseDamageWeapon;

protected:
	// Called when the game starts
	virtual void BeginPlay() override;

private:	
	// variables
	// references
	class AAIBaseCharacter* Attacker;
	// trace
	float TraceLoopValue;
	TArray<AActor*> IgnoredActors;
	AActor* HitActor;
	FVector HitLocation;
	FVector ForwardVectorInner;
	FVector RightVectorInner;
	FVector UpVectorInner;
	FVector ForwardVectorOuter;
	FVector RightVectorOuter;
	FVector UpVectorOuter;

	// weapon
	UPROPERTY(EditAnywhere, Category = "Weapon")
	TSubclassOf<UDamageType> DamageTypeClass;

	// Helper functions
	FVector GetVectorMidpoint(FVector Origin, FVector Vector1, FVector Vector2, float DistanceBetweenPoints, float DistanceFromOrigin);
	void GetDirectionalVectorsFromPlayer(bool Inner, FVector Origin, float DistanceFromPlayer, bool InvertForward = false, bool InvertRight = false, bool InvertUp = false);

		
};
