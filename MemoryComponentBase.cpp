// Fill out your copyright notice in the Description page of Project Settings.

#include "MemoryComponentBase.h"
#include "../AI/AIBaseCharacter.h"
#include "../AI/NPCAIController.h"
#include "../AI/EnemyAIController.h"
#include "../AI/Villager.h"
#include "../AI/Goblin.h"
#include "../Framework/EalondGameMode.h"
#include "../Interfaces/MemoryInterface.h"
#include "../Player/EalondCharacter.h"
#include "../Progress/CharacterProgressComponent.h"
#include "../World/EalondCharacterBase.h"
#include "Kismet/KismetMathLibrary.h"

// Sets default values for this component's properties
UMemoryComponentBase::UMemoryComponentBase()
{
	// Set this component to be initialized when the game starts, and to be ticked every frame.  You can turn these features
	// off to improve performance if you don't need them.
	PrimaryComponentTick.bCanEverTick = true;
}

void UMemoryComponentBase::AssignPersonality()
{
	int32 RandInt = FMath::RandRange(0, 20);
	// TODO USE THE ABOVE TO ASSIGN PERSONALITY
	// Temporarily assign only normal personalities depending on class
	if (Cast<AGoblin>(GetOwner())) {PersonalityType = EEnemyAIPersonality::EP_Goblin;}
	else {PersonalityType = EEnemyAIPersonality::EP_Ogre;}

	// TODO write in other personality types
	if (PersonalityType == EEnemyAIPersonality::EP_Goblin)
	{
		EnemyWeightings = FTargetSelectionWeightings(1.0, 0.25, 1.0, 0.5, 5.f);
	}
	else
	{
		EnemyWeightings = FTargetSelectionWeightings(0.5, 0.25, 2.0, 0.25, 0);
	}
}

// Called when the game starts
void UMemoryComponentBase::BeginPlay()
{
	Super::BeginPlay();
	
	OwningCharacter = Cast<AEalondCharacterBase>(GetOwner());

	if (OwningCharacter)
	{
		if (GetOwner()->IsA(AAIBaseCharacter::StaticClass()))
		{
			OwningEnemyController = Cast<AEnemyAIController>(OwningCharacter->GetController());
		}
		else if (GetOwner()->IsA(AVillager::StaticClass()))
		{
			OwningVillagerController = Cast<ANPCAIController>(OwningCharacter->GetController());
		}
		// initialise variables
		MyData = FAbsoluteEnemyData();
		MyData.Character = GetOwner();
		UpdateMyData();
	}
}

// Called every frame
void UMemoryComponentBase::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	bool bHasUpdated = false;

	// kill all functionality when dead
	if (!GetOwner() || MyData.RemainingHealth <= 0) {return;}

	// update all tethered enemies FEnemyData every half a second
	if (UpdateTetheredTimer > 0.5 && !TetheredEnemies.IsEmpty())
	{
		UpdateMyData();
		for (auto& MemComp : TetheredEnemies)
		{
			// add any dead enemies that slipped through the net to remove list
			if (!MemComp || !MemComp->GetOwner()->IsValidLowLevelFast())
			{
				continue;
			}
			// if enemy has not perceived self, skip update
			else if (MemComp->EnemiesInMemory.IsEmpty()) {continue;}
			else if (MemComp->EnemiesInMemory.Contains(MyData.Character))
			{
				MemComp->EnemiesInMemory[MyData.Character].ArmorType = MyData.ArmorType;
				MemComp->EnemiesInMemory[MyData.Character].WeaponType = MyData.WeaponType;
				MemComp->EnemiesInMemory[MyData.Character].RemainingHealth = MyData.RemainingHealth;
				MemComp->EnemiesInMemory[MyData.Character].RemainingStamina = MyData.RemainingStamina;
				MemComp->EnemiesInMemory[MyData.Character].LastRotation = MyData.LastRotation;
				if (MyData.Character->GetDistanceTo(MemComp->GetData(this).Character) < 100.f)
				{
					auto EnemyInterface = Cast<IMemoryInterface>(MemComp->GetData(this).Character);
					if (EnemyInterface)
					{
						EnemyInterface->Execute_NotifyClose(Cast<UObject>(EnemyInterface), GetOwner());
					}
				}
				MemComp->EnemiesInMemory[MyData.Character].LastSeenLocation = MyData.LastSeenLocation;
				bHasUpdated = true;
			}
		}
	}
	if (UpdateTetheredTimer > 0.5 && !TetheredFriendlies.IsEmpty())
	{
		for (auto& Pair : TetheredFriendlies)
		{
			if (!Pair.Key || !Pair.Key->GetOwner()->IsValidLowLevelFast())
			{
				break;
			}
			else if (Pair.Key->TetheredFriendlies.IsEmpty())
			{
				continue;
			}
			else if (Pair.Key->TetheredFriendlies.Contains(this))
			{
				Pair.Key->TetheredFriendlies[this].ArmorType = MyData.ArmorType;
				Pair.Key->TetheredFriendlies[this].WeaponType = MyData.WeaponType;
				Pair.Key->TetheredFriendlies[this].RemainingHealth = MyData.RemainingHealth;
				Pair.Key->TetheredFriendlies[this].RemainingStamina = MyData.RemainingStamina;
				Pair.Key->TetheredFriendlies[this].LastRotation = MyData.LastRotation;
				Pair.Key->TetheredFriendlies[this].LastSeenLocation = MyData.LastSeenLocation;
				bHasUpdated = true;
			}
		}
	}
	if (!TetheredEnemies.IsEmpty() || !TetheredFriendlies.IsEmpty())
	{
		if (bHasUpdated)
		{
			UpdateTetheredTimer = 0;
		}
		else
		{
			UpdateTetheredTimer += DeltaTime;
		}
	}
	
	if (RelativeEnemyData.Num())
	{
		if (AggroTimer > 1.f)
		{
			for (auto& Pair : RelativeEnemyData)
			{
				if (Pair.Key->IsValidLowLevelFast())
				{
					if (Pair.Value.AggroScore > 0)
					{
						Pair.Value.AggroScore -= 1;
					}
					if (Pair.Value.DamageDealt > 0)
					{
						if (GetWorld()->GetTimeSeconds() - Pair.Value.LastTimeDealtDamage > 5.f)
						{
							Pair.Value.DamageDealt = 0;
						}
					}
				}
			}
			AggroTimer = 0;
		}
		else
		{
			AggroTimer += DeltaTime;
		}
	}
}

