// Copyright Epic Games, Inc. All Rights Reserved.

#include "ShooterCharacter.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Engine/LocalPlayer.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SphereComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Curves/CurveFloat.h"
#include "Engine/SkeletalMesh.h"
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
#include "Upgrade/OutlierUpgradeComponent.h"
#include "Net/UnrealNetwork.h"
#include "OutlierPlayerState.h"
#include "OutlierNetUtils.h"
#include "Outlier.h"
#include "UI/LocalPlayerUILayerSubsystem.h"
#include "UI/ShooterReflectionBarrier.h"
#include "UI/UILayerGameplayTags.h"

namespace
{
AActor* ResolveDamageSource(AController* EventInstigator, AActor* DamageCauser)
{
	APawn* InstigatorPawn = EventInstigator ? EventInstigator->GetPawn() : nullptr;
	AActor* CauserOwner = DamageCauser ? DamageCauser->GetOwner() : nullptr;
	if (IsValid(InstigatorPawn))
	{
		return InstigatorPawn;
	}
	if (IsValid(EventInstigator))
	{
		return EventInstigator;
	}
	if (IsValid(CauserOwner))
	{
		return CauserOwner;
	}
	return IsValid(DamageCauser) ? DamageCauser : nullptr;
}
}

FName AShooterCharacter::GetFirstPersonWeaponSocketByType(EWeaponType WeaponType) const
{
	return InventoryComponent ? InventoryComponent->GetFirstPersonWeaponSocketByType(WeaponType) : NAME_None;
}

FName AShooterCharacter::GetThirdPersonWeaponSocketByType(EWeaponType WeaponType) const
{
	return InventoryComponent ? InventoryComponent->GetThirdPersonWeaponSocketByType(WeaponType) : NAME_None;
}

