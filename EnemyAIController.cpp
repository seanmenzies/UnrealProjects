// Fill out your copyright notice in the Description page of Project Settings.


#include "EnemyAIController.h"
#include "AIBaseCharacter.h"
#include "Goblin.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/KismetStringLibrary.h"
#include "../Buildings/Building.h"
#include "../BehaviorTree/BTTask_MakeCombatDecision.h"
#include "../Components/MemoryComponentBase.h"
#include "../Interfaces/MemoryInterface.h"
#include "../Interfaces/AIClassInterface.h"
#include "../Interfaces/PlayerAIInteractionInterface.h"
#include "../Items/ProjectileBase.h"
#include "../Framework/EalondGameMode.h"
#include "../Player/EalondCharacter.h"
#include "../Progress/CharacterProgressComponent.h"
#include "Perception/AISenseConfig_Sight.h"
#include "Perception/AISenseConfig_Hearing.h"
#include "Perception/AIPerceptionComponent.h"
#include "PhysicsEngine/PhysicsSettings.h"
#include "Navigation/PathFollowingComponent.h"
#include "BrainComponent.h"

#define OUT

AEnemyAIController::AEnemyAIController() 
{
    PrimaryActorTick.bCanEverTick = true;

    InitPerception();

    SetGenericTeamId(FGenericTeamId(1));
    TeamId = FGenericTeamId(1);
}

void AEnemyAIController::BeginPlay()
{
    Super::BeginPlay();
    
    bCanReselectTarget = true;
}

void AEnemyAIController::OnPossess(APawn* InPawn)
{
    Super::OnPossess(InPawn);

    PerceptionComp->Activate();
    ControlledCharacter = Cast<AAIBaseCharacter>(InPawn);
    ControlledCharacter->AIController = this;
    ControlledCharacter->GetMesh()->SetVisibility(false);
    AEalondGameMode* GameMode = Cast<AEalondGameMode>(GetWorld()->GetAuthGameMode());
    if (ControlledCharacter && GameMode)
    {
        GameMode->AddAIToMap(ControlledCharacter);
    }   
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("Could not initialise damage variables from %s"), *this->GetName());
    }

    // check components set up correctly
    PathComp = GetPathFollowingComponent();
    PathComp->SetBlockDetectionState(true);
    PathComp->SetStopMovementOnFinish(false);
    
    if (!ControlledCharacter) {UE_LOG(LogTemp, Warning, TEXT("Couldn't find pawn for controller %s"), *this->GetName());}

    if (!PerceptionComp) {UE_LOG(LogTemp, Warning, TEXT("Couldn't find perception component for controller %s"), *this->GetName());}

    if (SpawnAnimation)
    {
        FTimerHandle AnimTimer;
        FTimerDelegate AnimDelegate;
        AnimDelegate.BindLambda([this]()
            {
                if (AIBehaviorTree) {RunBehaviorTree(AIBehaviorTree);}
                else {UE_LOG(LogTemp, Warning, TEXT("Couldn't find BT for controller %s"), *this->GetName());}
            });
        FTimerHandle VisTimer;
        FTimerDelegate VisDelegate;
        VisDelegate.BindLambda([this]()
            {
                ControlledCharacter->GetMesh()->SetVisibility(true);
            });
        float PlayRate = FMath::RandRange(0.9, 1.1);
        GetWorld()->GetTimerManager().SetTimer(VisTimer, VisDelegate, .75f, false);
        GetWorld()->GetTimerManager().SetTimer(AnimTimer, AnimDelegate,SpawnAnimation->GetPlayLength() * PlayRate, false);
        ControlledCharacter->Server_PlayAnim(SpawnAnimation, PlayRate);
    }
}

