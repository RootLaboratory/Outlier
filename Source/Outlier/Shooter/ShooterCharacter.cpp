// Copyright Epic Games, Inc. All Rights Reserved.

#include "ShooterCharacter.h"
#include "Animation/AnimInstance.h"
#include "Engine/LocalPlayer.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Controller.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "ShooterPlayerController.h"
#include "LocalPlayerUISubSystem.h"
#include "InputActionValue.h"
#include "ShooterInputConfig.h"
#include "ShooterHealthComponent.h"
#include "ShooterInventoryComponent.h"
#include "ShooterCombatComponent.h"
#include "ShooterMovementComponent.h"
#include "Weapon/WeaponBase.h"
#include "Weapon/RangedWeaponBase.h"
#include "Interface/InteractableInterface.h"
#include "Net/UnrealNetwork.h"
#include "OutlierNetUtils.h"
#include "Outlier.h"

FName AShooterCharacter::GetFirstPersonWeaponSocketByType(EWeaponType WeaponType) const
{
	return InventoryComponent ? InventoryComponent->GetFirstPersonWeaponSocketByType(WeaponType) : NAME_None;
}

FName AShooterCharacter::GetThirdPersonWeaponSocketByType(EWeaponType WeaponType) const
{
	return InventoryComponent ? InventoryComponent->GetThirdPersonWeaponSocketByType(WeaponType) : NAME_None;
}

AShooterCharacter::AShooterCharacter() : AFirstPersonCharacter()
{
	PrimaryActorTick.bCanEverTick = true;

	GetCharacterMovement()->MaxWalkSpeed = WalkSpeed;

	CaptureComponent = CreateDefaultSubobject< USceneCaptureComponent2D>(TEXT("PartnerCameraCapture"));
	CaptureComponent->SetupAttachment(RootComponent);
	HealthComponent = CreateDefaultSubobject<UShooterHealthComponent>(TEXT("HealthComponent"));
	InventoryComponent = CreateDefaultSubobject<UShooterInventoryComponent>(TEXT("InventoryComponent"));
	CombatComponent = CreateDefaultSubobject<UShooterCombatComponent>(TEXT("CombatComponent"));
	MovementComponent = CreateDefaultSubobject<UShooterMovementComponent>(TEXT("MovementComponent"));
}

void AShooterCharacter::BeginPlay()
{
	Super::BeginPlay();

	if (USceneComponent* CameraRoot = GetFirstPersonCameraRoot())
	{
		BaseFirstPersonCameraRootRotation = CameraRoot->GetRelativeRotation();
	}

	if (USceneComponent* ViewModelRoot = GetFirstPersonViewModelRoot())
	{
		BaseFirstPersonViewModelRootLocation = ViewModelRoot->GetRelativeLocation();
		BaseFirstPersonViewModelRootRotation = ViewModelRoot->GetRelativeRotation();
	}

	if (FirstPersonMesh)
	{
		BaseFirstPersonMeshLocation = FirstPersonMesh->GetRelativeLocation();
		BaseFirstPersonMeshRotation = FirstPersonMesh->GetRelativeRotation();
	}

	if (USceneComponent* ViewModelRoot = GetFirstPersonViewModelRoot())
	{
		if (FirstPersonMesh && !BaseFirstPersonMeshLocation.IsNearlyZero())
		{
			// Treat the view-model root as the single framing anchor and keep the mesh at its local origin.
			BaseFirstPersonViewModelRootLocation += BaseFirstPersonMeshLocation;
			ViewModelRoot->SetRelativeLocation(BaseFirstPersonViewModelRootLocation);
			BaseFirstPersonMeshLocation = FVector::ZeroVector;
			FirstPersonMesh->SetRelativeLocation(BaseFirstPersonMeshLocation);
		}
	}

	if (HasAuthority())
	{
		CurHP = FMath::Clamp(CurHP, 0.0f, MaxHP);
	}

	RefreshWeaponMode();
	RefreshMovementState();
	RefreshCombatState();
	UpdateLocalHealthUI();
	UpdateFirstPersonPresentation(0.0f);
}

void AShooterCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	UpdateFirstPersonPresentation(DeltaSeconds);
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
	EnhancedInputComponent->BindAction(InputConfig->JumpAction,         ETriggerEvent::Started,   this, &AShooterCharacter::DoJumpStart);
	EnhancedInputComponent->BindAction(InputConfig->JumpAction,			ETriggerEvent::Completed, this, &AShooterCharacter::DoJumpEnd);

	// Switch Weapon
	EnhancedInputComponent->BindAction(InputConfig->SwitchWeapon1Action, ETriggerEvent::Started,  this, &AShooterCharacter::TrySwitchWeapon1);
	EnhancedInputComponent->BindAction(InputConfig->SwitchWeapon2Action, ETriggerEvent::Started,  this, &AShooterCharacter::TrySwitchWeapon2);
	EnhancedInputComponent->BindAction(InputConfig->SwitchWeapon3Action, ETriggerEvent::Started,  this, &AShooterCharacter::TrySwitchWeapon3);

	// Sprint
	EnhancedInputComponent->BindAction(InputConfig->SprintAction,		ETriggerEvent::Started,   this, &AShooterCharacter::HandleSprintPressed);
	EnhancedInputComponent->BindAction(InputConfig->SprintAction,		ETriggerEvent::Completed, this, &AShooterCharacter::HandleSprintReleased);

	// Crouch
	EnhancedInputComponent->BindAction(InputConfig->CrouchAction,		ETriggerEvent::Started,   this, &AShooterCharacter::HandleCrouchToggled);

	// Lean
	EnhancedInputComponent->BindAction(InputConfig->LeanAction,			ETriggerEvent::Triggered, this, &AShooterCharacter::TryLean);
	EnhancedInputComponent->BindAction(InputConfig->LeanAction,			ETriggerEvent::Completed, this, &AShooterCharacter::TryLean);

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
	EnhancedInputComponent->BindAction(InputConfig->AimAction,			ETriggerEvent::Started,   this, &AShooterCharacter::HandleAimPressed);
	EnhancedInputComponent->BindAction(InputConfig->AimAction,			ETriggerEvent::Completed, this, &AShooterCharacter::HandleAimReleased);
}

void AShooterCharacter::OnStartCrouch(float HalfHeightAdjust, float ScaledHalfHeightAdjust)
{
	Super::OnStartCrouch(HalfHeightAdjust, ScaledHalfHeightAdjust);

	UpdateFirstPersonPresentation(0.0f);
	RefreshMovementState();
}

void AShooterCharacter::OnEndCrouch(float HalfHeightAdjust, float ScaledHalfHeightAdjust)
{
	Super::OnEndCrouch(HalfHeightAdjust, ScaledHalfHeightAdjust);

	UpdateFirstPersonPresentation(0.0f);
	RefreshMovementState();
}

void AShooterCharacter::Landed(const FHitResult& Hit)
{
	Super::Landed(Hit);

	if (IsSliding())
	{
		StopSlide(ESlideEndReason::JumpCancel);
		return;
	}

	RefreshMovementState();
}

void AShooterCharacter::OnMovementModeChanged(EMovementMode  PrevMovementMode, uint8 PreviousCustomMode)
{
	Super::OnMovementModeChanged(PrevMovementMode, PreviousCustomMode);

	if (IsSliding() && GetCharacterMovement()->IsFalling())
	{
		StopSlide(ESlideEndReason::FallCancel);
		return;
	}

	RefreshMovementState();
}

void AShooterCharacter::TryReload()
{
	if (CombatComponent)
	{
		CombatComponent->TryReload();
	}
}

void AShooterCharacter::ServerReload_Implementation()
{
	if (CombatComponent)
	{
		CombatComponent->TryReload();
	}
}

void AShooterCharacter::TrySwitchWeapon1()
{
	if (InventoryComponent)
	{
		InventoryComponent->TrySwitchWeapon1();
	}
}

void AShooterCharacter::TrySwitchWeapon2()
{
	if (InventoryComponent)
	{
		InventoryComponent->TrySwitchWeapon2();
	}
}

void AShooterCharacter::TrySwitchWeapon3()
{
	if (InventoryComponent)
	{
		InventoryComponent->TrySwitchWeapon3();
	}
}

void AShooterCharacter::SelectWeaponByIndex(int32 SlotIndex)
{
	if (InventoryComponent)
	{
		InventoryComponent->SelectWeaponByIndex(SlotIndex);
	}
}

void AShooterCharacter::HandleAimPressed()
{
	if (CombatComponent)
	{
		CombatComponent->HandleAimPressed();
	}
}

void AShooterCharacter::HandleAimReleased()
{
	if (CombatComponent)
	{
		CombatComponent->HandleAimReleased();
	}
}

void AShooterCharacter::HandleSprintPressed()
{
	if (MovementComponent)
	{
		MovementComponent->HandleSprintPressed();
	}
}

void AShooterCharacter::HandleSprintReleased()
{
	if (MovementComponent)
	{
		MovementComponent->HandleSprintReleased();
	}
}

void AShooterCharacter::HandleCrouchToggled()
{
	if (MovementComponent)
	{
		MovementComponent->HandleCrouchToggled();
	}
}

