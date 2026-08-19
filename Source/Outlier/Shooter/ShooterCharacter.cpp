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
#include "Damage/OutlierTaggedDamageEvent.h"
#include "Enemy/EnemyRoomSubsystem.h"
#include "TagDrivenUIGameplayTags.h"
#include "ShooterInputConfig.h"
#include "ShooterHealthComponent.h"
#include "ShooterInventoryComponent.h"
#include "ShooterCombatComponent.h"
#include "Weapon/RangedWeaponBase.h"
#include "ShooterFirstPersonAnimInstance.h"
#include "ShooterMovementComponent.h"
#include "LocalPlayerPostProcessSubsystem.h"
#include "Weapon/WeaponBase.h"
#include "Weapon/RangedWeaponBase.h"
#include "GameplayTags/OutlierGameplayTags.h"
#include "GAS/OutlierAbilitySystemComponent.h"
#include "GAS/Attributes/OutlierVitalAttributeSet.h"
#include "GAS/Attributes/OutlierShieldAttributeSet.h"
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
	OwnedQueryTags.AddTag(OutlierGameplayTags::Actor::Role::Shooter());

	OutlierAbilitySystemComponent = CreateDefaultSubobject<UOutlierAbilitySystemComponent>(
		TEXT("AbilitySystemComponent"));
	OutlierAbilitySystemComponent->SetReplicationMode(EGameplayEffectReplicationMode::Mixed);
	VitalAttributeSet = CreateDefaultSubobject<UOutlierVitalAttributeSet>(TEXT("VitalAttributeSet"));
	ShieldAttributeSet = CreateDefaultSubobject<UOutlierShieldAttributeSet>(TEXT("ShieldAttributeSet"));

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
	RefreshAbilitySystemActorInfo();
	BindGasVitalityObservers();
	InitializeGasVitality();
	InitializeGasSuitAbilities();
	if (!SelectedAbilityTag.IsValid())
	{
		SelectedAbilityTag = OutlierGameplayTags::Ability::Shooter::Stealth();
	}

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

	BroadcastCurrentUIState();

	RefreshWeaponMode();
	RefreshMovementState();
	RefreshCombatState();
	SetStealthVisualEnabled(IsStealthed());
	RefreshShooterSuitCooldownUI();
}

void AShooterCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	UpdateSlideCameraEffect(DeltaSeconds);
	UpdateCameraFOV(DeltaSeconds);
}

void AShooterCharacter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	CancelActiveQuantumLeap();
	EndActiveStealth(false);
	SetStealthVisualEnabled(false);

	if (HasAuthority())
	{
		CleanupOwnedWeapons();
	}

	if (OutlierAbilitySystemComponent)
	{
		UnbindGasVitalityObservers();
		OutlierAbilitySystemComponent->ClearForPawn(this);
	}

	Super::EndPlay(EndPlayReason);
}

void AShooterCharacter::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);
	RefreshAbilitySystemActorInfo();

	RefreshFirstPersonShadowPolicy();
	RefreshShooterSuitCooldownUI();
}

void AShooterCharacter::OnRep_Controller()
{
	Super::OnRep_Controller();
	RefreshAbilitySystemActorInfo();
	RefreshShooterSuitCooldownUI();

	RefreshFirstPersonShadowPolicy();
}

UAbilitySystemComponent* AShooterCharacter::GetAbilitySystemComponent() const
{
	return OutlierAbilitySystemComponent;
}

float AShooterCharacter::GetCurPartnerShield() const
{
	return ShieldAttributeSet ? ShieldAttributeSet->GetPartnerShield() : 0.0f;
}

float AShooterCharacter::GetMaxPartnerShield() const
{
	return ShieldAttributeSet ? ShieldAttributeSet->GetMaxPartnerShield() : 0.0f;
}

float AShooterCharacter::GetCurShield() const
{
	return ShieldAttributeSet ? ShieldAttributeSet->GetShield() : 0.0f;
}

float AShooterCharacter::GetMaxShield() const
{
	return ShieldAttributeSet ? ShieldAttributeSet->GetMaxShield() : 0.0f;
}

