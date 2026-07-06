#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "HackType.generated.h"

UENUM(BlueprintType)
enum class EHackResult : uint8
{
	Success,
	Fail,
	Cancelled
};

USTRUCT(BlueprintType)
struct FHackQueryContext
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite)
	TObjectPtr<AActor> InstigatorActor = nullptr;

	UPROPERTY(BlueprintReadWrite)
	FVector ViewLocation = FVector::ZeroVector;

	UPROPERTY(BlueprintReadWrite)
	FVector ViewDirection = FVector::ForwardVector;

	UPROPERTY(BlueprintReadWrite)
	float MaxRange = 0.0f;

	UPROPERTY(BlueprintReadWrite)
	FGameplayTagContainer RequiredTags;

	UPROPERTY(BlueprintReadWrite)
	FGameplayTagContainer BlockedTags;
};

USTRUCT(BlueprintType)
struct FHackProcessContext
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite)
	TObjectPtr<AActor> InstigatorActor = nullptr;

	UPROPERTY(BlueprintReadWrite)
	TObjectPtr<AActor> TargetActor = nullptr;

	UPROPERTY(BlueprintReadWrite)
	FVector2D ScreenLocation = FVector2D::ZeroVector;
};

USTRUCT(BlueprintType)
struct FHackResultContext
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite)
	TObjectPtr<AActor> InstigatorActor = nullptr;

	UPROPERTY(BlueprintReadWrite)
	TObjectPtr<AActor> TargetActor = nullptr;

	UPROPERTY(BlueprintReadWrite)
	EHackResult Result = EHackResult::Fail;

	UPROPERTY(BlueprintReadWrite)
	FGameplayTagContainer ResultTags;
};
