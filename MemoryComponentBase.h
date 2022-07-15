// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "../Interfaces/MemoryInterface.h"
#include "MemoryComponentBase.generated.h"

UENUM()
enum EArmorType
{
	AT_Padded UMETA(DisplayName = "Padded"),
	AT_Leather UMETA(DisplayName = "Leather"),
	AT_Chainmail UMETA(DisplayName = "Chainmail"),
	AT_Plate UMETA(DisplayName = "Plate"),
};

UENUM()
enum EWeaponType
{
	WT_Unarmed UMETA(DisplayName = "Unarmed"),
	WT_Ranged UMETA(DisplayName = "Ranged"),
	WT_Melee UMETA(DisplayName = "Melee"),
	WT_Magic UMETA(DisplayName = "Magic"),
};

UENUM(BlueprintType)
enum ECurrentGoal
{
	CG_AtRest UMETA(DisplayName = "At Rest"),
	CG_Fleeing UMETA(DisplayName = "Fleeing"),
	// enemies only
	CG_EngagingEnemy UMETA(DisplayName = "Engaging Enemy"),
	CG_EngagingStatic UMETA(DisplayName = "Engaging Static"),
	CG_EngagingMonument UMETA(DisplayName = "Engaging Monument"),
	// villagers only
	CG_Patrolling UMETA(DisplayName = "Patrolling"),
	CG_PartyCombat UMETA(DisplayName = "In combat party"),
	CG_RaisingAlarm UMETA(DisplayName = "Raising alarm"),
	CG_Gathering UMETA(DisplayName = "Gathering"),
	CG_Farming UMETA(DisplayName = "Farming"),
	CG_Forging UMETA(DisplayName = "Forging"),
};

UENUM(BlueprintType)
enum ETeamRole
{
	TR_NoRole UMETA(DisplayName = "No Role"),
	TR_Leader UMETA(DisplayName = "Leader"),
	TR_AttackMelee UMETA(DisplayName = "Attack Melee"),
	TR_AttackRanged UMETA(DisplayName = "Attack Ranged"),
	TR_FlankMelee UMETA(DisplayName = "Flank Melee"),
	TR_FlankRanged UMETA(DisplayName = "Flank Ranged"),
	TR_Arsonist UMETA(DisplayName = "Arsonist"),
};

USTRUCT()
struct FEnemyData
{
	GENERATED_BODY()
	
	UPROPERTY()
	AActor* Character;
	UPROPERTY()
	bool bIsCurrentlyPerceived;
	UPROPERTY()
	float RemainingHealth;
	UPROPERTY()
	float RemainingStamina;
	UPROPERTY()
	float MaxHealth;
	UPROPERTY()
	TEnumAsByte<EArmorType> ArmorType;
	UPROPERTY()
	TEnumAsByte<EWeaponType> WeaponType;
	UPROPERTY()
	FVector LastSeenLocation;
	UPROPERTY()
	FRotator LastRotation;
	UPROPERTY()
	FTimerHandle TimerHandle;
	UPROPERTY()
	float TimeSincePerceived;

	FEnemyData()
	{
		Character = nullptr;
		bIsCurrentlyPerceived = false;
		RemainingHealth = 0;
		RemainingStamina = 0;
		MaxHealth = 0;
		ArmorType = EArmorType::AT_Padded;
		WeaponType = EWeaponType::WT_Melee;
		LastSeenLocation = FVector(0, 0, 0);
		LastRotation = FRotator(0, 0, 0);
		TimerHandle = FTimerHandle();
		TimeSincePerceived = 0;
	};