float AShooterCharacter::GetCurHealth() const
{
	return VitalAttributeSet ? VitalAttributeSet->GetHealth() : 0.0f;
}

float AShooterCharacter::GetMaxHealth() const
{
	return VitalAttributeSet ? VitalAttributeSet->GetMaxHealth() : 0.0f;
}

bool AShooterCharacter::IsDead() const
{
	return OutlierAbilitySystemComponent
		&& OutlierAbilitySystemComponent->HasMatchingGameplayTag(OutlierGameplayTags::State::Dead());
}

void AShooterCharacter::RefreshAbilitySystemActorInfo()
{
	if (OutlierAbilitySystemComponent)
	{
		OutlierAbilitySystemComponent->InitializeForPawn(this);
	}
}

void AShooterCharacter::InitializeGasVitality()
{
	if (!HasAuthority() || !OutlierAbilitySystemComponent)
	{
		return;
	}

	OutlierAbilitySystemComponent->SetNumericAttributeBase(
		UOutlierVitalAttributeSet::GetMaxHealthAttribute(),
		FMath::Max(MaxHP, 0.0f));
	OutlierAbilitySystemComponent->SetNumericAttributeBase(
		UOutlierVitalAttributeSet::GetHealthAttribute(),
		FMath::Max(MaxHP, 0.0f));
	OutlierAbilitySystemComponent->SetNumericAttributeBase(
		UOutlierShieldAttributeSet::GetMaxShieldAttribute(),
		FMath::Max(MaxShield, 0.0f));
	OutlierAbilitySystemComponent->SetNumericAttributeBase(
		UOutlierShieldAttributeSet::GetShieldAttribute(),
		FMath::Max(MaxShield, 0.0f));
	OutlierAbilitySystemComponent->SetNumericAttributeBase(
		UOutlierShieldAttributeSet::GetMaxPartnerShieldAttribute(),
		0.0f);
	OutlierAbilitySystemComponent->SetNumericAttributeBase(
		UOutlierShieldAttributeSet::GetPartnerShieldAttribute(),
		0.0f);
}

void AShooterCharacter::InitializeGasSuitAbilities()
{
	if (bShooterSuitDataInitialized || !OutlierAbilitySystemComponent)
	{
		return;
	}

	FString Error;
	FOutlierShooterSuitConfig ResolvedConfig;
	if (!OutlierShooterSuitData::TryResolveConfiguration(
		ShooterSuitAbilityDataTable, ResolvedConfig, Error))
	{
#if UE_BUILD_SHIPPING
		UE_LOG(LogOutlier, Error, TEXT("[GAS.ShooterSuit] %s"), *Error);
		return;
#else
		checkf(false, TEXT("[GAS.ShooterSuit] %s"), *Error);
		return;
#endif
	}

	ShooterSuitConfig = ResolvedConfig;
	bShooterSuitDataInitialized = true;
	if (HasAuthority())
	{
		const bool bConfigured = OutlierAbilitySystemComponent->ConfigureShooterSuitAbilities(ShooterSuitConfig);
		checkf(bConfigured, TEXT("Shooter suit abilities must configure on authority."));
		if (CachedPartnerCharacter)
		{
			CachedPartnerCharacter->ConfigureSuitDisableBoundaryRadius(ShooterSuitConfig.MaxPartnerDistance);
		}
	}
}

