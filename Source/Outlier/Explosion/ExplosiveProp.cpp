#include "Explosion/ExplosiveProp.h"

#include "Components/BoxComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Damage/OutlierTaggedDamageEvent.h"
#include "Explosion/ExplosionComponent.h"
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

	HitCollision = CreateDefaultSubobject<UBoxComponent>(TEXT("HitCollision"));
	HitCollision->SetupAttachment(SceneRoot);
	HitCollision->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	HitCollision->SetCollisionObjectType(ECC_WorldDynamic);
	HitCollision->SetCollisionResponseToAllChannels(ECR_Ignore);
	// 총기 판정 채널만 막고 Tick이나 상시 Overlap Event는 사용하지 않는다.
	HitCollision->SetCollisionResponseToChannel(ECC_PhysicsBody, ECR_Block);
	HitCollision->SetGenerateOverlapEvents(false);

	ExplosionComponent = CreateDefaultSubobject<UExplosionComponent>(TEXT("ExplosionComponent"));
}

void AExplosiveProp::BeginPlay()
{
	Super::BeginPlay();

	if (!InitializeFromDataTable())
	{
		UE_LOG(LogTemp, Error, TEXT("Explosive prop %s has an invalid ExplosivePropRow"), *GetName());
	}

	if (ExplosionComponent)
	{
		ExplosionComponent->OnExplosionProcessed.AddDynamic(this, &AExplosiveProp::HandleExplosionProcessed);
	}

	ApplyExplodedState();
}

void AExplosiveProp::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AExplosiveProp, bExploded);
}

float AExplosiveProp::TakeDamage(
	float DamageAmount,
	FDamageEvent const& DamageEvent,
	AController* EventInstigator,
	AActor* DamageCauser)
{
	UE_LOG(
		LogOutlier,
		Warning,
		TEXT("[ExplosiveProp] TakeDamage called. Actor=%s Damage=%.2f CurrentHP=%.2f Authority=%s Exploded=%s EventType=%d"),
		*GetNameSafe(this),
		DamageAmount,
		CurrentHP,
		HasAuthority() ? TEXT("true") : TEXT("false"),
		bExploded ? TEXT("true") : TEXT("false"),
		DamageEvent.GetTypeID());

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

	if (DamageAmount <= 0.0f)
	{
		UE_LOG(LogOutlier, Warning, TEXT("[ExplosiveProp] Damage rejected: damage is not positive. Actor=%s"), *GetNameSafe(this));
		return 0.0f;
	}

	if (!DamageEvent.IsOfType(FOutlierTaggedDamageEvent::ClassID))
	{
		UE_LOG(
			LogOutlier,
			Warning,
			TEXT("[ExplosiveProp] Damage rejected: event is not FOutlierTaggedDamageEvent. Actor=%s EventType=%d ExpectedType=%d"),
			*GetNameSafe(this),
			DamageEvent.GetTypeID(),
			FOutlierTaggedDamageEvent::ClassID);
		return 0.0f;
	}

	const FOutlierTaggedDamageEvent& TaggedEvent = static_cast<const FOutlierTaggedDamageEvent&>(DamageEvent);
	// 배치 폭발물은 일반 물리 충돌이 아니라 무기와 폭발 계열 피해에만 반응한다.
	const bool bAllowedDamage = TaggedEvent.DamageTag.MatchesTag(OutlierGameplayTags::Damage::Weapon())
		|| TaggedEvent.DamageTag.MatchesTag(OutlierGameplayTags::Damage::Explosion());
	if (!bAllowedDamage)
	{
		UE_LOG(
			LogOutlier,
			Warning,
			TEXT("[ExplosiveProp] Damage rejected: unsupported damage tag. Actor=%s Tag=%s"),
			*GetNameSafe(this),
			*TaggedEvent.DamageTag.ToString());
		return 0.0f;
	}

	const float AppliedDamage = Super::TakeDamage(DamageAmount, DamageEvent, EventInstigator, DamageCauser);
	if (AppliedDamage <= 0.0f)
	{
		UE_LOG(
			LogOutlier,
			Warning,
			TEXT("[ExplosiveProp] Damage rejected by AActor::TakeDamage. Actor=%s RequestedDamage=%.2f CanBeDamaged=%s"),
			*GetNameSafe(this),
			DamageAmount,
			CanBeDamaged() ? TEXT("true") : TEXT("false"));
		return 0.0f;
	}

	const float PreviousHP = CurrentHP;
	CurrentHP = FMath::Max(CurrentHP - AppliedDamage, 0.0f);
	UE_LOG(
		LogOutlier,
		Warning,
		TEXT("[ExplosiveProp] Damage applied. Actor=%s Tag=%s AppliedDamage=%.2f HP=%.2f->%.2f"),
		*GetNameSafe(this),
		*TaggedEvent.DamageTag.ToString(),
		AppliedDamage,
		PreviousHP,
		CurrentHP);

	if (CurrentHP <= 0.0f)
	{
		// 실제 폭발과 연쇄 처리는 Subsystem Queue에 위임한다.
		if (ExplosionComponent)
		{
			const bool bDetonationRequested = ExplosionComponent->DetonateAt(GetActorLocation(), EventInstigator);
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
	else if (RuntimePropRow.IsSet())
	{
		FVector ImpactPoint = GetActorLocation();
		if (TaggedEvent.HitResult.bBlockingHit)
		{
			ImpactPoint = TaggedEvent.HitResult.ImpactPoint;
		}

		MulticastPlayHitFeedback(
			ImpactPoint,
			HitFeedbackVFX.Get(),
			HitFeedbackSFX.Get(),
			RuntimePropRow->HitFlashDuration);
	}

	return AppliedDamage;
}

void AExplosiveProp::ResetToInitialState()
{
	// SaveGame 담당 시스템이 서버에서 호출할 복구 진입점이다.
	if (!HasAuthority() || !InitializeFromDataTable())
	{
		return;
	}

	bExploded = false;
	CurrentHP = RuntimePropRow->MaxHP;
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

	CurrentHP = FMath::Max(RuntimePropRow->MaxHP, 0.0f);
	return true;
}

void AExplosiveProp::ApplyExplodedState()
{
	if (ExplosiveMesh)
	{
		ExplosiveMesh->SetVisibility(!bExploded, true);
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
