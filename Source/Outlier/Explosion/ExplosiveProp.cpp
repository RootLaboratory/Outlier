#include "Explosion/ExplosiveProp.h"
#include "Level/Elevator/ElevatorShaftController.h"

#include "Components/CapsuleComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Explosion/ExplosionComponent.h"
#include "Enemy/SelfDestructDrone.h"
#include "GAS/Attributes/OutlierVitalAttributeSet.h"
#include "GAS/OutlierAbilitySystemComponent.h"
#include "GameplayTags/OutlierGameplayTags.h"
#include "Kismet/GameplayStatics.h"
#include "Net/UnrealNetwork.h"
#include "NiagaraFunctionLibrary.h"
#include "Outlier.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Save/OutlierSaveSubSystem.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

AExplosiveProp::AExplosiveProp()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	SetReplicateMovement(false);

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	ExplosiveMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ExplosiveMesh"));
	ExplosiveMesh->SetupAttachment(SceneRoot);
	ExplosiveMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	FirstPersonExplosiveMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("FirstPersonExplosiveMesh"));
	FirstPersonExplosiveMesh->SetupAttachment(SceneRoot);
	FirstPersonExplosiveMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	FirstPersonExplosiveMesh->SetOnlyOwnerSee(true);
	FirstPersonExplosiveMesh->SetVisibility(false, true);
	FirstPersonExplosiveMesh->FirstPersonPrimitiveType = EFirstPersonPrimitiveType::FirstPerson;

	HitCollision = CreateDefaultSubobject<UCapsuleComponent>(TEXT("HitCollision"));
	HitCollision->SetupAttachment(SceneRoot);
	HitCollision->InitCapsuleSize(30.0f, 50.0f);
	HitCollision->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	HitCollision->SetCollisionObjectType(ECC_WorldDynamic);
	HitCollision->SetCollisionResponseToAllChannels(ECR_Ignore);
	// 총기 판정 채널만 막고 Tick이나 상시 Overlap Event는 사용하지 않는다.
	HitCollision->SetCollisionResponseToChannel(ECC_PhysicsBody, ECR_Block);
	HitCollision->SetGenerateOverlapEvents(false);

	ExplosionComponent = CreateDefaultSubobject<UExplosionComponent>(TEXT("ExplosionComponent"));

	OutlierAbilitySystemComponent = CreateDefaultSubobject<UOutlierAbilitySystemComponent>(
		TEXT("AbilitySystemComponent"));
	OutlierAbilitySystemComponent->SetReplicationMode(EGameplayEffectReplicationMode::Minimal);
	VitalAttributeSet = CreateDefaultSubobject<UOutlierVitalAttributeSet>(TEXT("VitalAttributeSet"));

	// 원본 경고음의 펄스 시각으로 기본 커브를 만든다. BP에서는 같은 초 단위 축의 커브로 교체할 수 있다.
	FRichCurve* Curve = ChargeEmissiveCurve.GetRichCurve();
	Curve->AddKey(0.0f, 0.0f);
	const float PulseTimes[] = {0.03f, 0.67f, 1.17f, 1.59f, 2.01f, 2.34f, 2.65f,
		2.92f, 3.13f, 3.34f, 3.54f, 3.72f, 3.86f};
	for (const float Time : PulseTimes)
	{
		const float Progress = Time / 4.029208f;
		// 매 경고음 사이에는 완전히 꺼져야 다음 펄스의 대비가 유지된다.
		Curve->AddKey(FMath::Max(0.0f, Time - 0.015f), 0.0f);
		Curve->AddKey(Time, FMath::Lerp(0.35f, 1.0f, Progress));
		Curve->AddKey(Time + 0.08f, 0.0f);
	}
	Curve->AddKey(3.97f, 1.0f);
	Curve->AddKey(4.029208f, 1.0f);
}

UAbilitySystemComponent* AExplosiveProp::GetAbilitySystemComponent() const
{
	return OutlierAbilitySystemComponent;
}

