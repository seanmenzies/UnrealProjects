// Fill out your copyright notice in the Description page of Project Settings.


#include "EnemyAIController.h"
#include "AIBaseCharacter.h"
#include "Components/CapsuleComponent.h"
#include "../Buildings/Building.h"
#include "../Components/AIDamageComponent.h"
#include "../Components/MemoryComponentBase.h"
#include "../Components/EnemyAIMemoryCOmponent.h"
#include "../Interfaces/MemoryInterface.h"
#include "../Framework/EalondGameMode.h"
#include "../Player/EalondCharacter.h"
#include "Perception/AISenseConfig_Sight.h"
#include "Perception/AIPerceptionComponent.h"
#include "Navigation/PathFollowingComponent.h"
#include "BrainComponent.h"

#define OUT


AEnemyAIController::AEnemyAIController() 
{
    PrimaryActorTick.bCanEverTick = true;
    // Set AI to opposing team for hostile detection
    SetGenericTeamId(FGenericTeamId(1));
    TeamId = FGenericTeamId(1);

    InitPerception();
    DamageComponent = CreateDefaultSubobject<UAIDamageComponent>("Damage Component");
}

void AEnemyAIController::BeginPlay()
{
    Super::BeginPlay();
    
    bCanReselectTarget = true;
}

void AEnemyAIController::OnPossess(APawn* InPawn)
{
    Super::OnPossess(InPawn);

    AAIBaseCharacter* CharRef = Cast<AAIBaseCharacter>(InPawn);
    CharRef->AIController = this;

    DamageComponent->Activate();
    PerceptionComp->Activate();
    ControlledCharacter = Cast<AAIBaseCharacter>(InPawn);
    ControlledCharacter->MemoryComponent->Activate();
    GameMode = Cast<AEalondGameMode>(GetWorld()->GetAuthGameMode());
    if (ControlledCharacter)
    {
        AIMapIndex = GameMode->AddAIToMap(ControlledCharacter);
        if (DamageComponent)
        {
            DamageComponent->InitComp();
            DamageComponent->ArmLength = ControlledCharacter->ArmLength;
            DamageComponent->WeaponLength = ControlledCharacter->WeaponLength;
            DamageComponent->BaseDamageWeapon = ControlledCharacter->BaseDamage;
            DamageComponent->TraceRadius = ControlledCharacter->TraceRadius;
        }
    }
    
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("Could not initialise damage variables from %s"), *this->GetName());
    }

    // check components set up correctly
    PathComp = GetPathFollowingComponent();
    PathComp->SetBlockDetectionState(true);

    if (AIBehaviorTree) {RunBehaviorTree(AIBehaviorTree);}
    else {UE_LOG(LogTemp, Warning, TEXT("Couldn't find BT for controller %s"), *this->GetName());}

    if (!ControlledCharacter) {UE_LOG(LogTemp, Warning, TEXT("Couldn't find pawn for controller %s"), *this->GetName());}

    if (!PerceptionComp) {UE_LOG(LogTemp, Warning, TEXT("Couldn't find perception component for controller %s"), *this->GetName());}
}

void AEnemyAIController::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);

    // character state checks
    if (ControlledCharacter)
    {
        //if (ControlledCharacter->bIsCombat && EnemyTarget) {ControlledCharacter->FaceTarget(EnemyTarget);}
        if (ControlledCharacter->bIsHurt && !ControlledCharacter->bIsDead) { Timer_Hurt += DeltaTime; }
        if (ControlledCharacter->bIsHurt && Timer_Hurt > 0.5) { ControlledCharacter->bIsHurt = false; Timer_Hurt = 0; }
    }
    // update engage condition
    if (EnemyTarget) 
    {
        Timer_CheckEngageCondition += DeltaTime;
        if (Timer_CheckEngageCondition > 1)
        {
            if (!CheckEngageCondition()) {Disengage();}
            Timer_CheckEngageCondition = 0;
        }
    }
    // default to monument if close enough
    if (Monument && ControlledCharacter)
    {
        if (StaticTarget && ControlledCharacter->GetDistanceTo(Monument) < 200.f)
        {
            StaticTarget = nullptr;
        }
        // TODO force enemy reevaluation when health low
    }
}