void UMemoryComponentBase::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(UMemoryComponentBase, BuildingData);
}

// MANIPULATE MY DATA
void UMemoryComponentBase::UpdateMyData()
{
	if (OwningCharacter)
	{
		MyData.RemainingHealth = OwningCharacter->GetHealth();
		MyData.MaxHealth = OwningCharacter->GetMaxHealth();
		MyData.RemainingStamina = OwningCharacter->GetStamina();
		MyData.LastSeenLocation = OwningCharacter->GetActorLocation();
		MyData.LastRotation = OwningCharacter->GetActorRotation();
	}
}

void UMemoryComponentBase::UpdateGearData()
{
	if (OwningCharacter && OwningCharacter->HasAuthority())
	{
		if (OwningCharacter->bShieldEquipped) MyData.bShieldEquipped = true;
		else MyData.bShieldEquipped = false;
		if (OwningCharacter->b1hSwordEquipped || OwningCharacter->b2hSwordEquipped)
		{
			MyData.WeaponType = WT_Melee;
		}
		else if (OwningCharacter->bBowEquipped)
		{
			MyData.WeaponType = WT_Ranged;
		}
		else
		{
			MyData.WeaponType = WT_Unarmed;
		}
		// ARMOR UPDATES HERE
	}
}

FAbsoluteEnemyData UMemoryComponentBase::GetData(UMemoryComponentBase* MemComp)
{
	// Assumed that any actor requesting data will then be tethered (and added to the to-keep-updated list)
	UpdateMyData();
	if (MemComp && !TetheredEnemies.Contains(MemComp) && MemComp != this && MemComp->MyData.RemainingHealth > 0)
	{
		TetheredEnemies.Add(MemComp);
	}
	return MyData;
}

void UMemoryComponentBase::ShareData()
{
	if (!CurrentTeam.IsEmpty() && !EnemiesInMemory.IsEmpty())
	{
		for (auto& Teammate : CurrentTeam)
		{
			for (auto& Pair : EnemiesInMemory)
			{
				if (auto IntEnemy = Cast<IMemoryInterface>(Pair.Key))
				{
					IntEnemy->Execute_GetEnemyData(Cast<UObject>(IntEnemy), Teammate);
				}
			}
		}
	}
}

void UMemoryComponentBase::AddEnemyData(FAbsoluteEnemyData DataToAdd)
{
	if (!DataToAdd.Character || !DataToAdd.Character->IsValidLowLevelFast()) return;
	// update or add data
	if (EnemiesInMemory.Num() <= 10)
	{
		EnemiesInMemory.Add(DataToAdd.Character, DataToAdd);
		RelativeEnemyData.Add(DataToAdd.Character, FRelativeEnemyData(DataToAdd.Character, true, FTimerHandle(), 0, 0, 0, 0));
	}
	// max 10 enemies in memory at a time. If at max, replace existing memory.
	else
	{
		// remove first unperceieved enemy or furthest away
		bool bMemoryRemoved = false;
		AActor* ActorToRemove = nullptr;
		float LongestDistance = -1.f;
		for (auto& Pair : EnemiesInMemory)
		{
			if (!RelativeEnemyData[Pair.Key].bIsCurrentlyPerceived)
			{
				EnemiesInMemory.Remove(Pair.Key);
				RelativeEnemyData.Remove(Pair.Key);
				bMemoryRemoved = true;
				break;
			}
			else
			{
				float CompareDistance = GetOwner()->GetDistanceTo(Pair.Key);
				if (CompareDistance > LongestDistance)
				{
					LongestDistance = CompareDistance;
					ActorToRemove = Pair.Key;
				}
			}
		}
		if (!bMemoryRemoved && ActorToRemove)
		{
			EnemiesInMemory.Remove(ActorToRemove);
			RelativeEnemyData.Remove(ActorToRemove);
		}
		// add new memory
		EnemiesInMemory.Add(DataToAdd.Character, DataToAdd);
		RelativeEnemyData.Add(DataToAdd.Character, FRelativeEnemyData(DataToAdd.Character, true, FTimerHandle(), 0, 0, 0, 0));
	}
	// enter combat mode with delay
	if (!OwningCharacter->bInCombatMode && EnemiesInMemory.Num() && GetWorld())
	{
		float RandEngageDelay = FMath::RandRange(.05, .2);
		FTimerHandle EnterCombatTimer;
		FTimerDelegate EnterCombatDelegate;
		EnterCombatDelegate.BindLambda([this]()
			{
				OwningCharacter->Server_SetInCombatMode(true);
			});
		GetWorld()->GetTimerManager().SetTimer(EnterCombatTimer, EnterCombatDelegate, RandEngageDelay, false);
	}

}