void AExplosiveProp::BeginPlay()
{
	Super::BeginPlay();

	CachedOwningDrone = Cast<ASelfDestructDrone>(GetOwner());
	if (!CachedOwningDrone.IsValid())
	{
		CachedOwningDrone = Cast<ASelfDestructDrone>(GetAttachParentActor());
	}
	if (CachedOwningDrone.IsValid())
	{
		SetupMountedPresentation();
	}

	// 부착형은 외형과 HitBox만 공유하고 HP 및 폭발 책임은 소유 자폭 드론이 가진다.
	if (!CachedOwningDrone.IsValid() && OutlierAbilitySystemComponent)
	{
		OutlierAbilitySystemComponent->InitializeForActor(this);
		BindGasVitalityObservers();
	}
	if (HasAuthority() && !CachedOwningDrone.IsValid() && !InitializeFromDataTable())
	{
		UE_LOG(LogTemp, Error, TEXT("Explosive prop %s has an invalid ExplosivePropRow"), *GetName());
	}

	if (!CachedOwningDrone.IsValid() && ExplosionComponent)
	{
		ExplosionComponent->OnExplosionProcessed.AddDynamic(this, &AExplosiveProp::HandleExplosionProcessed);
	}

	// 배치 폭발물만 안정 ID로 저장한다. 드론의 부착 폭발물은 드론 대여 수명에서 다시 구성한다.
	if (HasAuthority() && !CachedOwningDrone.IsValid() && bSaveCheckpointState)
	{
		if (UOutlierSaveSubSystem* SaveSubsystem = GetGameInstance()
			? GetGameInstance()->GetSubsystem<UOutlierSaveSubSystem>()
			: nullptr)
		{
			bProgressIdRegistered = SaveSubsystem->RegisterWorldProgressId(
				EOutlierWorldProgressType::ExplodedProp,
				ExplosivePropId,
				this);
			if (bProgressIdRegistered
				&& SaveSubsystem->HasWorldProgress(
					EOutlierWorldProgressType::ExplodedProp,
					ExplosivePropId))
			{
				// 복원 시 폭발 피해와 연출을 재생하지 않고 완료된 외형만 적용한다.
				bExploded = true;
			}
		}
	}

	ApplyExplodedState();
}

void AExplosiveProp::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ResetChargeEmissive();
	if (bProgressIdRegistered)
	{
		if (UOutlierSaveSubSystem* SaveSubsystem = GetGameInstance()
			? GetGameInstance()->GetSubsystem<UOutlierSaveSubSystem>()
			: nullptr)
		{
			SaveSubsystem->UnregisterWorldProgressId(
				EOutlierWorldProgressType::ExplodedProp,
				ExplosivePropId,
				this);
		}
		bProgressIdRegistered = false;
	}

	if (OutlierAbilitySystemComponent)
	{
		UnbindGasVitalityObservers();
		OutlierAbilitySystemComponent->ClearForActor(this);
	}

	Super::EndPlay(EndPlayReason);
}

#if WITH_EDITOR
EDataValidationResult AExplosiveProp::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);
	if (bSaveCheckpointState && ExplosivePropId.IsNone())
	{
		Context.AddError(FText::FromString(
			TEXT("A checkpoint-persistent explosive prop requires an ExplosivePropId.")));
		return EDataValidationResult::Invalid;
	}

	return Result == EDataValidationResult::NotValidated
		? EDataValidationResult::Valid
		: Result;
}
#endif

void AExplosiveProp::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AExplosiveProp, bExploded);
	DOREPLIFETIME(AExplosiveProp, MountedSocketName);
}

void AExplosiveProp::InitializeMountedSocket(FName InMountedSocketName)
{
	if (HasAuthority())
	{
		MountedSocketName = InMountedSocketName;
	}
}

bool AExplosiveProp::IsMountedOnSelfDestructDrone() const
{
	// Deferred Spawn 직후처럼 BeginPlay 캐시가 아직 준비되지 않은 시점에도 스폰 Owner로 판별한다.
	return CachedOwningDrone.IsValid()
		|| Cast<ASelfDestructDrone>(GetOwner()) != nullptr
		|| Cast<ASelfDestructDrone>(GetAttachParentActor()) != nullptr;
}

