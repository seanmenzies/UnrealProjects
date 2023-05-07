// Fill out your copyright notice in the Description page of Project Settings.


#include "PlayerDamageComponent.h"
#include "../AI/Animal.h"
#include "../AI/AIBaseCharacter.h"
#include "../AI/Villager.h"
#include "../AI/NPCAIController.h"
#include "../Interfaces/PlayerAIInteractionInterface.h"
#include "../Interfaces/FXAudioInterface.h"
#include "../Items/EquippableItem.h"
#include "../Items/GearItem.h"
#include "../Items/ProjectileBase.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "NiagaraComponent.h"
#include "../Player/EalondCharacter.h"
#include "../Progress/CharacterProgressComponent.h"
#include "../World/EalondCharacterBase.h"
#include "../World/ResourceActor.h"
#include "GameFramework/DamageType.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "GenericTeamAgentInterface.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Kismet/KismetMathLibrary.h"
#include "NiagaraSystem.h"
#include "NiagaraFunctionLibrary.h"


// Sets default values for this component's properties
UPlayerDamageComponent::UPlayerDamageComponent()
{
	// Set this component to be initialized when the game starts, and to be ticked every frame.  You can turn these features
	// off to improve performance if you don't need them.
	PrimaryComponentTick.bCanEverTick = false;
}

// Called when the game starts
void UPlayerDamageComponent::BeginPlay()
{
	Super::BeginPlay();

	PlayerCharacter = Cast<AEalondCharacterBase>(GetOwner());
	if (PlayerCharacter && !PlayerCharacter->IsA(AAIBaseCharacter::StaticClass()))
	{
		ProgComp = PlayerCharacter->ProgressComponent;
	}
}

void UPlayerDamageComponent::WeaponTrace()
{
	if (PlayerCharacter->GetEquippedItems().Num())
	{
		// Get currently equipped weapon and return if none found
		if (!GetWeapon()) return;
		// set bShouldContinueTrace here
		FHitResult HitResult;
		FVector TraceEnd;
		IgnoredActors.AddUnique(PlayerCharacter);
		if (PlayerCharacter->bIsBlocking)
		{
			FVector TraceStart = PlayerCharacter->GetMesh()->GetSocketLocation(TEXT("shield"));
			FVector BoxExtent = FVector(CurrentWeapon->DamageStats.WeaponWidth, CurrentWeapon->DamageStats.WeaponLength, 0);
			FRotator BoxRot = FRotator(PlayerCharacter->ShieldMesh->GetComponentRotation());
			// add traces if weapon swing too quick
			FRotator TraceOrientation = UKismetMathLibrary::FindLookAtRotation(PlayerCharacter->GetMesh()->GetSocketLocation(TEXT("spine_01")), TraceStart);
			TraceEnd = TraceOrientation.Vector() * 20.f + TraceStart;
			bool bHitSuccess = UKismetSystemLibrary::BoxTraceSingle(GetWorld(), TraceStart, TraceEnd, BoxExtent, FRotator(90.f, BoxRot.Pitch, BoxRot.Yaw), UEngineTypes::ConvertToTraceType(ECollisionChannel::ECC_GameTraceChannel4), false, IgnoredActors, EDrawDebugTrace::None, HitResult, true);
			if (bHitSuccess && HitResult.GetActor())
			{
				FVector LastEndPoint = LastStartPosition + LastDirection * CurrentWeapon->DamageStats.WeaponLength;
				FVector HitDirection = (TraceEnd - LastEndPoint).GetSafeNormal();
				OnHitActor(HitResult, HitDirection);
				bHasHit = true;
			}
			LastStartPosition = TraceStart;
			LastDirection = TraceOrientation.Vector();
		}
		else
		{
			FVector TraceStart = PlayerCharacter->CharacterMesh->GetSocketLocation(TEXT("righthand"));
			float TraceDistance = FVector::Distance(TraceStart, LastStartPosition);
			TraceStart = PlayerCharacter->CharacterMesh->GetSocketLocation(TEXT("righthand"));
			FRotator TraceDirection = UKismetMathLibrary::FindLookAtRotation(TraceStart, PlayerCharacter->CharacterMesh->GetSocketLocation(TEXT("weapontraceoffset")));
			TraceEnd = TraceDirection.Vector() * CurrentWeapon->DamageStats.WeaponLength + TraceStart;
			// add traces if weapon swing too quick
			if (LastEndPosition != FVector(0, 0, 0))
			{
				float TraceDiff = FVector::Distance(LastEndPosition, TraceEnd);
				if (TraceDiff > 20.f)
				{
					int32 TotalIts = FMath::DivideAndRoundUp(TraceDiff, 20.f);
					for (int32 i = 1; i < TotalIts; i++)
					{
						FVector NewTraceStart;
						FVector NewTraceEnd;
						GetTraceMidpoints(LastStartPosition, TraceStart, LastEndPosition, TraceEnd, TotalIts, i, NewTraceStart, NewTraceEnd);
						bool bHitSuccess = UKismetSystemLibrary::SphereTraceSingle(GetWorld(), NewTraceStart, NewTraceEnd, CurrentWeapon->DamageStats.WeaponWidth, UEngineTypes::ConvertToTraceType(ECollisionChannel::ECC_GameTraceChannel4), false, IgnoredActors, EDrawDebugTrace::None, HitResult, true);
						if (bHitSuccess && HitResult.GetActor())
						{
							FVector LastEndPoint = LastStartPosition + LastDirection * CurrentWeapon->DamageStats.WeaponLength;
							FVector HitDirection = (NewTraceEnd - LastEndPoint).GetSafeNormal();
							OnHitActor(HitResult, HitDirection);
							bHasHit = true;
						}
					}
				}
			}
			bool bHitSuccess = UKismetSystemLibrary::SphereTraceSingle(GetWorld(), TraceStart, TraceEnd, CurrentWeapon->DamageStats.WeaponWidth, UEngineTypes::ConvertToTraceType(ECollisionChannel::ECC_GameTraceChannel4), false, IgnoredActors, EDrawDebugTrace::None, HitResult, true);
			if (bHitSuccess && HitResult.GetActor())
			{
				// calculations for physical animation
				FVector LastEndPoint = LastStartPosition + LastDirection * CurrentWeapon->DamageStats.WeaponLength;
				FVector HitVelocity = (TraceEnd - LastEndPoint) * FMath::Clamp(FVector::Distance(LastEndPoint, TraceEnd) / GetWorld()->GetDeltaSeconds(), 0, 5000.f);
				OnHitActor(HitResult, HitVelocity);
				bHasHit = true;
			}
			LastStartPosition = TraceStart;
			LastEndPosition = TraceEnd;
			LastDirection = TraceDirection.Vector();
		}
	}
}