// LEADER FUNCTIONS
bool UMemoryComponentBase::TeamHasLeader() const
{
	if (CurrentTeam.IsEmpty()) { return false; }
	else
	{
		if (bIsLeader) {return true;}
		for (auto& Teammate : CurrentTeam)
		{
			if (Teammate->bIsLeader)
			{
				return true;
			}
		}
		return false;
	}
}

const UMemoryComponentBase* UMemoryComponentBase::GetLeader() const
{
	if (bIsLeader) 
	{
		return this;
	}
	for (auto& MemComp : CurrentTeam)
	{
		if (MemComp->bIsLeader) 
		{
			return MemComp;
		}
	}
	return nullptr;
}

// TEAM FUNCTIONS
void UMemoryComponentBase::ForgetMe(UMemoryComponentBase* DeadActorMem)
{
	if (!DeadActorMem) {return;}
	// check dead actor is same class as owning actor
	if (DeadActorMem->bIsDarkSide == bIsDarkSide)
	{
		if (TetheredFriendlies.Contains(DeadActorMem))
		{
			TetheredFriendlies.Remove(DeadActorMem);
		}
	}
	else
	{
		if (!TetheredEnemies.IsEmpty() && TetheredEnemies.Contains(DeadActorMem))
		{
			TetheredEnemies.Remove(DeadActorMem);
		}
		if (!EnemiesInMemory.IsEmpty() && EnemiesInMemory.Contains(DeadActorMem->GetOwner()))
		{
			EnemiesInMemory.Remove(DeadActorMem->GetOwner());
		}
		// switch target if dead actor is current target
		if (IMemoryInterface* IntMyCharacter = Cast<IMemoryInterface>(GetOwner()))
		{
			IntMyCharacter->Execute_OnEnemyDead(Cast<UObject>(IntMyCharacter), DeadActorMem->GetOwner());
		}
	}
}

// for enemy AI - called from game mode
TArray<TEnumAsByte<ETeamRole>> UMemoryComponentBase::GenerateTeamRoles(int32 SpawnNum)
{
	TArray<TEnumAsByte<ETeamRole>> Roles;
	if (SpawnNum > 0)
	{
		for (int32 i = 0; i < SpawnNum; i++)
		{
			if (i % 2 == 0)
			{
				Roles.Add(ETeamRole::TR_AttackMelee_1h);
			}
			else
			{
				Roles.Add(ETeamRole::TR_FlankMelee);
			}
		}
	}
	return Roles;
}

TArray<UMemoryComponentBase*> UMemoryComponentBase::GetCurrentTeam() const
{
	return CurrentTeam;
}

void UMemoryComponentBase::TeamUp(UMemoryComponentBase* MemComp, bool bIsFirstCall)
{
	if (!CurrentTeam.Contains(MemComp) && MemComp != this)
	{
		CurrentTeam.Add(MemComp);
		TetheredFriendlies.Add(MemComp, MemComp->MyData);
	}
	if (GetCurrentTeam().Num())
	{
		bIsInFormation = true;
		if (!TeamHasLeader() && (TeamRole == TR_AttackMelee_1h || TeamRole == TR_AttackMelee_2h))
		{
			bIsLeader = true;
		}
	}
	if (bIsFirstCall) MemComp->TeamUp(this, false);
}

void UMemoryComponentBase::LeaveTeam()
{
	if (GetCurrentTeam().Num())
	{
		for (auto Teammate : GetCurrentTeam())
		{
			Teammate->RemoveTeammate(Teammate);
		}
	}
}

void UMemoryComponentBase::RemoveTeammate(UMemoryComponentBase* TeammateToRemove)
{
	if (TeammateToRemove)
	{
		if (CurrentTeam.Contains(TeammateToRemove))
		{
			CurrentTeam.Remove(TeammateToRemove);
		}
		if (TetheredFriendlies.Contains(TeammateToRemove))
		{
			TetheredFriendlies.Remove(TeammateToRemove);
		}
	}
}

void UMemoryComponentBase::RemoveEnemyFromMemory(UMemoryComponentBase* EnemyToRemove)
{
	if (EnemyToRemove)
	{
		if (EnemiesInMemory.Contains(EnemyToRemove->GetOwner()))
		{
			EnemiesInMemory.Remove(EnemyToRemove->GetOwner());
			RelativeEnemyData.Remove(EnemyToRemove->GetOwner());
		}
		if (TetheredEnemies.Contains(EnemyToRemove))
		{
			TetheredEnemies.Remove(EnemyToRemove);
		}
	}
}

