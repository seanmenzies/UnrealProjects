// Fill out your copyright notice in the Description page of Project Settings.

#include "MemoryComponentBase.h"
#include "../AI/NPCAIController.h"

// Sets default values for this component's properties
UMemoryComponentBase::UMemoryComponentBase()
{
	// Set this component to be initialized when the game starts, and to be ticked every frame.  You can turn these features
	// off to improve performance if you don't need them.
	PrimaryComponentTick.bCanEverTick = true;
}

// Called when the game starts
void UMemoryComponentBase::BeginPlay()
{
	Super::BeginPlay();
	
	MyData = FEnemyData();
	UpdateMyData();
	MyData.Character = GetOwner();
}

// Called every frame
void UMemoryComponentBase::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// update all tethered enemies FEnemyData every half a second
	if (UpdateTetheredTimer > 0.5 && !TetheredEnemies.IsEmpty())
	{
		UpdateMyData();
		for (TPair<UMemoryComponentBase*, FEnemyData> Pair : TetheredEnemies)
		{
			if (Pair.Key == this)
			{
				Pair.Value.ArmorType = MyData.ArmorType;
				Pair.Value.WeaponType = MyData.WeaponType;
				Pair.Value.RemainingHealth = MyData.RemainingHealth;
				Pair.Value.RemainingStamina = MyData.RemainingStamina;
				Pair.Value.LastRotation = MyData.LastRotation;
				if (GetOwner()->GetDistanceTo(Pair.Value.Character) < 1000.f)
				{
					auto EnemyInterface = Cast<IMemoryInterface>(Pair.Value.Character);
					if (EnemyInterface)
					{
						EnemyInterface->Execute_NotifyClose(Cast<UObject>(EnemyInterface), GetOwner());
					}
				}
				Pair.Value.LastSeenLocation = MyData.LastSeenLocation;
			}
		}
		// TODO Add friendly tethered
	}
	else if (!TetheredEnemies.IsEmpty())
	{
		UpdateTetheredTimer += DeltaTime;
	}
}

void UMemoryComponentBase::UpdateMyData()
{
	MyData.RemainingHealth = Health;
	MyData.RemainingStamina = Stamina;
	MyData.ArmorType = ArmorType;
	MyData.WeaponType = WeaponType;
	MyData.LastSeenLocation = GetOwner()->GetActorLocation();
	MyData.LastRotation = GetOwner()->GetActorRotation();
}

FEnemyData UMemoryComponentBase::GetData(UMemoryComponentBase* MemComp)
{
	// Assumed that any actor requesting data will then be tethered (and added to the to-keep-updated list)
	UpdateMyData();
	TetheredEnemies.Add(MemComp, MyData);
	return MyData;
}

// On Death, access all tethered enemies forcing them to remove owner from memory
void UMemoryComponentBase::ForgetMe(AActor* DeadActor)
{
	TArray<UMemoryComponentBase*> TetheredMemories;
	TetheredEnemies.GenerateKeyArray(TetheredMemories);
	for (auto& Memory : TetheredMemories)
	{
		if (Memory->EnemiesInMemory.Contains(DeadActor))
		{
			EnemiesInMemory.Remove(DeadActor);
			break;
		}
	}
	// clear memory
	EnemiesInMemory.Empty();
	TetheredEnemies.Empty();
}

bool UMemoryComponentBase::TeamHasLeader() const
{
	if (CurrentTeam.IsEmpty()) { return false; }
	else
	{
		for (auto& Teammate : CurrentTeam)
		{
			if (Teammate->TeamRole == ETeamRole::TR_Leader)
			{
				return true;
			}
		}
		return false;
	}
}

bool UMemoryComponentBase::IsInTeam() const
{
	if (CurrentTeam.IsEmpty()) {return false;}
	else {return true;}
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

void UMemoryComponentBase::AddEnemyData(FEnemyData DataToAdd)
{
	// assumed that any time we add data, the enemy will be in perception
	DataToAdd.bIsCurrentlyPerceived = true;
	// update data if already exists
	if (EnemiesInMemory.Contains(DataToAdd.Character))
	{
		EnemiesInMemory[DataToAdd.Character] = DataToAdd;
	}
	// limit memory to 10 enemies at a time
	else if (EnemiesInMemory.Num() <= 10)
	{
		EnemiesInMemory.Add(DataToAdd.Character, DataToAdd);
	}
	// TODO logic for deleting decayed memories if at capacity
}

void UMemoryComponentBase::CheckUnperceivedEnemies(TArray<AActor*> ArrayToCheck)
{
	TArray<AActor*> EnemiesToDecay;
	EnemiesInMemory.GenerateKeyArray(EnemiesToDecay);
	// compare perceived hostiles with enemies in memory, mark those not present in the latter for decay
	for (auto& Actor : ArrayToCheck)
	{
		if (EnemiesInMemory.Contains(Actor))
		{
			EnemiesToDecay.Remove(Actor);
		}
	}
	if (!EnemiesToDecay.IsEmpty())
	{
		// start memory decay
		for (auto& Enemy : EnemiesToDecay)
		{
			GetWorld()->GetTimerManager().SetTimer(EnemiesInMemory[Enemy].TimerHandle, FTimerDelegate::CreateUObject(this, &UMemoryComponentBase::DecayMemory, Enemy, EnemiesInMemory[Enemy]), 1.f, true, 1.f);
			EnemiesInMemory[Enemy].bIsCurrentlyPerceived = false;
		}
	}
}

void UMemoryComponentBase::DecayMemory(AActor* ActorToDecay, FEnemyData Struct)
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
	}
}

void UMemoryComponentBase::RemoveEnemyFromMemory(AActor* ActorToRemove)
{
	if (EnemiesInMemory.Contains(ActorToRemove))
	{
		EnemiesInMemory.Remove(ActorToRemove);
	}
	// force enemy target recheck if Actor to remove is current target
}