void AEnemyAIController::InitPerception() 
{
    SightConfig = CreateDefaultSubobject<UAISenseConfig_Sight>(TEXT("Sight Config"));
    PerceptionComp = CreateDefaultSubobject<UAIPerceptionComponent>(TEXT("Perception Component"));
    SightConfig->SightRadius = 1000;
    SightConfig->LoseSightRadius = 1200;
    SightConfig->PeripheralVisionAngleDegrees = 90.0f;
    SightConfig->SetMaxAge(5.f);
    SightConfig->AutoSuccessRangeFromLastSeenLocation = 1.f;
    SightConfig->DetectionByAffiliation.bDetectEnemies = true;
    SightConfig->DetectionByAffiliation.bDetectNeutrals = false;
    SightConfig->DetectionByAffiliation.bDetectFriendlies = false;

    PerceptionComp->SetDominantSense(*SightConfig->GetSenseImplementation());
    PerceptionComp->ConfigureSense(*SightConfig);
    PerceptionComp->OnPerceptionUpdated.AddDynamic(this, &AEnemyAIController::UpdatePerceivedActors);

}

ETeamAttitude::Type AEnemyAIController::GetTeamAttitudeTowards(const AActor& Other) const
{
    auto StimulusInterface = Cast<IGenericTeamAgentInterface>(&Other);
    if (!StimulusInterface) {return ETeamAttitude::Neutral;}
    else if (StimulusInterface->GetGenericTeamId() != TeamId) {return ETeamAttitude::Hostile;}
    else {return ETeamAttitude::Friendly;};
}

void AEnemyAIController::SetMonument() 
{
    Monument = nullptr;
    TArray<AActor*> Monuments;
    UGameplayStatics::GetAllActorsOfClass(GetWorld(), AMonument::StaticClass(), Monuments);
    if(Monuments.Num() > 0)
    {
        Monument = Cast<AMonument>(Monuments[0]);
        bMonumentSet = true;
    }
}

AMonument* AEnemyAIController::GetMonument() 
{
    if (Monument) {return Monument;}
    else {return nullptr;}
}

bool AEnemyAIController::CheckMonumentBlocked() const
{
    FHitResult HitResult;
    FVector StartLocation = ControlledCharacter->GetActorLocation();
    FVector EndLocation = Monument->GetActorLocation();
    FCollisionQueryParams Params;
	Params.AddIgnoredActor(ControlledCharacter);
    bool bHitSuccess = GetWorld()->LineTraceSingleByChannel(HitResult, StartLocation, EndLocation, ECollisionChannel::ECC_GameTraceChannel1, Params);
    if (HitResult.GetActor() == Monument)
    {
        return false;
    }
    else
    {
        return true;
    }
 
}

float AEnemyAIController::GetPathToMonument() 
{
    if (PathComp->HasValidPath() && Monument)
    {
       return PathComp->GetPath()->GetLength();
    }
    else return 0;
}

AActor* AEnemyAIController::CheckBlocked()
{
    FHitResult HitResult;
    FVector StartLocation = ControlledCharacter->GetActorLocation();
    FVector EndLocation = ControlledCharacter->GetActorRotation().Vector() * 200.f + StartLocation;
    FCollisionQueryParams Params;
	Params.AddIgnoredActor(ControlledCharacter);
    bool bHitSuccess = GetWorld()->LineTraceSingleByChannel(HitResult, StartLocation, EndLocation, ECollisionChannel::ECC_GameTraceChannel1, Params);
    if (HitResult.GetActor()) {return HitResult.GetActor();}
    else {return nullptr;}
}