void UPlayerDamageComponent::ResetTraceVariables()
{
	IgnoredActors.Empty();
	LastStartPosition = FVector(0, 0, 0);
	LastEndPosition = FVector(0, 0, 0);
	LastDirection = FVector(0, 0, 0);
	bHasHit = false;
}

void UPlayerDamageComponent::OnHitActor(FHitResult IN_HitResult, FVector HitDirection)
{
	if (PlayerCharacter && PlayerCharacter->GetEquippedItems().Num())
	{
		bool bCanDamageBuilding = false;
		IgnoredActors.AddUnique(IN_HitResult.GetActor());
		// recoil
		if (PlayerCharacter->IsA(AAIBaseCharacter::StaticClass()))
		{
			bCanDamageBuilding = true;
			if (IN_HitResult.GetActor()->IsA(AResourceActor::StaticClass()))
			{
				PlayerCharacter->bBodyRecoilSmall = true;
			}
		}
		// on hit resource or building
		else
		{
			if (IN_HitResult.GetActor()->IsA(ABuilding::StaticClass()) || IN_HitResult.GetActor()->IsA(AResourceActor::StaticClass()))
			{
				if (!PlayerCharacter->bToolEquipped)
				{
					PlayerCharacter->bBodyRecoilSmall = true;
					PlayerCharacter->Server_SetIsAttacking(false);
				}
				else
				{
					if (AResourceActor* Resource = Cast<AResourceActor>(IN_HitResult.GetActor()))
					{
						if (Resource->HasAcceptedTool(Cast<UToolItem>(PlayerCharacter->EquippedItems[EEquippableSlot::EIS_Tool])))
						{
							ApplyDamage(IN_HitResult, HitDirection, CalculateDamage(PlayerCharacter->EquippedItems[EEquippableSlot::EIS_Tool]));
						}
					}
					else if (ABuilding* Building = Cast<ABuilding>(IN_HitResult.GetActor()))
					{
						if (Building->HasAcceptedTool(Cast<UToolItem>(PlayerCharacter->EquippedItems[EEquippableSlot::EIS_Tool])))
						{
							ApplyDamage(IN_HitResult, HitDirection, CalculateDamage(PlayerCharacter->EquippedItems[EEquippableSlot::EIS_Tool]));
						}
					}
				}
			}
			else if (IN_HitResult.GetActor()->IsA(AAnimal::StaticClass()))
			{
				ApplyDamage(IN_HitResult, IN_HitResult.GetActor()->GetActorLocation() - GetOwner()->GetActorLocation(), CalculateDamage(CurrentWeapon));
			}
		}
		// play fx
		if (auto IntHitObj = Cast<IFXAudioInterface>(IN_HitResult.GetActor()))
		{
			FFXData FXData;
			FXData.ImpactMaterial = EIM_MetalWeapon;
			FXData.HitLocation = IN_HitResult.Location;
			FXData.HitNormal = IN_HitResult.Normal;
			FXData.BoneHit = IN_HitResult.BoneName;
			if (!IN_HitResult.GetActor()->IsA(ACharacter::StaticClass())) IntHitObj->Execute_PlayFXOnHit(Cast<UObject>(IntHitObj), FXData, PlayerCharacter);
			// hit characters if tool not equipped; fx played from child character OnTakeHit function
			else if (!PlayerCharacter->bToolEquipped)
			{
				if (auto Int_HitActor = Cast<IPlayerAIInteractionInterface>(IN_HitResult.GetActor()))
				{
					// check block/parry
					if (Int_HitActor->Execute_TakeHit(Cast<UObject>(Int_HitActor), PlayerCharacter, IN_HitResult.GetComponent(), CurrentWeapon->DamageStats.WeaponBaseDamage, FXData))
					{
						// add impulse
						if (PlayerCharacter->bIsBlocking && GetWeapon())
						{
							CurrentWeapon->DamageStats.bImpulseDamage = true;
							CurrentWeapon->DamageStats.ImpulseDirection = HitDirection;
						}
						else
						{
							CurrentWeapon->DamageStats.bImpulseDamage = false;
						}
						ApplyDamage(IN_HitResult, HitDirection, CalculateDamage(CurrentWeapon));
						return;
					}
					else
					{
						PlayerCharacter->bBodyRecoilBig = true;
					}
				}
			}
			// hit building
			if (bCanDamageBuilding && IN_HitResult.GetActor()->IsA(ABuilding::StaticClass()))
			{
				ApplyDamage(IN_HitResult, HitDirection, CalculateDamage(CurrentWeapon));
			}
		}
	}
}