bool UMemoryComponentBase::CheckInDanger() const
{
	if (!EnemiesInMemory.Num()) return false;
	for (auto& Pair : EnemiesInMemory)
	{
		if (EnemyIsInRange(Pair.Key, 1000.f)) return true;
	}
	return false;
}

AActor* UMemoryComponentBase::GetNearestEnemy() const
{
	if (!EnemiesInMemory.Num()) return nullptr;
	float Distance = -1.f;
	AActor* NearestEnemy = nullptr;
	for (auto& Pair : EnemiesInMemory)
	{
		float CheckedDistance = FVector::Distance(GetOwner()->GetActorLocation(), Pair.Key->GetActorLocation());
		if (Distance < 0)
		{
			Distance = CheckedDistance;
			NearestEnemy = Pair.Key;
		}
		else if (CheckedDistance < Distance)
		{
			Distance = CheckedDistance;
			NearestEnemy = Pair.Key;
		}
	}
	return NearestEnemy;
}

bool UMemoryComponentBase::OnOwnerReceiveAggro(AActor* AggroActor, float Amount, bool bHasDealtDamage)
{
	if (RelativeEnemyData.Contains(AggroActor))
	{
		if (!bAggroEngaged)
		{
			RelativeEnemyData[AggroActor].AggroScore += int32(Amount);
			if (RelativeEnemyData[AggroActor].AggroScore > OwningCharacter->AggroThreshold)
			{
				bAggroEngaged = true;
				return true;
			}
			if (bHasDealtDamage)
			{
				RelativeEnemyData[AggroActor].DamageDealt += Amount;
				RelativeEnemyData[AggroActor].LastTimeDealtDamage = GetWorld()->GetTimeSeconds();
				if (RelativeEnemyData[AggroActor].DamageDealt > MyData.MaxHealth / 2.f)
				{
					return true;
				}
			}
		}
	}
	// add actor to memory if not present
	else
	{
		if (IMemoryInterface* EnemyActor = Cast<IMemoryInterface>(AggroActor))
		{
			EnemyActor->Execute_GetEnemyData(Cast<UObject>(EnemyActor), this);
		}
	}
	return false;
}

float UMemoryComponentBase::GetBuildingHealth(AActor* BuildingToFind) const
{
	if (BuildingData.IsEmpty() || !BuildingToFind) return -1.f;
	for (auto& BData : BuildingData)
	{
		if (BuildingToFind == BData.Building)
		{
			return BData.BuildingHealth / BData.BuildingMaxHealth;
		}
	}
	return -1.f;
}

void UMemoryComponentBase::SetLeader(bool bOverrideCurrentLeader, bool bRandomize)
{
	if (CurrentTeam.Num())
	{
		// unset current leader
		if (bOverrideCurrentLeader)
		{
			for (auto Teammate : CurrentTeam)
			{
				if (Teammate->bIsLeader) Teammate->bIsLeader = false;
			}
		}
		bool bHasLeader = false;
		// check if team has leader
		for (auto Teammate : CurrentTeam)
		{
			if (Teammate->bIsLeader) {bHasLeader = true; break;}
		}
		if (!bHasLeader && bRandomize)
		{
			int32 TeamIndex = FMath::RandRange(0, CurrentTeam.Num() - 1);
			CurrentTeam[TeamIndex]->bIsLeader = true;
			return;
		}
	}
	bIsLeader = true;
}

void UMemoryComponentBase::AssignFormationPos()
{
	if (bIsLeader)
	{
		bool bPositionFlipFlopLat = false;
		bool bPositionFlipFlopCentre = false;
		for (auto& Teammate : CurrentTeam)
		{
            if (IMemoryInterface* IntMem = Cast<IMemoryInterface>(Teammate->GetOwner()))
            {
                if (UMemoryComponentBase* MemComp = IntMem->Execute_GetCharacterMemory(Cast<UObject>(IntMem)))
                {
					MemComp->bIsInFormation = true;
					if (MemComp->TeamRole == TR_FlankMelee)
					{
						bPositionFlipFlopLat ? MemComp->FormationPosition = EFP_Right : MemComp->FormationPosition = EFP_Left;
						bPositionFlipFlopLat = !bPositionFlipFlopLat;
					}
					else if (MemComp->TeamRole == TR_AttackMelee_1h)
					{
						bPositionFlipFlopCentre ? MemComp->FormationPosition = EFP_CentreRight : MemComp->FormationPosition = EFP_CentreLeft;
						bPositionFlipFlopCentre = !bPositionFlipFlopCentre;
					}
                }
            }
		}
	}
}

// should only be called when CurrentTeam.Num() > 0
UMemoryComponentBase* UMemoryComponentBase::GetNearestTeammateTo(AActor* Object, bool bIgnoreLeader)
{
	if (Object) 
	{
		UMemoryComponentBase* ClosestTeammate = nullptr;
		float LastDistance = -1.f;
		for (auto& Teammate : CurrentTeam)
		{
			float Distance = FVector::Distance(Teammate->MyData.LastSeenLocation, Object->GetActorLocation());
			if (LastDistance < 0) 
			{
				LastDistance = Distance; 
				ClosestTeammate = Teammate;
				continue;
			}
			else if (Distance < LastDistance)
			{
				ClosestTeammate = Teammate;
			}
			LastDistance = Distance;
		}
		// check leader against closest teammate if required
		if (!bIgnoreLeader)
		{
			float MyDistance = FVector::Distance(GetOwner()->GetActorLocation(), Object->GetActorLocation());
			if (MyDistance < FVector::Distance(ClosestTeammate->MyData.LastSeenLocation, Object->GetActorLocation()))
			{
				ClosestTeammate = this;
			}
		}
		return ClosestTeammate;
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("MemoryComp: Requested closest teammate but object parameter was null"));
		return nullptr;
	}
}