void AShooterCharacter::TryInteract()
{
	if (bIsDead)
	{
		UE_LOG(LogTemp, Warning, TEXT("%s %s TryInteract blocked: dead"), OutlierNet::GetNetPrefix(this), *GetName());
		return;
	}

	if (!GetController())
	{
		UE_LOG(LogTemp, Warning, TEXT("%s %s TryInteract blocked: controller is null"), OutlierNet::GetNetPrefix(this), *GetName());
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
	Params.AddIgnoredActor(CurrentWeapon);

	const bool bHit = GetWorld()->LineTraceSingleByChannel(
		Hit,
		Start,
		End,
		ECC_Visibility,
		Params
	);
	
	if (!bHit)
	{
		UE_LOG(LogTemp, Log, TEXT("%s %s TryInteract miss Start=%s End=%s"), OutlierNet::GetNetPrefix(this), *GetName(), *Start.ToString(), *End.ToString());
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("%s %s TryInteract hit Target=%s"), OutlierNet::GetNetPrefix(this), *GetName(), *GetNameSafe(Hit.GetActor()));
	ServerInteract(Hit.GetActor());
}


void AShooterCharacter::ServerInteract_Implementation(AActor* TargetActor)
{
	if(!TargetActor || bIsDead)
	{
		UE_LOG(LogTemp, Warning, TEXT("[Server] %s ServerInteract blocked Target=%s Dead=%d"), *GetName(), *GetNameSafe(TargetActor), bIsDead ? 1 : 0);
		return;
	}

	if (!GetController())
	{
		UE_LOG(LogTemp, Warning, TEXT("[Server] %s ServerInteract blocked: controller is null"), *GetName());
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
	Params.AddIgnoredActor(CurrentWeapon);

	const bool bHit = GetWorld()->LineTraceSingleByChannel(
		Hit,
		Start,
		End,
		ECC_Visibility,
		Params
	);

	DrawDebugLine(
		GetWorld(),
		Start,
		End,
		FColor::Red,
		false,   // PersistentLines
		3.0f,    // LifeTime
		0,       // DepthPriority
		1.0f     // Thickness
	);

	if (!bHit || Hit.GetActor() != TargetActor)
	{
		UE_LOG(LogTemp, Warning, TEXT("[Server] %s ServerInteract validation failed Requested=%s Hit=%s"), *GetName(), *GetNameSafe(TargetActor), *GetNameSafe(Hit.GetActor()));
		return;
	}

	if (IInteractableInterface* Interactable = Cast<IInteractableInterface>(TargetActor))
	{
		UE_LOG(LogTemp, Log, TEXT("[Server] %s ServerInteract success Target=%s"), *GetName(), *GetNameSafe(TargetActor));
		Interactable->Interact(this);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[Server] %s ServerInteract failed: target not interactable Target=%s"), *GetName(), *GetNameSafe(TargetActor));
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

	if (SelectedSuitSlot == INDEX_NONE)
	{
		return;
	}

	// 현재 선택된 슈트 능력 사용
}

void AShooterCharacter::TrySlide()
{
	if (MovementComponent)
	{
		MovementComponent->TrySlide();
	}
}

void AShooterCharacter::TryLean(const FInputActionValue& Value)
{
	if (bIsDead)
	{
		return;
	}

	const float LeanAlpha = Value.Get<float>();
	TargetLeanAlpha = FMath::Abs(LeanAlpha) > KINDA_SMALL_NUMBER ? LeanAlpha : 0.0f;
	StartLeanUpdate();
}

void AShooterCharacter::OnRep_CurHP()
{
	UE_LOG(LogTemp, Log, TEXT("%s %s OnRep_CurHP CurHP=%.1f / %.1f"), OutlierNet::GetNetPrefix(this), *GetName(), CurHP, MaxHP);
	UpdateLocalHealthUI();
}

void AShooterCharacter::OnRep_MovementState()
{
	OnMovementStateChanged.Broadcast(MovementState);
}

void AShooterCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AShooterCharacter, CurHP);
	DOREPLIFETIME(AShooterCharacter, bIsDead);
	DOREPLIFETIME(AShooterCharacter, MovementState);
	DOREPLIFETIME(AShooterCharacter, WeaponMode);
	DOREPLIFETIME(AShooterCharacter, CombatState);
}

void AShooterCharacter::EquipWeapon(AWeaponBase* Weapon)
{
	if (InventoryComponent)
	{
		InventoryComponent->HandleEquipWeapon(Weapon);
	}
}

float AShooterCharacter::GetAimYawForAnimation() const
{
	return 0.0f;
}

float AShooterCharacter::GetAimPitchForAnimation() const
{
	return FRotator::NormalizeAxis(GetBaseAimRotation().Pitch);
}

bool AShooterCharacter::CanEnterCombatState(EWeaponMode InWeaponMode, ECombatState NextState) const
{
	return CombatComponent ? CombatComponent->CanEnterCombatState(InWeaponMode, NextState) : false;
}

bool AShooterCharacter::CanAimInCurrentState() const
{
	return CombatComponent ? CombatComponent->CanAimInCurrentState() : false;
}

bool AShooterCharacter::CanReloadInCurrentState() const
{
	return CombatComponent ? CombatComponent->CanReloadInCurrentState() : false;
}

bool AShooterCharacter::CanFireInCurrentState() const
{
	return CombatComponent ? CombatComponent->CanFireInCurrentState() : false;
}

bool AShooterCharacter::WantsToAim() const
{
	return CombatComponent ? CombatComponent->WantsToAim() : false;
}

bool AShooterCharacter::IsAiming() const
{
	return CombatComponent ? CombatComponent->IsAiming() : false;
}

bool AShooterCharacter::IsSliding() const
{
	return MovementComponent ? MovementComponent->IsSliding() : false;
}

bool AShooterCharacter::IsSprinting() const
{
	return MovementComponent ? MovementComponent->IsSprinting() : false;
}

bool AShooterCharacter::IsSlidingCanceled() const
{
	return MovementComponent ? MovementComponent->IsSlidingCanceled() : false;
}

bool AShooterCharacter::IsReloading() const
{
	return CombatComponent ? CombatComponent->IsReloading() : false;
}

void AShooterCharacter::RefreshWeaponMode()
{
	if (CombatComponent)
	{
		CombatComponent->RefreshWeaponMode();
	}
}

void AShooterCharacter::RefreshCombatState()
{
	if (CombatComponent)
	{
		CombatComponent->RefreshCombatState();
	}
}

void AShooterCharacter::ResolveStateConflicts()
{
	if (CombatComponent)
	{
		CombatComponent->ResolveStateConflicts();
	}
}

void AShooterCharacter::StopSprintInternal()
{
	if (MovementComponent)
	{
		MovementComponent->StopSprintInternal();
	}
}

void AShooterCharacter::StopAimInternal()
{
	if (CombatComponent)
	{
		CombatComponent->StopAimInternal();
	}
}

void AShooterCharacter::BeginReloadInternal()
{
	if (CombatComponent)
	{
		CombatComponent->BeginReloadInternal();
	}
}

void AShooterCharacter::CancelReloadInternal()
{
	if (CombatComponent)
	{
		CombatComponent->CancelReloadInternal();
	}
}

void AShooterCharacter::FinishReloadInternal()
{
	if (CombatComponent)
	{
		CombatComponent->FinishReloadInternal();
	}
}

void AShooterCharacter::HandleReloadCommitNotify()
{
	if (CombatComponent)
	{
		CombatComponent->HandleReloadCommitNotify();
	}
}

void AShooterCharacter::BeginSecondaryCooldownInternal(float CooldownDuration)
{
	if (CombatComponent)
	{
		CombatComponent->BeginSecondaryCooldownInternal(CooldownDuration);
	}
}

void AShooterCharacter::FinishSecondaryCooldownInternal()
{
	if (CombatComponent)
	{
		CombatComponent->FinishSecondaryCooldownInternal();
	}
}

void AShooterCharacter::ResetSecondaryCooldownInternal()
{
	if (CombatComponent)
	{
		CombatComponent->ResetSecondaryCooldown();
	}
}

void AShooterCharacter::ApplyDamageInternal(float DamageAmount)
{
	if (HealthComponent)
	{
		HealthComponent->ApplyDamage(DamageAmount);
	}
}

void AShooterCharacter::HandleWeaponAttackStoppedInternal()
{
	if (CombatComponent)
	{
		CombatComponent->HandleWeaponAttackStopped();
	}
}

void AShooterCharacter::HandleAutoReloadRequested()
{
	if (CombatComponent)
	{
		CombatComponent->HandleAutoReloadRequested();
	}
}

void AShooterCharacter::HandleFireShotAnimation()
{
	UAnimMontage* FirstPersonMontage = FirstPersonFireMontage;
	UAnimMontage* ThirdPersonMontage = ThirdPersonFireMontage;

	UE_LOG(
		LogTemp,
		Log,
		TEXT("%s %s HandleFireShotAnimation WeaponType=%d FP=%s TP=%s Local=%d Authority=%d"),
		OutlierNet::GetNetPrefix(this),
		*GetName(),
		static_cast<int32>(GetWeaponType()),
		*GetNameSafe(FirstPersonMontage),
		*GetNameSafe(ThirdPersonMontage),
		IsLocallyControlled() ? 1 : 0,
		HasAuthority() ? 1 : 0);

	MulticastPlayThirdPersonActionMontage(EShooterMontageAction::Fire, GetWeaponType());

	if (IsLocallyControlled())
	{
		PlayFirstPersonActionMontage(EShooterMontageAction::Fire, GetWeaponType());
		return;
	}

	ClientPlayFirstPersonActionMontage(EShooterMontageAction::Fire, GetWeaponType());
}

void AShooterCharacter::StartLeanUpdate()
{
	if (GetWorldTimerManager().IsTimerActive(LeanUpdateTimerHandle))
	{
		return;
	}

	GetWorldTimerManager().SetTimer(
		LeanUpdateTimerHandle,
		this,
		&AShooterCharacter::UpdateLeanStep,
		1.0f / 60.0f,
		true
	);
}

void AShooterCharacter::StopLeanUpdateIfSettled()
{
	if (FMath::IsNearlyEqual(CurrentLeanAlpha, TargetLeanAlpha, KINDA_SMALL_NUMBER))
	{
		CurrentLeanAlpha = TargetLeanAlpha;
		GetWorldTimerManager().ClearTimer(LeanUpdateTimerHandle);
	}
}

void AShooterCharacter::UpdateLeanStep()
{
	CurrentLeanAlpha = FMath::FInterpTo(CurrentLeanAlpha, TargetLeanAlpha, 1.0f / 60.0f, LeanInterpSpeed);

	if (FirstPersonMesh)
	{
		FirstPersonMesh->SetRelativeLocation(BaseFirstPersonMeshLocation);
		FirstPersonMesh->SetRelativeRotation(BaseFirstPersonMeshRotation);
	}

	UpdateFirstPersonPresentation(1.0f / 60.0f);

	StopLeanUpdateIfSettled();
}

void AShooterCharacter::Die()
{
	if (HealthComponent)
	{
		HealthComponent->Die();
	}
}

void AShooterCharacter::TryStartAttack()
{
	if (CombatComponent)
	{
		CombatComponent->TryStartAttack();
	}
}

void AShooterCharacter::ServerStartAttack_Implementation()
{
	if (CombatComponent)
	{
		CombatComponent->TryStartAttack();
	}
}

void AShooterCharacter::TryStopAttack()
{
	if (CombatComponent)
	{
		CombatComponent->TryStopAttack();
	}
}


void AShooterCharacter::ServerStopAttack_Implementation()
{
	if (CombatComponent)
	{
		CombatComponent->TryStopAttack();
	}
}

void AShooterCharacter::DoJumpStart()
{
	if (MovementComponent)
	{
		MovementComponent->DoJumpStart();
	}
}

void AShooterCharacter::DoJumpEnd()
{
	if (MovementComponent)
	{
		MovementComponent->DoJumpEnd();
	}
}

void AShooterCharacter::RefreshMovementState()
{
	if (MovementComponent)
	{
		MovementComponent->RefreshMovementState();
	}
}

void AShooterCharacter::SetMovementStateImmediate(EMovementState NewState)
{
	if (MovementComponent)
	{
		MovementComponent->SetMovementStateImmediate(NewState);
	}
}

bool AShooterCharacter::CanStartSlide() const
{
	return MovementComponent ? MovementComponent->CanStartSlide() : false;
}

void AShooterCharacter::StopSlide(ESlideEndReason EndReason)
{
	if (MovementComponent)
	{
		MovementComponent->StopSlide(EndReason);
	}
}

void AShooterCharacter::HandleSlideWallHit(const FHitResult& Hit)
{
	if (MovementComponent)
	{
		MovementComponent->HandleSlideWallHit(Hit);
	}
}

void AShooterCharacter::HandleDeath()
{
	ClearInputIntent();
	TargetLeanAlpha = 0.0f;
	CurrentLeanAlpha = 0.0f;

	if (IsSliding())
	{
		StopSlide(ESlideEndReason::ForcedCancel);
	}

	StopAimInternal();
	CancelReloadInternal();
	StopSprintInternal();
	TryStopAttack();

	StopJumping();
	GetWorldTimerManager().ClearTimer(LeanUpdateTimerHandle);

	if (USceneComponent* CameraRoot = GetFirstPersonCameraRoot())
	{
		CameraRoot->SetRelativeRotation(BaseFirstPersonCameraRootRotation);
	}

	if (FirstPersonMesh)
	{
		FirstPersonMesh->SetRelativeLocation(BaseFirstPersonMeshLocation);
		FirstPersonMesh->SetRelativeRotation(BaseFirstPersonMeshRotation);
	}

	if (USceneComponent* ViewModelRoot = GetFirstPersonViewModelRoot())
	{
		ViewModelRoot->SetRelativeLocation(BaseFirstPersonViewModelRootLocation);
		ViewModelRoot->SetRelativeRotation(BaseFirstPersonViewModelRootRotation);
	}

	GetCharacterMovement()->DisableMovement();

	DisableInput(Cast<APlayerController>(GetController()));

	RefreshCombatState();
	RefreshMovementState();
	OnCharacterDeath.Broadcast();
}

void AShooterCharacter::ClearInputIntent()
{
	if (CombatComponent)
	{
		CombatComponent->ClearInputIntent();
	}

	if (MovementComponent)
	{
		MovementComponent->ClearInputIntent();
	}
}

void AShooterCharacter::UpdateLocalHealthUI() const
{
	AShooterPlayerController* ShooterPlayerController = Cast<AShooterPlayerController>(GetController());
	if (!ShooterPlayerController)
	{
		return;
	}

	if (ULocalPlayer* LocalPlayer = ShooterPlayerController->GetLocalPlayer())
	{
		if (ULocalPlayerUISubSystem* UISubsystem = LocalPlayer->GetSubsystem<ULocalPlayerUISubSystem>())
		{
			UISubsystem->OnRep_HealthChanged(CurHP, MaxHP);
		}
	}
}

FName AShooterCharacter::ResolveMontageSectionNameForWeapon(EWeaponType WeaponType) const
{
	switch (WeaponType)
	{
	case EWeaponType::Rifle:
		return RifleMontageSectionName;
	case EWeaponType::Pistol:
		return PistolMontageSectionName;
	default:
		return DefaultMontageSectionName;
	}
}

void AShooterCharacter::PlayFirstPersonActionMontage(EShooterMontageAction Action, EWeaponType WeaponType)
{
	UAnimMontage* Montage = nullptr;
	bool bUseWeaponSection = true;
	switch (Action)
	{
	case EShooterMontageAction::Fire:
		Montage = FirstPersonFireMontage;
		break;
	case EShooterMontageAction::Reload:
		Montage = FirstPersonReloadMontage;
		break;
	case EShooterMontageAction::Slide:
		Montage = FirstPersonSlideMontage;
		bUseWeaponSection = false;
		break;
	case EShooterMontageAction::Equip:
		Montage = FirstPersonEquipMontage;
		break;
	default:
		break;
	}

	PlayFirstPersonMontageForWeapon(Montage, WeaponType, bUseWeaponSection);
}

void AShooterCharacter::PlayThirdPersonActionMontage(EShooterMontageAction Action, EWeaponType WeaponType)
{
	UAnimMontage* Montage = nullptr;
	bool bUseWeaponSection = true;
	switch (Action)
	{
	case EShooterMontageAction::Fire:
		Montage = ThirdPersonFireMontage;
		break;
	case EShooterMontageAction::Reload:
		Montage = ThirdPersonReloadMontage;
		break;
	case EShooterMontageAction::Slide:
		Montage = ThirdPersonSlideMontage;
		bUseWeaponSection = false;
		break;
	case EShooterMontageAction::Equip:
		Montage = ThirdPersonEquipMontage;
		break;
	default:
		break;
	}

	PlayThirdPersonMontageForWeapon(Montage, WeaponType, bUseWeaponSection);
}

void AShooterCharacter::PlayFirstPersonMontage(UAnimMontage* Montage)
{
	PlayFirstPersonMontageForWeapon(Montage, GetWeaponType());
}

void AShooterCharacter::PlayFirstPersonMontageForWeapon(UAnimMontage* Montage, EWeaponType WeaponType, bool bUseWeaponSection)
{
	if (!Montage || !IsLocallyControlled() || !FirstPersonMesh)
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("%s %s PlayFirstPersonMontageForWeapon skipped Montage=%s Local=%d FirstPersonMesh=%s WeaponType=%d"),
			OutlierNet::GetNetPrefix(this),
			*GetName(),
			*GetNameSafe(Montage),
			IsLocallyControlled() ? 1 : 0,
			*GetNameSafe(FirstPersonMesh),
			static_cast<int32>(WeaponType));
		return;
	}

	if (UAnimInstance* FirstPersonAnimInstance = FirstPersonMesh->GetAnimInstance())
	{
		const FName SectionName = bUseWeaponSection ? ResolveMontageSectionNameForWeapon(WeaponType) : NAME_None;
		UE_LOG(
			LogTemp,
			Log,
			TEXT("%s %s PlayFirstPersonMontageForWeapon Montage=%s Section=%s SectionValid=%d UseSection=%d AnimInstance=%s WeaponType=%d"),
			OutlierNet::GetNetPrefix(this),
			*GetName(),
			*GetNameSafe(Montage),
			*SectionName.ToString(),
			(SectionName != NAME_None && Montage->IsValidSectionName(SectionName)) ? 1 : 0,
			bUseWeaponSection ? 1 : 0,
			*GetNameSafe(FirstPersonAnimInstance),
			static_cast<int32>(WeaponType));

		FirstPersonAnimInstance->Montage_Play(Montage);
		if (SectionName != NAME_None && Montage->IsValidSectionName(SectionName))
		{
			FirstPersonAnimInstance->Montage_JumpToSection(SectionName, Montage);
		}
		else if (bUseWeaponSection)
		{
			UE_LOG(
				LogTemp,
				Warning,
				TEXT("%s %s PlayFirstPersonMontageForWeapon no valid section Montage=%s Section=%s"),
				OutlierNet::GetNetPrefix(this),
				*GetName(),
				*GetNameSafe(Montage),
				*SectionName.ToString());
		}
	}
	else
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("%s %s PlayFirstPersonMontageForWeapon missing AnimInstance Mesh=%s Montage=%s"),
			OutlierNet::GetNetPrefix(this),
			*GetName(),
			*GetNameSafe(FirstPersonMesh),
			*GetNameSafe(Montage));
	}
}