void UPlayerDamageComponent::ApplyDamage_Implementation(FHitResult IN_HitResult, FVector HitDirection, float TotalDamage)
{
	if (GetOwner())
	{
		// check opposite "team"
		if (auto ActorTeamID = Cast<IGenericTeamAgentInterface>(IN_HitResult.GetActor()))
		{
			if (ActorTeamID->GetGenericTeamId() != PlayerCharacter->GetGenericTeamId())
			{
				float DamageDealth = UGameplayStatics::ApplyDamage(IN_HitResult.GetActor(), TotalDamage, PlayerCharacter->GetInstigatorController(), PlayerCharacter, UEalondDamageType::StaticClass());
				float XPToAward = FMath::DivideAndRoundDown(int32(DamageDealth), 10);
				// increase depending on weapon
				if (ProgComp) ProgComp->IncreaseXP(PlayerCharacter, EXPType::EXP_Combat, XPToAward, false);
			}
			else
			{
				UGameplayStatics::ApplyDamage(IN_HitResult.GetActor(), TotalDamage, PlayerCharacter->GetInstigatorController(), PlayerCharacter, UEalondDamageType::StaticClass());
			}
		}
		else if (IN_HitResult.GetActor()->IsA(ABuilding::StaticClass()) || IN_HitResult.GetActor()->IsA(AResourceActor::StaticClass()))
		{
				UGameplayStatics::ApplyDamage(IN_HitResult.GetActor(), TotalDamage, PlayerCharacter->GetController(), PlayerCharacter, UEalondDamageType::StaticClass());
		}
	}
}