// TARGET FUNCTIONS
void UMemoryComponentBase::CheckUnperceivedEnemies(TArray<AActor*> ArrayToCheck)
{
	if (!RelativeEnemyData.Num()) return;
	for (auto& SeenActor : ArrayToCheck)
	{
		if (SeenActor && SeenActor->IsValidLowLevelFast())
		{
			if (RelativeEnemyData.Contains(SeenActor))
			{
				RelativeEnemyData[SeenActor].bIsCurrentlyPerceived = true;
				RelativeEnemyData[SeenActor].TimeSincePerceived = 0;
				if (GetWorld()->GetTimerManager().IsTimerActive(RelativeEnemyData[SeenActor].TimerHandle)) GetWorld()->GetTimerManager().ClearTimer(RelativeEnemyData[SeenActor].TimerHandle);
			}
		}
	}
	// compare perceived hostiles with enemies in memory, mark those not present in the latter for decay
	TArray<AActor*> EnemiesToDecay;
	for (auto& Pair : RelativeEnemyData)
	{
		if (!ArrayToCheck.Contains(Pair.Key))
		{
			EnemiesToDecay.Add(Pair.Key);
		}
	}

	if (!EnemiesToDecay.IsEmpty())
	{
		// start memory decay
		for (auto& Enemy : EnemiesToDecay)
		{
			if (Enemy && Enemy->IsValidLowLevelFast())
			{
				GetWorld()->GetTimerManager().SetTimer(RelativeEnemyData[Enemy].TimerHandle, FTimerDelegate::CreateUObject(this, &UMemoryComponentBase::DecayMemory, Enemy, RelativeEnemyData[Enemy]), 1.f, true, 1.f);
				RelativeEnemyData[Enemy].bIsCurrentlyPerceived = false;
			}
		}
	}
}

TMap<AActor*, FAbsoluteEnemyData> UMemoryComponentBase::GetEnemiesInMemory() const
{
	return EnemiesInMemory;
}

void UMemoryComponentBase::DecayMemory(AActor* ActorToDecay, FRelativeEnemyData Struct)
{
	// start/continue decay for 60 seconds
	if (Struct.TimeSincePerceived <= 60.f)
	{
		Struct.TimeSincePerceived += 1.f;
	}
	// remove enemy from memory after 60 seconds
	else
	{
		EnemiesInMemory.Remove(ActorToDecay);
		// untether enemy update
		UMemoryComponentBase* OtherMemComp = ActorToDecay->FindComponentByClass<UMemoryComponentBase>();
		if (OtherMemComp)
		{
			OtherMemComp->TetheredEnemies.Remove(this);
		}
		GetWorld()->GetTimerManager().ClearTimer(Struct.TimerHandle);
	}
}

void UMemoryComponentBase::AddBuildingData(ABuilding* BuildingToAdd, FAIBuildingData DataToAdd)
{
	BuildingMap.Add(BuildingToAdd, DataToAdd);
	BuildingData.Add(DataToAdd);
	if (!GetOwner()->HasAuthority())
	{
		Server_AddBuildingData(BuildingToAdd, DataToAdd);
	}
}

void UMemoryComponentBase::Server_AddBuildingData_Implementation(ABuilding* BuildingToAdd, FAIBuildingData DataToAdd)
{
	AddBuildingData(BuildingToAdd, DataToAdd);
}

