// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "LunarCharacterMovementComponent.generated.h"

/**
 * 
 */
UCLASS()
class LUNARROGUE_API ULunarCharacterMovementComponent : public UCharacterMovementComponent
{
	GENERATED_BODY()
public:
	virtual void PhysCustom(float deltaTime, int32 Iterations);

	virtual void PhysSliding(float deltaTime, int32 Iterations);
	virtual void PhysAirSliding(float deltaTime, int32 Iterations);

	UFUNCTION(BlueprintCallable, Category="Character Movement: Lunar Slide")
	virtual bool IsSliding() const;
	UFUNCTION(BlueprintCallable, Category="Character Movement: Lunar Slide")
	virtual bool IsSlidingOnGround() const;
	UFUNCTION(BlueprintCallable, Category="Character Movement: Lunar Slide")
	virtual bool IsSlidingInAir() const;
	UFUNCTION(BlueprintCallable, Category="Character Movement: Lunar Slide")
	virtual void BeginSlide();
	UFUNCTION(BlueprintCallable, Category="Character Movement: Lunar Slide")
	virtual void EndSlide();

	// properties
	UPROPERTY(Category="Character Movement: Lunar Slide", EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0", UIMin="0"))
	float GroundFrictionFactor = 20;
	UPROPERTY(Category="Character Movement: Lunar Slide", EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0", UIMin="0"))
	float MinimumSpeed = 100;

	// engine overrides
	virtual void HandleWalkingOffLedge(const FVector& PreviousFloorImpactNormal, const FVector& PreviousFloorContactNormal, const FVector& PreviousLocation, float TimeDelta);
	virtual void ProcessLanded(const FHitResult& Hit, float remainingTime, int32 Iterations);
	virtual FVector ConstrainInputAcceleration(const FVector& InputAcceleration) const;
	virtual bool IsWalkable(const FHitResult& Hit) const;
	virtual float SlideAlongSurface(const FVector& Delta, float Time, const FVector& Normal, FHitResult& Hit, bool bHandleImpact) override;
};