	FEnemyData
	(
		AActor* EnemyCharacter_NEW,
		bool bIsCurrentlyPerceived_NEW,
		float RemainingHealth_NEW,
		float RemainingStamina_NEW,
		float MaxHealth_NEW,
		EArmorType ArmorType_NEW,
		EWeaponType WeaponType_NEW,
		FVector LastSeenLocation_NEW,
		FRotator LastRotation_NEW,
		FTimerHandle TimerHandle_NEW,
		float TimeSincePerceived_NEW
	)
	{
		Character = EnemyCharacter_NEW;
		bIsCurrentlyPerceived = bIsCurrentlyPerceived_NEW;
		RemainingHealth = RemainingHealth_NEW;
		RemainingStamina = RemainingStamina_NEW;
		MaxHealth = MaxHealth_NEW;
		ArmorType = ArmorType_NEW;
		WeaponType = WeaponType_NEW;
		LastSeenLocation = LastSeenLocation_NEW;
		LastRotation = LastRotation_NEW;
		TimerHandle = TimerHandle_NEW;
		TimeSincePerceived = TimeSincePerceived_NEW;
	}
};

USTRUCT()
struct FEnemySelectionWeightings
{
	GENERATED_BODY()

	UPROPERTY()
	float HealthWeighting;
	UPROPERTY()
	float StaminaWeighting;
	UPROPERTY()
	float LocationWeighting;
	UPROPERTY()
	float RotationWeighting;
	UPROPERTY()
	float SelectionThreshold;  // Minumum score needed to select target

	FEnemySelectionWeightings()
	{
		HealthWeighting = 1.f;
		StaminaWeighting = 1.f;
		LocationWeighting = 1.f;
		RotationWeighting = 1.f;
		SelectionThreshold = 0;
	}
	FEnemySelectionWeightings(
		float Health_NEW, float Stamina_NEW, float Location_NEW, float Rotation_NEW, float Threshold_NEW
	)
	{
		HealthWeighting = Health_NEW;
		StaminaWeighting = Stamina_NEW;
		LocationWeighting = Location_NEW;
		RotationWeighting = Rotation_NEW;
		SelectionThreshold = Threshold_NEW;
	}
};

UCLASS( ClassGroup=(Custom), meta=(BlueprintSpawnableComponent) )
class EALOND_API UMemoryComponentBase : public UActorComponent, public IMemoryInterface
{
	GENERATED_BODY()

public:	
	// Sets default values for this component's properties
	UMemoryComponentBase();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	float Health;
	float MaxHealth;
	float Stamina;
	float MaxStamina;
	bool bIsDarkSide;

	TEnumAsByte<EArmorType> ArmorType;
	TEnumAsByte<EWeaponType> WeaponType;
	FVector CurrentLocation;
	FRotator CurrentRotation;
	TEnumAsByte<ECurrentGoal> CurrentGoal;
	TEnumAsByte<ETeamRole> TeamRole;

	// enemy
	FEnemyData GetData(UMemoryComponentBase* MemComp);
	void AddEnemyData(FEnemyData DataToAdd);
	void CheckUnperceivedEnemies(TArray<AActor*> ArrayToCheck);
	void RemoveEnemyFromMemory(AActor* ActorToRemove);
	void ForgetMe(AActor* DeadActor);

	// friendly
	bool TeamHasLeader() const;
	bool IsInTeam() const;
	UMemoryComponentBase* GetNearestTeammateTo(AActor* Object, bool bIgnoreLeader = false);

private:
	void UpdateMyData();
	TMap<UMemoryComponentBase*, FEnemyData> TetheredEnemies;  // All actors whose memory needs updating with this actor's variables
	float UpdateTetheredTimer;
	void DecayMemory(AActor* ActorToDecay, FEnemyData Struct);

protected:
	// Called when the game starts
	virtual void BeginPlay() override;
	// available to child classes
	FEnemyData MyData;
	TMap<AActor*, FEnemyData> EnemiesInMemory;  // All enemies currently in memory
	TArray<UMemoryComponentBase*> CurrentTeam;
	friend class ANPCAIController;
	TMap<UMemoryComponentBase*, FEnemyData> TetheredFriendlies;
};