void AShooterCharacter::BindGasVitalityObservers()
{
	if (!OutlierAbilitySystemComponent || HealthChangedHandle.IsValid())
	{
		return;
	}

	HealthChangedHandle = OutlierAbilitySystemComponent->GetGameplayAttributeValueChangeDelegate(
		UOutlierVitalAttributeSet::GetHealthAttribute()).AddUObject(
			this, &AShooterCharacter::HandleHealthChanged);
	MaxHealthChangedHandle = OutlierAbilitySystemComponent->GetGameplayAttributeValueChangeDelegate(
		UOutlierVitalAttributeSet::GetMaxHealthAttribute()).AddUObject(
			this, &AShooterCharacter::HandleMaxHealthChanged);
	ShieldChangedHandle = OutlierAbilitySystemComponent->GetGameplayAttributeValueChangeDelegate(
		UOutlierShieldAttributeSet::GetShieldAttribute()).AddUObject(
			this, &AShooterCharacter::HandleShieldChanged);
	MaxShieldChangedHandle = OutlierAbilitySystemComponent->GetGameplayAttributeValueChangeDelegate(
		UOutlierShieldAttributeSet::GetMaxShieldAttribute()).AddUObject(
			this, &AShooterCharacter::HandleMaxShieldChanged);
	PartnerShieldChangedHandle = OutlierAbilitySystemComponent->GetGameplayAttributeValueChangeDelegate(
		UOutlierShieldAttributeSet::GetPartnerShieldAttribute()).AddUObject(
			this, &AShooterCharacter::HandlePartnerShieldChanged);
	MaxPartnerShieldChangedHandle = OutlierAbilitySystemComponent->GetGameplayAttributeValueChangeDelegate(
		UOutlierShieldAttributeSet::GetMaxPartnerShieldAttribute()).AddUObject(
			this, &AShooterCharacter::HandleMaxPartnerShieldChanged);
	DeadTagChangedHandle = OutlierAbilitySystemComponent->RegisterGameplayTagEvent(
		OutlierGameplayTags::State::Dead()).AddUObject(
			this, &AShooterCharacter::HandleDeadTagChanged);
	StealthTagChangedHandle = OutlierAbilitySystemComponent->RegisterGameplayTagEvent(
		OutlierGameplayTags::State::Stealthed()).AddUObject(
			this, &AShooterCharacter::HandleStealthTagChanged);
	QuantumLeapCooldownTagChangedHandle = OutlierAbilitySystemComponent->RegisterGameplayTagEvent(
		OutlierGameplayTags::Cooldown::Shooter::QuantumLeap()).AddUObject(
			this, &AShooterCharacter::HandleQuantumLeapCooldownTagChanged);
	StealthCooldownTagChangedHandle = OutlierAbilitySystemComponent->RegisterGameplayTagEvent(
		OutlierGameplayTags::Cooldown::Shooter::Stealth()).AddUObject(
			this, &AShooterCharacter::HandleStealthCooldownTagChanged);
}

void AShooterCharacter::UnbindGasVitalityObservers()
{
	if (!OutlierAbilitySystemComponent)
	{
		return;
	}

	OutlierAbilitySystemComponent->GetGameplayAttributeValueChangeDelegate(
		UOutlierVitalAttributeSet::GetHealthAttribute()).Remove(HealthChangedHandle);
	OutlierAbilitySystemComponent->GetGameplayAttributeValueChangeDelegate(
		UOutlierVitalAttributeSet::GetMaxHealthAttribute()).Remove(MaxHealthChangedHandle);
	OutlierAbilitySystemComponent->GetGameplayAttributeValueChangeDelegate(
		UOutlierShieldAttributeSet::GetShieldAttribute()).Remove(ShieldChangedHandle);
	OutlierAbilitySystemComponent->GetGameplayAttributeValueChangeDelegate(
		UOutlierShieldAttributeSet::GetMaxShieldAttribute()).Remove(MaxShieldChangedHandle);
	OutlierAbilitySystemComponent->GetGameplayAttributeValueChangeDelegate(
		UOutlierShieldAttributeSet::GetPartnerShieldAttribute()).Remove(PartnerShieldChangedHandle);
	OutlierAbilitySystemComponent->GetGameplayAttributeValueChangeDelegate(
		UOutlierShieldAttributeSet::GetMaxPartnerShieldAttribute()).Remove(MaxPartnerShieldChangedHandle);
	OutlierAbilitySystemComponent->RegisterGameplayTagEvent(
		OutlierGameplayTags::State::Dead()).Remove(DeadTagChangedHandle);
	OutlierAbilitySystemComponent->RegisterGameplayTagEvent(
		OutlierGameplayTags::State::Stealthed()).Remove(StealthTagChangedHandle);
	OutlierAbilitySystemComponent->RegisterGameplayTagEvent(
		OutlierGameplayTags::Cooldown::Shooter::QuantumLeap()).Remove(QuantumLeapCooldownTagChangedHandle);
	OutlierAbilitySystemComponent->RegisterGameplayTagEvent(
		OutlierGameplayTags::Cooldown::Shooter::Stealth()).Remove(StealthCooldownTagChangedHandle);

	HealthChangedHandle.Reset();
	MaxHealthChangedHandle.Reset();
	ShieldChangedHandle.Reset();
	MaxShieldChangedHandle.Reset();
	PartnerShieldChangedHandle.Reset();
	MaxPartnerShieldChangedHandle.Reset();
	DeadTagChangedHandle.Reset();
	StealthTagChangedHandle.Reset();
	QuantumLeapCooldownTagChangedHandle.Reset();
	StealthCooldownTagChangedHandle.Reset();
}