void AEnemyAIController::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);

    // update engage condition
    if (EnemyTarget) 
    {
        Timer_CheckEngageCondition += DeltaTime;
        if (Timer_CheckEngageCondition > 1)
        {
            if (!CheckEngageCondition()) {Disengage();}
            Timer_CheckEngageCondition = 0;
        }
        DistanceFromEnemy = EnemyTarget->GetDistanceTo(ControlledCharacter);
    }

    if (ControlledCharacter)
    {
        if (ControlledCharacter->bInCombatMode)
        {
            if (CheckDangerTimer < .5) CheckDangerTimer += DeltaTime;
            else
            {
                bInDanger = ControlledCharacter->MemoryComp->CheckInDanger();
                CheckDangerTimer = 0;
            }
        }
        else if (bInDanger) bInDanger = false;

        if (((EnemyTarget && EnemyTarget->IsValidLowLevelFast()) || (ActorToFocusOn && ActorToFocusOn->IsValidLowLevelFast())) && !GetFocusActor() && !ControlledCharacter->bOverrideProceduralGaze && !(ControlledCharacter->bIsHurt || ControlledCharacter->bIsDead))
        {
            FVector MeshForwardVector = ControlledCharacter->GetActorForwardVector();
            AActor* GazeFocus;
            EnemyTarget ? GazeFocus = EnemyTarget : GazeFocus = ActorToFocusOn;
            FVector VectorBetweenUs = (GazeFocus->GetActorLocation() - GetPawn()->GetActorLocation()).GetSafeNormal();
            bool bTargetInFront = VectorBetweenUs.Dot(MeshForwardVector) > .2;
            if (bTargetInFront)
            {
                ControlledCharacter->GazeFocusLocation = UKismetMathLibrary::VInterpTo(ControlledCharacter->GazeFocusLocation, GazeFocus->GetActorLocation(), DeltaTime, 4.f);
            }
            else
            {
                FVector LookAtLocation = ControlledCharacter->GetActorLocation() + MeshForwardVector * 100.f;
                ControlledCharacter->GazeFocusLocation = LookAtLocation;
            }
        }
        if (ControlledCharacter->bIsBlocking && EnemyTarget && EnemyTarget->IsValidLowLevelFast())
        {
            SetFocus(EnemyTarget);
            ControlledCharacter->bUseControllerRotationYaw = true;
            ControlledCharacter->GetCharacterMovement()->bOrientRotationToMovement = false;
        }
    }
}

void AEnemyAIController::InitPerception() 
{
    SightConfig = CreateDefaultSubobject<UAISenseConfig_Sight>(TEXT("Sight Config"));
    PerceptionComp = CreateDefaultSubobject<UAIPerceptionComponent>(TEXT("Perception Component"));
    HearingConfig = CreateDefaultSubobject<UAISenseConfig_Hearing>(TEXT("Hearing Config"));
    SightConfig->SightRadius = 3000;
    SightConfig->LoseSightRadius = 3200;
    SightConfig->PeripheralVisionAngleDegrees = 90.0f;
    SightConfig->SetMaxAge(5.f);
    SightConfig->AutoSuccessRangeFromLastSeenLocation = 1.f;
    SightConfig->DetectionByAffiliation.bDetectEnemies = true;
    SightConfig->DetectionByAffiliation.bDetectNeutrals = false;
    SightConfig->DetectionByAffiliation.bDetectFriendlies = false;

    HearingConfig->HearingRange = 2000.f;
    HearingConfig->DetectionByAffiliation.bDetectEnemies = true;
    HearingConfig->DetectionByAffiliation.bDetectNeutrals = true;
    HearingConfig->DetectionByAffiliation.bDetectFriendlies = true;

    PerceptionComp->SetDominantSense(*SightConfig->GetSenseImplementation());
    PerceptionComp->ConfigureSense(*SightConfig);
    PerceptionComp->OnPerceptionUpdated.AddDynamic(this, &AEnemyAIController::UpdatePerceivedActors);
    PerceptionComp->OnTargetPerceptionUpdated.AddDynamic(this, &AEnemyAIController::OnUpdateHearing);
}

void AEnemyAIController::OnUpdateHearing(AActor* actor, FAIStimulus stimulus)
{
}