UOutlierUpgradeComponent* AShooterCharacter::GetUpgradeComponent() const
{
	return UpgradeComponent;
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

	LeanExposureCollision = CreateDefaultSubobject<USphereComponent>(TEXT("LeanExposureCollision"));
	LeanExposureCollision->SetupAttachment(GetCapsuleComponent());
	LeanExposureCollision->InitSphereRadius(LeanCollisionRadius);
	LeanExposureCollision->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	LeanExposureCollision->SetCollisionResponseToAllChannels(ECR_Ignore);
	LeanExposureCollision->SetCollisionResponseToChannel(ECC_PhysicsBody, ECR_Block);
	LeanExposureCollision->SetGenerateOverlapEvents(false);
	LeanExposureCollision->SetCanEverAffectNavigation(false);

	
	HealthComponent = CreateDefaultSubobject<UShooterHealthComponent>(TEXT("HealthComponent"));
	InventoryComponent = CreateDefaultSubobject<UShooterInventoryComponent>(TEXT("InventoryComponent"));
	CombatComponent = CreateDefaultSubobject<UShooterCombatComponent>(TEXT("CombatComponent"));
	MovementComponent = CreateDefaultSubobject<UShooterMovementComponent>(TEXT("MovementComponent"));
	UpgradeComponent = CreateDefaultSubobject<UOutlierUpgradeComponent>(TEXT("UpgradeComponent"));
	if (UpgradeComponent)
	{
		UpgradeComponent->SetUpgradeRole(EOutlierUpgradeRole::Shooter);
	}

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
		BaseFirstPersonCameraRootLocation = CameraRoot->GetRelativeLocation();
		BaseFirstPersonCameraRootRotation = CameraRoot->GetRelativeRotation();
	}
	if (LeanExposureCollision)
	{
		LeanExposureCollision->SetSphereRadius(LeanCollisionRadius, false);
		UpdateLeanExposureCollision();
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

	UE_LOG(
		LogTemp,
		Log,
		TEXT("%s [FPAnimDiag][FramingBefore] Owner=%s ViewRootRelLoc=%s ViewRootRelRot=%s Mesh=%s Asset=%s Skeleton=%s MeshRelLoc=%s MeshRelRot=%s MeshRelScale=%s"),
		OutlierNet::GetNetPrefix(this),
		*GetName(),
		GetFirstPersonViewModelRoot() ? *GetFirstPersonViewModelRoot()->GetRelativeLocation().ToCompactString() : TEXT("None"),
		GetFirstPersonViewModelRoot() ? *GetFirstPersonViewModelRoot()->GetRelativeRotation().ToCompactString() : TEXT("None"),
		*GetNameSafe(FirstPersonMesh),
		FirstPersonMesh ? *GetNameSafe(FirstPersonMesh->GetSkeletalMeshAsset()) : TEXT("None"),
		FirstPersonMesh && FirstPersonMesh->GetSkeletalMeshAsset() ? *GetNameSafe(FirstPersonMesh->GetSkeletalMeshAsset()->GetSkeleton()) : TEXT("None"),
		FirstPersonMesh ? *FirstPersonMesh->GetRelativeLocation().ToCompactString() : TEXT("None"),
		FirstPersonMesh ? *FirstPersonMesh->GetRelativeRotation().ToCompactString() : TEXT("None"),
		FirstPersonMesh ? *FirstPersonMesh->GetRelativeScale3D().ToCompactString() : TEXT("None"));

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

	UE_LOG(
		LogTemp,
		Log,
		TEXT("%s [FPAnimDiag][FramingAfter] Owner=%s ViewRootRelLoc=%s ViewRootRelRot=%s MeshRelLoc=%s MeshRelRot=%s MeshWorldLoc=%s"),
		OutlierNet::GetNetPrefix(this),
		*GetName(),
		GetFirstPersonViewModelRoot() ? *GetFirstPersonViewModelRoot()->GetRelativeLocation().ToCompactString() : TEXT("None"),
		GetFirstPersonViewModelRoot() ? *GetFirstPersonViewModelRoot()->GetRelativeRotation().ToCompactString() : TEXT("None"),
		FirstPersonMesh ? *FirstPersonMesh->GetRelativeLocation().ToCompactString() : TEXT("None"),
		FirstPersonMesh ? *FirstPersonMesh->GetRelativeRotation().ToCompactString() : TEXT("None"),
		FirstPersonMesh ? *FirstPersonMesh->GetComponentLocation().ToCompactString() : TEXT("None"));

	RefreshWeaponMode();
	RefreshMovementState();
	RefreshCombatState();
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
	PopReflectionBarrierWidget();
	UnbindPartnerSuitStateObserver();
	CancelActiveQuantumLeap();
	EndActiveBulletReflection(false);
	EndActiveWeaponOvercharge(false);
	EndActiveStealth(false);

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

	if (UpgradeComponent)
	{
		UpgradeComponent->SyncFromPlayerState();
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
	ShieldChangedHandle = OutlierAbilitySystemComponent->GetGameplayAttributeValueChangeDelegate(
		UOutlierShieldAttributeSet::GetShieldAttribute()).AddUObject(
			this, &AShooterCharacter::HandleShieldChanged);
	DeadTagChangedHandle = OutlierAbilitySystemComponent->RegisterGameplayTagEvent(
		OutlierGameplayTags::State::Dead()).AddUObject(
			this, &AShooterCharacter::HandleDeadTagChanged);
	BulletReflectionTagChangedHandle = OutlierAbilitySystemComponent->RegisterGameplayTagEvent(
		OutlierGameplayTags::State::BulletReflecting()).AddUObject(
			this, &AShooterCharacter::HandleBulletReflectionTagChanged);
	WeaponOverchargeTagChangedHandle = OutlierAbilitySystemComponent->RegisterGameplayTagEvent(
		OutlierGameplayTags::State::WeaponOvercharged()).AddUObject(
			this, &AShooterCharacter::HandleWeaponOverchargeTagChanged);
	QuantumLeapCooldownTagChangedHandle = OutlierAbilitySystemComponent->RegisterGameplayTagEvent(
		OutlierGameplayTags::Cooldown::Shooter::QuantumLeap()).AddUObject(
			this, &AShooterCharacter::HandleQuantumLeapCooldownTagChanged);
	BulletReflectionCooldownTagChangedHandle = OutlierAbilitySystemComponent->RegisterGameplayTagEvent(
		OutlierGameplayTags::Cooldown::Shooter::BulletReflection()).AddUObject(
			this, &AShooterCharacter::HandleBulletReflectionCooldownTagChanged);
	WeaponOverchargeCooldownTagChangedHandle = OutlierAbilitySystemComponent->RegisterGameplayTagEvent(
		OutlierGameplayTags::Cooldown::Shooter::WeaponOvercharge()).AddUObject(
			this, &AShooterCharacter::HandleWeaponOverchargeCooldownTagChanged);
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
		UOutlierShieldAttributeSet::GetShieldAttribute()).Remove(ShieldChangedHandle);
	OutlierAbilitySystemComponent->RegisterGameplayTagEvent(
		OutlierGameplayTags::State::Dead()).Remove(DeadTagChangedHandle);
	OutlierAbilitySystemComponent->RegisterGameplayTagEvent(
		OutlierGameplayTags::State::BulletReflecting()).Remove(BulletReflectionTagChangedHandle);
	OutlierAbilitySystemComponent->RegisterGameplayTagEvent(
		OutlierGameplayTags::State::WeaponOvercharged()).Remove(WeaponOverchargeTagChangedHandle);
	OutlierAbilitySystemComponent->RegisterGameplayTagEvent(
		OutlierGameplayTags::Cooldown::Shooter::QuantumLeap()).Remove(QuantumLeapCooldownTagChangedHandle);
	OutlierAbilitySystemComponent->RegisterGameplayTagEvent(
		OutlierGameplayTags::Cooldown::Shooter::BulletReflection()).Remove(BulletReflectionCooldownTagChangedHandle);
	OutlierAbilitySystemComponent->RegisterGameplayTagEvent(
		OutlierGameplayTags::Cooldown::Shooter::WeaponOvercharge()).Remove(WeaponOverchargeCooldownTagChangedHandle);
	OutlierAbilitySystemComponent->RegisterGameplayTagEvent(
		OutlierGameplayTags::Cooldown::Shooter::Stealth()).Remove(StealthCooldownTagChangedHandle);

	HealthChangedHandle.Reset();
	ShieldChangedHandle.Reset();
	DeadTagChangedHandle.Reset();
	BulletReflectionTagChangedHandle.Reset();
	WeaponOverchargeTagChangedHandle.Reset();
	QuantumLeapCooldownTagChangedHandle.Reset();
	BulletReflectionCooldownTagChangedHandle.Reset();
	WeaponOverchargeCooldownTagChangedHandle.Reset();
	StealthCooldownTagChangedHandle.Reset();
}