void UPlayerDamageComponent::RangedTrace(AProjectileBase* ProjectileObject, FVector SweepHitLocation, float MaxVelocity, float FinalVelocity)
{
	if (ProjectileObject && PlayerCharacter->GetEquippedItems().Num())
	{
		if (UEquippableItem* EquippedArrow = GetEquippedArrow())
		{
			float DamageFallOff = 1.f;
			if (FinalVelocity / MaxVelocity < .95) DamageFallOff = .7;
			else if (FinalVelocity / MaxVelocity < .96) DamageFallOff = .75;
			else if (FinalVelocity / MaxVelocity < .97) DamageFallOff = .8;
			else if (FinalVelocity / MaxVelocity < .98) DamageFallOff = .85;
			else if (FinalVelocity / MaxVelocity < .99) DamageFallOff = .9;
			else if (FinalVelocity / MaxVelocity < 1.f) DamageFallOff = .95;
			float ArrowDamage = EquippedArrow->DamageStats.WeaponBaseDamage;
			if (GetWeapon()) ArrowDamage *= CurrentWeapon->DamageStats.BowDamageMultiplier;
			ArrowDamage *= PlayerCharacter->GetEquippedItems()[EEquippableSlot::EIS_Ranged]->DamageStats.BowDamageMultiplier;
			ArrowDamage *= DamageFallOff;
			IgnoredActors.Empty();
			if (ProjectileObject->IsValidLowLevel())
			{
				IgnoredActors.Add(ProjectileObject);
			}
			IgnoredActors.Add(PlayerCharacter);
			FHitResult HitResult;
			FRotator ArrowRotation = ProjectileObject->GetActorRotation();
			FVector TraceEnd = ArrowRotation.Vector() * EquippedArrow->DamageStats.WeaponLength + SweepHitLocation;
			bool HitSuccess = UKismetSystemLibrary::SphereTraceSingle(GetWorld(), SweepHitLocation, TraceEnd, EquippedArrow->DamageStats.WeaponWidth, UEngineTypes::ConvertToTraceType(ECollisionChannel::ECC_GameTraceChannel4), false, IgnoredActors, EDrawDebugTrace::None, HitResult, true);
			if (HitSuccess && HitResult.GetActor())
			{
				if (HitResult.GetActor()->IsA(ACharacter::StaticClass()))
				{
					ProjectileObject->ProjectileComp->StopMovementImmediately();
					ProjectileObject->ProjectileComp->DestroyComponent();
					if ((ProjectileObject->Shooter && HitResult.GetActor()->IsA(AAIBaseCharacter::StaticClass())) || HitResult.GetActor()->IsA(AAnimal::StaticClass()))
					{
						if (HitResult.BoneName == FName("head"))
						{
							ApplyDamage(HitResult, ArrowRotation.Vector(), ArrowDamage);
						}
						else
						{
							ApplyDamage(HitResult, ArrowRotation.Vector(), ArrowDamage);						}
						}
					FAttachmentTransformRules AttachRules = FAttachmentTransformRules(EAttachmentRule::KeepWorld, EAttachmentRule::KeepWorld, EAttachmentRule::KeepWorld, true);
					FVector StickLoc = ProjectileObject->GetActorForwardVector() * -5.f + HitResult.Location;
					ProjectileObject->SetActorLocation(StickLoc);
					ProjectileObject->AttachToComponent(HitResult.GetComponent(), AttachRules, HitResult.BoneName);
					if (auto Int_HitActor = Cast<IFXAudioInterface>(HitResult.GetActor()))
					{
						FFXData FXData;
						FXData.ImpactMaterial = EIM_WoodProjectile;
						FXData.HitLocation = HitResult.Location;
						FXData.HitNormal = HitResult.Normal;
						Int_HitActor->Execute_PlayFXOnHit(Cast<UObject>(Int_HitActor), FXData, PlayerCharacter);
					}
				}
				// check for penetrable building material
				else if (ABuilding* HitBuilding = Cast<ABuilding>(HitResult.GetActor()))
				{
					if (auto Int_HitBuilding = Cast<IFXAudioInterface>(HitResult.GetActor()))
					{
						FFXData FXData;
						FXData.HitLocation = HitResult.Location;
						FXData.HitNormal = HitResult.Normal;
						FXData.ImpactMaterial = EIM_WoodProjectile;
						Int_HitBuilding->Execute_PlayFXOnHit(Cast<UObject>(Int_HitBuilding), FXData, PlayerCharacter);
					}
					if (HitBuilding->BuildingType == EBuildingType::BT_Wood)
					{
						// lodge if hit angle not too great
						float VelocityHitNormalDot = ProjectileObject->ProjectileComp->Velocity.GetSafeNormal().Dot(HitResult.Normal);
						if (VelocityHitNormalDot < .5)
						{
							ProjectileObject->ProjectileComp->StopMovementImmediately();
							ProjectileObject->ProjectileComp->DestroyComponent();
							FVector StickLoc = ProjectileObject->GetActorForwardVector() * -15.f + HitResult.Location;
							ProjectileObject->SetActorLocation(StickLoc);
						}
						else
						{
							ProjectileObject->RootComp->SetCollisionEnabled(ECollisionEnabled::PhysicsOnly);
							ProjectileObject->RootComp->SetSimulatePhysics(true);
							ProjectileObject->MeshComp->SetCollisionEnabled(ECollisionEnabled::PhysicsOnly);
							ProjectileObject->MeshComp->SetSimulatePhysics(true);
							ProjectileObject->ProjectileComp->bShouldBounce = true;
							ProjectileObject->ProjectileComp->Bounciness = 0.3;
						}
					}
					else
					{
						ProjectileObject->RootComp->SetCollisionEnabled(ECollisionEnabled::PhysicsOnly);
						ProjectileObject->RootComp->SetSimulatePhysics(true);
						ProjectileObject->MeshComp->SetCollisionEnabled(ECollisionEnabled::PhysicsOnly);
						ProjectileObject->MeshComp->SetSimulatePhysics(true);
						ProjectileObject->ProjectileComp->bShouldBounce = true;
						ProjectileObject->ProjectileComp->Bounciness = 0.3;
					}
				}
				else if (AResourceActor* HitResource = Cast<AResourceActor>(HitResult.GetActor()))
				{
					if (auto Int_HitResource = Cast<IFXAudioInterface>(HitResult.GetActor()))
					{
						FFXData FXData;
						FXData.HitLocation = HitResult.Location;
						FXData.HitNormal = HitResult.Normal;
						FXData.ImpactMaterial = EIM_WoodProjectile;
						Int_HitResource->Execute_PlayFXOnHit(Cast<UObject>(Int_HitResource), FXData, PlayerCharacter);
					}
					if (HitResource->ResourceMaterial == EResourceMaterial::RM_Wood)
					{
						float VelocityHitNormalDot = ProjectileObject->ProjectileComp->Velocity.GetSafeNormal().Dot(HitResult.Normal);
						if (VelocityHitNormalDot < .5)
						{
							ProjectileObject->ProjectileComp->StopMovementImmediately();
							ProjectileObject->ProjectileComp->DestroyComponent();
							FVector StickLoc = ProjectileObject->GetActorForwardVector() * -15.f + HitResult.Location;
							ProjectileObject->SetActorLocation(StickLoc);
						}
						else
						{
							ProjectileObject->RootComp->SetCollisionEnabled(ECollisionEnabled::PhysicsOnly);
							ProjectileObject->RootComp->SetSimulatePhysics(true);
							ProjectileObject->MeshComp->SetCollisionEnabled(ECollisionEnabled::PhysicsOnly);
							ProjectileObject->MeshComp->SetSimulatePhysics(true);
							ProjectileObject->ProjectileComp->bShouldBounce = true;
							ProjectileObject->ProjectileComp->Bounciness = 0.3;
						}
					}
					else
					{
						ProjectileObject->RootComp->SetCollisionEnabled(ECollisionEnabled::PhysicsOnly);
						ProjectileObject->RootComp->SetSimulatePhysics(true);
						ProjectileObject->MeshComp->SetCollisionEnabled(ECollisionEnabled::PhysicsOnly);
						ProjectileObject->MeshComp->SetSimulatePhysics(true);
						ProjectileObject->ProjectileComp->bShouldBounce = true;
						ProjectileObject->ProjectileComp->Bounciness = 0.3;
					}
				}
			}
		}
	}
}