void AShooterCharacter::PlayThirdPersonMontage(UAnimMontage* Montage)
{
	PlayThirdPersonMontageForWeapon(Montage, GetWeaponType());
}

void AShooterCharacter::PlayThirdPersonMontageForWeapon(UAnimMontage* Montage, EWeaponType WeaponType, bool bUseWeaponSection)
{
	if (!Montage)
	{
		UE_LOG(LogTemp, Warning, TEXT("%s %s PlayThirdPersonMontageForWeapon skipped: montage is null"), OutlierNet::GetNetPrefix(this), *GetName());
		return;
	}

	if (USkeletalMeshComponent* ThirdPersonMesh = GetMesh())
	{
		if (UAnimInstance* ThirdPersonAnimInstance = ThirdPersonMesh->GetAnimInstance())
		{
			const FName SectionName = bUseWeaponSection ? ResolveMontageSectionNameForWeapon(WeaponType) : NAME_None;
			UE_LOG(
				LogTemp,
				Log,
				TEXT("%s %s PlayThirdPersonMontageForWeapon Montage=%s Section=%s SectionValid=%d UseSection=%d AnimInstance=%s WeaponType=%d"),
				OutlierNet::GetNetPrefix(this),
				*GetName(),
				*GetNameSafe(Montage),
				*SectionName.ToString(),
				(SectionName != NAME_None && Montage->IsValidSectionName(SectionName)) ? 1 : 0,
				bUseWeaponSection ? 1 : 0,
				*GetNameSafe(ThirdPersonAnimInstance),
				static_cast<int32>(WeaponType));

			ThirdPersonAnimInstance->Montage_Play(Montage);
			if (SectionName != NAME_None && Montage->IsValidSectionName(SectionName))
			{
				ThirdPersonAnimInstance->Montage_JumpToSection(SectionName, Montage);
			}
			else if (bUseWeaponSection)
			{
				UE_LOG(
					LogTemp,
					Warning,
					TEXT("%s %s PlayThirdPersonMontageForWeapon no valid section Montage=%s Section=%s"),
					OutlierNet::GetNetPrefix(this),
					*GetName(),
					*GetNameSafe(Montage),
					*SectionName.ToString());
			}
		}
		else
		{
			UE_LOG(
				LogTemp,
				Warning,
				TEXT("%s %s PlayThirdPersonMontageForWeapon missing AnimInstance Mesh=%s Montage=%s"),
				OutlierNet::GetNetPrefix(this),
				*GetName(),
				*GetNameSafe(ThirdPersonMesh),
				*GetNameSafe(Montage));
		}
	}
}