/* scoring system assigns score to each building based on 1) building material type, 2) distance from player and 
3) Building health. Highest score = target */
// TODO refine to order according to percentage of health left rather than simply lowest to highest
// TODO add weighting parameters
ABuilding* UMemoryComponentBase::SelectBuildingTarget(AActor* BuildingToIgnore)
{
	if (BuildingMap.IsEmpty()) {return nullptr;}
	// initialise array for 3 scores
	TArray<int32> EmptyArray;
	EmptyArray.Init(0, 3);
	// TODO filter out buildings e.g. no hp
	// sort by health
	TMap<ABuilding*, float> HealthMap = SortBuildingHealth();
	int32 HealthScore = 10;
	// initialise value to check to the first (and therefore lowest) health
	TArray<float> HealthArray; 
	HealthMap.GenerateValueArray(HealthArray);
	float CheckedHealth = HealthArray[0];
	for (TPair<ABuilding*, float> Pair : HealthMap)
	{
		// initialise scores tmap
		BuildingScores.Add(Pair.Key, EmptyArray);
		// material score: wood = 10, else = 1
		if (BuildingMap[Pair.Key].BuildingType == EBuildingType::BT_Wood)
		{
			BuildingScores[Pair.Key][0] = 10;
		}
		else
		{
			BuildingScores[Pair.Key][0] = 1;
		}
		// give buildings with no health a negative score
		if (Pair.Value <= 0)
		{
			BuildingScores[Pair.Key][0] = -100;
		}
		else if (Pair.Value == CheckedHealth)
		{
			BuildingScores[Pair.Key][1] = HealthScore;
		}
		else if (Pair.Value > CheckedHealth)
		{
			BuildingScores[Pair.Key][1] = --HealthScore;
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("Memory component: Building health scores have not been ordered correctly"));
		}
		CheckedHealth = Pair.Value;
	}
	// sort by distance
	TMap<ABuilding*, float> DistanceMap = SortBuildingDistance();
	// initialise first value to check
	TArray<float> DistanceArray; 
	DistanceMap.GenerateValueArray(DistanceArray);
	float CheckedDistance = DistanceArray[0];
	int32 DistanceScore = 10;
	for (TPair<ABuilding*, float> Pair : DistanceMap)
	{
		if (Pair.Value == CheckedDistance)
		{
			BuildingScores[Pair.Key][2] = DistanceScore;
		}
		else if (Pair.Value > CheckedDistance)
		{
			BuildingScores[Pair.Key][2] = --DistanceScore;
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("Building Distance scores have not been ordered correctly"));
		}
		CheckedDistance = Pair.Value;
	}
	// evaluate targets
	ABuilding* CurrentTarget = nullptr;
	int32 HighScore = 0;
	for (TPair<ABuilding*, TArray<int32>> Pair : BuildingScores)
	{
		// optional skip building
		if (BuildingToIgnore && Pair.Key == BuildingToIgnore) {continue;}
		// skip negative scores
		int32 CurrentScore = 0;
		for (int32 Score : Pair.Value)
		{
			if (Score <= 0) {continue;}
			CurrentScore += Score;
		}
		if (CurrentScore > HighScore)
		{
			HighScore = CurrentScore;
			CurrentTarget = Pair.Key;
		}
	}

	return CurrentTarget;
}