void AShooterCharacter::HandleHealthChanged(const FOnAttributeChangeData& ChangeData)
{
	OnShooterHealthChanged.Broadcast(ChangeData.NewValue, GetMaxHealth());
	OnShooterConditionChanged.Broadcast(ResolveShooterConditionTag());
	if (HasAuthority() && bApplyingGameplayDamage && ChangeData.NewValue < ChangeData.OldValue)
	{
		EndActiveStealth(true);
	}

	if (HasAuthority() && ChangeData.OldValue > 0.0f && ChangeData.NewValue <= 0.0f)
	{
		Die();
	}
}

void AShooterCharacter::HandleMaxHealthChanged(const FOnAttributeChangeData& ChangeData)
{
	(void)ChangeData;
	OnShooterHealthChanged.Broadcast(GetCurHealth(), GetMaxHealth());
}

void AShooterCharacter::HandleShieldChanged(const FOnAttributeChangeData& ChangeData)
{
	if (HasAuthority() && bApplyingGameplayDamage && ChangeData.NewValue < ChangeData.OldValue)
	{
		EndActiveStealth(true);
	}
	OnShooterShieldChanged.Broadcast(ChangeData.NewValue, GetMaxShield());

	if (IsLocallyControlled())
	{
		if (UMaterialPostProcessSubsystem* PPS = GetWorld()->GetSubsystem<UMaterialPostProcessSubsystem>())
		{
			const float MaxValue = GetMaxShield();
			PPS->UpdateDamagedPostProcess(
				MaxValue > 0.0f ? ChangeData.NewValue / MaxValue : 0.0f,
				FVector4(0, 0, 1, 0));
			PPS->SetPostProcessEnabled(EOutlierPostProcessMaterialType::Damaged, true);
		}
	}

	OnShooterConditionChanged.Broadcast(ResolveShooterConditionTag());
}

void AShooterCharacter::HandleMaxShieldChanged(const FOnAttributeChangeData& ChangeData)
{
	(void)ChangeData;
	OnShooterShieldChanged.Broadcast(GetCurShield(), GetMaxShield());
}

void AShooterCharacter::HandlePartnerShieldChanged(const FOnAttributeChangeData& ChangeData)
{
	if (HasAuthority() && bApplyingGameplayDamage && ChangeData.NewValue < ChangeData.OldValue)
	{
		EndActiveStealth(true);
	}
	BroadcastPartnerShieldState();
}

void AShooterCharacter::HandleMaxPartnerShieldChanged(const FOnAttributeChangeData& ChangeData)
{
	(void)ChangeData;
	BroadcastPartnerShieldState();
}

void AShooterCharacter::HandleDeadTagChanged(const FGameplayTag Tag, int32 NewCount)
{
	(void)Tag;
	if (NewCount > 0)
	{
		CancelActiveQuantumLeap(false);
		EndActiveStealth(false);
		HandleDeath();
	}
}

