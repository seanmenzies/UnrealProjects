// Fill out your copyright notice in the Description page of Project Settings.


#include "AIDamageComponent.h"
#include "../AI/AIBaseCharacter.h"
#include "../Player/EalondCharacter.h"
#include "Components/BoxComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"

// Sets default values for this component's properties
UAIDamageComponent::UAIDamageComponent()
{
	// Set this component to be initialized when the game starts, and to be ticked every frame.  You can turn these features
	// off to improve performance if you don't need them.
	PrimaryComponentTick.bCanEverTick = false;

	// initialise values
	TraceLoopValue = 0.4;
	ArmExtension = 50.f;
}

void UAIDamageComponent::InitComp()
{
	Attacker = Cast<AAIBaseCharacter>(GetOwner()->GetInstigatorController()->GetPawn());
}

// Called when the game starts
void UAIDamageComponent::BeginPlay()
{
	Super::BeginPlay();
}


FVector UAIDamageComponent::GetVectorMidpoint(FVector Origin, FVector Vector1, FVector Vector2, float DistanceBetweenPoints, float DistanceFromOrigin)
{
	FVector ExtendedFromPlayer = (Vector1 - Vector2) * DistanceBetweenPoints;
	FVector UnnormalisedMidpoint = ExtendedFromPlayer + Vector2 - Origin;
	FVector Midpoint = Origin + ((UnnormalisedMidpoint.GetSafeNormal()) * DistanceFromOrigin);
	return Midpoint;
}

void UAIDamageComponent::GetDirectionalVectorsFromPlayer(bool Inner, FVector Origin, float DistanceFromPlayer, bool InvertForward, bool InvertRight, bool InvertUp)
{
	float ForwardMultiplier;
	float RightMultiplier;
	float UpMultiplier;
	if (InvertForward) {ForwardMultiplier = -1.f;}
	else {ForwardMultiplier = 1.f;}
	if (InvertRight) {RightMultiplier = -1.f;}
	else {RightMultiplier = 1.f;}
	if (InvertUp) {UpMultiplier = -1.f;}
	else {UpMultiplier = 1.f;}

	FVector ForwardVector = Attacker->GetActorForwardVector() * ForwardMultiplier;
	FVector RightVector = Attacker->GetActorRightVector() * RightMultiplier;
	FVector UpVector = Attacker->GetActorUpVector() * UpMultiplier;

	if (Inner)
	{
		ForwardVectorInner = Origin + (ForwardVector * DistanceFromPlayer);
		RightVectorInner = Origin + (RightVector * DistanceFromPlayer);
		UpVectorInner = Origin + (UpVector * DistanceFromPlayer);
	}
	else
	{
		ForwardVectorOuter = Origin + (ForwardVector * DistanceFromPlayer);
		RightVectorOuter = Origin + (RightVector * DistanceFromPlayer);
		UpVectorOuter = Origin + (UpVector * DistanceFromPlayer);
	}
}

void UAIDamageComponent::DiagonalTrace_Implementation(bool InvertForward, bool InvertRight, bool InvertUp)
{
	TraceLoopValue = 0.4f;
	IgnoredActors.Empty();
	FVector Origin = Attacker->GetActorLocation();
	float InnerDistance = ArmLength;
	float OuterDistance = ArmLength + WeaponLength;
	FHitResult HitResult;
	IgnoredActors.Add(Attacker);
	for (int32 i = 0; i < 7; i++)
	{
		GetDirectionalVectorsFromPlayer(true, Origin, InnerDistance, InvertForward, InvertRight, InvertUp);
		FVector FirstMidpoint = GetVectorMidpoint(Origin, RightVectorInner, UpVectorInner, TraceLoopValue, InnerDistance);
		FVector TraceStart = GetVectorMidpoint(Origin, ForwardVectorInner, FirstMidpoint, TraceLoopValue, InnerDistance);
		GetDirectionalVectorsFromPlayer(false, Origin, OuterDistance, InvertForward, InvertRight, InvertUp);
		FirstMidpoint = GetVectorMidpoint(Origin, RightVectorOuter, UpVectorOuter, TraceLoopValue, OuterDistance);
		FVector TraceEnd = GetVectorMidpoint(Origin, ForwardVectorOuter, FirstMidpoint, TraceLoopValue, OuterDistance);
		// single trace for non-slash weapons, add multi otherwise
		bool HitSuccess = UKismetSystemLibrary::SphereTraceSingle(GetWorld(), TraceStart, TraceEnd, TraceRadius, ETraceTypeQuery::TraceTypeQuery2, false, IgnoredActors, EDrawDebugTrace::None, HitResult, true);
		if (HitSuccess)
		{
			HitLocation = HitResult.Location;
			HitActor = HitResult.GetActor();
			AEalondCharacter* Player = Cast<AEalondCharacter>(HitActor);
			if (Player && HitResult.GetComponent() == Player->ShieldHitBox)
			{
				Player->Server_ChangeShieldHealth(BaseDamageWeapon * Player->ShieldArmorMultiplier);
				Player->Server_ChangeStamina(BaseDamageWeapon);
				Attacker->bHitShield = true;
				return;
			}
			IgnoredActors.Add(HitActor);
			UGameplayStatics::ApplyDamage(HitActor, BaseDamageWeapon, GetOwner()->GetInstigatorController(), Attacker, DamageTypeClass);
			return;
		}
		else {TraceLoopValue += 0.1;}
	}
}