void AShooterCharacter::HandleHealthChanged(const FOnAttributeChangeData& ChangeData)
{
	if (HasAuthority() && ChangeData.OldValue > 0.0f && ChangeData.NewValue <= 0.0f)
	{
		Die();
	}
}

void AShooterCharacter::HandleShieldChanged(const FOnAttributeChangeData& ChangeData)
{
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

}

void AShooterCharacter::HandleDeadTagChanged(const FGameplayTag Tag, int32 NewCount)
{
	(void)Tag;
	if (NewCount > 0)
	{
		CancelActiveQuantumLeap(false);
		EndActiveBulletReflection(false);
		EndActiveWeaponOvercharge(false);
		EndActiveStealth(false);
		HandleDeath();
	}
}

void AShooterCharacter::HandleBulletReflectionTagChanged(const FGameplayTag Tag, int32 NewCount)
{
	(void)Tag;
	const bool bReflectionActive = NewCount > 0;
	BP_OnBulletReflectionStateChanged(bReflectionActive);

	if (!IsLocallyControlled())
	{
		return;
	}

	if (bReflectionActive)
	{
		PushReflectionBarrierWidget();
	}
	else
	{
		PopReflectionBarrierWidget();
	}
}

void AShooterCharacter::PushReflectionBarrierWidget()
{
	if (ReflectionBarrierLayerHandle.IsValid() || !ReflectionBarrierWidgetClass)
	{
		return;
	}

	APlayerController* PlayerController = Cast<APlayerController>(GetController());
	ULocalPlayer* LocalPlayer = PlayerController ? PlayerController->GetLocalPlayer() : nullptr;
	ULocalPlayerUILayerSubsystem* LayerSubsystem = LocalPlayer
		? LocalPlayer->GetSubsystem<ULocalPlayerUILayerSubsystem>()
		: nullptr;
	if (!LayerSubsystem)
	{
		return;
	}

	UShooterReflectionBarrier* BarrierWidget = CreateWidget<UShooterReflectionBarrier>(
		PlayerController,
		ReflectionBarrierWidgetClass);
	if (!BarrierWidget)
	{
		return;
	}
	ReflectionBarrierWidgetInstance = BarrierWidget;

	ReflectionBarrierLayerHandle = LayerSubsystem->PushWidget(
		UILayerTags::Gameplay(),
		BarrierWidget,
		FirstPersonInputModeTags::UI(),
		this,
		EUILayerFocusTarget::None,
		false,
		false);
	if (!ReflectionBarrierLayerHandle.IsValid())
	{
		ReflectionBarrierWidgetInstance = nullptr;
		return;
	}

	if (bHasPendingReflectionVisual)
	{
		ReflectionBarrierWidgetInstance->PlayHitRipple(PendingReflectionVisualOrigin);
		bHasPendingReflectionVisual = false;
	}
}

void AShooterCharacter::PopReflectionBarrierWidget()
{
	if (!ReflectionBarrierLayerHandle.IsValid())
	{
		return;
	}

	APlayerController* PlayerController = Cast<APlayerController>(GetController());
	ULocalPlayer* LocalPlayer = PlayerController ? PlayerController->GetLocalPlayer() : nullptr;
	if (ULocalPlayerUILayerSubsystem* LayerSubsystem = LocalPlayer
		? LocalPlayer->GetSubsystem<ULocalPlayerUILayerSubsystem>()
		: nullptr)
	{
		LayerSubsystem->PopLayer(ReflectionBarrierLayerHandle);
	}

	ReflectionBarrierLayerHandle.Reset();
	ReflectionBarrierWidgetInstance = nullptr;
	bHasPendingReflectionVisual = false;
}

