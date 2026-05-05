// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "ShooterAbility.generated.h"

UENUM(BlueprintType)
enum class EShooterAbility : uint8
{
	Teleport UMETA(DisplayName = "Teleport"),
	Shield   UMETA(DisplayName = "Shield"),
	Stealth  UMETA(DisplayName = "Stealth"),
	Stimpack UMETA(DisplayName = "Stimpack"),

	None     UMETA(DisplayName = "None")
};