void AEnemyAIController::CheckStaticTargets(bool CheckBlocked, float CheckCone) 
{
    if (Monument)
    {
        /*Instigating controller sends out a trace from left to right according to the angle input with the monument
         at the centre of the cone*/

         // TODO Check floor hit
        if (CheckCone > 180.f || CheckCone < 10.f)
        {
            UE_LOG(LogTemp, Warning, TEXT("AI Controller error: Trace cone cannot be less than 10 or greater than 180 degrees."));
            return;
        }
        FHitResult HitResult;
        FVector StartLocation = ControlledCharacter->GetActorLocation();
        FVector EndLocation = Monument->GetActorLocation();
        // get world rotation of AI relative to monument
        FRotator OriginalRotation = UKismetMathLibrary::FindLookAtRotation(StartLocation, EndLocation);
        float DistanceToMonument = FVector::Distance(StartLocation, EndLocation);
        FCollisionQueryParams Params;
        Params.AddIgnoredActor(ControlledCharacter);
        // check if direct path to monument
        if (!CheckBlocked)
        {
            bool bHitSuccess = GetWorld()->LineTraceSingleByChannel(HitResult, StartLocation, EndLocation, ECollisionChannel::ECC_GameTraceChannel1, Params);
            if (HitResult.GetActor() == Monument)
            {
                StaticTarget = nullptr;
                //return;
            }
        }
        // get total traces to be fired based on passed in cone
        int32 Iterations = FMath::FloorToInt32(CheckCone / 5);
        // first angle should be half a cone to the left of OriginRotation
        float FirstAngle = CheckCone / -2;
        for (int32 i = 0; i < Iterations; i++)
        {
            FRotator NewRotation = FRotator(OriginalRotation.Pitch, OriginalRotation.Yaw + FirstAngle, OriginalRotation.Roll);
            EndLocation = NewRotation.Vector() * DistanceToMonument + StartLocation;
            bool bHitSuccess = GetWorld()->LineTraceSingleByChannel(HitResult, StartLocation, EndLocation, ECollisionChannel::ECC_GameTraceChannel1, Params);
            if (bHitSuccess && HitResult.GetActor())
            {
                // if direct path to monument, stop looking for or attacking building and switch to monument
                if (HitResult.GetActor() == Monument) { StaticTarget = nullptr; }
                // attack player if found
                else if (Cast<AEalondCharacter>(HitResult.GetActor())) { Engage(HitResult.GetActor()); }
                // add building data if found
                else
                {
                    auto BuildingInterface = Cast<IBuildingInterface>(HitResult.GetActor());
                    if (BuildingInterface && BuildingInterface->Execute_CheckIfBuilding(Cast<UObject>(BuildingInterface), HitResult.GetActor()))
                    {
                        BuildingInterface->Execute_GetBuildingData(Cast<UObject>(BuildingInterface), ControlledCharacter->MemoryComponent);
                        Params.AddIgnoredActor(HitResult.GetActor());
                    }
                }
            }

            // continue the sweep
            FirstAngle += 5.f;
        }
    }
}

void AEnemyAIController::SetStaticTarget(bool bCharacterBlocked)
{
    // auto select target if stationary
    if (bCharacterBlocked)
    {
        StaticTarget = ControlledCharacter->MemoryComponent->SelectBuildingTarget();
        return;
    }
    // check for short, direct path to monument
    float PathToMonument = GetPathToMonument();
    if (PathToMonument > ControlledCharacter->GetDistanceTo(Monument) * 1.5)
    {
        ABuilding* Target = ControlledCharacter->MemoryComponent->SelectBuildingTarget();
        if (Target)
        {
            ControlledCharacter->bBlockedByTarget= false;
            StaticTarget = Target;
        }
    }
}