void AShooterCharacter::NotifyLocalBulletReflected(const FVector& IncomingOrigin)
{
	if (!IsLocallyControlled())
	{
		return;
	}

	if (IsValid(ReflectionBarrierWidgetInstance))
	{
		ReflectionBarrierWidgetInstance->PlayHitRipple(IncomingOrigin);
		return;
	}

	// The cosmetic multicast can arrive before the replicated reflection tag
	// creates the local barrier widget. Preserve the latest hit for that frame.
	PendingReflectionVisualOrigin = IncomingOrigin;
	bHasPendingReflectionVisual = true;
}

void AShooterCharacter::HandleWeaponOverchargeTagChanged(const FGameplayTag Tag, int32 NewCount)
{
	(void)Tag;
	const bool bOverchargeActive = NewCount > 0;
	if (IsLocallyControlled())
	{
		if (const APlayerController* PlayerController = Cast<APlayerController>(GetController()))
		{
			if (ULocalPlayer* LocalPlayer = PlayerController->GetLocalPlayer())
			{
				if (ULocalPlayerPostProcessSubsystem* PPSubsystem =
					LocalPlayer->GetSubsystem<ULocalPlayerPostProcessSubsystem>())
				{
					PPSubsystem->SetOverlayEnabled(bOverchargeActive);
				}
			}
		}
	}

	BP_OnWeaponOverchargeStateChanged(bOverchargeActive);
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

void AShooterCharacter::HandleBulletReflectionCooldownTagChanged(const FGameplayTag Tag, int32 NewCount)
{
	(void)Tag;
	if (NewCount > 0 && IsLocallyControlled())
	{
		RefreshShooterSuitCooldownUI();
	}
}

void AShooterCharacter::HandleWeaponOverchargeCooldownTagChanged(const FGameplayTag Tag, int32 NewCount)
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
		OutlierGameplayTags::Ability::Shooter::BulletReflection(),
		OutlierAbilitySystemComponent->GetShooterBulletReflectionCooldownRemaining());
	ApplyCooldown(
		OutlierGameplayTags::Ability::Shooter::WeaponOvercharge(),
		OutlierAbilitySystemComponent->GetShooterWeaponOverchargeCooldownRemaining());
	ApplyCooldown(
		OutlierGameplayTags::Ability::Shooter::Stealth(),
		OutlierAbilitySystemComponent->GetShooterStealthCooldownRemaining());
}

void AShooterCharacter::RefreshShooterSuitUI()
{
	if (!IsLocallyControlled())
	{
		return;
	}

	if (AShooterPlayerController* ShooterController = Cast<AShooterPlayerController>(GetController()))
	{
		if (ULocalPlayerUISubSystem* UISubsystem = ShooterController->GetLocalPlayer()
			? ShooterController->GetLocalPlayer()->GetSubsystem<ULocalPlayerUISubSystem>()
			: nullptr)
		{
			UISubsystem->OnCurrentAbilityChanged(SelectedAbilityTag);
		}
	}

	RefreshShooterSuitAvailabilityUI();
	RefreshShooterSuitCooldownUI();
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
	const bool bCancellingActiveStealth = IsStealthed()
		&& SelectedAbilityTag.MatchesTagExact(OutlierGameplayTags::Ability::Shooter::Stealth());
	if (IsDead()
		|| !SelectedAbilityTag.IsValid()
		|| (IsShooterSuitUseDisabled() && !bCancellingActiveStealth))
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

void AShooterCharacter::ServerUseSuitAbility_Implementation(FGameplayTag AbilityTag)
{
	const bool bCancellingActiveStealth = IsStealthed()
		&& AbilityTag.MatchesTagExact(OutlierGameplayTags::Ability::Shooter::Stealth());
	if (!IsDead()
		&& (!IsShooterSuitUseDisabled() || bCancellingActiveStealth)
		&& OutlierAbilitySystemComponent)
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
	SetLeanTarget(Value.Get<float>(), true);
}

void AShooterCharacter::StopLean()
{
	SetLeanTarget(0.0f, true);
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

void AShooterCharacter::OnRep_CurrentLeanAlpha()
{
	ApplyLeanPresentation();
}

void AShooterCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AShooterCharacter, MovementState);
	DOREPLIFETIME(AShooterCharacter, WeaponMode);
	DOREPLIFETIME(AShooterCharacter, CombatState);
	DOREPLIFETIME(AShooterCharacter, ActionLock);
	DOREPLIFETIME(AShooterCharacter, CurrentLeanAlpha);
}

