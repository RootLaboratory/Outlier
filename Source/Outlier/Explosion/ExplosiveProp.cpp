#include "Explosion/ExplosiveProp.h"

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

	ApplyExplodedState();
}

void AExplosiveProp::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (OutlierAbilitySystemComponent)
	{
		UnbindGasVitalityObservers();
		OutlierAbilitySystemComponent->ClearForActor(this);
	}

	Super::EndPlay(EndPlayReason);
}

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

		const float PreviousDroneHealth = CachedOwningDrone->GetCurrentHealth();
		const float WeakPointMultiplier = FMath::Max(
			CachedOwningDrone->GetRuntimeStat().ExplosiveWeakPointMultiplier,
			1.0f);
		const FString DroneName = GetNameSafe(CachedOwningDrone.Get());
		const FString ExplosiveName = GetNameSafe(this);
		const FString ComponentName = GetNameSafe(Request.HitResult.GetComponent());
		const float AppliedDamage = OutlierDamage::Apply(CachedOwningDrone.Get(), Request);
		UE_LOG(
			LogOutlier,
			Warning,
			TEXT("[EnemyWeakPoint] Type=MountedExplosive Drone=%s Explosive=%s Component=%s RawDamage=%.2f Multiplier=%.2f AppliedDamage=%.2f HP=%.2f->%.2f"),
			*DroneName,
			*ExplosiveName,
			*ComponentName,
			Request.DamageAmount,
			WeakPointMultiplier,
			AppliedDamage,
			PreviousDroneHealth,
			CachedOwningDrone->GetCurrentHealth());
		return AppliedDamage;
	}

	UE_LOG(
		LogOutlier,
		Warning,
		TEXT("[ExplosiveProp] Damage requested. Actor=%s Damage=%.2f CurrentHP=%.2f Authority=%s Exploded=%s Tag=%s"),
		*GetNameSafe(this),
		Request.DamageAmount,
		GetCurrentHP(),
		HasAuthority() ? TEXT("true") : TEXT("false"),
		bExploded ? TEXT("true") : TEXT("false"),
		*Request.DamageTag.ToString());

	if (!HasAuthority())
	{
		UE_LOG(LogOutlier, Warning, TEXT("[ExplosiveProp] Damage rejected: actor has no server authority. Actor=%s"), *GetNameSafe(this));
		return 0.0f;
	}

	if (bExploded)
	{
		UE_LOG(LogOutlier, Warning, TEXT("[ExplosiveProp] Damage rejected: actor already exploded. Actor=%s"), *GetNameSafe(this));
		return 0.0f;
	}

	if (GetCurrentHP() <= 0.0f)
	{
		UE_LOG(LogOutlier, Warning, TEXT("[ExplosiveProp] Damage rejected: actor HP is already depleted. Actor=%s"), *GetNameSafe(this));
		return 0.0f;
	}

	if (Request.DamageAmount <= 0.0f)
	{
		UE_LOG(LogOutlier, Warning, TEXT("[ExplosiveProp] Damage rejected: damage is not positive. Actor=%s"), *GetNameSafe(this));
		return 0.0f;
	}

	// 배치 폭발물은 일반 물리 충돌이 아니라 무기와 폭발 계열 피해에만 반응한다.
	const bool bAllowedDamage = Request.DamageTag.MatchesTag(OutlierGameplayTags::Damage::Weapon())
		|| Request.DamageTag.MatchesTag(OutlierGameplayTags::Damage::Explosion());
	if (!bAllowedDamage)
	{
		UE_LOG(
			LogOutlier,
			Warning,
			TEXT("[ExplosiveProp] Damage rejected: unsupported damage tag. Actor=%s Tag=%s"),
			*GetNameSafe(this),
			*Request.DamageTag.ToString());
		return 0.0f;
	}

	if (!CanBeDamaged())
	{
		UE_LOG(
			LogOutlier,
			Warning,
			TEXT("[ExplosiveProp] Damage rejected: CanBeDamaged is false. Actor=%s RequestedDamage=%.2f CanBeDamaged=%s"),
			*GetNameSafe(this),
			Request.DamageAmount,
			CanBeDamaged() ? TEXT("true") : TEXT("false"));
		return 0.0f;
	}

	const float PreviousHP = GetCurrentHP();
	PendingDamageInstigator = Request.EventInstigator;
	const bool bDamageApplied = OutlierAbilitySystemComponent
		&& OutlierAbilitySystemComponent->ApplyDamageToSelf(
			Request.DamageAmount,
			Request.EventInstigator,
			Request.DamageCauser,
			Request.DamageTag);
	PendingDamageInstigator.Reset();
	if (!bDamageApplied)
	{
		return 0.0f;
	}

	const float CurrentHP = GetCurrentHP();
	UE_LOG(
		LogOutlier,
		Warning,
		TEXT("[ExplosiveProp] Damage applied. Actor=%s Tag=%s AppliedDamage=%.2f HP=%.2f->%.2f"),
		*GetNameSafe(this),
		*Request.DamageTag.ToString(),
		Request.DamageAmount,
		PreviousHP,
		CurrentHP);

	if (CurrentHP <= 0.0f)
	{
		UE_LOG(LogOutlier, Warning, TEXT("[ExplosiveProp] HP reached zero. Actor=%s"), *GetNameSafe(this));
	}
	else if (RuntimePropRow.IsSet())
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
		UE_LOG(
			LogOutlier,
			Warning,
			TEXT("[ExplosiveProp] Prop row loaded. Actor=%s RowName=%s MaxHP=%.2f"),
			*GetNameSafe(this),
			*ExplosivePropRow.RowName.ToString(),
			RuntimePropRow->MaxHP);
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
		const bool bDetonationRequested = ExplosionComponent->DetonateAt(
			GetActorLocation(),
			PendingDamageInstigator.IsValid() ? PendingDamageInstigator.Get() : GetInstigatorController());
		if (bDetonationRequested)
		{
			UE_LOG(LogOutlier, Warning, TEXT("[ExplosiveProp] HP reached zero. DetonateAt succeeded. Actor=%s"), *GetNameSafe(this));
		}
		else
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
