// Copyright Epic Games, Inc. All Rights Reserved.

#include "ShooterCharacter.h"
#include "Animation/AnimInstance.h"
#include "Engine/LocalPlayer.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Controller.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputActionValue.h"
#include "ShooterInputConfig.h"
#include "Weapon/WeaponBase.h"
#include "Outlier.h"

AShooterCharacter::AShooterCharacter() : AFirstPersonCharacter()
{
	
}

void AShooterCharacter::BeginPlay()
{
	Super::BeginPlay();

	CurHP = MaxHP;
}

void AShooterCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	// Set up action bindings
	UEnhancedInputComponent* EnhancedInputComponent = Cast<UEnhancedInputComponent>(PlayerInputComponent);
	if (!EnhancedInputComponent || !InputConfig) {
		UE_LOG(LogTemp, Warning, TEXT("EnhancedInputComponent or InputConfig is Null"));
		return;
	}

	// Jumping
	EnhancedInputComponent->BindAction(InputConfig->JumpAction,         ETriggerEvent::Started,   this, &ACharacter::Jump);
	EnhancedInputComponent->BindAction(InputConfig->JumpAction,			ETriggerEvent::Completed, this, &ACharacter::StopJumping);

	// Switch Weapon
	EnhancedInputComponent->BindAction(InputConfig->SwitchWeapon1Action, ETriggerEvent::Triggered, this, &AShooterCharacter::TrySwitchWeapon1);
	EnhancedInputComponent->BindAction(InputConfig->SwitchWeapon2Action, ETriggerEvent::Triggered, this, &AShooterCharacter::TrySwitchWeapon2);
	EnhancedInputComponent->BindAction(InputConfig->SwitchWeapon3Action, ETriggerEvent::Triggered, this, &AShooterCharacter::TrySwitchWeapon3);

	// Sprint
	EnhancedInputComponent->BindAction(InputConfig->SprintAction,		ETriggerEvent::Started,   this, &AShooterCharacter::TryStartSprint);
	EnhancedInputComponent->BindAction(InputConfig->SprintAction,		ETriggerEvent::Completed, this, &AShooterCharacter::TryStopSprint);

	// Crouch
	EnhancedInputComponent->BindAction(InputConfig->CrouchAction,		ETriggerEvent::Started,   this, &AShooterCharacter::TryStartCrouch);
	EnhancedInputComponent->BindAction(InputConfig->CrouchAction,		ETriggerEvent::Completed, this, &AShooterCharacter::TryStopCrouch);

	// Lean
	EnhancedInputComponent->BindAction(InputConfig->LeanAction,			ETriggerEvent::Triggered, this, &AShooterCharacter::TryLean);

	// Slide
	EnhancedInputComponent->BindAction(InputConfig->SlideAction,		ETriggerEvent::Started,   this, &AShooterCharacter::TrySlide);

	// Interaction
	EnhancedInputComponent->BindAction(InputConfig->InteractionAction,  ETriggerEvent::Started,   this, &AShooterCharacter::TryInteract);

	// Suit Select
	EnhancedInputComponent->BindAction(InputConfig->SuitSelectAction,	ETriggerEvent::Triggered, this, &AShooterCharacter::TrySelectSuit);

	// Suit Use
	EnhancedInputComponent->BindAction(InputConfig->SuitUseAction,		ETriggerEvent::Started,   this, &AShooterCharacter::TryUseSuit);

	// Reload
	EnhancedInputComponent->BindAction(InputConfig->ReloadAction,		ETriggerEvent::Started,   this, &AShooterCharacter::TryReload);

	// Aim
	EnhancedInputComponent->BindAction(InputConfig->AimAction,			ETriggerEvent::Started,   this, &AShooterCharacter::TryStartAim);
	EnhancedInputComponent->BindAction(InputConfig->AimAction,			ETriggerEvent::Completed, this, &AShooterCharacter::TryStopAim);
}

void AShooterCharacter::TryReload()
{
	if (bIsDead)
	{
		return;
	}

	if (!CurrentWeapon)
	{
		return;
	}

	CurrentWeapon->Reload();
}

void AShooterCharacter::TrySwitchWeapon1()
{
	SelectWeaponByIndex(0);
}

void AShooterCharacter::TrySwitchWeapon2()
{
	SelectWeaponByIndex(1);
}

void AShooterCharacter::TrySwitchWeapon3()
{
	SelectWeaponByIndex(2);
}

void AShooterCharacter::SelectWeaponByIndex(int32 SlotIndex)
{
	if (bIsDead)
	{
		return;
	}

	if (OwnedWeapons.IsValidIndex(SlotIndex))
	{
		return;
	}

	if (CurrentWeapon == OwnedWeapons[SlotIndex])
	{
		return;
	}

	CurrentWeapon = OwnedWeapons[SlotIndex];
}

void AShooterCharacter::TryStartAim()
{
	if (bIsDead)
	{
		return;
	}

	bIsAiming = true;

	if (CurrentWeapon)
	{
		CurrentWeapon->SetAiming(true);
	}
}

void AShooterCharacter::TryStopAim()
{
	bIsAiming = false;

	if (CurrentWeapon)
	{
		CurrentWeapon->SetAiming(false);
	}
}

void AShooterCharacter::TryStartSprint()
{
	if (bIsDead || GetCharacterMovement()->IsCrouching())
	{
		return;
	}

	bIsSprinting = true;

	GetCharacterMovement()->MaxWalkSpeed = SprintSpeed;
}

void AShooterCharacter::TryStopSprint()
{
	bIsSprinting = false;
	GetCharacterMovement()->MaxWalkSpeed = WalkSpeed;
}

void AShooterCharacter::TryStartCrouch()
{
	if (bIsDead)
	{
		return;
	}

	Crouch();
}

void AShooterCharacter::TryStopCrouch()
{
	UnCrouch();
}

void AShooterCharacter::TryInteract()
{
	if (bIsDead)
	{
		return;
	}

	// 라인트레이스로 상호작용 대상 검사
	// 맞은 액터가 인터랙션 인터페이스를 구현하면 호출
}

void AShooterCharacter::TryUseSuit()
{
	if (bIsDead)
	{
		return;
	}

	// 현재 선택된 슈트 능력 사용
}

void AShooterCharacter::TrySelectSuit(const FInputActionValue& Value)
{
	if (bIsDead)
	{
		return;
	}

	// 방향에 따른 Index 선택
	// Index로 어떤 능력이다로 해야된다? 배열?
	// 그냥 int32로? 고민중
}

void AShooterCharacter::TrySlide()
{
	if (bIsDead)
	{
		return;
	}

	if (!bIsSprinting)
	{
		return;
	}

	bIsSliding = true;
}

void AShooterCharacter::TryLean(const FInputActionValue& Value)
{
	if (bIsDead)
	{
		return;
	}

	const float LeanValue = Value.Get<float>();
	CurrentLeanValue = LeanValue;
}

void AShooterCharacter::ApplyDamageInternal(float DamageAmount)
{
	if (bIsDead || DamageAmount <= 0.0f)
	{
		return;
	}

	CurHP = FMath::Clamp(CurHP - DamageAmount, 0.0f, MaxHP);

	if (CurHP < 0.0f)
	{
		Die();
	}
}

void AShooterCharacter::Die()
{
	if (bIsDead)
	{
		return;
	}

	bIsDead = true;
	StopJumping();
	GetCharacterMovement()->DisableMovement();
}

void AShooterCharacter::DoJumpStart()
{
	// signal the character to jump
	Jump();
}

void AShooterCharacter::DoJumpEnd()
{
	// signal the character to stop jumping
	StopJumping();
}