void AShooterCharacter::StopFirstPersonMontage(UAnimMontage* Montage)
{
	if (!Montage || !IsLocallyControlled() || !FirstPersonMesh)
	{
		return;
	}

	if (UAnimInstance* FirstPersonAnimInstance = FirstPersonMesh->GetAnimInstance())
	{
		FirstPersonAnimInstance->Montage_Stop(0.0f, Montage);
	}
}

void AShooterCharacter::StopThirdPersonMontage(UAnimMontage* Montage)
{
	if (!Montage)
	{
		return;
	}

	if (USkeletalMeshComponent* ThirdPersonMesh = GetMesh())
	{
		if (UAnimInstance* ThirdPersonAnimInstance = ThirdPersonMesh->GetAnimInstance())
		{
			ThirdPersonAnimInstance->Montage_Stop(0.0f, Montage);
		}
	}
}

void AShooterCharacter::StopSplitMontages(UAnimMontage* FirstPersonMontage, UAnimMontage* ThirdPersonMontage)
{
	if (IsLocallyControlled())
	{
		StopFirstPersonMontage(FirstPersonMontage);
		return;
	}

	StopThirdPersonMontage(ThirdPersonMontage);
}

void AShooterCharacter::PlayEquipMontages()
{
	const EWeaponType EquippedWeaponType = GetWeaponType();

	UE_LOG(
		LogTemp,
		Log,
		TEXT("%s %s PlayEquipMontages WeaponType=%d FP=%s TP=%s Local=%d Authority=%d"),
		OutlierNet::GetNetPrefix(this),
		*GetName(),
		static_cast<int32>(EquippedWeaponType),
		*GetNameSafe(FirstPersonEquipMontage),
		*GetNameSafe(ThirdPersonEquipMontage),
		IsLocallyControlled() ? 1 : 0,
		HasAuthority() ? 1 : 0);

	if (IsLocallyControlled())
	{
		PlayFirstPersonActionMontage(EShooterMontageAction::Equip, EquippedWeaponType);
	}

	if (HasAuthority())
	{
		MulticastPlayThirdPersonActionMontage(EShooterMontageAction::Equip, EquippedWeaponType);
	}
}

