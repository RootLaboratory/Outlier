// Copyright Epic Games, Inc. All Rights Reserved.

#include "ShooterCharacter.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Engine/LocalPlayer.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Curves/CurveFloat.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Controller.h"
#include "GameFramework/PlayerController.h"
#include "TimerManager.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "ShooterPlayerController.h"
#include "PostProcess/MaterialPostProcessSubsystem.h"
#include "PostProcess/OutlierPostProcessVolume.h"
#include "LocalPlayerUISubSystem.h"
#include "InputActionValue.h"
#include "Drone/Partner/PartnerCharacter.h"
#include "TagDrivenUIGameplayTags.h"
#include "ShooterInputConfig.h"
#include "ShooterHealthComponent.h"
#include "ShooterInventoryComponent.h"
#include "ShooterCombatComponent.h"
#include "ShooterFirstPersonAnimInstance.h"
#include "ShooterMovementComponent.h"
#include "LocalPlayerPostProcessSubsystem.h"
#include "Weapon/WeaponBase.h"
#include "Weapon/RangedWeaponBase.h"
#include "GameplayTags/OutlierGameplayTags.h"
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

	ShadowMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("ShadowMesh"));
	ShadowMesh->SetupAttachment(GetCapsuleComponent());
	ShadowMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ShadowMesh->SetOnlyOwnerSee(false);
	ShadowMesh->SetOwnerNoSee(false);
	ShadowMesh->SetCastShadow(false);
	ShadowMesh->SetCastHiddenShadow(false);
	ShadowMesh->SetRenderInMainPass(true);
	ShadowMesh->SetRenderInDepthPass(false);

	
	HealthComponent = CreateDefaultSubobject<UShooterHealthComponent>(TEXT("HealthComponent"));
	InventoryComponent = CreateDefaultSubobject<UShooterInventoryComponent>(TEXT("InventoryComponent"));
	CombatComponent = CreateDefaultSubobject<UShooterCombatComponent>(TEXT("CombatComponent"));
	MovementComponent = CreateDefaultSubobject<UShooterMovementComponent>(TEXT("MovementComponent"));

	if (CaptureComponent)
	{
		if (USkeletalMeshComponent* ThirdPersonMesh = GetMesh())
		{
			CaptureComponent->HideComponent(ThirdPersonMesh);
		}
	}
}

void AShooterCharacter::BeginPlay()
{
	Super::BeginPlay();

	RefreshFirstPersonShadowPolicy();

	if (USceneComponent* CameraRoot = GetFirstPersonCameraRoot())
	{
		BaseFirstPersonCameraRootRotation = CameraRoot->GetRelativeRotation();
	}
	if (FirstPersonCamera)
	{
		BaseCameraFOV = FirstPersonCamera->FieldOfView;
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
			// 뷰모델 루트를 단일 프레이밍 앵커로 쓰고, 메시는 로컬 원점에 유지한다
			BaseFirstPersonViewModelRootLocation += BaseFirstPersonMeshLocation;
			ViewModelRoot->SetRelativeLocation(BaseFirstPersonViewModelRootLocation);
			BaseFirstPersonMeshLocation = FVector::ZeroVector;
			FirstPersonMesh->SetRelativeLocation(BaseFirstPersonMeshLocation);
		}
	}

	if (HasAuthority())
	{
		CurHP = FMath::Clamp(CurHP, 0.0f, MaxHP);
		CurShield = MaxShield;
	}
	
	RefreshWeaponMode();
	RefreshMovementState();
	RefreshCombatState();
}

void AShooterCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	UpdateSlideCameraEffect(DeltaSeconds);
	UpdateCameraFOV(DeltaSeconds);
	UpdateCameraRecoil(DeltaSeconds);
}

void AShooterCharacter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	SetStealthVisualEnabled(false);

	if (HasAuthority())
	{
		CleanupOwnedWeapons();
	}

	Super::EndPlay(EndPlayReason);
}

void AShooterCharacter::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);

	RefreshFirstPersonShadowPolicy();
}

void AShooterCharacter::OnRep_Controller()
{
	Super::OnRep_Controller();

	RefreshFirstPersonShadowPolicy();
}