FVector AShooterCharacter::GetBaseLeanViewLocation() const
{
	if (!HasActorBegunPlay())
	{
		return Super::GetPawnViewLocation();
	}

	return GetActorTransform().TransformPosition(BaseFirstPersonCameraRootLocation);
}

FVector AShooterCharacter::GetLeanViewOffsetWorld(float LeanAlpha) const
{
	const float ClampedLeanAlpha = FMath::Clamp(LeanAlpha, -1.0f, 1.0f);
	const float LeanAbs = FMath::Abs(ClampedLeanAlpha);
	if (LeanAbs <= KINDA_SMALL_NUMBER)
	{
		return FVector::ZeroVector;
	}

	const FRotator ViewYawRotation(0.0f, GetControlRotation().Yaw, 0.0f);
	const FVector ViewForward = ViewYawRotation.Vector();
	const FVector ViewRight = FRotationMatrix(ViewYawRotation).GetScaledAxis(EAxis::Y);
	const float EasedLeanAbs = FMath::InterpEaseInOut(0.0f, 1.0f, LeanAbs, 2.0f);

	return
		(ViewRight * ClampedLeanAlpha * LeanCameraSideOffset) -
		(FVector::UpVector * EasedLeanAbs * LeanCameraDownOffset) -
		(ViewForward * EasedLeanAbs * LeanCameraBackwardOffset);
}

FVector AShooterCharacter::GetPawnViewLocation() const
{
	return GetBaseLeanViewLocation() + GetLeanViewOffsetWorld(CurrentLeanAlpha);
}

UAISense_Sight::EVisibilityResult AShooterCharacter::CanBeSeenFrom(
	const FCanBeSeenFromContext& Context,
	FVector& OutSeenLocation,
	int32& OutNumberOfLoSChecksPerformed,
	int32& OutNumberOfAsyncLosCheckRequested,
	float& OutSightStrength,
	int32* UserData,
	const FOnPendingVisibilityQueryProcessedDelegate* Delegate)
{
	OutNumberOfAsyncLosCheckRequested = 0;
	OutSightStrength = 0.0f;

	UWorld* World = GetWorld();
	if (!World)
	{
		return UAISense_Sight::EVisibilityResult::NotVisible;
	}

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(ShooterLeanAISight), true, Context.IgnoreActor);
	if (Context.IgnoreActor)
	{
		QueryParams.AddIgnoredActor(Context.IgnoreActor);
	}

	const auto IsVisibleAt = [&](const FVector& TargetLocation)
	{
		FHitResult Hit;
		++OutNumberOfLoSChecksPerformed;
		const bool bBlocked = World->LineTraceSingleByChannel(
			Hit,
			Context.ObserverLocation,
			TargetLocation,
			ECC_Visibility,
			QueryParams);
		if (!bBlocked || Hit.GetActor() == this)
		{
			OutSeenLocation = TargetLocation;
			OutSightStrength = 1.0f;
			return true;
		}
		return false;
	};

	if (FMath::Abs(CurrentLeanAlpha) >= LeanExposureCollisionMinAlpha && IsVisibleAt(GetPawnViewLocation()))
	{
		return UAISense_Sight::EVisibilityResult::Visible;
	}
	if (IsVisibleAt(GetActorLocation()))
	{
		return UAISense_Sight::EVisibilityResult::Visible;
	}

	return UAISense_Sight::EVisibilityResult::NotVisible;
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

bool AShooterCharacter::ApplyDamageInternal(
	float DamageAmount,
	AController* EventInstigator,
	AActor* DamageCauser,
	const FGameplayTag& DamageTag)
{
	return HealthComponent
		&& HealthComponent->ApplyDamage(DamageAmount, EventInstigator, DamageCauser, DamageTag);
}

float AShooterCharacter::ReceiveOutlierDamage(const FOutlierDamageRequest& Request)
{
	if (!HasAuthority() || !CanBeDamaged() || Request.DamageAmount <= 0.0f)
	{
		return 0.0f;
	}
	if (TryReflectIncomingDamage(Request))
	{
		return 0.0f;
	}

	return ApplyDamageInternal(
		Request.DamageAmount,
		Request.EventInstigator,
		Request.DamageCauser,
		Request.DamageTag)
		? Request.DamageAmount : 0.0f;
}