void UPlayerDamageComponent::ResourceTrace_Implementation()
{
	if (GetOwner()->HasAuthority())
	{
		IgnoredActors.Add(PlayerCharacter);
		TArray<FHitResult> HitResults;
		FVector Offset1 = FVector(PlayerCharacter->GetActorLocation().X, PlayerCharacter->GetActorLocation().Y, PlayerCharacter->GetActorLocation().Z + 50.f);
		FVector SweepTraceStart = Offset1 + PlayerCharacter->GetActorRotation().Vector() * 150.f;
		FVector Offset2 = FVector(PlayerCharacter->GetActorLocation().X, PlayerCharacter->GetActorLocation().Y, PlayerCharacter->GetActorLocation().Z - 50.f);
		FVector SweepTraceEnd = Offset2 + PlayerCharacter->GetActorRotation().Vector() * 150.f;
		bool HitSuccess = UKismetSystemLibrary::SphereTraceMulti(GetWorld(), SweepTraceStart, SweepTraceEnd, 50.f, UEngineTypes::ConvertToTraceType(ECC_GameTraceChannel6), false, IgnoredActors, EDrawDebugTrace::None, HitResults, true);
		if (HitSuccess && HitResults.Num())
		{
			// add impulse?
			OnHitActor(HitResults[0], FVector(0, 0, 0));
			for (auto& Result : HitResults)
			{
				if (Result.GetActor() && Result.GetActor()->IsA(AResourceActor::StaticClass()))
				{
					UGameplayStatics::ApplyDamage(Result.GetActor(), 10.f, PlayerCharacter->GetController(), PlayerCharacter, UEalondDamageType::StaticClass());
					return;
				}
			}
		}
	}
}