void AEnemyAIController::UpdatePerceivedActors(const TArray<AActor*>& PerceivedActors) 
{
    if (ControlledCharacter && GetBrainComponent())
    {
        TArray<AActor*> HostilesInRange;
        PerceptionComp->GetCurrentlyPerceivedActors(SightConfig->GetSenseImplementation(), HostilesInRange);
        // start decaying any enemies that leave perception
        ControlledCharacter->MemoryComp->CheckUnperceivedEnemies(HostilesInRange);
        if (HostilesInRange.IsEmpty())
        {
            return;
        }
        bool bEnemyFound = false;
        bool bBuildingFound = false;

        // get data
        for (auto& HostileActor : HostilesInRange)
        {
            if (!HostileActor->IsValidLowLevelFast()) continue;
            if (auto EnemyActor = Cast<IGenericTeamAgentInterface>(HostileActor))
            {
                if (EnemyActor && EnemyActor->GetGenericTeamId() != ControlledCharacter->GetGenericTeamId())
                {
                    bEnemyFound = true;
                    // add any new enemies to memory
                    if (auto IntHostileActor = Cast<IMemoryInterface>(HostileActor)) IntHostileActor->Execute_GetEnemyData(Cast<UObject>(EnemyActor), ControlledCharacter->MemoryComp);
                    // share data with teammates who will switch to combat mode after delay (see ShareData definition)
                    ControlledCharacter->MemoryComp->ShareData();
                    // turn head to face new enemy if in front
                    if (ControlledCharacter->GetActorForwardVector().Dot(HostileActor->GetActorLocation().GetSafeNormal()) > 0.2)
                    {
                        ControlledCharacter->bOverrideProceduralGaze = true;
                        ControlledCharacter->GazeFocusLocation = HostileActor->GetActorLocation();
                        FTimerHandle GazeTimer;
                        FTimerDelegate GazeDelegate;
                        GazeDelegate.BindLambda([this]()
                            {
                                if (ControlledCharacter) ControlledCharacter->bOverrideProceduralGaze = false;
                            });
                        GetWorld()->GetTimerManager().SetTimer(GazeTimer, GazeDelegate, .5, false);
                    }
                }
            }
            else if (auto EnemyBuilding = Cast<IBuildingInterface>(HostileActor))
            {
                // return if direct path to monument found
                bBuildingFound = true;
                EnemyBuilding->Execute_GetBuildingData(Cast<UObject>(EnemyBuilding), ControlledCharacter->MemoryComp);
                // TODO share data
            }
        }
        // if enemy found, get into combat mode, else exit
        if (bEnemyFound)
        {
            // when entering combat mode state
            if (!ControlledCharacter->bInCombatMode)
            {
                ActorToFocusOn = ControlledCharacter->MemoryComp->GetNearestEnemy();
                ControlledCharacter->Server_SetInCombatMode(true);
                if (!bInDanger) bInDanger = ControlledCharacter->MemoryComp->CheckInDanger();
                // notify teammates of danger
                if (bInDanger)
                {
                    if (ControlledCharacter->MemoryComp->TeamHasLeader()) ControlledCharacter->MemoryComp->GetLeader()->OwningEnemyController->TeamSelectTarget();
                    else Engage(ControlledCharacter->MemoryComp->SelectEnemyTarget());
                }
                else if (ControlledCharacter->MemoryComp->bIsLeader)
                {
                    ControlledCharacter->bControllerOverrideMovement = true;
                    ControlledCharacter->SetWalkSpeed(0);
                    GetBrainComponent()->PauseLogic(TEXT("Play animation"));
                    if (EngageAnimation)
                    {
                        ControlledCharacter->PlayAnimMontage(EngageAnimation, 1.f);
                        FTimerHandle ReengageTimer;
                        FTimerDelegate ReengageDelegate;
                        ReengageDelegate.BindLambda([this]()
                            {
                                ControlledCharacter->bControllerOverrideMovement = false;
                                GetBrainComponent()->ResumeLogic(TEXT("Animation finished"));
                            });
                        GetWorld()->GetTimerManager().SetTimer(ReengageTimer, ReengageDelegate, EngageAnimation->GetPlayLength(), false);
                    }
                }
            }
        }
        else if (!EnemyTarget && !StaticTarget && bBuildingFound)
        {
            SetStaticTarget();
        }
    }
}