bool AShooterCharacter::TryReflectIncomingDamage(const FOutlierDamageRequest& Request)
{
	if (!OutlierAbilitySystemComponent
		|| !GetWorld()
		|| !OutlierAbilitySystemComponent->HasMatchingGameplayTag(
			OutlierGameplayTags::State::BulletReflecting()))
	{
		return false;
	}
	if (Request.bReflectedDamage)
	{
		return false;
	}
	if (!Request.DamageTag.MatchesTagExact(OutlierGameplayTags::Damage::Weapon())
		&& !Request.DamageTag.MatchesTagExact(OutlierGameplayTags::Damage::Explosion()))
	{
		return false;
	}

	AActor* DamageSource = ResolveDamageSource(Request.EventInstigator, Request.DamageCauser);
	if (!DamageSource)
	{
		return false;
	}

	const float ReflectionRadius = ShooterSuitConfig.BulletReflection.ReflectionRadius;
	const FVector ReflectionEnd = Request.DamageOrigin;
	if (ReflectionRadius <= 0.0f
		|| FVector::DistSquared(GetActorLocation(), ReflectionEnd) > FMath::Square(ReflectionRadius))
	{
		return false;
	}

	FVector ReflectionStart = Request.HitResult.ImpactPoint;
	if (ReflectionStart.IsNearlyZero())
	{
		ReflectionStart = GetActorLocation();
	}
	if (ReflectionStart.Equals(ReflectionEnd, KINDA_SMALL_NUMBER))
	{
		return false;
	}

	FCollisionQueryParams Params(SCENE_QUERY_STAT(ShooterBulletReflection), false, this);
	if (CurrentWeapon)
	{
		Params.AddIgnoredActor(CurrentWeapon);
	}
	FHitResult ReflectedHit;
	const bool bHit = GetWorld()->LineTraceSingleByChannel(
		ReflectedHit,
		ReflectionStart,
		ReflectionEnd,
		ECC_PhysicsBody,
		Params);
	AActor* ReflectedTarget = bHit ? ReflectedHit.GetActor() : nullptr;
	if (!ReflectedTarget && DamageSource->GetActorLocation().Equals(ReflectionEnd, 100.0f))
	{
		ReflectedTarget = DamageSource;
		ReflectedHit.HitObjectHandle = FActorInstanceHandle(DamageSource);
		ReflectedHit.bBlockingHit = true;
		ReflectedHit.Location = ReflectionEnd;
		ReflectedHit.ImpactPoint = ReflectionEnd;
	}

	if (ReflectedTarget && ReflectedTarget != this)
	{
		FOutlierDamageRequest ReflectedRequest;
		const float ReflectDamageMult = OutlierAbilitySystemComponent
			->GetShooterSuitConfig().BulletReflection.ReflectDamageMult;
		ReflectedRequest.DamageAmount = Request.DamageAmount * ReflectDamageMult;
		ReflectedRequest.DamageTag = Request.DamageTag;
		ReflectedRequest.HitResult = ReflectedHit;
		ReflectedRequest.DamageOrigin = ReflectionStart;
		ReflectedRequest.bReflectedDamage = true;
		ReflectedRequest.EventInstigator = GetController();
		ReflectedRequest.DamageCauser = CurrentWeapon ? static_cast<AActor*>(CurrentWeapon) : this;
		OutlierDamage::Apply(ReflectedTarget, ReflectedRequest);
	}
	ClientPlayReflectionRipple(Request.DamageOrigin);

	// 팀과 관계없이 유효한 공격은 중간 충돌체 유무와 관계없이 보호막에서 소거된다.
	return true;
}

void AShooterCharacter::ClientPlayReflectionRipple_Implementation(
	FVector_NetQuantize IncomingOrigin)
{
	NotifyLocalBulletReflected(IncomingOrigin);
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

void AShooterCharacter::SetLeanTarget(float NewLeanAlpha, bool bSendToServer)
{
	if (!FMath::IsFinite(NewLeanAlpha))
	{
		NewLeanAlpha = 0.0f;
	}

	float ClampedLeanAlpha = FMath::Clamp(NewLeanAlpha, -1.0f, 1.0f);
	if (FMath::Abs(ClampedLeanAlpha) <= KINDA_SMALL_NUMBER || !CanLean())
	{
		ClampedLeanAlpha = 0.0f;
	}

	if (FMath::IsNearlyEqual(TargetLeanAlpha, ClampedLeanAlpha, 0.01f))
	{
		return;
	}

	TargetLeanAlpha = ClampedLeanAlpha;
	StartLeanUpdate();

	if (bSendToServer && !HasAuthority())
	{
		ServerSetLeanTarget(ClampedLeanAlpha);
	}
}

float AShooterCharacter::ResolveLeanAlphaForCollision(float DesiredLeanAlpha) const
{
	const FVector TraceStart = GetBaseLeanViewLocation();
	const FVector DesiredOffset = GetLeanViewOffsetWorld(DesiredLeanAlpha);
	if (DesiredOffset.IsNearlyZero() || LeanCollisionRadius <= KINDA_SMALL_NUMBER)
	{
		return DesiredLeanAlpha;
	}

	FHitResult Hit;
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(ShooterLeanCamera), false, this);
	QueryParams.AddIgnoredActor(this);
	const bool bHit = GetWorld() && GetWorld()->SweepSingleByChannel(
		Hit,
		TraceStart,
		TraceStart + DesiredOffset,
		FQuat::Identity,
		ECC_Visibility,
		FCollisionShape::MakeSphere(LeanCollisionRadius),
		QueryParams);
	if (!bHit)
	{
		return DesiredLeanAlpha;
	}

	return Hit.bStartPenetrating
		? 0.0f
		: DesiredLeanAlpha * FMath::Clamp(Hit.Time, 0.0f, 1.0f);
}