float UPlayerDamageComponent::CalculateIncomingDamage(float BaseDamage, FDamageInfo DamageInfo, UGearItem* ArmorHit)
{
	float TotalDamage = BaseDamage;
	if (!ArmorHit) {return TotalDamage;}
	bool bIsShoulder = ArmorHit->Slot == EEquippableSlot::EIS_Shoulders;
	float TotalResistance;
	float EffectChance = FMath::RandRange(0.f, 1.f);
	
	switch (DamageInfo.DamageType)
	{
	case EDT_FireDamage:
		if (bIsShoulder) {TotalResistance = CalculateAdditiveResistance(ArmorHit, EDT_FireDamage);}
		else {TotalResistance = ArmorHit->ArmorResistanceStats.FireResistance;}
		TotalDamage += DamageInfo.DamageTypeDamage - (DamageInfo.DamageTypeDamage * TotalResistance);
		if (DamageInfo.DamageEffect != EDamageEffect::EDE_NULL)
		{
			float Probability = DamageInfo.EffectProbability * TotalResistance;
			if (Probability > EffectChance)
			{
				Server_SetOnFire(PlayerCharacter, DamageInfo.EffectDuration, DamageInfo.DamagePerSecond);
			}
		}
		break;
	case EDT_BluntDamage:
		if (bIsShoulder) {TotalResistance = CalculateAdditiveResistance(ArmorHit, EDT_BluntDamage);}
		else {TotalResistance = ArmorHit->ArmorResistanceStats.BluntResistance;}
		TotalDamage += DamageInfo.DamageTypeDamage - (DamageInfo.DamageTypeDamage * TotalResistance);
		if (DamageInfo.DamageEffect != EDamageEffect::EDE_NULL)
		{
			float Probability = DamageInfo.EffectProbability * TotalResistance;
			if (Probability > EffectChance)
			{
				Server_BreakBone(PlayerCharacter, DamageInfo.BoneHit);
			}
		}
		break;
	case EDT_PiercingDamage:
		if (bIsShoulder) {TotalResistance = CalculateAdditiveResistance(ArmorHit, EDT_PiercingDamage);}
		else {TotalResistance = ArmorHit->ArmorResistanceStats.PierceResistance;}
		TotalDamage += DamageInfo.DamageTypeDamage - (DamageInfo.DamageTypeDamage * TotalResistance);
		if (DamageInfo.DamageEffect != EDamageEffect::EDE_NULL)
		{
			float Probability = DamageInfo.EffectProbability * TotalResistance;
			if (Probability > EffectChance)
			{
				TotalDamage += DamageInfo.DamageTypeDamage;
			}
		}
		break;
	case EDT_SlashDamage:
		if (bIsShoulder) {TotalResistance = CalculateAdditiveResistance(ArmorHit, EDT_SlashDamage);}
		else {TotalResistance = ArmorHit->ArmorResistanceStats.SlashResistance;}
		TotalDamage += DamageInfo.DamageTypeDamage - (DamageInfo.DamageTypeDamage * TotalResistance);
		if (DamageInfo.DamageEffect != EDamageEffect::EDE_NULL)
		{
			float Probability = DamageInfo.EffectProbability * TotalResistance;
			if (Probability > EffectChance)
			{
				Server_Bleed(PlayerCharacter, DamageInfo.EffectDuration, DamageInfo.DamagePerSecond);
			}
		}
		break;
	case EDT_PoisonDamage:
		if (bIsShoulder) {TotalResistance = CalculateAdditiveResistance(ArmorHit, EDT_PoisonDamage);}
		else {TotalResistance = ArmorHit->ArmorResistanceStats.PoisonResistance;}
		TotalDamage += DamageInfo.DamageTypeDamage - (DamageInfo.DamageTypeDamage * TotalResistance);
		if (DamageInfo.DamageEffect != EDamageEffect::EDE_NULL)
		{
			float Probability = DamageInfo.EffectProbability * TotalResistance;
			if (Probability > EffectChance)
			{
				Server_Poison(PlayerCharacter, DamageInfo.EffectDuration, DamageInfo.DamagePerSecond);
			}
		}
		break;
	case EDT_ChoppingDamage:
		if (bIsShoulder) {TotalResistance = CalculateAdditiveResistance(ArmorHit, EDT_ChoppingDamage);}
		else {TotalResistance = ArmorHit->ArmorResistanceStats.ChoppingResistance;}
		TotalDamage += DamageInfo.DamageTypeDamage - (DamageInfo.DamageTypeDamage * TotalResistance);
		break;
	case EDT_NULL:
		TotalDamage += DamageInfo.DamageTypeDamage;
		break;
	}


	return TotalDamage;
}