void AShooterCharacter::HandleStealthTagChanged(const FGameplayTag Tag, int32 NewCount)
{
	(void)Tag;
	SetStealthVisualEnabled(NewCount > 0);
}

void AShooterCharacter::HandleStealthCooldownTagChanged(const FGameplayTag Tag, int32 NewCount)
{
	(void)Tag;
	if (NewCount <= 0 || !IsLocallyControlled())
	{
		return;
	}
	RefreshShooterSuitCooldownUI();
}

void AShooterCharacter::HandleQuantumLeapCooldownTagChanged(const FGameplayTag Tag, int32 NewCount)
{
	(void)Tag;
	if (NewCount > 0 && IsLocallyControlled())
	{
		RefreshShooterSuitCooldownUI();
	}
}

void AShooterCharacter::RefreshShooterSuitCooldownUI()
{
	if (!IsLocallyControlled() || !OutlierAbilitySystemComponent)
	{
		return;
	}

	AShooterPlayerController* ShooterController = Cast<AShooterPlayerController>(GetController());
	if (!ShooterController)
	{
		return;
	}
	ULocalPlayerUISubSystem* UISubsystem = nullptr;
	if (ULocalPlayer* LocalPlayer = ShooterController->GetLocalPlayer())
	{
		UISubsystem = LocalPlayer->GetSubsystem<ULocalPlayerUISubSystem>();
	}
	const auto ApplyCooldown = [ShooterController, UISubsystem](
		const FGameplayTag& AbilityTag,
		float Remaining)
	{
		if (Remaining <= 0.0f)
		{
			return;
		}
		if (ShooterController->AbilityUIInstance)
		{
			ShooterController->AbilityUIInstance->ApplyCooldownIfMatches(AbilityTag, Remaining);
		}
		if (UISubsystem)
		{
			UISubsystem->OnAbilityUsed(AbilityTag, Remaining);
		}
	};
	ApplyCooldown(
		OutlierGameplayTags::Ability::Shooter::QuantumLeap(),
		OutlierAbilitySystemComponent->GetShooterQuantumLeapCooldownRemaining());
	ApplyCooldown(
		OutlierGameplayTags::Ability::Shooter::Stealth(),
		OutlierAbilitySystemComponent->GetShooterStealthCooldownRemaining());
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
	}

	RefreshMovementState();
}

