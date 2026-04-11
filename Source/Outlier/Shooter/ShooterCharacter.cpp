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
#include "Weapon/RangedWeaponBase.h"
#include "Interface/InteractableInterface.h"
#include "Net/UnrealNetwork.h"
#include "Outlier.h"

AShooterCharacter::AShooterCharacter() : AFirstPersonCharacter()
{
	GetCharacterMovement()->MaxWalkSpeed = WalkSpeed;
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
	EnhancedInputComponent->BindAction(InputConfig->SwitchWeapon1Action, ETriggerEvent::Started,  this, &AShooterCharacter::TrySwitchWeapon1);
	EnhancedInputComponent->BindAction(InputConfig->SwitchWeapon2Action, ETriggerEvent::Started,  this, &AShooterCharacter::TrySwitchWeapon2);
	EnhancedInputComponent->BindAction(InputConfig->SwitchWeapon3Action, ETriggerEvent::Started,  this, &AShooterCharacter::TrySwitchWeapon3);

	// Sprint
	EnhancedInputComponent->BindAction(InputConfig->SprintAction,		ETriggerEvent::Started,   this, &AShooterCharacter::TryStartSprint);
	EnhancedInputComponent->BindAction(InputConfig->SprintAction,		ETriggerEvent::Completed, this, &AShooterCharacter::TryStopSprint);

	// Crouch
	EnhancedInputComponent->BindAction(InputConfig->CrouchAction,		ETriggerEvent::Started,   this, &AShooterCharacter::TryStartCrouchOrSlide);
	EnhancedInputComponent->BindAction(InputConfig->CrouchAction,		ETriggerEvent::Completed, this, &AShooterCharacter::TryStopCrouch);

	// Lean
	EnhancedInputComponent->BindAction(InputConfig->LeanAction,			ETriggerEvent::Triggered, this, &AShooterCharacter::TryLean);

	// Interaction
	EnhancedInputComponent->BindAction(InputConfig->InteractionAction,  ETriggerEvent::Started,   this, &AShooterCharacter::TryInteract);

	// Suit Menu Hold
	EnhancedInputComponent->BindAction(InputConfig->SuitMenuHoldAction, ETriggerEvent::Started,   this, &AShooterCharacter::TryOpenSuitMenu);
	EnhancedInputComponent->BindAction(InputConfig->SuitMenuHoldAction, ETriggerEvent::Completed, this, &AShooterCharacter::TryCloseSuitMenu);

	// Suit Navigate
	EnhancedInputComponent->BindAction(InputConfig->SuitNavigateAction, ETriggerEvent::Triggered, this, &AShooterCharacter::UpdateSuitSelection);

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
	ServerReload();
	
}

void AShooterCharacter::ServerReload_Implementation()
{
	if (bIsDead)
	{
		return;
	}

	ARangedWeaponBase* RangedWeapon = Cast<ARangedWeaponBase>(CurrentWeapon);

	if (!RangedWeapon)
	{
		return;
	}

	RangedWeapon->Reload();
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

	if (!OwnedWeapons.IsValidIndex(SlotIndex))
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

	ARangedWeaponBase* RangedWeapon = Cast<ARangedWeaponBase>(CurrentWeapon);

	if (!RangedWeapon)
	{
		return;
	}

	bIsAiming = true;

	if (RangedWeapon)
	{
		RangedWeapon->SetAiming(true);
	}
}