void UPlayerDamageComponent::GetTraceMidpoints(const FVector LastStartPoint, const FVector NewestStartPoint, const FVector LastEndPoint, const FVector NewestEndPoint, int32 numit, int32 it, FVector& OUT_Start, FVector& OUT_End)
{
	float Fraction = float(it) / float(numit);
	float DistanceStart = FVector::Distance(LastStartPoint, NewestStartPoint);
	float DistanceToTravelStart = DistanceStart * Fraction;
	FVector DirectionVecStart = (NewestStartPoint - LastStartPoint).GetSafeNormal();
	FVector RawStart = LastStartPoint + DirectionVecStart * DistanceToTravelStart;
	FVector NewDirectionalVecStart = (RawStart - PlayerCharacter->GetActorLocation()).GetSafeNormal();
	float DistFromActorDiffStart = FVector::Distance(PlayerCharacter->GetActorLocation(), NewestStartPoint) - FVector::Distance(PlayerCharacter->GetActorLocation(), LastStartPoint);
	OUT_Start = PlayerCharacter->GetActorLocation() + NewDirectionalVecStart * (DistFromActorDiffStart * Fraction);

	float DistanceEnd = FVector::Distance(LastEndPoint, NewestEndPoint);
	float DistanceToTravelEnd = DistanceEnd * Fraction;
	FVector DirectionVecEnd = (NewestEndPoint - LastEndPoint).GetSafeNormal();
	FVector RawEnd = LastEndPoint + DirectionVecEnd * DistanceToTravelEnd;
	FVector NewDirectionalVecEnd = (RawEnd - PlayerCharacter->GetActorLocation()).GetSafeNormal();
	float DistFromActorDiffEnd = FVector::Distance(PlayerCharacter->GetActorLocation(), NewestEndPoint) - FVector::Distance(PlayerCharacter->GetActorLocation(), LastEndPoint);
	OUT_End = PlayerCharacter->GetActorLocation() + NewDirectionalVecEnd * (FVector::Distance(PlayerCharacter->GetActorLocation(), LastEndPoint) + (DistFromActorDiffEnd * Fraction));
}

bool UPlayerDamageComponent::GetWeapon()
{
	if (PlayerCharacter->bIsBlocking && PlayerCharacter->GetEquippedItems().Find(EEquippableSlot::EIS_Shield))
	{
		CurrentWeapon = *PlayerCharacter->GetEquippedItems().Find(EEquippableSlot::EIS_Shield);
		return true;
	}
	else if (PlayerCharacter->bIsAiming && PlayerCharacter->GetEquippedItems().Find(EEquippableSlot::EIS_Ranged))
	{
		CurrentWeapon = *PlayerCharacter->GetEquippedItems().Find(EEquippableSlot::EIS_Ranged);
		return true;
	}
	else if (PlayerCharacter->bToolEquipped && PlayerCharacter->GetEquippedItems().Find(EEquippableSlot::EIS_Tool))
	{
		CurrentWeapon = *PlayerCharacter->GetEquippedItems().Find(EEquippableSlot::EIS_Tool);
		return true;
	}
	else if (PlayerCharacter->GetEquippedItems().Find(EEquippableSlot::EIS_Weapon))
	{
		CurrentWeapon = *PlayerCharacter->GetEquippedItems().Find(EEquippableSlot::EIS_Weapon);
		return true;
	}
	return false;
}

UEquippableItem* UPlayerDamageComponent::GetEquippedArrow() const
{
	TArray<UEquippableItem*> Arrows = PlayerCharacter->PlayerInventory->GetItemsBySlot(EEquippableSlot::EIS_Arrow);
	if (Arrows.Num())
	{
		for (auto& Arrow : Arrows)
		{
			if (Arrow->IsEquipped()) return Arrow;
		}
	}
	return nullptr;
}

void UPlayerDamageComponent::ApplyTimedDamage_Implementation(AActor* Enemy, float DamagePerSec, float StartTime, float Duration, FTimerHandle INTimer, UNiagaraSystem* Particle, UNiagaraComponent* EffectsComp)
{
	float Timer = GetWorld()->DeltaTimeSeconds;
	if (Timer - StartTime < Duration)
	{
		if (Particle)
		{
			EffectsComp = UNiagaraFunctionLibrary::SpawnSystemAtLocation(GetWorld(), Particle, Enemy->GetActorLocation(), FRotator(0, 0, 0), FVector(0, 0, 0));
			EffectsComp->Activate();
		}
		UGameplayStatics::ApplyDamage(Enemy, DamagePerSec, PlayerCharacter->GetInstigatorController(), PlayerCharacter, UEalondDamageType::StaticClass());
	}
	else
	{
		if (Particle)
		{
			EffectsComp->Deactivate();
		}
		GetWorld()->GetTimerManager().ClearTimer(INTimer);
	}
}