void UAIDamageComponent::OverheadTrace_Implementation()
{
	TraceLoopValue = 0.1f;
	IgnoredActors.Empty();
	FVector Origin = Attacker->GetActorLocation();
	float InnerDistance = ArmLength;
	float OuterDistance = ArmLength + WeaponLength;
	FHitResult HitResult;
	IgnoredActors.Add(Attacker);
	for (int32 i = 0; i < 10; i++)
	{
		GetDirectionalVectorsFromPlayer(true, Origin, InnerDistance);
		FVector TraceStart = GetVectorMidpoint(Origin, ForwardVectorInner, UpVectorInner, TraceLoopValue, InnerDistance);
		GetDirectionalVectorsFromPlayer(false, Origin, OuterDistance);
		FVector TraceEnd = GetVectorMidpoint(Origin, ForwardVectorOuter, UpVectorOuter, TraceLoopValue, OuterDistance);
		// single trace for non-slash weapons, add multi otherwise
		bool HitSuccess = UKismetSystemLibrary::SphereTraceSingle(GetWorld(), TraceStart, TraceEnd, TraceRadius, ETraceTypeQuery::TraceTypeQuery2, false, IgnoredActors, EDrawDebugTrace::None, HitResult, true);
		if (HitSuccess)
		{
			HitLocation = HitResult.Location;
			HitActor = HitResult.GetActor();
			AEalondCharacter* Player = Cast<AEalondCharacter>(HitActor);
			if (Player && HitResult.GetComponent() == Player->ShieldHitBox)
			{
				Player->Server_ChangeShieldHealth(BaseDamageWeapon * Player->ShieldArmorMultiplier);
				Player->Server_ChangeStamina(BaseDamageWeapon);
				Attacker->bHitShield = true;
				return;
			}
			IgnoredActors.Add(HitActor);
			UGameplayStatics::ApplyDamage(HitActor, BaseDamageWeapon, GetOwner()->GetInstigatorController(), Attacker, DamageTypeClass);
			return;
		}
		else {TraceLoopValue += 0.1;}
	}
}

void UAIDamageComponent::ThrustTrace_Implementation()
{
	IgnoredActors.Empty();
	float Extension = ArmLength + WeaponLength + ArmExtension;
	FHitResult HitResult;
	FVector TraceStart = Attacker->GetActorRightVector() * 40.f + Attacker->GetActorLocation();
	FVector TraceEnd = Attacker->GetActorForwardVector() * Extension + Attacker->GetActorLocation();
	IgnoredActors.Add(Attacker);
	// single trace for non-slash weapons, add multi otherwise
	bool HitSuccess = UKismetSystemLibrary::SphereTraceSingle(GetWorld(), TraceStart, TraceEnd, TraceRadius, ETraceTypeQuery::TraceTypeQuery2, false, IgnoredActors, EDrawDebugTrace::None, HitResult, true);
	if (HitSuccess)
	{
		HitLocation = HitResult.Location;
		HitActor = HitResult.GetActor();
		AEalondCharacter* Player = Cast<AEalondCharacter>(HitActor);
		if (Player && HitResult.GetComponent() == Player->ShieldHitBox)
		{
			Player->Server_ChangeShieldHealth(BaseDamageWeapon * Player->ShieldArmorMultiplier);
			Player->Server_ChangeStamina(BaseDamageWeapon);
			Attacker->bHitShield = true;
			return;
		}
		IgnoredActors.Add(HitActor);
		UGameplayStatics::ApplyDamage(HitActor, BaseDamageWeapon, GetOwner()->GetInstigatorController(), Attacker, DamageTypeClass);
		return;
	}
}