void AEnemyAIController::UpdatePerceivedActors(const TArray<AActor*>& PerceivedActors) 
{
    if (ControlledCharacter)
    {
        TArray<AActor*> HostilesInRange;
        PerceptionComp->GetCurrentlyPerceivedActors(SightConfig->GetSenseImplementation(), HostilesInRange);
        // start decaying any enemies that leave perception
        ControlledCharacter->MemoryComponent->CheckUnperceivedEnemies(HostilesInRange);
        bool bEnemyFound = false;
        bool bBuildingFound = false;

        // get data
        for (auto& Actor : HostilesInRange)
        {
            auto EnemyActor = Cast<IMemoryInterface>(Actor);
            auto EnemyBuilding = Cast<IBuildingInterface>(Actor);
            if (EnemyActor && !EnemyActor->Execute_CheckIsDarkSide(Cast<UObject>(EnemyActor)))
            {
                bEnemyFound = true;
                // add any new enemies to memory
                EnemyActor->Execute_GetEnemyData(Cast<UObject>(EnemyActor), Cast<UMemoryComponentBase>(ControlledCharacter->MemoryComponent));
            }
            else if (EnemyBuilding && Actor != Monument && EnemyBuilding->Execute_CheckIfBuilding(Cast<UObject>(EnemyBuilding), Actor))
            {
                // return if direct path to monument found
                bBuildingFound = true;
                EnemyBuilding->Execute_GetBuildingData(Cast<UObject>(EnemyBuilding), ControlledCharacter->MemoryComponent);
            }
        }
        // set targets if no target or cooldown finished
        if (bEnemyFound)
        {
            if (!EnemyTarget || bCanReselectTarget)
            {
                AActor* CandidateTarget = ControlledCharacter->MemoryComponent->SelectEnemyTarget();
                if (CandidateTarget && CheckEngageCondition(CandidateTarget)) { Engage(CandidateTarget); }
            }
        }
        if (!EnemyTarget && !StaticTarget && bBuildingFound)
        {
            SetStaticTarget();
        }
    }
}

AActor* AEnemyAIController::GetCurrentTarget() const
{
    if (EnemyTarget) {return EnemyTarget;}
    else if (StaticTarget) {return EnemyTarget;}
    else if (ControlledCharacter->GetDistanceTo(Monument) < 200.f) {return Monument;}
    else {return nullptr;}
}

void AEnemyAIController::MoveToTarget(AActor* Target, float Radius)
{
    if (!ControlledCharacter->bIsCombatReady) {ControlledCharacter->SetWalkSpeed(ControlledCharacter->PatrolSpeed);}
    else {ControlledCharacter->SetWalkSpeed(ControlledCharacter->TopSpeed);}

    ControlledCharacter->bIsCombat = false;

    FHitResult HitResult;
    FVector StartLocation = ControlledCharacter->GetActorLocation();
    FVector EndLocation = Target->GetActorLocation();
    FCollisionQueryParams Params;
	Params.AddIgnoredActor(ControlledCharacter);
    bool HitSuccess = GetWorld()->LineTraceSingleByChannel(HitResult, StartLocation, EndLocation, ECC_WorldDynamic, Params);
    // Only use NavMesh if something is blocking our path
    if (HitSuccess && HitResult.GetActor() != Target)
    {
        ControlledCharacter->bIsCombat = false;
        MoveToActor(Target, Radius, false);
    }
    else
    {
        ControlledCharacter->bIsCombat = false;
        MoveToActor(Target, Radius, true, false);
    }
}

bool AEnemyAIController::CheckEngageCondition(AActor* CheckedActor) 
{
    if (!Monument) {return false;}
    AActor* ActorToCheck = nullptr;
    if (CheckedActor) {ActorToCheck = CheckedActor;}
    else if (EnemyTarget) {ActorToCheck = EnemyTarget;}
    // disengage if target moves too far from AI or monument
    if (ActorToCheck)
    {
        float DistanceToMonumentBirdsEye = ControlledCharacter->GetDistanceTo(Monument);
        float DistanceFromPlayer = ControlledCharacter->GetDistanceTo(ActorToCheck);
        if (DistanceToMonumentBirdsEye > 3500.f) 
        {
            DisengagePlayerThreshold = 10.f;
        }
        else if (DistanceToMonumentBirdsEye > 2500.f)
        {
            DisengagePlayerThreshold = SightConfig->SightRadius / 2.f;   
        }
        else
        {
            DisengagePlayerThreshold = SightConfig->SightRadius;   
        }
        if (DistanceFromPlayer > DisengagePlayerThreshold)
        {
            if (EnemyTarget)
            {
                FTimerHandle DisengageTimer;
                ControlledCharacter->SetWalkSpeed(0);
                if (ControlledCharacter->DisengageAnim)
                {
                    Multi_PlayAnim(ControlledCharacter->DisengageAnim);
                }
                GetWorld()->GetTimerManager().SetTimer(DisengageTimer, this, &AEnemyAIController::Disengage, 1.f, false);
                return false;
            }
            else {return false;}
        }
        else {return true;}
    }
    return false;
}