bool AExplosiveProp::RegisterElevatorTransport(AElevatorShaftController* Controller)
{
	if (!HasAuthority() || !IsValid(Controller) || Controller->GetWorld() != GetWorld()
		|| (TransportController.IsValid() && TransportController.Get() != Controller))
	{
		return false;
	}
	TransportController = Controller;
	PrepareTransportMobility();
	FlushNetDormancy();
	SetReplicateMovement(true);
	ForceNetUpdate();
	return true;
}

void AExplosiveProp::UnregisterElevatorTransport(const AElevatorShaftController* Controller)
{
	if (TransportController.Get() == Controller) TransportController.Reset();
}

void AExplosiveProp::PrepareTransportMobility()
{
	TInlineComponentArray<USceneComponent*> Components;
	GetComponents(Components, false);
	for (USceneComponent* Component : Components)
	{
		if (Component->Mobility != EComponentMobility::Movable)
		{
			Component->SetMobility(EComponentMobility::Movable);
		}
	}
}

void AExplosiveProp::OnRep_ReplicatedMovement()
{
	// 레벨 BP에 Static이 저장돼 있어도 서버 위치를 적용하기 전에 클라이언트도 이동 가능하게 한다.
	if (IsReplicatingMovement()) PrepareTransportMobility();
	Super::OnRep_ReplicatedMovement();
}

float AExplosiveProp::GetCurrentHP() const
{
	if (CachedOwningDrone.IsValid())
	{
		return CachedOwningDrone->GetCurrentHealth();
	}
	return VitalAttributeSet ? VitalAttributeSet->GetHealth() : 0.0f;
}

void AExplosiveProp::SetupMountedPresentation()
{
	if (!CachedOwningDrone.IsValid()) CachedOwningDrone = Cast<ASelfDestructDrone>(GetOwner());
	ASelfDestructDrone* OwningDrone = CachedOwningDrone.Get();
	if (!OwningDrone || MountedSocketName.IsNone())
	{
		return;
	}

	USkeletalMeshComponent* ThirdPersonMesh = OwningDrone->GetMesh();
	if (ThirdPersonMesh && ThirdPersonMesh->DoesSocketExist(MountedSocketName))
	{
		AttachToComponent(
			ThirdPersonMesh,
			FAttachmentTransformRules::SnapToTargetNotIncludingScale,
			MountedSocketName);
		ExplosiveMesh->SetOwnerNoSee(true);
	}
	InitializeChargeEmissive();

	USkeletalMeshComponent* FirstPersonMesh = OwningDrone->GetFirstPersonMesh();
	if (!FirstPersonMesh
		|| !FirstPersonMesh->DoesSocketExist(MountedSocketName)
		|| !FirstPersonExplosiveMesh)
	{
		if (HasAuthority())
		{
			UE_LOG(
				LogOutlier,
				Warning,
				TEXT("[ExplosiveProp] Matching first-person socket is missing. Actor=%s Socket=%s"),
				*GetNameSafe(this),
				*MountedSocketName.ToString());
		}
		return;
	}

	FirstPersonExplosiveMesh->SetStaticMesh(ExplosiveMesh->GetStaticMesh());
	for (int32 MaterialIndex = 0; MaterialIndex < ExplosiveMesh->GetNumMaterials(); ++MaterialIndex)
	{
		FirstPersonExplosiveMesh->SetMaterial(
			MaterialIndex,
			ExplosiveMesh->GetMaterial(MaterialIndex));
	}
	FirstPersonExplosiveMesh->AttachToComponent(
		FirstPersonMesh,
		FAttachmentTransformRules::SnapToTargetNotIncludingScale,
		MountedSocketName);
	FirstPersonExplosiveMesh->SetRelativeTransform(ExplosiveMesh->GetRelativeTransform());
	FirstPersonExplosiveMesh->SetVisibility(!bExploded, true);
	// 부착과 복제 상태 도착 순서가 달라도 현재 사운드 위치의 밝기로 합류한다.
	OwningDrone->RefreshChargePresentation();
}