void AShooterCharacter::RefreshFirstPersonShadowPolicy()
{
	const bool bLocalView = IsLocallyControlled();

	if (USkeletalMeshComponent* ThirdPersonMesh = GetMesh())
	{
		ThirdPersonMesh->SetOwnerNoSee(bLocalView);
		ThirdPersonMesh->SetCastShadow(!bLocalView);
		ThirdPersonMesh->SetCastHiddenShadow(false);
		if (bLocalView)
		{
			// 1인칭이 이 인스턴스와 애님 상태(벽 ADS 해제, 근접 단계 union)를
			// 공유하고 그림자 메시도 이 포즈를 따라가므로, 로컬에서 몸이
			// 하나도 렌더되지 않아도 계속 애니메이션을 갱신해야 한다
			ThirdPersonMesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
		}
	}

	if (FirstPersonMesh)
	{
		FirstPersonMesh->SetOnlyOwnerSee(true);
		FirstPersonMesh->SetCastShadow(false);
		FirstPersonMesh->SetCastHiddenShadow(false);
	}

	if (ShadowMesh)
	{
		USkeletalMeshComponent* ThirdPersonMesh = GetMesh();
		if (ThirdPersonMesh)
		{
			if (!ShadowMesh->GetSkeletalMeshAsset())
			{
				ShadowMesh->SetSkeletalMeshAsset(ThirdPersonMesh->GetSkeletalMeshAsset());
			}
			ShadowMesh->SetLeaderPoseComponent(ThirdPersonMesh);
		}

		ShadowMesh->SetOnlyOwnerSee(false);
		ShadowMesh->SetOwnerNoSee(false);
		ShadowMesh->SetHiddenInGame(true);
		ShadowMesh->SetVisibility(true, true);
		ShadowMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		ShadowMesh->SetRenderInMainPass(true);
		ShadowMesh->SetRenderInDepthPass(false);
		ShadowMesh->SetCastShadow(bLocalView);
		ShadowMesh->SetCastHiddenShadow(bLocalView);
		ShadowMesh->SetComponentTickEnabled(bLocalView);
	}

	if (AWeaponBase* EquippedWeapon = GetCurrentWeapon())
	{
		EquippedWeapon->RefreshShadowWeaponPresentation();
	}
}

void AShooterCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	// Set up Action Bindings
	UEnhancedInputComponent* EnhancedInputComponent = Cast<UEnhancedInputComponent>(PlayerInputComponent);
	UShooterInputConfig* ShooterInputConfig = Cast<UShooterInputConfig>(InputConfig);
	if (!EnhancedInputComponent || !ShooterInputConfig) {
		UE_LOG(LogTemp, Warning, TEXT("EnhancedInputComponent or Shooter InputConfig is Null"));
		return;
	}

	// Jumping
	EnhancedInputComponent->BindAction(ShooterInputConfig->JumpAction, ETriggerEvent::Started,   this, &AShooterCharacter::DoJumpStart);
	EnhancedInputComponent->BindAction(ShooterInputConfig->JumpAction, ETriggerEvent::Completed, this, &AShooterCharacter::DoJumpEnd);

	// Switch Weapon
	EnhancedInputComponent->BindAction(ShooterInputConfig->SwitchWeapon1Action, ETriggerEvent::Started,  this, &AShooterCharacter::TrySwitchWeapon1);
	EnhancedInputComponent->BindAction(ShooterInputConfig->SwitchWeapon2Action, ETriggerEvent::Started,  this, &AShooterCharacter::TrySwitchWeapon2);
	EnhancedInputComponent->BindAction(ShooterInputConfig->SwitchWeapon3Action, ETriggerEvent::Started,  this, &AShooterCharacter::TrySwitchWeapon3);

	// Sprint
	EnhancedInputComponent->BindAction(ShooterInputConfig->SprintAction,		ETriggerEvent::Started,   this, &AShooterCharacter::HandleSprintPressed);
	EnhancedInputComponent->BindAction(ShooterInputConfig->SprintAction,		ETriggerEvent::Completed, this, &AShooterCharacter::HandleSprintReleased);

	// Crouch
	EnhancedInputComponent->BindAction(ShooterInputConfig->CrouchAction,		ETriggerEvent::Started,   this, &AShooterCharacter::HandleCrouchToggled);

	// Lean
	EnhancedInputComponent->BindAction(ShooterInputConfig->LeanAction,			ETriggerEvent::Triggered, this, &AShooterCharacter::TryLean);
	EnhancedInputComponent->BindAction(ShooterInputConfig->LeanAction,			ETriggerEvent::Completed, this, &AShooterCharacter::TryLean);

	// Suit Menu Hold
	EnhancedInputComponent->BindAction(ShooterInputConfig->SuitMenuHoldAction, ETriggerEvent::Started,   this, &AShooterCharacter::TryOpenSuitMenu);
	EnhancedInputComponent->BindAction(ShooterInputConfig->SuitMenuHoldAction, ETriggerEvent::Triggered, this, &AShooterCharacter::TryHandleSuitMenuHover);
	EnhancedInputComponent->BindAction(ShooterInputConfig->SuitMenuHoldAction, ETriggerEvent::Completed, this, &AShooterCharacter::TryCloseSuitMenu);

	// Suit Navigate
	EnhancedInputComponent->BindAction(ShooterInputConfig->SuitNavigateAction, ETriggerEvent::Triggered, this, &AShooterCharacter::UpdateSuitSelection);

	// Suit Use
	EnhancedInputComponent->BindAction(ShooterInputConfig->SuitUseAction,		ETriggerEvent::Started,   this, &AShooterCharacter::TryUseSuit);

	// Reload
	EnhancedInputComponent->BindAction(ShooterInputConfig->ReloadAction,		ETriggerEvent::Started,   this, &AShooterCharacter::TryReload);

	// Aim
	EnhancedInputComponent->BindAction(ShooterInputConfig->AimAction,			ETriggerEvent::Started,   this, &AShooterCharacter::HandleAimPressed);
	EnhancedInputComponent->BindAction(ShooterInputConfig->AimAction,			ETriggerEvent::Completed, this, &AShooterCharacter::HandleAimReleased);
	EnhancedInputComponent->BindAction(ShooterInputConfig->AimAction,			ETriggerEvent::Canceled,  this, &AShooterCharacter::HandleAimReleased);
}

