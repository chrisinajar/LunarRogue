// Fill out your copyright notice in the Description page of Project Settings.


#include "LunarCharacterMovementComponent.h"
#include "GameFramework/Character.h"
#include "LunarTypes.h"

bool ULunarCharacterMovementComponent::IsWalkable(const FHitResult& Hit) const
{
    if (!IsSliding())
	{
		return UCharacterMovementComponent::IsWalkable(Hit);
	}
	return true;
}

void ULunarCharacterMovementComponent::BeginSlide()
{
    if (IsMovingOnGround())
    {
        SetMovementMode(MOVE_Custom, CMOVE_Slide);
    }
}

void ULunarCharacterMovementComponent::EndSlide()
{
    if (IsSliding())
    {
        if (CustomMovementMode == CMOVE_AirSlide)
        {
            SetMovementMode(MOVE_Falling, CMOVE_AirSlide);
        }
        else if (CustomMovementMode == CMOVE_Slide)
        {
            SetMovementMode(MOVE_Walking, CMOVE_Slide);
        }
    }
}

bool ULunarCharacterMovementComponent::IsSliding() const
{
    return MovementMode == MOVE_Custom && (CustomMovementMode == CMOVE_AirSlide || CustomMovementMode == CMOVE_Slide);
}

bool ULunarCharacterMovementComponent::IsSlidingInAir() const
{
	return (MovementMode == MOVE_Custom && CustomMovementMode == CMOVE_AirSlide);
}

bool ULunarCharacterMovementComponent::IsSlidingOnGround() const
{
	return (MovementMode == MOVE_Custom && CustomMovementMode == CMOVE_Slide);
}

void ULunarCharacterMovementComponent::HandleWalkingOffLedge(const FVector& PreviousFloorImpactNormal, const FVector& PreviousFloorContactNormal, const FVector& PreviousLocation, float TimeDelta)
{
    UCharacterMovementComponent::HandleWalkingOffLedge(PreviousFloorImpactNormal, PreviousFloorContactNormal, PreviousLocation, TimeDelta);
    if (IsSlidingOnGround())
    {
        CustomMovementMode = CMOVE_AirSlide;
    }
}

void ULunarCharacterMovementComponent::ProcessLanded(const FHitResult& Hit, float remainingTime, int32 Iterations)
{
    UCharacterMovementComponent::ProcessLanded(Hit, remainingTime, Iterations);
    if (IsSlidingInAir())
    {
        CustomMovementMode = CMOVE_Slide;
		
		const FVector PreImpactAccel = Acceleration + (-GetGravityDirection() * GetGravityZ());
		ApplyImpactPhysicsForces(Hit, PreImpactAccel, Velocity);
    }
}

float ULunarCharacterMovementComponent::SlideAlongSurface(const FVector& Delta, float Time, const FVector& InNormal, FHitResult& Hit, bool bHandleImpact)
{
	if (!Hit.bBlockingHit)
	{
		return 0.f;
	}

	FVector Normal(InNormal);
	const FVector::FReal NormalZ = GetGravitySpaceZ(Normal);
	if (IsSlidingOnGround())
	{
		// We don't want to be pushed up an unwalkable surface.
		if (NormalZ > 0.f)
		{
			if (!IsWalkable(Hit))
			{
				Normal = ProjectToGravityFloor(Normal).GetSafeNormal();
			}
		}
		else if (NormalZ < -UE_KINDA_SMALL_NUMBER)
		{
			// Don't push down into the floor when the impact is on the upper portion of the capsule.
			if (CurrentFloor.FloorDist < MIN_FLOOR_DIST && CurrentFloor.bBlockingHit)
			{
				const FVector FloorNormal = CurrentFloor.HitResult.Normal;
				const bool bFloorOpposedToMovement = (Delta | FloorNormal) < 0.f && (GetGravitySpaceZ(FloorNormal) < 1.f - UE_DELTA);
				if (bFloorOpposedToMovement)
				{
					Normal = FloorNormal;
				}
				
				Normal = ProjectToGravityFloor(Normal).GetSafeNormal();
			}
		}
	}

	return Super::SlideAlongSurface(Delta, Time, Normal, Hit, bHandleImpact);
}