void AEnemyAIController::Engage(AActor* TargetCandidate) 
{
    if (bCanReselectTarget && TargetCandidate)
    {
        EnemyTarget = TargetCandidate;
        if (EnemyTarget)
        {
            StaticTarget = nullptr;
            ControlledCharacter->bIsCombatReady = true;
            // set enemy selection cooldown
            bCanReselectTarget = false;
            FTimerDelegate DisableEnemySelection;
            DisableEnemySelection.BindLambda([this]()
                {
                    bCanReselectTarget = true;
                });
            GetWorld()->GetTimerManager().SetTimer(TargetSelectionCooldownTimer, DisableEnemySelection, 30.f, false);
        }
    }
}

void AEnemyAIController::Attack(AActor* Target) 
{
    ControlledCharacter->SetWalkSpeed(0);
    ControlledCharacter->bIsCombat = true;
}

void AEnemyAIController::Disengage() 
{
    ControlledCharacter->SetWalkSpeed(ControlledCharacter->TopSpeed);
    if (ControlledCharacter->bIsCombatReady && ControlledCharacter->DisengageAnim)
    {
        ControlledCharacter->PlayAnimMontage(ControlledCharacter->DisengageAnim);
        Multi_PlayAnim(ControlledCharacter->DisengageAnim);
    }
    EnemyTarget = nullptr;
    bCanReselectTarget = true;
    ControlledCharacter->bIsCombat = false;
    ControlledCharacter->bIsCombatReady = false;
}

void AEnemyAIController::Multi_PlayAnim_Implementation(UAnimMontage* Montage)
{
    GetBrainComponent()->PauseLogic(TEXT("Montage playing"));
	ControlledCharacter->PlayAnimMontage(Montage, 1.0);
    GetBrainComponent()->ResumeLogic(TEXT("Montage ended"));
}

void AEnemyAIController::ChangeHealth(float ChangeAmount, AActor* DamageCauser)
{
	ControlledCharacter->Health -= ChangeAmount;
    ControlledCharacter->bIsHurt = true;
    auto Assailant = Cast<IMemoryInterface>(DamageCauser);
	if (Assailant && !Assailant->Execute_CheckIsDarkSide(Cast<UObject>(Assailant)))
	{
		if (ControlledCharacter->Health <= 0)
		{
			HandleDestruction();
		}
		else if (!EnemyTarget && DamageCauser)
		{
            ControlledCharacter->FaceTarget(DamageCauser);
            Engage(DamageCauser);
		}
	}
}

 void AEnemyAIController::HandleDestruction() 
 {
     ControlledCharacter->bIsDead = true;
     ControlledCharacter->SetWalkSpeed(0.f);
     GetBrainComponent()->StopLogic(TEXT("Pawn died"));
     GameMode->RemoveAIFromMap(AIMapIndex);
     Cast<UMemoryComponentBase>(ControlledCharacter->MemoryComponent)->ForgetMe(ControlledCharacter);
     FTimerHandle DespawnTimer;
     FTimerDelegate Despawn;
     Despawn.BindLambda([this]()
         {
             ControlledCharacter->GetCapsuleComponent()->DestroyComponent();
             ControlledCharacter->Destroy();
         });
     GetWorld()->GetTimerManager().SetTimer(DespawnTimer, Despawn, 10.f, false);
     UnPossess();
     // deactivate components
     DamageComponent->Deactivate();
     PerceptionComp->Deactivate();
     ControlledCharacter->MemoryComponent->Deactivate();
 }

 void AEnemyAIController::DestroyBuilding(AActor* Building)
 {
     Building->SetActorEnableCollision(false);
     Building->Destroy();
 }