void AExplosiveProp::OnRep_Owner()
{
	Super::OnRep_Owner();
	CachedOwningDrone = Cast<ASelfDestructDrone>(GetOwner());
	if (CachedOwningDrone.IsValid()) SetupMountedPresentation();
}

void AExplosiveProp::OnRep_MountedSocketName()
{
	SetupMountedPresentation();
}

void AExplosiveProp::InitializeChargeEmissive()
{
	if (bChargeMaterialInitialized || GetNetMode() == NM_DedicatedServer) return;
	bChargeMaterialInitialized = true;
	UMaterialInterface* Material = ExplosiveMesh && EmissiveMaterialIndex >= 0
		&& EmissiveMaterialIndex < ExplosiveMesh->GetNumMaterials()
		? ExplosiveMesh->GetMaterial(EmissiveMaterialIndex) : nullptr;
	const FRichCurve* Curve = ChargeEmissiveCurve.GetRichCurveConst();
	if (!Material || !Curve || Curve->IsEmpty() || EmissiveParameterName.IsNone()
		|| !Material->GetScalarParameterValue(FMaterialParameterInfo(EmissiveParameterName), IdleEmissiveStrength)
		|| !FMath::IsFinite(IdleEmissiveStrength) || !FMath::IsFinite(PeakEmissiveStrength)
		|| PeakEmissiveStrength < IdleEmissiveStrength)
	{
		UE_LOG(LogOutlier, Warning, TEXT("[ExplosiveProp] Charge emissive curve/material/strength is invalid Actor=%s"), *GetName());
		return;
	}
	ChargeEmissiveMID = ExplosiveMesh->CreateDynamicMaterialInstance(EmissiveMaterialIndex);
	if (ChargeEmissiveMID && FirstPersonExplosiveMesh)
	{
		FirstPersonExplosiveMesh->SetMaterial(EmissiveMaterialIndex, ChargeEmissiveMID);
	}
}

void AExplosiveProp::ApplyChargeEmissive(float SoundTime, float ChargeElapsedTime)
{
	if (!ChargeEmissiveMID || !FMath::IsFinite(SoundTime) || !FMath::IsFinite(ChargeElapsedTime)) return;
	const float Alpha = ChargeEmissiveCurve.GetRichCurveConst()->Eval(SoundTime);
	if (!FMath::IsFinite(Alpha)) return;
	// 사운드가 중간부터 시작해도 최초 80ms는 기본 밝기 -> 0으로 내린다.
	// 이후 펄스는 원본 사운드 시간축을 그대로 사용하고 0 -> Peak -> 0으로 반복한다.
	constexpr float InitialFadeSeconds = 0.08f;
	const float Strength = ChargeElapsedTime < InitialFadeSeconds
		? FMath::Lerp(IdleEmissiveStrength, 0.0f, FMath::Clamp(ChargeElapsedTime / InitialFadeSeconds, 0.0f, 1.0f))
		: PeakEmissiveStrength * FMath::Clamp(Alpha, 0.0f, 1.0f);
	ChargeEmissiveMID->SetScalarParameterValue(EmissiveParameterName, Strength);
}

void AExplosiveProp::ResetChargeEmissive()
{
	if (ChargeEmissiveMID) ChargeEmissiveMID->SetScalarParameterValue(EmissiveParameterName, IdleEmissiveStrength);
}