float AEnemyAIController::GetPathToMonument() 
{
    if (PathComp->HasValidPath() && ControlledCharacter->GetMonument())
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
    if (ControlledCharacter->GetMonument())
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
        FVector EndLocation = ControlledCharacter->GetMonument()->GetActorLocation();
        // get world rotation of AI relative to monument
        FRotator OriginalRotation = UKismetMathLibrary::FindLookAtRotation(StartLocation, EndLocation);
        float DistanceToMonument = FVector::Distance(StartLocation, EndLocation);
        FCollisionQueryParams Params;
        Params.AddIgnoredActor(ControlledCharacter);
        // check if direct path to monument
        if (!CheckBlocked)
        {
            bool bHitSuccess = GetWorld()->LineTraceSingleByChannel(HitResult, StartLocation, EndLocation, ECollisionChannel::ECC_GameTraceChannel1, Params);
            if (HitResult.GetActor() == ControlledCharacter->GetMonument())
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
                if (HitResult.GetActor() == ControlledCharacter->GetMonument()) { StaticTarget = nullptr; }
                // attack player if found
                else if (Cast<AEalondCharacter>(HitResult.GetActor())) { Engage(HitResult.GetActor()); }
                // add building data if found
                else
                {
                    auto BuildingInterface = Cast<IBuildingInterface>(HitResult.GetActor());
                    if (BuildingInterface && BuildingInterface->Execute_CheckIfBuilding(Cast<UObject>(BuildingInterface), HitResult.GetActor()))
                    {
                        BuildingInterface->Execute_GetBuildingData(Cast<UObject>(BuildingInterface), ControlledCharacter->MemoryComp);
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
        StaticTarget = ControlledCharacter->MemoryComp->SelectBuildingTarget();
        return;
    }
    // check for short, direct path to monument
    float PathToMonument = GetPathToMonument();
    if (PathToMonument > ControlledCharacter->GetDistanceTo(ControlledCharacter->GetMonument()) * 1.5)
    {
        ABuilding* Target = ControlledCharacter->MemoryComp->SelectBuildingTarget();
        if (Target)
        {
            ControlledCharacter->bBlockedByTarget= false;
            StaticTarget = Target;
        }
    }
}

FVector AEnemyAIController::GetStaticTargetLocation(ABuilding* BldTarget) const
{
    if (!BldTarget) {return FVector();}
    return BldTarget->GetNearestEdge(ControlledCharacter);
}

AActor* AEnemyAIController::GetCurrentTarget() const
{
    if (EnemyTarget) {return EnemyTarget;}
    else if (StaticTarget) {return StaticTarget;}
    else if (ControlledCharacter->GetDistanceTo(ControlledCharacter->GetMonument()) < 200.f) {return ControlledCharacter->GetMonument();}
    else {return nullptr;}
}

void AEnemyAIController::OnTeammateDead(UMemoryComponentBase* DeadTeammateMemory)
{
    // increment chance to flee for gobbos
    if (DeadTeammateMemory && ControlledCharacter->IsA(AGoblin::StaticClass()))
    {
        if (DeadTeammateMemory->bIsLeader)
        {
            ControlledCharacter->FleeProbability += 0.4;
        }
        else
        {
            ControlledCharacter->FleeProbability += 0.1;
        }
        CheckFlee();
    }
}

void AEnemyAIController::TeamSelectTarget(AActor* EnemyToIgnore)
{
    // have teammates select
    if (ControlledCharacter && ControlledCharacter->MemoryComp->bIsLeader)
    {
        for (auto& Teammate : ControlledCharacter->MemoryComp->GetCurrentTeam())
        {
            if (IMemoryInterface* IntMem = Cast<IMemoryInterface>(Teammate->GetOwner()))
            {
                if (UMemoryComponentBase* EnemyMemComp = IntMem->Execute_GetCharacterMemory(Cast<UObject>(IntMem)))
                {
                    AActor* NewTarget = EnemyMemComp->SelectEnemyTarget();
                    if (NewTarget) {EnemyMemComp->OwningEnemyController->Engage(NewTarget);}
                }
            }
        }
        // select own target
        AActor* NewTarget = ControlledCharacter->MemoryComp->SelectEnemyTarget();
        if (NewTarget) {Engage(NewTarget);}
    }
}

bool AEnemyAIController::CheckEngageCondition(AActor* CheckedActor) 
{
    return true;
}

void AEnemyAIController::Engage(AActor* TargetCandidate, bool bOverrideCooldown) 
{
    bool bShouldEngage = bOverrideCooldown || (!bOverrideCooldown && bCanReselectTarget);
    if (bShouldEngage && TargetCandidate)
    {
        if (IMemoryInterface* IntEnemy = Cast<IMemoryInterface>(TargetCandidate))
        {
            IntEnemy->Execute_GetAttackers(Cast<UObject>(IntEnemy), true, true);
        }
        ControlledCharacter->MemoryComp->bIsInFormation = false;
        EnemyTarget = TargetCandidate;
        if (CheckEngageCondition(TargetCandidate)) {return;}
        StaticTarget = nullptr;
        ControlledCharacter->EquipWeapons();
        ControlledCharacter->Server_SetInCombatMode(true);
        // set enemy selection cooldown
        bCanReselectTarget = false;
        FTimerDelegate DisableEnemySelection;
        DisableEnemySelection.BindLambda([this]()
            {
                bCanReselectTarget = true;
            });
        GetWorld()->GetTimerManager().SetTimer(TargetSelectionCooldownTimer, DisableEnemySelection, 15.f, false);
    }
}

ECombatDecision AEnemyAIController::MakeCombatDecision(AActor* Target) 
{
    if (!ControlledCharacter || ControlledCharacter->bIsHurt || ControlledCharacter->bIsRolling || ControlledCharacter->bIsDodging || ControlledCharacter->bIsAttacking || ControlledCharacter->GetCharacterMovement()->IsFalling())
    {
        return ECD_NoAttack;
    }
    if (Target)
    {
        // cast target to Ealond Base Character
        const AEalondCharacterBase* EalondCharTarget = nullptr;
        if (auto IntTarget = Cast<IPlayerAIInteractionInterface>(Target))
        {
            EalondCharTarget = IntTarget->Execute_GetBaseCharRef(Cast<UObject>(IntTarget));
        }
        if (!EalondCharTarget)
        {
            GEngine->AddOnScreenDebugMessage(-1, 3.f, FColor::Blue, TEXT("Failed to cast target to base class. Attack failed."));
            return ECD_NoAttack;
        }
        // Goblin
        int32 DiceThrow = FMath::RandRange(1, 20);
        if (ControlledCharacter->IsA(AGoblin::StaticClass()))
        {
            DistanceFromEnemy = ControlledCharacter->GetDistanceTo(Target);
            FVector VectorBetweenUs = (GetPawn()->GetActorLocation() - Target->GetActorLocation()).GetSafeNormal();
            bool bTargetFacingMe = Target->GetActorForwardVector().Dot(VectorBetweenUs) > .75f;
            // 0 = not moving, >0.8 = moving towards, negative = moving away
            bool bTargetMovingTowardsMe = Target->GetVelocity().GetSafeNormal().Dot(VectorBetweenUs) > .8;
            bTargetMovingTowardsMe ? ControlledCharacter->SetSpeed(1) : ControlledCharacter->SetSpeed(3);
            if (ControlledCharacter->MemoryComp->TeamRole == TR_AttackMelee_1h)
            {
                // return no move taken if too far
                if (DistanceFromEnemy > 750.f) return ECD_NoAttack;
                else if (DistanceFromEnemy < 200.f)
                {
                    if (bTargetFacingMe && (EalondCharTarget->bAttackPressed || EalondCharTarget->bIsAttacking) && DiceThrow > 4.f)
                    {
                        Block(Target);
                        return ECD_Block;
                    }
                    else
                    {
                        Attack(Target, ECD_CloseAttack);
                        return ECD_CloseAttack;
                    }
                }
                else if ((ControlledCharacter->IsPartyLeader() && DiceThrow > 13.f) || (!ControlledCharacter->IsPartyLeader() && DiceThrow > 17.f))
                {
                    ControlledCharacter->bIsCharging = true;
                    Attack(Target, ECD_DistanceAttack);
                    return ECD_DistanceAttack;
                }
            }
            else if (ControlledCharacter->MemoryComp->TeamRole == TR_FlankMelee)
            {
                bool bCanAttack = false;
                // if melee teammate/s alive, move to flank, otherwise attack
                if (ControlledCharacter->TeamHasMelee())
                {
                    FlankPosition = GetFlankPosition(Target);
                    if (!bTargetFacingMe) bCanAttack = true;
                }
                else bCanAttack = true;
                if (!bCanAttack || DistanceFromEnemy > 500.f) return ECD_NoAttack;
                if (DistanceFromEnemy < 200.f)
                {
                    // small chance of dodging if target moving towards me, 100% chance if moving towards and attacking
                    if (bTargetMovingTowardsMe && !(EalondCharTarget->bAttackPressed || EalondCharTarget->bIsAttacking))
                    {
                        if (DiceThrow > 16.f && Dodge(FMath::RandBool())) return ECD_Evade;
                        else
                        {
                            Attack(Target, ECD_CloseAttack);
                            return ECD_CloseAttack;
                        }
                    }
                    else if (EalondCharTarget->bAttackPressed || EalondCharTarget->bIsAttacking)
                    {
                        if (Dodge(FMath::RandBool())) return ECD_Evade;
                        else
                        {
                            Attack(Target, ECD_CloseAttack);
                            return ECD_CloseAttack;
                        }
                    }
                    else
                    {
                        Attack(Target, ECD_CloseAttack);
                        return ECD_CloseAttack;
                    }
                }
                else if ((ControlledCharacter->IsPartyLeader() && DiceThrow > 10.f) || (!ControlledCharacter->IsPartyLeader() && DiceThrow > 14.f))
                {
                    ControlledCharacter->bIsCharging = true;
                    Attack(Target, ECD_DistanceAttack);
                    return ECD_DistanceAttack;
                }
            }
        }
    }
    return ECD_NoAttack;
}

void AEnemyAIController::ResetAttackState()
{
    ControlledCharacter->bIsAttacking = false;
    ControlledCharacter->bIsCharging = false;
    ControlledCharacter->bBodyRecoilSmall = false;
    ControlledCharacter->bIsTaunting = false;
    ControlledCharacter->bShieldBreak = false;
    CombatType = ECD_NoAttack;
}

bool AEnemyAIController::IsFlankUnit() const
{
    if (ControlledCharacter->MemoryComp->TeamRole == TR_FlankMelee) return true;
    else return false;
}

FVector AEnemyAIController::GetFlankPosition(AActor* Target)
{
    if (!Target) return FVector(0,0,0);
    // determine side to flank
    FVector VectorBetweenUs = ControlledCharacter->GetActorLocation() - Target->GetActorLocation();
    bool bFlankRight = (VectorBetweenUs.Dot(Target->GetActorRightVector()) >= 0);
    if (bFlankRight)
    {
        return Target->GetActorRightVector() * 500.f + Target->GetActorLocation();
    }
    else return Target->GetActorRightVector() * -500.f + Target->GetActorLocation();
}

bool AEnemyAIController::Dodge(bool bCanRoll)
{
    if (ControlledCharacter->GetCharacterMovement()->IsFalling()) {return false;}
    bool bCanDodge = false;
    FVector Start = ControlledCharacter->GetActorLocation();
    TArray<FVector> EndPoints;
    FHitResult HitResult;
    FCollisionQueryParams Params;
    bool HitSuccess;
    Params.AddIgnoredActor(ControlledCharacter);
    // get trace checks for each direction
    EndPoints.Add(Start + GetPawn()->GetActorRightVector() * -500.f);
    EndPoints.Add(Start + GetPawn()->GetActorRightVector() * 500.f);
    EndPoints.Add(Start + GetPawn()->GetActorForwardVector() * -500.f);
    TArray<int32> Directions = {0, 1, 2};
    // shuffle directions
    int32 LastIndex = EndPoints.Num() - 1;
    for (int32 i = 0; i <= LastIndex; ++i)
    {
        int32 Index = FMath::RandRange(i, LastIndex);
        if (i != Index)
        {
            EndPoints.Swap(i, Index);
            Directions.Swap(i, Index);
        }
    }
    for (int32 i = 0; i < EndPoints.Num(); i++)
    {
        int32 Dir = Directions[i];
        float JumpAngle = 10.f;
        HitSuccess = GetWorld()->LineTraceSingleByChannel(HitResult, Start, EndPoints[i], ECC_Visibility, Params);
        if (HitSuccess)
        {
            // check blocked by actor
            if (HitResult.GetActor() && HitResult.Distance < 400.f)
            {
               continue;
            }
        }
        // check ground height
        FVector NewStartPoint = EndPoints[i] + FVector(0, 0, 350.f);
        FVector NewEndPoint = EndPoints[i] + FVector(0, 0, -350.f);
        bool bHitResultGround = GetWorld()->LineTraceSingleByChannel(HitResult, NewStartPoint, NewEndPoint, ECC_Visibility, Params);
        if (bHitResultGround)
        {
            float ZDifference = HitResult.Location.Z - ControlledCharacter->GetActorLocation().Z;
            // too steep
            if (ZDifference < -300.f || ZDifference > 300.f)
            {
                continue;
            }
            // jump higher
            else if (ZDifference > 150.f)
            {
                bCanDodge = true;
                JumpAngle = 30.f;
            }
            // jump lower
            else if (ZDifference < 150.f)
            {
                bCanDodge = true;
                JumpAngle = 5.f;
            }
            else
            {
                bCanDodge = true;
            }
        }
        else bCanDodge = false;
        if (bCanDodge)
        {
            if (!bCanRoll)
            {
                FVector LaunchTarget;
                if (Dir == 0) LaunchTarget = GetPawn()->GetActorLocation() + GetPawn()->GetActorRightVector() * -500.f;
                else if (Dir == 1) LaunchTarget = GetPawn()->GetActorLocation() + (GetPawn()->GetActorRightVector() * -1.f) * 500.f;
                else
                {
                    LaunchTarget = GetPawn()->GetActorLocation() + GetPawn()->GetActorForwardVector() * -500.f;
                    Dir = 2;
                }
                FVector LaunchVelocity = ControlledCharacter->GetLaunchVelocityToObject(ControlledCharacter->GetActorLocation(), LaunchTarget, JumpAngle);
                ControlledCharacter->DodgeDirection = Dir;
                ControlledCharacter->Server_SetIsDodging(true);
                FTimerHandle DodgeTimer;
                FTimerDelegate DodgeDelegate;
                DodgeDelegate.BindLambda([this, LaunchVelocity]()
                    {
                        ControlledCharacter->Server_LaunchCharacter(LaunchVelocity);
                    });
                GetWorld()->GetTimerManager().SetTimer(DodgeTimer, DodgeDelegate, .2, false);
                return true;
            }
            else
            {
                ControlledCharacter->DodgeDirection = Dir;
                ControlledCharacter->bIsRolling = true;
                return true;
            }
        }
    }
    return false;
}

void AEnemyAIController::CheckFlee()
{
    float DiceRoll = FMath::RandRange(0.f, 1.f);
    if (DiceRoll < ControlledCharacter->FleeProbability)
    {
        ControlledCharacter->bIsFleeing = true;
    }
}

void AEnemyAIController::OnPawnDead()
{
    UAIPerceptionSystem::GetCurrent(GetWorld())->UnregisterSource(*ControlledCharacter, UAISense_Sight::StaticClass());
    ResetAttackState();
    ClearFocus(EAIFocusPriority::Default);
    GetBrainComponent()->StopLogic(TEXT("Pawn dead"));
    if (PathComp) PathComp->Deactivate();
    if (AEalondGameMode* GameMode = Cast<AEalondGameMode>(GetWorld()->GetAuthGameMode())) GameMode->RemoveAIFromMap(ControlledCharacter);
    if (PerceptionComp) PerceptionComp->Deactivate();
    UnPossess();
}

FPathFollowingRequestResult AEnemyAIController::MoveTo(const FAIMoveRequest& MoveRequest, FNavPathSharedPtr* OutPath)
{
    if (MoveRequest.IsMoveToActorRequest())
    {
        UE_LOG(LogTemp, Warning, TEXT("%s, %s"), *MoveRequest.GetGoalActor()->GetName(), *MoveRequest.GetGoalActor()->GetActorLocation().ToString());
    }

    return Super::MoveTo(MoveRequest, OutPath);
}

void AEnemyAIController::Disengage(bool bExitCombatState) 
{
    EnemyTarget = nullptr;
    bCanReselectTarget = true;
    ControlledCharacter->bIsAttacking = false;
    if (bExitCombatState)
    {
        ControlledCharacter->Server_SetInCombatMode(false);
        ControlledCharacter->SetSpeed(3);
    }
    else
    {
        ControlledCharacter->SetSpeed(2);
    }
}

void AEnemyAIController::DestroyBuilding(AActor* Building)
 {
     Building->SetActorEnableCollision(false);
     Building->Destroy();
 }

void AEnemyAIController::CheckEngageFromDamage(AController* ContrInstigator, float IncomingDamage)
{
    // update flee for goblins
    if (ControlledCharacter->IsA(AGoblin::StaticClass()))
    {
        float HealthPerc = ControlledCharacter->GetHealth() / ControlledCharacter->ProgressComponent->GetPhysicalStats().MaxHealth;
        if (HealthPerc > 0.3) {ControlledCharacter->FleeProbability += (1.f - HealthPerc) / 4.f;}
        else if (HealthPerc > 0.1) {ControlledCharacter->FleeProbability += (1.f - HealthPerc) / 3.f;}
        else {ControlledCharacter->FleeProbability += 0.3;}
        ControlledCharacter->FleeProbability = FMath::Clamp(ControlledCharacter->FleeProbability, 0, 1.f);
        CheckFlee();
    }

    // engage if no target set
    if (!EnemyTarget && ContrInstigator->GetPawn()) Engage(ContrInstigator->GetPawn()); return;
    // sum damage by enemy and aggro on those dealing damage over a threshold
	if (DamageByActor.Contains(ContrInstigator))
	{
		DamageByActor[ContrInstigator] += IncomingDamage;
	}
	else
	{
		DamageByActor.Add(ContrInstigator, IncomingDamage);
	}
	if (DamageByActor[ContrInstigator] > 30.f)
	{
		Engage(ContrInstigator->GetPawn(), true);
	}
}

void AEnemyAIController::Attack(AActor* Target, ECombatDecision CombatDecision)
{
    if (!Target) return;
    ControlledCharacter->Server_SetIsAttacking(true);
    CombatType = CombatDecision;
    float TargetVelDot = Target->GetVelocity().GetSafeNormal().Dot((Target->GetActorLocation() - GetPawn()->GetActorLocation()).GetSafeNormal());
    FRotator MyRot = GetPawn()->GetActorRotation();
    switch (CombatDecision)
    {
    case ECD_CloseAttack:
        // if target is moving away from character, move towards and attack
        if (TargetVelDot < 0)
        {
            MoveToActor(Target);
        }
        // if being approached, move away from target
        else if (TargetVelDot > .8)
        {
            FVector VectorBetweenUs = (GetPawn()->GetActorLocation()- Target->GetActorLocation()).GetSafeNormal();
            FVector MoveToLoc = GetPawn()->GetActorLocation() + VectorBetweenUs * 150.f;
            MoveTo(MoveToLoc);
        }
        break;
    case ECD_DistanceAttack:
        if (!ControlledCharacter->bIsCharging && !ControlledCharacter->GetCharacterMovement()->IsFalling())
        {
            // get vector close to target
            FVector VectorBetweenChars = Target->GetActorLocation() - ControlledCharacter->GetActorLocation();
            float DistanceBetweenChars = ControlledCharacter->GetDistanceTo(Target);
            FVector LaunchTarget = ControlledCharacter->GetActorLocation() + VectorBetweenChars.GetSafeNormal() * (DistanceBetweenChars - 50.f);
            FVector JumpVelocity = ControlledCharacter->GetLaunchVelocityToObject(GetPawn()->GetActorLocation(), LaunchTarget, 10.f);
            ControlledCharacter->Server_LaunchCharacter(JumpVelocity);
        }
    }
}

void AEnemyAIController::Block(AActor* Target)
{
    if (!Target || !Target->IsValidLowLevelFast() || !GetPawn()) return;
    const AEalondCharacterBase* EalondCharTarget = nullptr;
    if (auto IntTarget = Cast<IPlayerAIInteractionInterface>(Target))
    {
        EalondCharTarget = IntTarget->Execute_GetBaseCharRef(Cast<UObject>(IntTarget));
    }
    if (!EalondCharTarget) return;
    FVector VectorBetweenUs = (GetPawn()->GetActorLocation() - Target->GetActorLocation()).GetSafeNormal();
    bool bIsFacingMe = Target->GetActorForwardVector().Dot(VectorBetweenUs) > .8f;
    if (bIsFacingMe && (EalondCharTarget->bIsAttacking || EalondCharTarget->bAttackPressed))
    {
        float BlockTime = FMath::RandRange(.5f, 2.f);
        FTimerHandle BlockTimer;
        FTimerDelegate BlockDelegate = FTimerDelegate::CreateUObject(this, &AEnemyAIController::Block, Target);
        ControlledCharacter->Server_SetIsBlocking(true);
        SetFocus(Target);
        ControlledCharacter->bUseControllerRotationYaw = true;
        ControlledCharacter->GetCharacterMovement()->bOrientRotationToMovement = false;
        GetWorld()->GetTimerManager().SetTimer(BlockTimer, BlockDelegate, BlockTime, false);
    }
    else
    {
        ClearFocus(EAIFocusPriority::Gameplay);
        ControlledCharacter->bUseControllerRotationYaw = true;
        ControlledCharacter->GetCharacterMovement()->bOrientRotationToMovement = false;
        ControlledCharacter->Server_SetIsBlocking(false);
    }
}

void AEnemyAIController::ShieldBash(AActor* Target)
{
    FTimerHandle FlipBoolTimer;
    FTimerDelegate FlipBoolDelegate;
    ControlledCharacter->Server_SetIsBlocking(true);
    ControlledCharacter->Server_SetIsAttacking(true);
    FlipBoolDelegate.BindLambda([this]()
        {
            ControlledCharacter->Server_SetIsAttacking(false);
            ControlledCharacter->Server_SetIsBlocking(false);
        });
    GetWorld()->GetTimerManager().SetTimer(FlipBoolTimer, FlipBoolDelegate, 2.f, false);
}

void AEnemyAIController::JumpAttack(AActor* Target)
{
    FTimerHandle AttackTimer;
    FTimerDelegate FlipAttackBools;
    ControlledCharacter->bIsJumpAttack = true;
    ControlledCharacter->bIsAttacking = true;
    FlipAttackBools.BindLambda([this, Target]()
        {
            FVector JumpVelocity = ControlledCharacter->GetLaunchVelocityToObject(GetPawn()->GetActorLocation(), Target->GetActorLocation(), 10.f);
            ControlledCharacter->Server_LaunchCharacter(JumpVelocity);
        });
    GetWorld()->GetTimerManager().SetTimer(AttackTimer, FlipAttackBools, .5f, false);
}

void AEnemyAIController::ForceTargetRecheck(AActor* DeadTarget)
{
    if (DeadTarget && ControlledCharacter)
    {
        if (EnemyTarget == DeadTarget)
        {
            float Delay = FMath::RandRange(0.2, 0.7);
            float AnimLength = Delay;
            GetBrainComponent()->PauseLogic(TEXT("Playing animation..."));
            // if animation is valid, play after delay and set final delay to anim length; othwerwise total delay will be Delay variable
            if (OnKilledEnemyAnimations.Num())
            {
                int32 RandIndex = FMath::RandRange(0, OnKilledEnemyAnimations.Num() - 1);
                if (OnKilledEnemyAnimations[RandIndex])
                {
                    ControlledCharacter->Server_PlayAnim(OnKilledEnemyAnimations[RandIndex], Delay);
                    AnimLength = OnKilledEnemyAnimations[RandIndex]->GetPlayLength();
                }
            }
            // force recheck after animation finished
            FTimerHandle RestartTimer;
            FTimerDelegate RestartDelegate;
            RestartDelegate.BindLambda([this]()
                {
                    GetBrainComponent()->ResumeLogic(TEXT("Animation finished"));
                    AActor* NewTarget = ControlledCharacter->MemoryComp->SelectEnemyTarget();
                    if (NewTarget)
                    {
                        Engage(NewTarget);
                    }
                    else if (ControlledCharacter->MemoryComp->GetEnemiesInMemory().IsEmpty())
                    {
                        Disengage(true);
                    }
                    else
                    {
                        Disengage(false);
                    }
                });
            GetWorld()->GetTimerManager().SetTimer(RestartTimer, RestartDelegate, AnimLength, false);
        }
        else if (ControlledCharacter->MemoryComp->GetEnemiesInMemory().Num() <= 1)
        {
            if (ControlledCharacter->MemoryComp->GetEnemiesInMemory().IsEmpty() || ControlledCharacter->MemoryComp->GetEnemiesInMemory().Contains(DeadTarget))
            {
                Disengage(true);
            }
        }
    }
}

ETeamAttitude::Type AEnemyAIController::GetTeamAttitudeTowards(const AActor& Other) const
{
    auto StimulusInterface = Cast<IGenericTeamAgentInterface>(&Other);
    if (!StimulusInterface) {return ETeamAttitude::Neutral;}
    else if (StimulusInterface->GetGenericTeamId() == FGenericTeamId(0)) {return ETeamAttitude::Hostile;}
    else if (StimulusInterface->GetGenericTeamId() == FGenericTeamId(1)) {return ETeamAttitude::Friendly;}
    else return ETeamAttitude::Neutral;
}
