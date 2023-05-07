float UCustomMathLibrary::GetLandscapeHeightAtLocation(UWorld* WorldRef, FVector Location)
{
	FHitResult HitResult;
	FCollisionQueryParams Params;
	//Params.AddIgnoredActor(this);
	FVector Start = Location + FVector(0, 0, 1000.f);
	FVector End = Location + FVector(0, 0, -1000.f);
	bool bGroundCheck = WorldRef->LineTraceSingleByChannel(HitResult, Start, End, ECC_GameTraceChannel8, Params);
	float GroundZ = 0;
	if (bGroundCheck)
	{
		return HitResult.Location.Z;
	}
	return 0.0f;
}

FRotator UCustomMathLibrary::GetAngleToHitMovingTarget(FVector ArcherLocation, FVector TargetLocation, FVector TargetVelocity, float ProjectileVelocity)
{
	// get whether moving right or left in relation to character
	float RightVecDot = (TargetVelocity.GetSafeNormal()).Dot(GetActorRightVector());
	bool bIsMovingAtAngle = RightVecDot > .75 || RightVecDot < -.75;
	bool bIsMovingRight = RightVecDot > 0;
    FVector Direction = TargetLocation - ArcherLocation;
    float Distance = Direction.Size();
    FVector DirectionNormalized = Direction / Distance;
    float Time = Distance / ProjectileVelocity;
    FVector AimLocation;
	TargetVelocity.Length() > 0 ? AimLocation = TargetLocation + TargetVelocity * Time : AimLocation = TargetLocation;
    FVector AimDirection = AimLocation - ArcherLocation;
    FRotator AimRotation = AimDirection.Rotation();
	float PitchOffset = 0;
	float YawOffset = 0;
	if (bIsMovingAtAngle)
	{
		if (Distance > 1000.f)
		{
			PitchOffset = Distance / 1000.f;
			YawOffset = Distance / 1000.f;
			if (!bIsMovingRight) YawOffset *= -1.f;
		}
		else if (TargetVelocity.Length() > 400.f)
		{
			PitchOffset = .5;
			YawOffset = .5;
			if (!bIsMovingRight) YawOffset *= -1.f;
		}
	}
	AimRotation.Pitch += PitchOffset;
	AimRotation.Yaw += YawOffset;
  return AimRotation;
}

FVector UCustomMathLibrary::GetLaunchVelocityToObject(FVector SubjectLoc, FVector TargetLoc, float TakeOffAngle)
{
	const float Gravity = GetCharacterMovement()->GetGravityZ();
	float DisplacementZ = TargetLoc.Z - SubjectLoc.Z;
	FVector DisplacementXY = FVector(TargetLoc.X, TargetLoc.Y, 0) - FVector(SubjectLoc.X, SubjectLoc.Y, 0);
	float Height = FVector::Distance(TargetLoc, SubjectLoc) / 2 * UKismetMathLibrary::Tan(FMath::DegreesToRadians(TakeOffAngle));
	float Time = UKismetMathLibrary::Sqrt(-2 * Height / Gravity) + UKismetMathLibrary::Sqrt(2 * (DisplacementZ - Height) / Gravity);
	FVector VelocityZ = FVector(0, 0, 1.f) * UKismetMathLibrary::Sqrt(-2 * Gravity * Height);
	FVector VelocityXY = DisplacementXY / Time;
	FVector FinalVelocity = VelocityXY + VelocityZ;
	return FinalVelocity;
}

FVector UCustomMathLibrary::CalculateBezierPoint(const FVector& StartPoint, const FVector& ControlPoint1, const FVector& ControlPoint2, const FVector& EndPoint, const float T)
{
    const float OneMinusT = 1.0f - T;
    const float OneMinusTSquared = OneMinusT * OneMinusT;
    const float TSquared = T * T;

    const FVector BezierPoint =
        OneMinusTSquared * OneMinusT * StartPoint +
        3.0f * OneMinusTSquared * T * ControlPoint1 +
        3.0f * OneMinusT * TSquared * ControlPoint2 +
        TSquared * T * EndPoint;

    return BezierPoint;
}