void AShooterCharacter::OnMovementModeChanged(EMovementMode  PrevMovementMode, uint8 PreviousCustomMode)
{
	Super::OnMovementModeChanged(PrevMovementMode, PreviousCustomMode);

	if (IsSliding() && GetCharacterMovement()->IsFalling())
	{
		StopSlide(ESlideEndReason::FallCancel);
	}

	if (GetCharacterMovement()->IsFalling() && CombatComponent)
	{
		CombatComponent->SuspendAimInternal();
	}

	RefreshMovementState();
	RefreshCombatState();

	if (!GetCharacterMovement()->IsFalling()
		&& PrevMovementMode == MOVE_Falling
		&& CombatComponent)
	{
		CombatComponent->RestoreAimIfRequested();
	}
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
	if (IsDead())
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

	UShooterAbilityUI* AbilityUI = ShooterController->AbilityUIInstance;
	AbilityUI->TryHovering();

	// 휠을 닫는 한 프레임에만 선택을 계산하면 위젯 Geometry가 사라져 기본 Stealth가 남을 수 있다.
	// 유효한 영역을 가리키는 동안 선택 태그를 보존하되, 매 프레임 UI 선택 이벤트는 방송하지 않는다.
	FGameplayTag HoveredAbilityTag;
	if (AbilityUI->TryGetHoveredAbility(HoveredAbilityTag, false)
		&& HoveredAbilityTag != SelectedAbilityTag)
	{
		UE_LOG(
			LogOutlier,
			Warning,
			TEXT("[GAS.ShooterSuit.Trace][UI] HoverSelection Shooter=%s Previous=%s New=%s"),
			*GetName(),
			*SelectedAbilityTag.ToString(),
			*HoveredAbilityTag.ToString());
		SelectedAbilityTag = HoveredAbilityTag;
	}

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
		UShooterAbilityUI* AbilityUI = ShooterController->AbilityUIInstance;
		// Collapsed 이후에는 CachedGeometry를 신뢰할 수 없으므로 반드시 위젯을 숨기기 전에 최종 선택을 확정한다.
		FGameplayTag FinalAbilityTag;
		if (AbilityUI->TryGetHoveredAbility(FinalAbilityTag))
		{
			UE_LOG(
				LogOutlier,
				Warning,
				TEXT("[GAS.ShooterSuit.Trace][UI] SelectionCommitted Shooter=%s Previous=%s Final=%s"),
				*GetName(),
				*SelectedAbilityTag.ToString(),
				*FinalAbilityTag.ToString());
			SelectedAbilityTag = FinalAbilityTag;
		}
		AbilityUI->SetVisibility(ESlateVisibility::Collapsed);
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
	if (IsDead() || bSuitDisabledByPartnerBoundary || !SelectedAbilityTag.IsValid())
	{
		return;
	}
	if (HasAuthority())
	{
		OutlierAbilitySystemComponent->TryActivateShooterSuitAbility(SelectedAbilityTag);
	}
	else
	{
		ServerUseSuitAbility(SelectedAbilityTag);
	}
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

void AShooterCharacter::ServerUseSuitAbility_Implementation(FGameplayTag AbilityTag)
{
	if (!IsDead() && !bSuitDisabledByPartnerBoundary && OutlierAbilitySystemComponent)
	{
		OutlierAbilitySystemComponent->TryActivateShooterSuitAbility(AbilityTag);
	}
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

void AShooterCharacter::OnRep_MovementState()
{
	OnMovementStateChanged.Broadcast(MovementState);
}

void AShooterCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AShooterCharacter, MovementState);
	DOREPLIFETIME(AShooterCharacter, WeaponMode);
	DOREPLIFETIME(AShooterCharacter, CombatState);
	DOREPLIFETIME(AShooterCharacter, ActionLock);
}

FGameplayTagContainer AShooterCharacter::GetOwnedGameplayTagsForQuery() const
{
	FGameplayTagContainer GameplayTags = Super::GetOwnedGameplayTagsForQuery();
	if (OutlierAbilitySystemComponent)
	{
		FGameplayTagContainer AbilitySystemTags;
		OutlierAbilitySystemComponent->GetOwnedGameplayTags(AbilitySystemTags);
		GameplayTags.AppendTags(AbilitySystemTags);
	}
	return GameplayTags;
}

bool AShooterCharacter::IsStealthed() const
{
	return OutlierAbilitySystemComponent
		&& OutlierAbilitySystemComponent->HasMatchingGameplayTag(OutlierGameplayTags::State::Stealthed());
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
	return !IsDead();
}