void AShooterCharacter::TryStopAim()
{
	bIsAiming = false;

	ARangedWeaponBase* RangedWeapon = Cast<ARangedWeaponBase>(CurrentWeapon);

	if (RangedWeapon)
	{
		RangedWeapon->SetAiming(false);
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

	if (!bIsSliding)
	{
		GetCharacterMovement()->MaxWalkSpeed = WalkSpeed;
	}
}

void AShooterCharacter::TryStartCrouchOrSlide()
{
	if (bIsDead)
	{
		return;
	}

	if (bIsSprinting && GetVelocity().SizeSquared() > 0.0f)
	{
		TrySlide();
		return;
	}

	Crouch();
}

void AShooterCharacter::TryStopCrouch()
{
	if (bIsSliding)
	{
		return;
	}

	UnCrouch();
}

void AShooterCharacter::TryInteract()
{
	if (bIsDead)
	{
		return;
	}

	FVector CameraLocation;
	FRotator CameraRotation;

	GetController()->GetPlayerViewPoint(CameraLocation, CameraRotation);

	FVector Start = CameraLocation;
	FVector End = Start + (CameraRotation.Vector() * InteractRange); // 사거리

	FHitResult Hit;
	FCollisionQueryParams Params;
	Params.AddIgnoredActor(this);

	const bool bHit = GetWorld()->LineTraceSingleByChannel(
		Hit,
		Start,
		End,
		ECC_Visibility,
		Params
	);

	if (!bHit)
	{
		return;
	}

	ServerInteract(Hit.GetActor());
}


void AShooterCharacter::ServerInteract_Implementation(AActor* TargetActor)
{
	if(!TargetActor || bIsDead)
	{
		UE_LOG(LogTemp, Warning, TEXT("TargetActor is null or Dead"));
		return;
	}

	FVector CameraLocation;
	FRotator CameraRotation;
	GetController()->GetPlayerViewPoint(CameraLocation, CameraRotation);

	const FVector Start = CameraLocation;
	const FVector End = Start + (CameraRotation.Vector() * InteractRange);

	FHitResult Hit;
	FCollisionQueryParams Params;
	Params.AddIgnoredActor(this);

	const bool bHit = GetWorld()->LineTraceSingleByChannel(
		Hit,
		Start,
		End,
		ECC_Visibility,
		Params
	);

	if (!bHit || Hit.GetActor() != TargetActor)
	{
		UE_LOG(LogTemp, Warning, TEXT("Server interact validation failed"));
		return;
	}

	if (IInteractableInterface* Interactable = Cast<IInteractableInterface>(TargetActor))
	{
		Interactable->Interact(this);
	}
}

void AShooterCharacter::TryOpenSuitMenu()
{
	if (bIsDead)
	{
		return;
	}

	bIsSuitMenuOpen = true;

	// 라디얼 UI 표시
	// 마우스 커서 표시
	// 필요하면 이동 입력 제한
}

void AShooterCharacter::TryCloseSuitMenu()
{
	if (!bIsSuitMenuOpen)
	{
		return;
	}

	bIsSuitMenuOpen = false;

	// 라디얼 UI 숨김
	// 마우스 커서 숨김
}

void AShooterCharacter::UpdateSuitSelection(const FInputActionValue& Value)
{
	if (!bIsSuitMenuOpen)
	{
		return;
	}

	const FVector2D Input = Value.Get<FVector2D>();

	const float Angle = FMath::Atan2(Input.Y, Input.X);
	//SelectedSuitIndex = ConvertAngleToSuitIndex(Angle); Suit쪽 로직?
}

void AShooterCharacter::TryUseSuit()
{
	if (bIsDead)
	{
		return;
	}

	if (SelectedSuitIndex == INDEX_NONE)
	{
		return;
	}

	// 현재 선택된 슈트 능력 사용
}

void AShooterCharacter::TrySlide()
{
	if (bIsDead || !bIsSprinting || bIsSliding)
	{
		return;
	}

	bIsSliding = true;

	// 예: 슬라이드 시작 처리
	// 캡슐 높이 조정
	// 슬라이드 타이머 시작
	// 속도 보정
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

void AShooterCharacter::OnRep_CurHP()
{
	// TODO: UI 갱신
}

void AShooterCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AShooterCharacter, CurHP);
}

void AShooterCharacter::ApplyDamageInternal(float DamageAmount)
{
	if (bIsDead || DamageAmount <= 0.0f)
	{
		return;
	}

	CurHP = FMath::Clamp(CurHP - DamageAmount, 0.0f, MaxHP);

	if (CurHP <= 0.0f)
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

void AShooterCharacter::TryStartAttack()
{
	ServerStartAttack();
}

void AShooterCharacter::ServerStartAttack_Implementation()
{
	if (bIsDead || !CurrentWeapon)
	{
		return;
	}

	CurrentWeapon->StartAttack();
}

void AShooterCharacter::TryStopAttack()
{
	ServerStopAttack();
}


void AShooterCharacter::ServerStopAttack_Implementation()
{
	if (!CurrentWeapon)
	{
		return;
	}

	CurrentWeapon->StopAttack();
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