void AShooterCharacter::UpdateFirstPersonPresentation(float DeltaSeconds)
{
	if (!IsLocallyControlled())
	{
		return;
	}

	USceneComponent* ViewModelRoot = GetFirstPersonViewModelRoot();
	if (!ViewModelRoot)
	{
		return;
	}

	const float AimPitch = FRotator::NormalizeAxis(GetBaseAimRotation().Pitch);
	const float PitchCompensation = FMath::Clamp(-AimPitch * FirstPersonPitchFollowScale, -FirstPersonPitchFollowClamp, FirstPersonPitchFollowClamp);

	FVector WeaponBaseOffset = FirstPersonViewModelOffset;
	switch (GetWeaponType())
	{
	case EWeaponType::Rifle:
		WeaponBaseOffset = RifleFirstPersonViewModelOffset;
		break;
	case EWeaponType::Pistol:
		WeaponBaseOffset = PistolFirstPersonViewModelOffset;
		break;
	default:
		break;
	}

	FVector PitchLocationOffset = FVector::ZeroVector;
	if (FMath::Abs(AimPitch) > FirstPersonPitchLocationOffsetStart)
	{
		const float PitchLocationAlpha = FMath::GetMappedRangeValueClamped(
			FVector2D(FirstPersonPitchLocationOffsetStart, 90.0f),
			FVector2D(0.0f, 1.0f),
			FMath::Abs(AimPitch));
		PitchLocationOffset = AimPitch >= 0.0f
			? (FirstPersonPitchLocationOffsetAtMaxUp * PitchLocationAlpha)
			: (FirstPersonPitchLocationOffsetAtMaxDown * PitchLocationAlpha);
	}

	FVector TargetLocation = BaseFirstPersonViewModelRootLocation + WeaponBaseOffset + PitchLocationOffset;
	if (bIsCrouched)
	{
		TargetLocation += CrouchedFirstPersonViewModelOffset;
	}

	const FRotator TargetRotation = BaseFirstPersonViewModelRootRotation + FRotator(PitchCompensation, 0.0f, 0.0f);

	if (DeltaSeconds <= 0.0f)
	{
		ViewModelRoot->SetRelativeLocation(TargetLocation);
		ViewModelRoot->SetRelativeRotation(TargetRotation);
		return;
	}

	ViewModelRoot->SetRelativeLocation(FMath::VInterpTo(
		ViewModelRoot->GetRelativeLocation(),
		TargetLocation,
		DeltaSeconds,
		FirstPersonViewModelInterpSpeed));
	ViewModelRoot->SetRelativeRotation(FMath::RInterpTo(
		ViewModelRoot->GetRelativeRotation(),
		TargetRotation,
		DeltaSeconds,
		FirstPersonViewModelInterpSpeed));
}