void ULunarCharacterMovementComponent::PhysCustom(float deltaTime, int32 Iterations)
{
    switch (CustomMovementMode)
    {
        case CMOVE_AirSlide:
            PhysAirSliding(deltaTime, Iterations);
            break;
        case CMOVE_Slide:
            PhysSliding(deltaTime, Iterations);
            break;
        default:
            return;
    }
}

void ULunarCharacterMovementComponent::PhysAirSliding(float deltaTime, int32 Iterations)
{
    PhysFalling(deltaTime, Iterations);
}

FVector ULunarCharacterMovementComponent::ConstrainInputAcceleration(const FVector& InputAcceleration) const
{
    if (IsSliding())
    {
        return FVector(0);
    }
    return UCharacterMovementComponent::ConstrainInputAcceleration(InputAcceleration);
}

void ULunarCharacterMovementComponent::PhysSliding(float deltaTime, int32 Iterations)
{
	if (deltaTime < MIN_TICK_TIME)
	{
		return;
	}

	if (!CharacterOwner || (!CharacterOwner->Controller && !bRunPhysicsWithNoController && !HasAnimRootMotion() && !CurrentRootMotion.HasOverrideVelocity() && (CharacterOwner->GetLocalRole() != ROLE_SimulatedProxy)))
	{
		Acceleration = FVector::ZeroVector;
		Velocity = FVector::ZeroVector;
		return;
	}

	if (!UpdatedComponent->IsQueryCollisionEnabled())
	{
		SetMovementMode(MOVE_Walking);
		return;
	}

	bJustTeleported = false;
	bool bCheckedFall = false;
	bool bTriedLedgeMove = false;
	float remainingTime = deltaTime;

	const EMovementMode StartingMovementMode = MovementMode;
	const uint8 StartingCustomMovementMode = CustomMovementMode;

	// Perform the move
	while ( (remainingTime >= MIN_TICK_TIME) && (Iterations < MaxSimulationIterations) && CharacterOwner && (CharacterOwner->Controller || bRunPhysicsWithNoController || HasAnimRootMotion() || CurrentRootMotion.HasOverrideVelocity() || (CharacterOwner->GetLocalRole() == ROLE_SimulatedProxy)) )
	{
		Iterations++;
		bJustTeleported = false;
		const float timeTick = GetSimulationTimeStep(remainingTime, Iterations);
		remainingTime -= timeTick;

		// Save current values
		UPrimitiveComponent * const OldBase = GetMovementBase();
		const FVector PreviousBaseLocation = (OldBase != NULL) ? OldBase->GetComponentLocation() : FVector::ZeroVector;
		const FVector OldLocation = UpdatedComponent->GetComponentLocation();
		const FFindFloorResult OldFloor = CurrentFloor;

		RestorePreAdditiveRootMotionVelocity();

		// Ensure velocity is horizontal.
		MaintainHorizontalGroundVelocity();
		const FVector OldVelocity = Velocity;
		Acceleration = ProjectToGravityFloor(Acceleration);

		// Apply acceleration
		const bool bSkipForLedgeMove = bTriedLedgeMove;
		if( !HasAnimRootMotion() && !CurrentRootMotion.HasOverrideVelocity() && !bSkipForLedgeMove )
		{
            CalcVelocity(timeTick, GroundFriction/GroundFrictionFactor, false, 0);
		}
		Velocity = Velocity * (1.f - FMath::Min(GroundFriction/GroundFrictionFactor * timeTick, 1.f));

        const auto FloorNormal = CurrentFloor.HitResult.ImpactNormal;
        const auto FloorNormalZ = GetGravitySpaceZ(FloorNormal);
        // FloorNormalZ is the z factor of floor normal, so it's 1.0 for a flat floor and 0.0 for a flat wall
        // it could also be negative for ceilings etc
        if (FloorNormalZ < (1.f - UE_KINDA_SMALL_NUMBER) && FloorNormalZ > UE_KINDA_SMALL_NUMBER)
        {
            // the NSEW normal pointing "towards" downhill
            const auto DownhillDirectionNormal = ProjectToGravityFloor(FloorNormal).GetSafeNormal();
            // pointing parallel with the surface perfectly downhill
		    // const auto DownhillVector = FVector::VectorPlaneProject(DownhillDirectionNormal + GetGravityDirection(), FloorNormal);
            // let gravity tick once against velocity
            const auto FallVelocity = NewFallVelocity(Velocity, GetGravityDirection() * GetGravityZ(), timeTick);
            // grab how much speed we gained, this is how much we need to relay into horizontal movement
            const auto FallSpeedToGain = FallVelocity.Z - Velocity.Z;

            Velocity = Velocity + (DownhillDirectionNormal * (FallSpeedToGain * FMath::Min(1.f - (FloorNormalZ * FloorNormalZ), 1.f)));
        }

		ApplyRootMotionToVelocity(timeTick);

		if (MovementMode != StartingMovementMode || CustomMovementMode != StartingCustomMovementMode)
		{
			// Root motion could have taken us out of our current mode
			// No movement has taken place this movement tick so we pass on full time/past iteration count
			StartNewPhysics(remainingTime+timeTick, Iterations-1);
			return;
		}

		// Compute move parameters
		const FVector MoveVelocity = Velocity;
		const FVector Delta = timeTick * MoveVelocity;
		const bool bZeroDelta = Delta.IsNearlyZero();
		FStepDownResult StepDownResult;

		if ( bZeroDelta )
		{
			remainingTime = 0.f;
		}
		else
		{
			// try to move forward
			MoveAlongFloor(MoveVelocity, timeTick, &StepDownResult);

			if (IsSwimming()) //just entered water
			{
				StartSwimming(OldLocation, OldVelocity, timeTick, remainingTime, Iterations);
				return;
			}
			else if (MovementMode != StartingMovementMode || CustomMovementMode != StartingCustomMovementMode)
			{
				// pawn ended up in a different mode, probably due to the step-up-and-over flow
				// let's refund the estimated unused time (if any) and keep moving in the new mode
				const float DesiredDist = Delta.Size();
				if (DesiredDist > UE_KINDA_SMALL_NUMBER)
				{
					const float ActualDist = ProjectToGravityFloor(UpdatedComponent->GetComponentLocation() - OldLocation).Size();
					remainingTime += timeTick * (1.f - FMath::Min(1.f,ActualDist/DesiredDist));
				}
				StartNewPhysics(remainingTime,Iterations);
				return;
			}
		}

		// Update floor.
		// StepUp might have already done it for us.
		if (StepDownResult.bComputedFloor)
		{
			CurrentFloor = StepDownResult.FloorResult;
		}
		else
		{
			FindFloor(UpdatedComponent->GetComponentLocation(), CurrentFloor, bZeroDelta, NULL);
		}

		// check for ledges here
		const bool bCheckLedges = !CanWalkOffLedges();
		if ( bCheckLedges && !CurrentFloor.IsWalkableFloor() )
		{
			// calculate possible alternate movement
			const FVector NewDelta = bTriedLedgeMove ? FVector::ZeroVector : GetLedgeMove(OldLocation, Delta, OldFloor);
			if ( !NewDelta.IsZero() )
			{
				// first revert this move
				RevertMove(OldLocation, OldBase, PreviousBaseLocation, OldFloor, false);

				// avoid repeated ledge moves if the first one fails
				bTriedLedgeMove = true;

				// Try new movement direction
				Velocity = NewDelta/timeTick;
				remainingTime += timeTick;
				Iterations--;
				continue;
			}
			else
			{
				// see if it is OK to jump
				// @todo collision : only thing that can be problem is that oldbase has world collision on
				bool bMustJump = bZeroDelta || (OldBase == NULL || (!OldBase->IsQueryCollisionEnabled() && MovementBaseUtility::IsDynamicBase(OldBase)));
				if ( (bMustJump || !bCheckedFall) && CheckFall(OldFloor, CurrentFloor.HitResult, Delta, OldLocation, remainingTime, timeTick, Iterations, bMustJump) )
				{
					return;
				}
				bCheckedFall = true;

				// revert this move
				RevertMove(OldLocation, OldBase, PreviousBaseLocation, OldFloor, true);
				remainingTime = 0.f;
				break;
			}
		}
		else
		{
			// Validate the floor check
			if (CurrentFloor.IsWalkableFloor())
			{
				if (ShouldCatchAir(OldFloor, CurrentFloor))
				{
					HandleWalkingOffLedge(OldFloor.HitResult.ImpactNormal, OldFloor.HitResult.Normal, OldLocation, timeTick);
					if (IsSlidingOnGround())
					{
						// If still walking, then fall. If not, assume the user set a different mode they want to keep.
						StartFalling(Iterations, remainingTime, timeTick, Delta, OldLocation);
					}
					return;
				}

				AdjustFloorHeight();
				SetBase(CurrentFloor.HitResult.Component.Get(), CurrentFloor.HitResult.BoneName);
			}
			else if (CurrentFloor.HitResult.bStartPenetrating && remainingTime <= 0.f)
			{
				// The floor check failed because it started in penetration
				// We do not want to try to move downward because the downward sweep failed, rather we'd like to try to pop out of the floor.
				FHitResult Hit(CurrentFloor.HitResult);
				Hit.TraceEnd = Hit.TraceStart + MAX_FLOOR_DIST * -GetGravityDirection();
				const FVector RequestedAdjustment = GetPenetrationAdjustment(Hit);
				ResolvePenetration(RequestedAdjustment, Hit, UpdatedComponent->GetComponentQuat());
				bForceNextFloorCheck = true;
			}

			// check if just entered water
			if ( IsSwimming() )
			{
				StartSwimming(OldLocation, Velocity, timeTick, remainingTime, Iterations);
				return;
			}

			// See if we need to start falling.
			if (!CurrentFloor.IsWalkableFloor() && !CurrentFloor.HitResult.bStartPenetrating)
			{
				const bool bMustJump = bJustTeleported || bZeroDelta || (OldBase == NULL || (!OldBase->IsQueryCollisionEnabled() && MovementBaseUtility::IsDynamicBase(OldBase)));
				if ((bMustJump || !bCheckedFall) && CheckFall(OldFloor, CurrentFloor.HitResult, Delta, OldLocation, remainingTime, timeTick, Iterations, bMustJump) )
				{
					return;
				}
				bCheckedFall = true;
			}
		}

		// Allow overlap events and such to change physics state and velocity
		if (IsSlidingOnGround())
		{
			// Make velocity reflect actual move
			if(bMovementInProgress && !bJustTeleported && !HasAnimRootMotion() && !CurrentRootMotion.HasOverrideVelocity() && timeTick >= MIN_TICK_TIME && OldFloor.IsWalkableFloor())
			{
				// TODO-RootMotionSource: Allow this to happen during partial override Velocity, but only set allowed axes?
				const auto DistanceTraveled = (UpdatedComponent->GetComponentLocation() - OldLocation) / timeTick;

				Velocity = DistanceTraveled;
				// Velocity = Velocity.GetSafeNormal() * DistanceTraveled.Size();
			}

			if (Velocity.Size() < MinimumSpeed)
			{
				EndSlide();
			}
			else
			{
				MaintainHorizontalGroundVelocity();
			}
			
		}

		// If we didn't move at all this iteration then abort (since future iterations will also be stuck).
		if (UpdatedComponent->GetComponentLocation() == OldLocation)
		{
			remainingTime = 0.f;
			break;
		}
	}

	if (IsSlidingOnGround())
	{
		MaintainHorizontalGroundVelocity();
	}
}