float AExplosiveProp::ReceiveOutlierDamage(const FOutlierDamageRequest& Request)
{
	if (CachedOwningDrone.IsValid())
	{
		if (!HasAuthority()
			|| Request.DamageAmount <= 0.0f)
		{
			return 0.0f;
		}

		// 폭발 범위에는 드론과 부착물이 함께 잡히므로 무기 피격만 본체로 전달한다.
		if (!Request.DamageTag.MatchesTag(OutlierGameplayTags::Damage::Weapon()))
		{
			return 0.0f;
		}

		return OutlierDamage::Apply(CachedOwningDrone.Get(), Request);
	}

	if (!HasAuthority())
	{
		return 0.0f;
	}

	if (bExploded)
	{
		return 0.0f;
	}

	if (GetCurrentHP() <= 0.0f)
	{
		return 0.0f;
	}

	if (Request.DamageAmount <= 0.0f)
	{
		return 0.0f;
	}

	// 배치 폭발물은 일반 물리 충돌이 아니라 무기와 폭발 계열 피해에만 반응한다.
	const bool bAllowedDamage = Request.DamageTag.MatchesTag(OutlierGameplayTags::Damage::Weapon())
		|| Request.DamageTag.MatchesTag(OutlierGameplayTags::Damage::Explosion());
	if (!bAllowedDamage)
	{
		return 0.0f;
	}

	if (!CanBeDamaged())
	{
		return 0.0f;
	}

	PendingDamageInstigator = Request.EventInstigator;
	// 폭발 자체는 총알이 아니므로 플레이어가 어떤 수단으로 기폭했든 NonGun으로 귀속한다.
	// Ignore 입력은 AI와 환경 폭발이 플레이어 Stack에 관여하지 않도록 그대로 유지한다.
	PendingAdaptationDamageCategory =
		Request.AdaptationDamageCategory == EOutlierAdaptationDamageCategory::Ignore
		? EOutlierAdaptationDamageCategory::Ignore
		: EOutlierAdaptationDamageCategory::NonGun;
	// ApplyDamageToSelf의 Health 콜백이 동기 실행되므로 폭발 Queue가 위 귀속 정보를 복사한 뒤 초기화한다.
	const bool bDamageApplied = OutlierAbilitySystemComponent
		&& OutlierAbilitySystemComponent->ApplyDamageToSelf(
			Request.DamageAmount,
			Request.EventInstigator,
			Request.DamageCauser,
			Request.DamageTag);
	PendingDamageInstigator.Reset();
	PendingAdaptationDamageCategory = EOutlierAdaptationDamageCategory::Ignore;
	if (!bDamageApplied)
	{
		return 0.0f;
	}

	const float CurrentHP = GetCurrentHP();
	if (CurrentHP > 0.0f && RuntimePropRow.IsSet())
	{
		FVector ImpactPoint = GetActorLocation();
		if (Request.HitResult.bBlockingHit)
		{
			ImpactPoint = Request.HitResult.ImpactPoint;
		}

		MulticastPlayHitFeedback(
			ImpactPoint,
			HitFeedbackVFX.Get(),
			HitFeedbackSFX.Get(),
			RuntimePropRow->HitFlashDuration);
	}

	return Request.DamageAmount;
}

void AExplosiveProp::ResetToInitialState()
{
	// SaveGame 담당 시스템이 서버에서 호출할 복구 진입점이다.
	if (!HasAuthority() || CachedOwningDrone.IsValid() || !InitializeFromDataTable())
	{
		return;
	}

	bExploded = false;
	PendingDamageInstigator.Reset();
	PendingAdaptationDamageCategory = EOutlierAdaptationDamageCategory::Ignore;
	if (ExplosionComponent)
	{
		ExplosionComponent->ResetExplosion();
	}

	ApplyExplodedState();
	ForceNetUpdate();
}

bool AExplosiveProp::InitializeFromDataTable()
{
	if (!RuntimePropRow.IsSet())
	{
		const FExplosivePropRow* Row = ExplosivePropRow.GetRow<FExplosivePropRow>(TEXT("ExplosivePropRow"));
		if (!Row)
		{
			UE_LOG(
				LogOutlier,
				Error,
				TEXT("[ExplosiveProp] Failed to load prop row. Actor=%s DataTable=%s RowName=%s"),
				*GetNameSafe(this),
				*GetNameSafe(ExplosivePropRow.DataTable),
				*ExplosivePropRow.RowName.ToString());
			return false;
		}

		RuntimePropRow.Emplace(*Row);
	}

	return !HasAuthority()
		|| (OutlierAbilitySystemComponent
			&& OutlierAbilitySystemComponent->InitializeVitalityToSelf(RuntimePropRow->MaxHP));
}