float UPlayerDamageComponent::CalculateAdditiveResistance(UGearItem* ShoulderPiece, EDamageType DamageType)
{
	UGearItem* ChestPiece = nullptr;
	float AdditiveResistance;
	if (IPlayerAIInteractionInterface* IntCharacter = Cast<IPlayerAIInteractionInterface>(PlayerCharacter))
	{
		ChestPiece = IntCharacter->Execute_GetChestPiece(Cast<UObject>(IntCharacter));
	}
	switch (DamageType)
	{
	case EDT_BluntDamage:
		if (ChestPiece)
		{
			AdditiveResistance = ChestPiece->ArmorResistanceStats.BluntResistance + ShoulderPiece->ArmorResistanceStats.BluntResistance;
			AdditiveResistance = FMath::Clamp(AdditiveResistance, -1.f, 1.f);
			return AdditiveResistance;
		}
		else
		{
			return ShoulderPiece->ArmorResistanceStats.BluntResistance;
		}
		break;
	case EDT_ChoppingDamage:
		if (ChestPiece)
		{
			AdditiveResistance = ChestPiece->ArmorResistanceStats.ChoppingResistance + ShoulderPiece->ArmorResistanceStats.ChoppingResistance;
			AdditiveResistance = FMath::Clamp(AdditiveResistance, -1.f, 1.f);
			return AdditiveResistance;
		}
		else
		{
			return ShoulderPiece->ArmorResistanceStats.ChoppingResistance;
		}
		break;
	case EDT_FireDamage:
		if (ChestPiece)
		{
			AdditiveResistance = ChestPiece->ArmorResistanceStats.FireResistance + ShoulderPiece->ArmorResistanceStats.FireResistance;
			AdditiveResistance = FMath::Clamp(AdditiveResistance, -1.f, 1.f);
			return AdditiveResistance;
		}
		else
		{
			return ShoulderPiece->ArmorResistanceStats.FireResistance;
		}
		break;
	case EDT_PiercingDamage:
		if (ChestPiece)
		{
			AdditiveResistance = ChestPiece->ArmorResistanceStats.PierceResistance + ShoulderPiece->ArmorResistanceStats.PierceResistance;
			AdditiveResistance = FMath::Clamp(AdditiveResistance, -1.f, 1.f);
			return AdditiveResistance;
		}
		else
		{
			return ShoulderPiece->ArmorResistanceStats.PierceResistance;
		}
		break;
	case EDT_PoisonDamage:
		if (ChestPiece)
		{
			AdditiveResistance = ChestPiece->ArmorResistanceStats.PoisonResistance + ShoulderPiece->ArmorResistanceStats.PoisonResistance;
			AdditiveResistance = FMath::Clamp(AdditiveResistance, -1.f, 1.f);
			return AdditiveResistance;
		}
		else
		{
			return ShoulderPiece->ArmorResistanceStats.PoisonResistance;
		}
		break;
	case EDT_SlashDamage:
		if (ChestPiece)
		{
			AdditiveResistance = ChestPiece->ArmorResistanceStats.SlashResistance + ShoulderPiece->ArmorResistanceStats.SlashResistance;
			AdditiveResistance = FMath::Clamp(AdditiveResistance, -1.f, 1.f);
			return AdditiveResistance;
		}
		else
		{
			return ShoulderPiece->ArmorResistanceStats.SlashResistance;
		}
		break;
	case EDT_NULL:
		return 0;
		break;
	}
	return 0;
}

float UPlayerDamageComponent::CalculateDamage(class UEquippableItem* Weapon)
{
	float Damage = 0.f;
	if (Weapon)
	{
		Damage += Weapon->DamageStats.WeaponBaseDamage;
		switch (Weapon->Slot)
		{
		case EEquippableSlot::EIS_Weapon:
			if (!Weapon->bIs2Hander) Damage *= PlayerCharacter->ProgressComponent->GetMeleeStats().Sword1hDamageMultiplier;
			else Damage *= PlayerCharacter->ProgressComponent->GetMeleeStats().Sword2hDamageMultiplier;
			if (FMath::RandRange(0.f, 1.f) > Weapon->DamageStats.CriticalChance) Damage *= Weapon->DamageStats.CriticalMultiplier;
			break;
		case EEquippableSlot::EIS_Shield:
			Damage *= PlayerCharacter->ProgressComponent->GetShieldStats().ShieldDamageMultiplier;
			break;
		case EEquippableSlot::EIS_Tool:
			break;
		}
	}
	return Damage;
}