/* Function sorts four FEnemyData variables from 'best' to 'worst', then assigns score
based on taking the difference between best and worst, dividing that by 10 (scores range from 1-10) 
and dividing values by that difference. Highest total scores = target*/
AActor* UMemoryComponentBase::SelectEnemyTarget(AActor* EnemyToIgnore, bool IgnoreUnperceivedEnemies, bool bAutoSetEnemyTarget)
{
	// aggro system
	if (bAggroEngaged && RelativeEnemyData.Num() && !GetWorld()->GetTimerManager().IsTimerActive(AggroStateTimer))
	{
		int32 HighestAggro = 0;
		AActor* AggroTarget = nullptr;
		for (auto& Pair : RelativeEnemyData)
		{
			if (Pair.Key->IsValidLowLevelFast() && Pair.Value.AggroScore > HighestAggro)
			{
				HighestAggro = Pair.Value.AggroScore;
				AggroTarget = Pair.Key;
			}
		}
		if (AggroTarget)
		{
			FTimerDelegate AggroDelegate;
			AggroDelegate.BindLambda([this]()
				{
					bAggroEngaged = false;
				});
			GetWorld()->GetTimerManager().SetTimer(AggroStateTimer, AggroDelegate, 30.f, false);
			return AggroTarget;
		}
	}
	// engage if damage dealt high enough
	if (RelativeEnemyData.Num())
	{
		for (auto& Pair : RelativeEnemyData)
		{
			if (Pair.Key->IsValidLowLevelFast() && Pair.Value.DamageDealt > MyData.MaxHealth / 2.f)
			{
				return Pair.Key;
			}
		}
	}
	if (EnemiesInMemory.IsEmpty()) {UE_LOG(LogTemp, Warning, TEXT("Memory Component: No enemies in memory. Target selection failed.")); return nullptr;}
	// skip calculation if only one enemy in memory and enemy is perceptible
	else if (EnemiesInMemory.Num() == 1)
	{
		// return if only enemy should be ignored
		if (EnemyToIgnore && EnemiesInMemory.Contains(EnemyToIgnore))
		{
			return nullptr;
		}
		// check attacker limit not reached
		if (IMemoryInterface* IntEnemy = Cast<IMemoryInterface>(EnemiesInMemory.begin()->Key))
		{
			if (IntEnemy->Execute_GetAttackers(Cast<UObject>(IntEnemy), false, true) >= 3)
			{
				return nullptr;
			}
		}
		if (!RelativeEnemyData.begin()->Value.bIsCurrentlyPerceived && !IgnoreUnperceivedEnemies)
		{
			if (bAutoSetEnemyTarget)
			{
				if (OwningEnemyController) OwningEnemyController->EnemyTarget = EnemiesInMemory.begin()->Key;
				else if (OwningVillagerController) OwningVillagerController->EnemyTarget = EnemiesInMemory.begin()->Key;
			}
			return RelativeEnemyData.begin()->Key;
		}
		else if (RelativeEnemyData.begin()->Value.bIsCurrentlyPerceived)
		{
			if (bAutoSetEnemyTarget)
			{
				if (OwningEnemyController) OwningEnemyController->EnemyTarget = EnemiesInMemory.begin()->Key;
				else if (OwningVillagerController) OwningVillagerController->EnemyTarget = EnemiesInMemory.begin()->Key;
			}
			return RelativeEnemyData.begin()->Key;
		}
		else {return nullptr;}
	}
	TMap<AActor*, FAbsoluteEnemyData> FilteredEnemies = EnemiesInMemory;
	TArray<float> ScoresArray;
	// Four variables to rate: health, stamina, distance and rotation
	ScoresArray.Init(0, 4);
	TMap<AActor*, TArray<float>> EnemyScoresMap;
	FVector MyLocation = GetOwner()->GetActorLocation();
	// optionally filter out non perceived actors and passed in actor, and get variable ranges
	for (auto& Pair : EnemiesInMemory) 
	{
		bool bIsInvalid = !Pair.Key || !Pair.Key->IsValidLowLevelFast() || Pair.Value.RemainingHealth <= 0 || (EnemyToIgnore && Pair.Key == EnemyToIgnore) || (IgnoreUnperceivedEnemies && !RelativeEnemyData[Pair.Key].bIsCurrentlyPerceived);
		// check attacker limit not reached
		if (IMemoryInterface* IntEnemy = Cast<IMemoryInterface>(Pair.Key))
		{
			if (IntEnemy->Execute_GetAttackers(Cast<UObject>(IntEnemy), false, true) < 4 || !bIsInvalid)
			{
				FilteredEnemies.Add(Pair.Key); 
				continue; 
			}
		}
	}
	float HighScore = 0;
	float LastScore = 10.f;
	float LastValue = -1.f;
	// isolate variables, sort, get maxes and define gradations
	if (FilteredEnemies.Num())
	{
		TMap<AActor*, float> HealthMap = SortEnemyHealth(FilteredEnemies);
		TMap<AActor*, float> StaminaMap = SortEnemyStamina(FilteredEnemies);
		TMap<AActor*, float> DistanceMap = SortEnemyDistance(FilteredEnemies);
		TMap<AActor*, float> RotationMap = SortEnemyRotation(FilteredEnemies);

		LastScore = 10;
		LastValue = -1.f;
		// assign health scores and apply weightings
		for (TPair<AActor*, float> Pair : HealthMap)
		{
			// initialise scores
			EnemyScoresMap.Add(Pair.Key, ScoresArray);
			if (LastValue < 0)
			{
				LastValue = Pair.Value;
				EnemyScoresMap[Pair.Key][0] = LastScore * EnemyWeightings.HealthWeighting;
			}
			else
			{
				if (Pair.Value == LastValue)
				{
					EnemyScoresMap[Pair.Key][0] = LastScore * EnemyWeightings.HealthWeighting;
					LastValue = Pair.Value;
				}
				else
				{
					EnemyScoresMap[Pair.Key][0] = --LastScore * EnemyWeightings.HealthWeighting;
					LastValue = Pair.Value;
				}
			}
		}
		LastScore = 10;
		LastValue = -1.f;
		// assign stamina scores and apply weightings
		for (TPair<AActor*, float> Pair : StaminaMap)
		{
			if (LastValue < 0)
			{
				LastValue = Pair.Value;
				EnemyScoresMap[Pair.Key][1] = LastScore * EnemyWeightings.StaminaWeighting;
			}
			else
			{
				if (Pair.Value == LastValue)
				{
					EnemyScoresMap[Pair.Key][1] = LastScore * EnemyWeightings.StaminaWeighting;
					LastValue = Pair.Value;
				}
				else
				{
					EnemyScoresMap[Pair.Key][1] = --LastScore * EnemyWeightings.StaminaWeighting;
					LastValue = Pair.Value;
				}
			}
		}
		LastScore = 10;
		LastValue = -1.f;
		// assign distance scores and apply weightings
		for (TPair<AActor*, float> Pair : DistanceMap)
		{
			if (LastValue < 0)
			{
				LastValue = Pair.Value;
				EnemyScoresMap[Pair.Key][2] = LastScore * EnemyWeightings.LocationWeighting;
			}
			else
			{
				if (Pair.Value == LastValue)
				{
					EnemyScoresMap[Pair.Key][2] = LastScore * EnemyWeightings.LocationWeighting;
					LastValue = Pair.Value;
				}
				else
				{
					EnemyScoresMap[Pair.Key][2] = --LastScore * EnemyWeightings.LocationWeighting;
					LastValue = Pair.Value;
				}
			}
		}
		LastScore = 10;
		LastValue = -1.f;
		// assign rotation scores and apply weightings
		for (TPair<AActor*, float> Pair : RotationMap)
		{
			if (LastValue < 0)
			{
				LastValue = Pair.Value;
				EnemyScoresMap[Pair.Key][3] = LastScore * EnemyWeightings.RotationWeighting;
			}
			else
			{
				if (Pair.Value == LastValue)
				{
					EnemyScoresMap[Pair.Key][3] = LastScore * EnemyWeightings.RotationWeighting;
					LastValue = Pair.Value;
				}
				else
				{
					EnemyScoresMap[Pair.Key][3] = --LastScore * EnemyWeightings.RotationWeighting;
					LastValue = Pair.Value;
				}
			}
		}

		// evaluate targets
		AActor* CurrentTarget = nullptr;
		for (TPair<AActor*, TArray<float>> Pair : EnemyScoresMap)
		{
			float CurrentScore = 0;
			for (float Score : Pair.Value)
			{
				CurrentScore += Score;
			}
			if (CurrentScore > HighScore)
			{
				HighScore = CurrentScore;
				CurrentTarget = Pair.Key;
			}
		}
		// only return target if minimum score met
		if (bAutoSetEnemyTarget)
		{
			if (OwningEnemyController) OwningEnemyController->EnemyTarget = CurrentTarget;
			else if (OwningVillagerController) OwningVillagerController->EnemyTarget = CurrentTarget;
		}
		if (HighScore > EnemyWeightings.SelectionThreshold) return CurrentTarget;
	}

	UE_LOG(LogTemp, Warning, TEXT("Memory Component: No enemies to choose from."));
	return nullptr;
}