void AShooterCharacter::ServerSelectWeaponByIndex_Implementation(int32 SlotIndex)
{
	if (InventoryComponent)
	{
		InventoryComponent->SelectWeaponByIndex(SlotIndex);
	}
}

void AShooterCharacter::ServerSetAimState_Implementation(bool bNewAiming)
{
	if (!CombatComponent)
	{
		return;
	}

	if (bNewAiming)
	{
		CombatComponent->HandleAimPressed();
	}
	else
	{
		CombatComponent->HandleAimReleased();
	}
}

void AShooterCharacter::ServerSetSprintState_Implementation(bool bNewSprinting)
{
	if (!MovementComponent)
	{
		return;
	}

	if (bNewSprinting)
	{
		MovementComponent->HandleSprintPressed();
	}
	else
	{
		MovementComponent->HandleSprintReleased();
	}
}

void AShooterCharacter::ServerRequestCrouchOrSlide_Implementation()
{
	if (MovementComponent)
	{
		MovementComponent->RequestCrouchOrSlide();
	}
}

void AShooterCharacter::ServerRequestUncrouch_Implementation()
{
	if (MovementComponent)
	{
		MovementComponent->RequestUncrouch();
	}
}

void AShooterCharacter::ServerJumpStart_Implementation()
{
	if (MovementComponent)
	{
		MovementComponent->DoJumpStart();
	}
}

void AShooterCharacter::ServerJumpEnd_Implementation()
{
	if (MovementComponent)
	{
		MovementComponent->DoJumpEnd();
	}
}

void AShooterCharacter::ClientPlayFirstPersonActionMontage_Implementation(EShooterMontageAction Action, EWeaponType WeaponType)
{
	PlayFirstPersonActionMontage(Action, WeaponType);
}

void AShooterCharacter::MulticastPlayThirdPersonActionMontage_Implementation(EShooterMontageAction Action, EWeaponType WeaponType)
{
	if (IsLocallyControlled())
	{
		return;
	}

	PlayThirdPersonActionMontage(Action, WeaponType);
}