void AShooterCharacter::ApplyLeanPresentation()
{
	if (!IsLocallyControlled())
	{
		return;
	}

	if (USceneComponent* CameraRoot = GetFirstPersonCameraRoot())
	{
		const FVector LocalLeanOffset = GetActorTransform().InverseTransformVectorNoScale(
			GetLeanViewOffsetWorld(CurrentLeanAlpha));
		CameraRoot->SetRelativeLocation(BaseFirstPersonCameraRootLocation + LocalLeanOffset);
	}
}

void AShooterCharacter::UpdateLeanExposureCollision()
{
	if (!LeanExposureCollision || !HasAuthority())
	{
		return;
	}

	LeanExposureCollision->SetWorldLocation(GetPawnViewLocation());
	const bool bExposeLeanHead = !IsDead()
		&& FMath::Abs(CurrentLeanAlpha) >= LeanExposureCollisionMinAlpha;
	LeanExposureCollision->SetCollisionEnabled(
		bExposeLeanHead ? ECollisionEnabled::QueryOnly : ECollisionEnabled::NoCollision);
}

void AShooterCharacter::StopLeanUpdateIfSettled()
{
	if (FMath::IsNearlyEqual(CurrentLeanAlpha, TargetLeanAlpha, KINDA_SMALL_NUMBER))
	{
		CurrentLeanAlpha = TargetLeanAlpha;
		if (FMath::IsNearlyZero(TargetLeanAlpha, KINDA_SMALL_NUMBER))
		{
			GetWorldTimerManager().ClearTimer(LeanUpdateTimerHandle);
		}
	}
}

void AShooterCharacter::UpdateLeanStep()
{
	const float DesiredLeanAlpha = FMath::FInterpTo(
		CurrentLeanAlpha,
		TargetLeanAlpha,
		1.0f / 60.0f,
		LeanInterpSpeed);
	CurrentLeanAlpha = ResolveLeanAlphaForCollision(DesiredLeanAlpha);
	ApplyLeanPresentation();
	UpdateLeanExposureCollision();

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
		CameraRoot->SetRelativeLocation(BaseFirstPersonCameraRootLocation);
		CameraRoot->SetRelativeRotation(BaseFirstPersonCameraRootRotation);
	}
	UpdateLeanExposureCollision();

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

void AShooterCharacter::ServerSetLeanTarget_Implementation(float NewLeanAlpha)
{
	SetLeanTarget(NewLeanAlpha, false);
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
	if (CachedPartnerCharacter == NewPartner)
	{
		RefreshShooterSuitAvailabilityUI();
		return;
	}

	if (HasAuthority() && CachedPartnerCharacter && !NewPartner)
	{
		EndActiveWeaponOvercharge(false);
	}
	UnbindPartnerSuitStateObserver();
	CachedPartnerCharacter = NewPartner;
	BindPartnerSuitStateObserver();
	if (HasAuthority() && CachedPartnerCharacter && bShooterSuitDataInitialized)
	{
		CachedPartnerCharacter->ConfigureSuitDisableBoundaryRadius(ShooterSuitConfig.MaxPartnerDistance);
	}
	RefreshShooterSuitAvailabilityUI();
}

void AShooterCharacter::SetSuitDisabledByPartnerBoundary(bool bDisabled)
{
	bSuitDisabledByPartnerBoundary = bDisabled;
	if (HasAuthority() && bDisabled)
	{
		CancelActiveQuantumLeap(true);
		EndActiveBulletReflection(true);
		EndActiveWeaponOvercharge(true);
	}

	RefreshShooterSuitAvailabilityUI();
}

bool AShooterCharacter::IsShooterSuitUseDisabled() const
{
	const UAbilitySystemComponent* PartnerAbilitySystem = IsValid(CachedPartnerCharacter)
		? CachedPartnerCharacter->GetAbilitySystemComponent()
		: nullptr;
	return bSuitDisabledByPartnerBoundary
		|| (PartnerAbilitySystem
			&& PartnerAbilitySystem->HasMatchingGameplayTag(OutlierGameplayTags::State::Rebooting()));
}