void AShooterCharacter::OnStartCrouch(float HalfHeightAdjust, float ScaledHalfHeightAdjust)
{
	Super::OnStartCrouch(HalfHeightAdjust, ScaledHalfHeightAdjust);

	RefreshMovementState();
}

void AShooterCharacter::OnEndCrouch(float HalfHeightAdjust, float ScaledHalfHeightAdjust)
{
	Super::OnEndCrouch(HalfHeightAdjust, ScaledHalfHeightAdjust);

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

void AShooterCharacter::OnMoveInputUpdated(const FVector2D& MoveValue)
{
  Super::OnMoveInputUpdated(MoveValue);

	if (MovementComponent)
	{
		MovementComponent->RefreshMovementState();
	}
}

void AShooterCharacter::LookInput(const FInputActionValue& Value)
{
	if (bIsSuitMenuOpen) return;

	FVector2D LookAxisVector = Value.Get<FVector2D>();
	LookAxisVector *= GetLookSensitivityScale();

	DoAim(LookAxisVector.X, -LookAxisVector.Y);
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

void AShooterCharacter::TryOpenSuitMenu()
{
	if (bIsDead)
	{
		return;
	}

	bIsSuitMenuOpen = true;

	// 마우스 커서 표시
	AShooterPlayerController* ShooterController = Cast<AShooterPlayerController>(GetController());
	if (!ShooterController || !ShooterController->AbilityUIInstance)
	{
		return;
	}

	if (ULocalPlayer* LP = ShooterController->GetLocalPlayer())
	{
		if (ULocalPlayerPostProcessSubsystem* PPSubsystem = LP->GetSubsystem<ULocalPlayerPostProcessSubsystem>())
		{
			PPSubsystem->SetDualKawaseBlurEnabled(true);
		}
	}
	ShooterController->AbilityUIInstance->SetVisibility(ESlateVisibility::Visible);
}

void AShooterCharacter::TryHandleSuitMenuHover()
{
	if (!bIsSuitMenuOpen) return;

	AShooterPlayerController* ShooterController = Cast<AShooterPlayerController>(GetController());
	if (!ShooterController || !ShooterController->AbilityUIInstance)
	{
		return;
	}

	ShooterController->AbilityUIInstance->TryHovering();

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

	AShooterPlayerController* ShooterController = Cast<AShooterPlayerController>(GetController());
	if (ShooterController && ShooterController->AbilityUIInstance)
	{
		ShooterController->AbilityUIInstance->SetVisibility(ESlateVisibility::Collapsed);
		ShooterController->AbilityUIInstance->TryGetHoveredAbility(SelectedAbilityTag);
	}
	else if (!ShooterController || !ShooterController->AbilityUIInstance)
	{
		UE_LOG(LogTemp, Error, TEXT("Null: AbilityUIInstance"));
	}

	if (ULocalPlayer* LP = ShooterController->GetLocalPlayer())
	{
		if (ULocalPlayerPostProcessSubsystem* PPSubsystem = LP->GetSubsystem<ULocalPlayerPostProcessSubsystem>())
		{
			PPSubsystem->SetDualKawaseBlurEnabled(false);
		}
	}

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
	UE_LOG(LogTemp, Error, TEXT("TryUseSuit"));


	constexpr float SuitAbilityCooldown = 5.0f;

	if (bIsDead)
	{
		UE_LOG(LogTemp, Error, TEXT("ShooterController"));

		return;
	}

	//if (bSuitDisabledByPartnerBoundary)
	//{
	//	UE_LOG(LogTemp, Error, TEXT("bSuitDisabledByPartnerBoundary"));

	//	return;
	//}

	/*if (!SelectedAbilityTag.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("SelectedAbilityTag"));

		return;
	}*/

	AShooterPlayerController* ShooterController = Cast<AShooterPlayerController>(GetController());
	if (!ShooterController)
	{
		UE_LOG(LogTemp, Error, TEXT("ShooterController"));
		return;
	}

	ULocalPlayerUISubSystem* UISubsystem = nullptr;
	if (ULocalPlayer* LocalPlayer = ShooterController->GetLocalPlayer())
	{
		UISubsystem = LocalPlayer->GetSubsystem<ULocalPlayerUISubSystem>();
	}

	/*if (!UISubsystem || !UISubsystem->ApplyCurrentAbilityCooldownIfMatches(SelectedAbilityTag, SuitAbilityCooldown))
	{
		UE_LOG(LogTemp, Error, TEXT("ApplyCurrentAbilityCooldownIfMatches"));

		return;
	}*/

	if (ShooterController->AbilityUIInstance)
	{
		ShooterController->AbilityUIInstance->ApplyCooldownIfMatches(SelectedAbilityTag, SuitAbilityCooldown);
	}

	if (HasAuthority())
	{
		ToggleStealth();
	}
	else
	{
		ServerToggleStealth();
	}
}

void AShooterCharacter::ToggleStealth()
{
	if (!HasAuthority() || bIsDead)
	{
		return;
	}

	bIsStealthed = !bIsStealthed;
	if (CachedPartnerCharacter)
	{
		CachedPartnerCharacter->SetTestStealthed(bIsStealthed);
	}
	OnRep_IsStealthed();
	ForceNetUpdate();
}

void AShooterCharacter::SetStealthVisualEnabled(bool bEnabled)
{
	if (!IsLocallyControlled())
	{
		return;
	}

	if (UMaterialPostProcessSubsystem* MaterialSub = GetWorld()->GetSubsystem<UMaterialPostProcessSubsystem>())
	{
		MaterialSub->SetPostProcessEnabled(EOutlierPostProcessMaterialType::Stealth, bEnabled);
	}
}

void AShooterCharacter::ServerToggleStealth_Implementation()
{
	ToggleStealth();
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
	if (!CanLean())
	{
		StopLean();
		return;
	}

	const float LeanAlpha = Value.Get<float>();
	TargetLeanAlpha = FMath::Abs(LeanAlpha) > KINDA_SMALL_NUMBER ? LeanAlpha : 0.0f;
	StartLeanUpdate();
}

void AShooterCharacter::StopLean()
{
	TargetLeanAlpha = 0.0f;
	StartLeanUpdate();
}

void AShooterCharacter::UpdateSlideCameraEffect(float DeltaSeconds)
{
	if (!IsLocallyControlled())
	{
		return;
	}

	const float InterpSpeed = TargetSlideCameraEffectAlpha > CurrentSlideCameraEffectAlpha
		? SlideCameraEffectInterpInSpeed
		: SlideCameraEffectInterpOutSpeed;

	CurrentSlideCameraEffectAlpha = FMath::FInterpTo(
		CurrentSlideCameraEffectAlpha,
		TargetSlideCameraEffectAlpha,
		DeltaSeconds,
		InterpSpeed
	);

	if (TargetSlideCameraEffectAlpha > KINDA_SMALL_NUMBER)
	{
		SlideCameraEffectElapsedTime += DeltaSeconds;
	}

	const float NormalizedSlideCameraTime = SlideCameraEffectDuration > KINDA_SMALL_NUMBER
		? FMath::Clamp(SlideCameraEffectElapsedTime / SlideCameraEffectDuration, 0.0f, 1.0f)
		: 0.0f;
	const float RollCurveAlpha = SlideCameraRollCurve
		? SlideCameraRollCurve->GetFloatValue(NormalizedSlideCameraTime)
		: 1.0f;

	ActiveSlideCameraRollDegrees = TargetSlideCameraRollDegrees * RollCurveAlpha * CurrentSlideCameraEffectAlpha;

	if (CurrentSlideCameraEffectAlpha <= KINDA_SMALL_NUMBER &&
		TargetSlideCameraEffectAlpha <= KINDA_SMALL_NUMBER)
	{
		SlideCameraEffectElapsedTime = 0.0f;
		SlideCameraEffectDuration = 0.0f;
		TargetSlideCameraRollDegrees = 0.0f;
		ActiveSlideCameraRollDegrees = 0.0f;
	}
}

void AShooterCharacter::OnRep_CurHP()
{
	//UE_LOG(LogTemp, Log, TEXT("%s %s OnRep_CurHP CurHP=%.1f / %.1f"), OutlierNet::GetNetPrefix(this), *GetName(), CurHP, MaxHP);
	OnShooterHealthChanged.Broadcast(CurHP, MaxHP);

	OnShooterConditionChanged.Broadcast(ResolveShooterConditionTag());
}

void AShooterCharacter::OnRep_MovementState()
{
	OnMovementStateChanged.Broadcast(MovementState);
}

void AShooterCharacter::OnRep_CurShield()
{
	OnShooterShieldChanged.Broadcast(CurShield, MaxShield);

	if (IsLocallyControlled())
	{
		UMaterialPostProcessSubsystem* PPS = GetWorld()->GetSubsystem<UMaterialPostProcessSubsystem>();
		if (PPS)
		{
			PPS->UpdateDamagedPostProcess(CurShield / MaxShield , FVector4(0,0,1,0));
			PPS->SetPostProcessEnabled(EOutlierPostProcessMaterialType::Damaged, true);
		}
	}

	OnShooterConditionChanged.Broadcast(ResolveShooterConditionTag());

}

void AShooterCharacter::OnRep_CurPartnerShield()
{
	BroadcastPartnerShieldState();
}

void AShooterCharacter::OnRep_IsStealthed()
{
	SetStealthVisualEnabled(bIsStealthed);
}

void AShooterCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AShooterCharacter, CurHP);
	DOREPLIFETIME(AShooterCharacter, MaxPartnerShield);
	DOREPLIFETIME(AShooterCharacter, CurPartnerShield);
	DOREPLIFETIME(AShooterCharacter, CurShield);
	DOREPLIFETIME(AShooterCharacter, bIsDead);
	DOREPLIFETIME(AShooterCharacter, MovementState);
	DOREPLIFETIME(AShooterCharacter, WeaponMode);
	DOREPLIFETIME(AShooterCharacter, CombatState);
	DOREPLIFETIME(AShooterCharacter, ActionLock);
	DOREPLIFETIME(AShooterCharacter, bIsStealthed);
}