bool UMemoryComponentBase::EnemyIsInRange(AActor* EnemyToCheck, float Range, bool bShouldUseMaxRange) const
{
	float AppliedRange;
	bShouldUseMaxRange ? AppliedRange = OwningCharacter->ProgressComponent->GetRangedStats().BowMaxRange : AppliedRange = Range;
	if (EnemyToCheck)
	{
		if (GetOwner()->GetDistanceTo(EnemyToCheck) <= AppliedRange)
		{
			return true;
		}
		else
		{
			return false;
		}
	}
	return false;
}

// HELPERS
// isolates building health and sorts from lowest to highest
TMap<ABuilding*, float> UMemoryComponentBase::SortBuildingHealth()
{
	TMap<ABuilding*, float> ReturnedMap;
	for (TPair<ABuilding*, FAIBuildingData> Pair : BuildingMap)
	{
		ReturnedMap.Add(Pair.Key, Pair.Value.BuildingHealth);
	}
	ReturnedMap.ValueSort([](const float x, const float y) {return x < y;});

	return ReturnedMap;
}

TMap<ABuilding*, float> UMemoryComponentBase::SortBuildingDistance()
{
	TMap<ABuilding*, float> ReturnedMap;
	FVector MyLocation = GetOwner()->GetActorLocation();
	for (TPair<ABuilding*, FAIBuildingData> Pair : BuildingMap)
	{
		float Distance = FVector::Distance(MyLocation, Pair.Value.BuildingLocation);
		ReturnedMap.Add(Pair.Key, Distance);
	}
	ReturnedMap.ValueSort([](const float x, const float y) {return x < y;});

	return ReturnedMap;
}

TMap<AActor*, float> UMemoryComponentBase::SortEnemyHealth(TMap<AActor*, FAbsoluteEnemyData> FilteredEnemies)
{
	TMap<AActor*, float> ReturnedMap;
	for (auto& Pair : FilteredEnemies)
	{
		ReturnedMap.Add(Pair.Key, Pair.Value.RemainingHealth);
	}
	ReturnedMap.ValueSort([](const float x, const float y) {return x < y;});

	return ReturnedMap;
}

TMap<AActor*, float> UMemoryComponentBase::SortEnemyDistance(TMap<AActor*, FAbsoluteEnemyData> FilteredEnemies)
{
	TMap<AActor*, float> ReturnedMap;
	FVector MyLocation = GetOwner()->GetActorLocation();
	for (auto& Pair : FilteredEnemies)
	{
		float Distance = FVector::Distance(MyLocation, Pair.Key->GetActorLocation());
		ReturnedMap.Add(Pair.Key, Distance);
	}
	ReturnedMap.ValueSort([](const float x, const float y) {return x < y;});

	return ReturnedMap;
}

TMap<AActor*, float> UMemoryComponentBase::SortEnemyStamina(TMap<AActor*, FAbsoluteEnemyData> FilteredEnemies)
{
	TMap<AActor*, float> ReturnedMap;
	for (auto& Pair : FilteredEnemies)
	{
		ReturnedMap.Add(Pair.Key, Pair.Value.RemainingStamina);
	}
	ReturnedMap.ValueSort([](const float x, const float y) {return x < y;});

	return ReturnedMap;
}

TMap<AActor*, float> UMemoryComponentBase::SortEnemyRotation(TMap<AActor*, FAbsoluteEnemyData> FilteredEnemies)
{
	TMap<AActor*, float> ReturnedMap;
	for (auto& Pair : FilteredEnemies)
	{
		FRotator EnemyToOwnerVec = UKismetMathLibrary::FindLookAtRotation(Pair.Key->GetActorLocation(), GetOwner()->GetActorLocation());
		float EnemyRotationDot = EnemyToOwnerVec.Vector().Dot(Pair.Key->GetActorForwardVector());
		ReturnedMap.Add(Pair.Key, EnemyRotationDot);
	}
	ReturnedMap.ValueSort([](const float x, const float y) {return x < y;});
	
	return ReturnedMap;
}

float UMemoryComponentBase::GetTeamHealth() const
{
	float MaxHP = OwningCharacter->GetMaxHealth();
	float CurrentHP = MyData.RemainingHealth;
	for (auto& Teammate : CurrentTeam)
	{
		if (Teammate)
		{
			MaxHP += Teammate->OwningCharacter->GetMaxHealth();
			CurrentHP += Teammate->MyData.RemainingHealth;
		}
	}
	return CurrentHP / MaxHP;
}