bool AShooterCharacter::CanLean() const
{
	return !IsDead()
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

bool AShooterCharacter::CanStartAction(EShooterActionLock NextLock) const
{
	const bool bCanOverrideSlideLock =
		ActionLock == EShooterActionLock::Slide &&
		(NextLock == EShooterActionLock::Reload || NextLock == EShooterActionLock::Equip);

	return !IsDead()
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

void AShooterCharacter::ApplyDamageInternal(
	float DamageAmount,
	AController* EventInstigator,
	AActor* DamageCauser,
	const FGameplayTag& DamageTag)
{
	if (HealthComponent)
	{
		HealthComponent->ApplyDamage(DamageAmount, EventInstigator, DamageCauser, DamageTag);
	}
}

float AShooterCharacter::TakeDamage(
	float DamageAmount,
	FDamageEvent const& DamageEvent,
	AController* EventInstigator,
	AActor* DamageCauser)
{
	if (!HasAuthority() || DamageAmount <= 0.0f)
	{
		return 0.0f;
	}

	const float AppliedDamage = Super::TakeDamage(DamageAmount, DamageEvent, EventInstigator, DamageCauser);
	// 폭발 등 공통 피해를 기존 Shooter 실드 및 HP 처리로 전달한다.
	FGameplayTag DamageTag;
	if (DamageEvent.IsOfType(FOutlierTaggedDamageEvent::ClassID))
	{
		DamageTag = static_cast<const FOutlierTaggedDamageEvent&>(DamageEvent).DamageTag;
	}
	ApplyDamageInternal(AppliedDamage, EventInstigator, DamageCauser, DamageTag);
	return AppliedDamage;
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
		OutlierAbilitySystemComponent->ApplyPartnerShieldDeltaToSelf(
			-GetCurPartnerShield(),
			-GetMaxPartnerShield());
		GetWorldTimerManager().ClearTimer(PartnerShieldTimerHandle);
		return;
	}

	const float DecayAmount = (GetMaxPartnerShield() / PartnerShieldDuration) * DeltaTime;
	OutlierAbilitySystemComponent->ApplyPartnerShieldDeltaToSelf(-DecayAmount, 0.0f);

	if (GetCurPartnerShield() <= 0.0f)
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
	if (ARangedWeaponBase* RangedWeapon = Cast<ARangedWeaponBase>(CurrentWeapon))
	{
		RangedWeapon->CancelLocalRecoilPresentation();
	}

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

	if (GetCurShield() > 0.0f)
	{
		ConditionTag = TagDrivenUITags::Condition::Shooter::Shield();
	}

	if (GetCurPartnerShield() > 0.0f)
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
	OnShooterHealthChanged.Broadcast(GetCurHealth(), GetMaxHealth());
	OnShooterShieldChanged.Broadcast(GetCurShield(), GetMaxShield());
	OnWeaponChanged.Broadcast(GetWeaponType());
	BroadcastPartnerShieldState();
}

void AShooterCharacter::BroadcastPartnerShieldState()
{
	OnShooterPartnerShieldChanged.Broadcast(GetCurPartnerShield(), GetMaxPartnerShield());
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
	if (HasAuthority() && CachedPartnerCharacter && bShooterSuitDataInitialized)
	{
		CachedPartnerCharacter->ConfigureSuitDisableBoundaryRadius(ShooterSuitConfig.MaxPartnerDistance);
	}
}

void AShooterCharacter::SetSuitDisabledByPartnerBoundary(bool bDisabled)
{
	bSuitDisabledByPartnerBoundary = bDisabled;
	if (HasAuthority() && bDisabled)
	{
		CancelActiveQuantumLeap(true);
		EndActiveStealth(true);
	}

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

void AShooterCharacter::NotifyOffensiveActionExecuted()
{
	if (HasAuthority())
	{
		EndActiveStealth(true);
	}
}

void AShooterCharacter::NotifyStealthDetected()
{
	if (HasAuthority())
	{
		EndActiveStealth(true);
	}
}

bool AShooterCharacter::EndActiveStealth(bool bCommitCooldown)
{
	return HasAuthority()
		&& OutlierAbilitySystemComponent
		&& OutlierAbilitySystemComponent->EndActiveShooterStealth(bCommitCooldown);
}

bool AShooterCharacter::CancelActiveQuantumLeap(bool bCommitFailureCooldown)
{
	return HasAuthority()
		&& OutlierAbilitySystemComponent
		&& OutlierAbilitySystemComponent->CancelActiveShooterQuantumLeap(bCommitFailureCooldown);
}
void AShooterCharacter::ApplyPartnerShield(float Amount, float Duration)
{
	if (!HasAuthority() || !OutlierAbilitySystemComponent)
	{
		return;
	}

	const float ClampedAmount = FMath::Max(Amount, 0.0f);
	OutlierAbilitySystemComponent->ApplyPartnerShieldDeltaToSelf(
		ClampedAmount - GetCurPartnerShield(),
		ClampedAmount - GetMaxPartnerShield());
	PartnerShieldDuration = Duration;

	GetWorldTimerManager().SetTimer(
		PartnerShieldTimerHandle,
		this,
		&AShooterCharacter::UpdatePartnerShieldDecay,
		1.0f / 60.0f,
		true
	);
}