FGameplayTagContainer AShooterCharacter::GetOwnedGameplayTagsForQuery() const
{
	FGameplayTagContainer GameplayTags = Super::GetOwnedGameplayTagsForQuery();
	if (bIsStealthed)
	{
		GameplayTags.AddTag(OutlierGameplayTags::State::Stealthed());
	}
	return GameplayTags;
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
	return FRotator::NormalizeAxis(GetBaseAimRotation().Yaw - GetActorRotation().Yaw);
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

bool AShooterCharacter::CanInteract() const
{
	return !bIsDead;
}

bool AShooterCharacter::CanLean() const
{
	return !bIsDead
		&& !IsSprinting()
		&& !GetCharacterMovement()->IsFalling()
		&& !IsSliding()
		&& !IsReloading();
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

bool AShooterCharacter::CanStartAction(EShooterActionLock NextLock) const
{
	const bool bCanOverrideSlideLock =
		ActionLock == EShooterActionLock::Slide &&
		(NextLock == EShooterActionLock::Reload || NextLock == EShooterActionLock::Equip);

	return !bIsDead
		&& NextLock != EShooterActionLock::None
		&& (ActionLock == EShooterActionLock::None || bCanOverrideSlideLock);
}

void AShooterCharacter::BeginActionLock(EShooterActionLock NewLock)
{
	ActionLock = NewLock;
	bIsEquipping = (ActionLock == EShooterActionLock::Equip);
	GetWorldTimerManager().ClearTimer(ActionLockTimerHandle);
}

void AShooterCharacter::EndActionLock(EShooterActionLock LockToEnd)
{
	if (ActionLock != LockToEnd)
	{
		return;
	}

	ActionLock = EShooterActionLock::None;
	bIsEquipping = false;
	GetWorldTimerManager().ClearTimer(ActionLockTimerHandle);
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

	StopLeanUpdateIfSettled();
}

void AShooterCharacter::UpdateCameraFOV(float DeltaSeconds)
{
	if (!IsLocallyControlled() || !FirstPersonCamera)
	{
		return;
	}

	const float EffectiveAimAlpha = IsSprinting() ? 0.0f : GetEffectiveFirstPersonAimAlpha();
	float TargetFOV = BaseCameraFOV;
	float FOVInterpSpeed = CameraFOVInterpSpeed;
	if (IsSprinting())
	{
		TargetFOV = SprintCameraFOV;
	}
	else if (EffectiveAimAlpha > KINDA_SMALL_NUMBER)
	{
		TargetFOV = FMath::Lerp(BaseCameraFOV, AimCameraFOV, EffectiveAimAlpha);
		FOVInterpSpeed = TargetFOV < FirstPersonCamera->FieldOfView
			? AimCameraFOVInterpInSpeed
			: AimCameraFOVInterpOutSpeed;
	}
	else if (FirstPersonCamera->FieldOfView < BaseCameraFOV)
	{
		FOVInterpSpeed = AimCameraFOVInterpOutSpeed;
	}

	TargetFOV += CameraRecoilFOVOffset;

	const float NewFOV = FMath::FInterpTo(
		FirstPersonCamera->FieldOfView,
		TargetFOV,
		DeltaSeconds,
		FOVInterpSpeed
	);
	FirstPersonCamera->SetFieldOfView(NewFOV);
}

float AShooterCharacter::GetEffectiveFirstPersonAimAlpha() const
{
	if (!IsAiming())
	{
		return 0.0f;
	}

	const USkeletalMeshComponent* Mesh1P = GetFirstPersonMesh();
	const UShooterFirstPersonAnimInstance* FirstPersonAnimInstance = Mesh1P
		? Cast<UShooterFirstPersonAnimInstance>(Mesh1P->GetAnimInstance())
		: nullptr;
	return FirstPersonAnimInstance
		? FMath::Clamp(FirstPersonAnimInstance->GetViewModelAimAlpha(), 0.0f, 1.0f)
		: 1.0f;
}

void AShooterCharacter::AddWeaponCameraRecoil(
	float PitchAmplitude,
	float YawAmplitude,
	float DirectionPitchAmplitude,
	float FOVAmplitude,
	float RecoverySpeed,
	const FVector2D& NormalizedShotDirection
)
{
	if (!IsLocallyControlled())
	{
		return;
	}

	CameraRecoilTarget.Pitch += PitchAmplitude + (NormalizedShotDirection.Y * DirectionPitchAmplitude);
	CameraRecoilTarget.Yaw += NormalizedShotDirection.X * YawAmplitude;
	CameraRecoilRecoverySpeed = FMath::Max(RecoverySpeed, 0.0f);
	CameraRecoilFOVOffset = FMath::Max(CameraRecoilFOVOffset, FMath::Max(FOVAmplitude, 0.0f));
}

void AShooterCharacter::UpdateCameraRecoil(float DeltaSeconds)
{
	if (!IsLocallyControlled())
	{
		return;
	}

	const FRotator NewRecoil = FMath::RInterpTo(
		CameraRecoilCurrent,
		CameraRecoilTarget,
		DeltaSeconds,
		CameraRecoilKickInterpSpeed
	);
	const FRotator DeltaRecoil = NewRecoil - CameraRecoilCurrent;

	AddControllerPitchInput(-DeltaRecoil.Pitch);
	AddControllerYawInput(DeltaRecoil.Yaw);

	CameraRecoilCurrent = NewRecoil;
	CameraRecoilTarget = FMath::RInterpTo(
		CameraRecoilTarget,
		FRotator::ZeroRotator,
		DeltaSeconds,
		CameraRecoilRecoverySpeed
	);
	CameraRecoilFOVOffset = FMath::FInterpTo(
		CameraRecoilFOVOffset,
		0.0f,
		DeltaSeconds,
		CameraRecoilFOVRecoverySpeed
	);
}

float AShooterCharacter::GetLookSensitivityScale() const
{
	if (IsReloading())
	{
		return ReloadLookSensitivityScale;
	}

	if (IsAiming())
	{
		return FMath::Lerp(1.0f, AimLookSensitivityScale, GetEffectiveFirstPersonAimAlpha());
	}

	if (IsSprinting())
	{
		return SprintLookSensitivityScale;
	}

	return 1.0f;
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

void AShooterCharacter::BeginSlideCameraEffect(float CameraRollDegrees, float Duration)
{
	if (!IsLocallyControlled())
	{
		return;
	}

	TargetSlideCameraEffectAlpha = 1.0f;
	SlideCameraEffectElapsedTime = 0.0f;
	SlideCameraEffectDuration = FMath::Max(Duration, KINDA_SMALL_NUMBER);
	TargetSlideCameraRollDegrees = CameraRollDegrees;
}

void AShooterCharacter::EndSlideCameraEffect()
{
	if (!IsLocallyControlled())
	{
		return;
	}

	TargetSlideCameraEffectAlpha = 0.0f;
}

void AShooterCharacter::UpdatePartnerShieldDecay()
{
	constexpr float DeltaTime = 1.0f / 60.0f;

	if (PartnerShieldDuration <= KINDA_SMALL_NUMBER)
	{
		CurPartnerShield = 0.0f;
		BroadcastPartnerShieldState();
		GetWorldTimerManager().ClearTimer(PartnerShieldTimerHandle);
		return;
	}

	const float DecayAmount = (MaxPartnerShield / PartnerShieldDuration) * DeltaTime;
	CurPartnerShield = FMath::Max(0.0f, CurPartnerShield - DecayAmount);

	if (GetNetMode() != NM_DedicatedServer)
	{
		BroadcastPartnerShieldState();
	}

	if (CurPartnerShield <= 0.0f)
	{
		GetWorldTimerManager().ClearTimer(PartnerShieldTimerHandle);
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
	ActionLock = EShooterActionLock::None;
	bIsEquipping = false;
	TargetLeanAlpha = 0.0f;
	CurrentLeanAlpha = 0.0f;
	CameraRecoilCurrent = FRotator::ZeroRotator;
	CameraRecoilTarget = FRotator::ZeroRotator;
	CameraRecoilFOVOffset = 0.0f;

	if (IsSliding())
	{
		StopSlide(ESlideEndReason::ForcedCancel);
	}

	StopAimInternal();
	CancelReloadInternal();
	StopSprintInternal();
	TryStopAttack();

	StopJumping();
	GetWorldTimerManager().ClearTimer(ActionLockTimerHandle);
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

void AShooterCharacter::CleanupOwnedWeapons()
{
	if (InventoryComponent)
	{
		InventoryComponent->CleanupOwnedWeapons();
	}
}

FGameplayTag AShooterCharacter::ResolveShooterConditionTag() const
{
	FGameplayTag ConditionTag = TagDrivenUITags::Condition::Shooter::HP();

	if (CurShield > 0.0f)
	{
		ConditionTag = TagDrivenUITags::Condition::Shooter::Shield();
	}

	if (CurPartnerShield > 0.0f)
	{
		ConditionTag = TagDrivenUITags::Condition::Shooter::PartnerShield();
	}

	return ConditionTag;
}

void AShooterCharacter::RefreshUIForRespawn()
{
	BroadcastCurrentUIState();
}

void AShooterCharacter::BroadcastCurrentUIState()
{
	OnShooterHealthChanged.Broadcast(CurHP, MaxHP);
	OnShooterShieldChanged.Broadcast(CurShield, MaxShield);
	OnWeaponChanged.Broadcast(GetWeaponType());
	BroadcastPartnerShieldState();
}

void AShooterCharacter::BroadcastPartnerShieldState()
{
	OnShooterPartnerShieldChanged.Broadcast(CurPartnerShield, MaxPartnerShield);
	OnShooterConditionChanged.Broadcast(ResolveShooterConditionTag());
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

namespace
{
float GetMontageSectionDurationOrFullLength(const UAnimMontage* Montage, FName SectionName)
{
	if (!Montage)
	{
		return 0.0f;
	}

	if (SectionName == NAME_None)
	{
		return Montage->GetPlayLength();
	}

	const int32 SectionIndex = Montage->GetSectionIndex(SectionName);
	if (SectionIndex == INDEX_NONE)
	{
		return Montage->GetPlayLength();
	}

	return Montage->GetSectionLength(SectionIndex);
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

		FirstPersonAnimInstance->Montage_Play(
			Montage,
			1.0f,
			EMontagePlayReturnType::MontageLength,
			0.0f,
			false);
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
		UE_LOG(LogTemp, Warning, TEXT("%s %s [TPMontage] skipped: montage is null WeaponType=%d Mesh=%s"),
			OutlierNet::GetNetPrefix(this),
			*GetName(),
			static_cast<int32>(WeaponType),
			*GetNameSafe(GetMesh()));
		return;
	}

	if (USkeletalMeshComponent* ThirdPersonMesh = GetMesh())
	{
		if (UAnimInstance* ThirdPersonAnimInstance = ThirdPersonMesh->GetAnimInstance())
		{
			const FName SectionName = bUseWeaponSection ? ResolveMontageSectionNameForWeapon(WeaponType) : NAME_None;
			const bool bHasSlot = Montage->IsValidSlot(FName(TEXT("UpperBody")));
			const bool bSectionValid = SectionName != NAME_None && Montage->IsValidSectionName(SectionName);
			UE_LOG(
				LogTemp,
				Log,
				TEXT("%s %s [TPMontage] Request Montage=%s Length=%.3f SlotUpperBody=%d Section=%s SectionValid=%d UseSection=%d AnimInstance=%s Mesh=%s WeaponType=%d"),
				OutlierNet::GetNetPrefix(this),
				*GetName(),
				*GetNameSafe(Montage),
				Montage->GetPlayLength(),
				bHasSlot ? 1 : 0,
				*SectionName.ToString(),
				bSectionValid ? 1 : 0,
				bUseWeaponSection ? 1 : 0,
				*GetNameSafe(ThirdPersonAnimInstance),
				*GetNameSafe(ThirdPersonMesh),
				static_cast<int32>(WeaponType));

			const float PlayedLength = ThirdPersonAnimInstance->Montage_Play(
				Montage,
				1.0f,
				EMontagePlayReturnType::MontageLength,
				0.0f,
				false);
			UE_LOG(
				LogTemp,
				Warning,
				TEXT("%s %s [TPMontage] Montage_Play Result=%.3f IsActive=%d Current=%s"),
				OutlierNet::GetNetPrefix(this),
				*GetName(),
				PlayedLength,
				ThirdPersonAnimInstance->Montage_IsActive(Montage) ? 1 : 0,
				*GetNameSafe(ThirdPersonAnimInstance->GetCurrentActiveMontage()));

			if (SectionName != NAME_None && bSectionValid)
			{
				ThirdPersonAnimInstance->Montage_JumpToSection(SectionName, Montage);
				UE_LOG(
					LogTemp,
					Warning,
					TEXT("%s %s [TPMontage] JumpToSection Section=%s Position=%.3f"),
					OutlierNet::GetNetPrefix(this),
					*GetName(),
					*SectionName.ToString(),
					ThirdPersonAnimInstance->Montage_GetPosition(Montage));
			}
			else if (bUseWeaponSection)
			{
				UE_LOG(
					LogTemp,
					Warning,
					TEXT("%s %s [TPMontage] no valid section Montage=%s Section=%s"),
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
				TEXT("%s %s [TPMontage] missing AnimInstance Mesh=%s Montage=%s"),
				OutlierNet::GetNetPrefix(this),
				*GetName(),
				*GetNameSafe(ThirdPersonMesh),
				*GetNameSafe(Montage));
		}
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("%s %s [TPMontage] missing ThirdPersonMesh Montage=%s"),
			OutlierNet::GetNetPrefix(this),
			*GetName(),
			*GetNameSafe(Montage));
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
	if (!CanStartAction(EShooterActionLock::Equip))
	{
		return;
	}

	const EWeaponType EquippedWeaponType = GetWeaponType();
	const FName EquipSectionName = ResolveMontageSectionNameForWeapon(EquippedWeaponType);
	const float FirstPersonEquipLockDuration =
		GetMontageSectionDurationOrFullLength(FirstPersonEquipMontage, EquipSectionName);
	const float ThirdPersonEquipLockDuration =
		GetMontageSectionDurationOrFullLength(ThirdPersonEquipMontage, EquipSectionName);
	const float EquipLockDuration = FMath::Max(
		FirstPersonEquipLockDuration,
		ThirdPersonEquipLockDuration);

	BeginActionLock(EShooterActionLock::Equip);

	UE_LOG(
		LogTemp,
		Log,
		TEXT("%s %s PlayEquipMontages WeaponType=%d Section=%s SectionValidFP=%d SectionValidTP=%d FPLock=%.3f TPLock=%.3f Lock=%.3f FP=%s TP=%s Local=%d Authority=%d"),
		OutlierNet::GetNetPrefix(this),
		*GetName(),
		static_cast<int32>(EquippedWeaponType),
		*EquipSectionName.ToString(),
		(FirstPersonEquipMontage && FirstPersonEquipMontage->IsValidSectionName(EquipSectionName)) ? 1 : 0,
		(ThirdPersonEquipMontage && ThirdPersonEquipMontage->IsValidSectionName(EquipSectionName)) ? 1 : 0,
		FirstPersonEquipLockDuration,
		ThirdPersonEquipLockDuration,
		EquipLockDuration,
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

	if (EquipLockDuration > 0.0f)
	{
		FTimerDelegate EquipEndDelegate;
		EquipEndDelegate.BindUObject(this, &AShooterCharacter::EndActionLock, EShooterActionLock::Equip);
		GetWorldTimerManager().SetTimer(ActionLockTimerHandle, EquipEndDelegate, EquipLockDuration, false);
	}
	else
	{
		EndActionLock(EShooterActionLock::Equip);
	}
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
	if (Action == EShooterMontageAction::Reload && FirstPersonReloadMontage && FirstPersonMesh)
	{
		if (UAnimInstance* FirstPersonAnimInstance = FirstPersonMesh->GetAnimInstance())
		{
			if (FirstPersonAnimInstance->Montage_IsPlaying(FirstPersonReloadMontage))
			{
				return;
			}
		}
	}

	PlayFirstPersonActionMontage(Action, WeaponType);
}

void AShooterCharacter::MulticastPlayThirdPersonActionMontage_Implementation(EShooterMontageAction Action, EWeaponType WeaponType)
{
	UE_LOG(
		LogTemp,
		Warning,
		TEXT("%s %s [TPMontage] Multicast received Action=%d WeaponType=%d Local=%d Authority=%d"),
		OutlierNet::GetNetPrefix(this),
		*GetName(),
		static_cast<int32>(Action),
		static_cast<int32>(WeaponType),
		IsLocallyControlled() ? 1 : 0,
		HasAuthority() ? 1 : 0);

	PlayThirdPersonActionMontage(Action, WeaponType);
}

void AShooterCharacter::SetPartnerCharacter(APartnerCharacter* NewPartner)
{
	CachedPartnerCharacter = NewPartner;

	if (HasAuthority() && CachedPartnerCharacter)
	{
		CachedPartnerCharacter->SetTestStealthed(bIsStealthed);
	}
}

void AShooterCharacter::SetSuitDisabledByPartnerBoundary(bool bDisabled)
{
	bSuitDisabledByPartnerBoundary = bDisabled;

	if (!IsLocallyControlled())
	{
		return;
	}

	APlayerController* PC = Cast<APlayerController>(GetController());
	if (!PC)
	{
		return;
	}

	ULocalPlayer* LP = PC->GetLocalPlayer();
	if (!LP)
	{
		return;
	}

	if (ULocalPlayerUISubSystem* SubSystem = LP->GetSubsystem<ULocalPlayerUISubSystem>())
	{
		if (bDisabled)
		{
			SubSystem->OnAbilityDisabledByDistance();
		}
		else
		{
			SubSystem->OnAbilityEnabledByDistance();
		}
	}
}
void AShooterCharacter::ApplyPartnerShield(float Amount, float Duration)
{
	CurPartnerShield = Amount;
	MaxPartnerShield = Amount;
	PartnerShieldDuration = Duration;
	if (GetNetMode() != NM_DedicatedServer)
	{
		BroadcastPartnerShieldState();
	}

	GetWorldTimerManager().SetTimer(
		PartnerShieldTimerHandle,
		this,
		&AShooterCharacter::UpdatePartnerShieldDecay,
		1.0f / 60.0f,
		true
	);
}