void AShooterCharacter::BindPartnerSuitStateObserver()
{
	if (!IsValid(CachedPartnerCharacter) || PartnerRebootTagChangedHandle.IsValid())
	{
		return;
	}

	if (UAbilitySystemComponent* PartnerAbilitySystem = CachedPartnerCharacter->GetAbilitySystemComponent())
	{
		PartnerRebootTagChangedHandle = PartnerAbilitySystem->RegisterGameplayTagEvent(
			OutlierGameplayTags::State::Rebooting()).AddUObject(
				this, &AShooterCharacter::HandlePartnerRebootTagChanged);
	}
}

void AShooterCharacter::UnbindPartnerSuitStateObserver()
{
	if (PartnerRebootTagChangedHandle.IsValid() && IsValid(CachedPartnerCharacter))
	{
		if (UAbilitySystemComponent* PartnerAbilitySystem = CachedPartnerCharacter->GetAbilitySystemComponent())
		{
			PartnerAbilitySystem->RegisterGameplayTagEvent(
				OutlierGameplayTags::State::Rebooting()).Remove(PartnerRebootTagChangedHandle);
		}
	}
	PartnerRebootTagChangedHandle.Reset();
}

void AShooterCharacter::HandlePartnerRebootTagChanged(const FGameplayTag Tag, int32 NewCount)
{
	(void)Tag;
	(void)NewCount;
	RefreshShooterSuitAvailabilityUI();
}

void AShooterCharacter::RefreshShooterSuitAvailabilityUI()
{
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
		if (IsShooterSuitUseDisabled())
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
	// 감지는 더 이상 은신 해제 조건이 아니다. 기존 Blueprint 호출 호환성을 위해 진입점만 유지한다.
}

bool AShooterCharacter::EndActiveStealth(bool bCommitCooldown)
{
	return HasAuthority()
		&& OutlierAbilitySystemComponent
		&& OutlierAbilitySystemComponent->EndActiveShooterStealth(bCommitCooldown);
}

bool AShooterCharacter::EndActiveBulletReflection(bool bCommitCooldown)
{
	return HasAuthority()
		&& OutlierAbilitySystemComponent
		&& OutlierAbilitySystemComponent->EndActiveShooterBulletReflection(bCommitCooldown);
}

bool AShooterCharacter::EndActiveWeaponOvercharge(bool bCommitCooldown)
{
	return HasAuthority()
		&& OutlierAbilitySystemComponent
		&& OutlierAbilitySystemComponent->EndActiveShooterWeaponOvercharge(bCommitCooldown);
}

bool AShooterCharacter::CancelActiveQuantumLeap(bool bCommitFailureCooldown)
{
	return HasAuthority()
		&& OutlierAbilitySystemComponent
		&& OutlierAbilitySystemComponent->CancelActiveShooterQuantumLeap(bCommitFailureCooldown);
}

bool AShooterCharacter::IsBulletReflecting() const
{
	return OutlierAbilitySystemComponent
		&& OutlierAbilitySystemComponent->HasMatchingGameplayTag(
			OutlierGameplayTags::State::BulletReflecting());
}

bool AShooterCharacter::IsWeaponOvercharged() const
{
	return OutlierAbilitySystemComponent
		&& OutlierAbilitySystemComponent->HasMatchingGameplayTag(
			OutlierGameplayTags::State::WeaponOvercharged());
}

bool AShooterCharacter::BeginWeaponOvercharge()
{
	if (!HasAuthority() || GetWeaponMode() != EWeaponMode::Primary)
	{
		return false;
	}

	ARangedWeaponBase* RangedWeapon = Cast<ARangedWeaponBase>(GetCurrentWeapon());
	if (!RangedWeapon || !OutlierAbilitySystemComponent)
	{
		return false;
	}

	// 과충전은 재장전을 즉시 취소한 뒤 현재 주무기 탄창을 가득 채운 상태로 시작한다.
	CancelReloadInternal();
	RangedWeapon->CancelReload();
	RangedWeapon->CancelLocalRecoilPresentation();
	RangedWeapon->RefillMagazineForWeaponOvercharge();
	const float MissingShield = FMath::Max(GetMaxShield() - GetCurShield(), 0.0f);
	if (MissingShield > 0.0f)
	{
		OutlierAbilitySystemComponent->ApplyShieldRecoveryToSelf(MissingShield);
	}
	return true;
}

void AShooterCharacter::FinishWeaponOvercharge(float ShieldRecoveryDelay)
{
	if (HasAuthority() && HealthComponent)
	{
		HealthComponent->DelayShieldRecovery(ShieldRecoveryDelay);
	}
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