void AExplosiveProp::BindGasVitalityObservers()
{
	if (!OutlierAbilitySystemComponent || HealthChangedHandle.IsValid())
	{
		return;
	}

	HealthChangedHandle = OutlierAbilitySystemComponent->GetGameplayAttributeValueChangeDelegate(
		UOutlierVitalAttributeSet::GetHealthAttribute()).AddUObject(
			this, &AExplosiveProp::HandleHealthChanged);
}

void AExplosiveProp::UnbindGasVitalityObservers()
{
	if (!OutlierAbilitySystemComponent || !HealthChangedHandle.IsValid())
	{
		return;
	}

	OutlierAbilitySystemComponent->GetGameplayAttributeValueChangeDelegate(
		UOutlierVitalAttributeSet::GetHealthAttribute()).Remove(HealthChangedHandle);
	HealthChangedHandle.Reset();
}

void AExplosiveProp::HandleHealthChanged(const FOnAttributeChangeData& ChangeData)
{
	if (!HasAuthority()
		|| bExploded
		|| ChangeData.OldValue <= 0.0f
		|| ChangeData.NewValue > 0.0f)
	{
		return;
	}

	if (ExplosionComponent)
	{
		// 배치 피봇은 통 바닥에 둘 수 있다. 피해와 연출은 피봇 대신 현재 메시 중앙에서 시작한다.
		const FVector ExplosionLocation = ExplosiveMesh && ExplosiveMesh->GetStaticMesh()
			? ExplosiveMesh->CalcBounds(ExplosiveMesh->GetComponentTransform()).Origin
			: GetActorLocation();
		const bool bDetonationRequested = ExplosionComponent->DetonateAt(
			ExplosionLocation,
			PendingDamageInstigator.IsValid() ? PendingDamageInstigator.Get() : GetInstigatorController(),
			PendingAdaptationDamageCategory);
		if (!bDetonationRequested)
		{
			UE_LOG(LogOutlier, Error, TEXT("[ExplosiveProp] HP reached zero. DetonateAt failed. Actor=%s"), *GetNameSafe(this));
		}
	}
	else
	{
		UE_LOG(LogOutlier, Error, TEXT("[ExplosiveProp] HP reached zero but ExplosionComponent is null. Actor=%s"), *GetNameSafe(this));
	}
}

void AExplosiveProp::ApplyExplodedState()
{
	if (ExplosiveMesh)
	{
		ExplosiveMesh->SetVisibility(!bExploded, true);
	}
	if (FirstPersonExplosiveMesh)
	{
		FirstPersonExplosiveMesh->SetVisibility(
			CachedOwningDrone.IsValid() && !bExploded,
			true);
	}

	if (HitCollision)
	{
		HitCollision->SetCollisionEnabled(
			bExploded ? ECollisionEnabled::NoCollision : ECollisionEnabled::QueryOnly);
	}
}

void AExplosiveProp::HandleExplosionProcessed()
{
	if (!HasAuthority())
	{
		return;
	}

	bExploded = true;
	if (bProgressIdRegistered)
	{
		if (UOutlierSaveSubSystem* SaveSubsystem = GetGameInstance()
			? GetGameInstance()->GetSubsystem<UOutlierSaveSubSystem>()
			: nullptr)
		{
			SaveSubsystem->SetWorldProgressState(
				EOutlierWorldProgressType::ExplodedProp,
				ExplosivePropId,
				true);
		}
	}
	ApplyExplodedState();
	ForceNetUpdate();
}

void AExplosiveProp::OnRep_Exploded()
{
	ApplyExplodedState();
}

void AExplosiveProp::MulticastPlayHitFeedback_Implementation(
	FVector_NetQuantize ImpactPoint,
	UNiagaraSystem* HitVFX,
	USoundBase* HitSFX,
	float HitFlashDuration)
{
	if (GetNetMode() == NM_DedicatedServer)
	{
		return;
	}

	if (HitVFX)
	{
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, HitVFX, ImpactPoint);
	}

	if (HitSFX)
	{
		UGameplayStatics::PlaySoundAtLocation(this, HitSFX, ImpactPoint);
	}

	OnHitFeedback(HitFlashDuration);
}
