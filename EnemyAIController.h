// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "../Buildings/Monument.h"
#include "../Interfaces/BuildingInterface.h"
#include "Math/TransformNonVectorized.h"
#include "GameFramework/Actor.h"
#include "Kismet/GameplayStatics.h"
#include "GenericTeamAgentInterface.h"
#include "CoreMinimal.h"
#include "AIController.h"
#include "EnemyAIController.generated.h"

class AAIBaseCharacter;

UCLASS()
class EALOND_API AEnemyAIController : public AAIController, public IBuildingInterface
{
	GENERATED_BODY()

public:
	AEnemyAIController();

	void BeginPlay() override;
	void Tick(float DeltaTime) override;

	class AEalondGameMode* GameMode;
	int32 AIMapIndex;

	UPROPERTY(BlueprintReadOnly)
	AAIBaseCharacter* ControlledCharacter;

	void HandleDestruction();

	// perception
	UFUNCTION(BlueprintCallable)
	void UpdatePerceivedActors(const TArray<AActor*>& PerceivedActors);

	// pathfinding
	UFUNCTION(BlueprintCallable)
	void MoveToTarget(AActor* Target, float Radius);
	UPROPERTY(BlueprintReadOnly)
	bool bMonumentSet;
	UFUNCTION(BlueprintCallable)	
	void SetMonument();
	UFUNCTION(BlueprintCallable)
	float GetPathToMonument();
	AActor* CheckBlocked();
	UFUNCTION(BlueprintCallable)
	void CheckStaticTargets(bool IgnoreMonument, float CheckCone);
	UFUNCTION(BlueprintCallable)
	void SetStaticTarget(bool bCharacterBlocked = false);
	UPROPERTY(BlueprintReadOnly)
	AActor* StaticTarget = nullptr;

	// combat
	class UAIDamageComponent* DamageComponent;
	UPROPERTY(BlueprintReadOnly)
	bool bIsCombatReady = false;
	UPROPERTY(BlueprintReadOnly)
	AActor* EnemyTarget = nullptr;
	UFUNCTION(BlueprintCallable)
	bool CheckEngageCondition(AActor* CheckedActor = nullptr);	
	void Engage(AActor* TargetCandidate);
	void Disengage();
	UFUNCTION(BlueprintCallable)
	void Attack(AActor* Target);
	float Timer_Hurt;
	void ChangeHealth(float ChangeAmount, AActor* DamageCauser);
	UFUNCTION(BlueprintCallable)
	AMonument* GetMonument();
	bool CheckMonumentBlocked() const;
	bool bCanReselectTarget;
	FTimerHandle TargetSelectionCooldownTimer;
	void DestroyBuilding(AActor* Building);
	AActor* GetCurrentTarget() const;

private:
	virtual void OnPossess(APawn* InPawn) override;

	// perception
	void InitPerception();
	virtual FGenericTeamId GetGenericTeamId() const override {return TeamId;}
	class UAISenseConfig_Sight* SightConfig;
	class UAIPerceptionComponent* PerceptionComp;
	bool bEnemyDetected = false;
	bool bIsBlockedByObject = false;
	float Timer_CheckEngageCondition;

	// pathfinding
	class UPathFollowingComponent* PathComp;
	UPROPERTY(EditAnywhere, Category = "Pathfinding")
	float DistanceToMonumentThreshold = 1000.f;  // radius within which AI will decide to attack object or find path
	bool bAttackObstacle = false;

	// behavior tree
	UPROPERTY(EditAnywhere)
	class UBehaviorTree* AIBehaviorTree;
	AMonument* Monument;

	// combat
	bool bHasTarget = false;
	UPROPERTY(EditAnywhere, Category = "Combat")
	float ReturnToMonumentThreshold = 4000.f; // Distance from monument at which the AI will disengage and move to monument
	UPROPERTY(EditAnywhere, Category = "Combat")
	float DisengagePlayerThreshold = 1000.f;  // Distance from player at which the AI will disengage and move to monument

	// animations
	UFUNCTION(NetMulticast, reliable)
	void Multi_PlayAnim(UAnimMontage* Montage);

protected:
	FGenericTeamId TeamId;
	virtual ETeamAttitude::Type GetTeamAttitudeTowards(const AActor& Other) const override;
	AActor* CheckTargetChanged;
};
