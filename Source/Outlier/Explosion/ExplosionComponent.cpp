#include "Explosion/ExplosionComponent.h"

#include "Explosion/ExplosionSubsystem.h"
#include "Explosion/ExplosionTypes.h"
#include "Kismet/GameplayStatics.h"
#include "Net/UnrealNetwork.h"
#include "NiagaraFunctionLibrary.h"
#include "Outlier.h"

UExplosionComponent::UExplosionComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void UExplosionComponent::BeginPlay()
{
	Super::BeginPlay();
	InitializeProfile();
}

void UExplosionComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UExplosionComponent, bHasDetonated);
}

bool UExplosionComponent::Detonate()
{
	const AActor* Owner = GetOwner();
	return Owner && DetonateAt(Owner->GetActorLocation(), Owner->GetInstigatorController());
}

bool UExplosionComponent::DetonateAt(const FVector& ExplosionLocation, AController* EventInstigator)
{
	AActor* Owner = GetOwner();
	// 서버 Queue에 폭발 요청을 한 번만 추가한다.
	// bHasDetonated는 같은 컴포넌트의 중복 요청과 연쇄 폭발 중 재요청을 막는다.
	if (!Owner)
	{
		UE_LOG(LogOutlier, Error, TEXT("[ExplosionComponent] DetonateAt rejected: owner is null. Component=%s"), *GetNameSafe(this));
		return false;
	}

	if (!Owner->HasAuthority())
	{
		UE_LOG(LogOutlier, Warning, TEXT("[ExplosionComponent] DetonateAt rejected: owner has no server authority. Owner=%s"), *GetNameSafe(Owner));
		return false;
	}

	if (bHasDetonated)
	{
		UE_LOG(LogOutlier, Warning, TEXT("[ExplosionComponent] DetonateAt rejected: already detonated. Owner=%s"), *GetNameSafe(Owner));
		return false;
	}

	if (!InitializeProfile() || !RuntimeProfile)
	{
		UE_LOG(
			LogOutlier,
			Error,
			TEXT("[ExplosionComponent] DetonateAt rejected: invalid explosion profile. Owner=%s DataTable=%s RowName=%s"),
			*GetNameSafe(Owner),
			*GetNameSafe(ExplosionProfileRow.DataTable),
			*ExplosionProfileRow.RowName.ToString());
		return false;
	}

	bHasDetonated = true;

	if (UWorld* World = GetWorld())
	{
		if (UExplosionSubsystem* ExplosionSubsystem = World->GetSubsystem<UExplosionSubsystem>())
		{
			ExplosionSubsystem->RequestExplosion(this, ExplosionLocation, EventInstigator, *RuntimeProfile);
			UE_LOG(
				LogOutlier,
				Warning,
				TEXT("[ExplosionComponent] Explosion queued. Owner=%s Profile=%s Location=%s"),
				*GetNameSafe(Owner),
				*ExplosionProfileRow.RowName.ToString(),
				*ExplosionLocation.ToCompactString());
			return true;
		}
	}

	bHasDetonated = false;
	UE_LOG(LogOutlier, Error, TEXT("[ExplosionComponent] DetonateAt failed: ExplosionSubsystem is unavailable. Owner=%s"), *GetNameSafe(Owner));
	return false;
}

void UExplosionComponent::ResetExplosion()
{
	// SaveGame 복구 자체는 외부 시스템이 담당하고 컴포넌트는 재폭발 가능 상태만 복원한다.
	if (AActor* Owner = GetOwner(); Owner && Owner->HasAuthority())
	{
		bHasDetonated = false;
	}
}

void UExplosionComponent::SetExplosionProfileRow(const FDataTableRowHandle& InProfileRow)
{
	ExplosionProfileRow = InProfileRow;
	RuntimeProfileStorage.Reset();
	RuntimeProfile = nullptr;
	InitializeProfile();
}

bool UExplosionComponent::InitializeProfile()
{
	if (RuntimeProfile)
	{
		return true;
	}

	const FExplosionProfileRow* Profile = ExplosionProfileRow.GetRow<FExplosionProfileRow>(TEXT("ExplosionProfileRow"));
	if (!Profile)
	{
		return false;
	}

	RuntimeProfileStorage.Emplace(*Profile);
	RuntimeProfile = &RuntimeProfileStorage.GetValue();
	return true;
}

void UExplosionComponent::NotifyExplosionProcessed(
	const FVector& ExplosionLocation,
	const FExplosionProfileRow& Profile)
{
	UE_LOG(
		LogOutlier,
		Warning,
		TEXT("[ExplosionComponent] Explosion processed. Owner=%s Profile=%s Location=%s"),
		*GetNameSafe(GetOwner()),
		*Profile.ExplosionId.ToString(),
		*ExplosionLocation.ToCompactString());

	MulticastPlayExplosionEffects(
		ExplosionLocation,
		ExplosionVFX.Get(),
		ExplosionSFX.Get());
	OnExplosionProcessed.Broadcast();
}

void UExplosionComponent::MulticastPlayExplosionEffects_Implementation(
	FVector_NetQuantize InExplosionLocation,
	UNiagaraSystem* InExplosionVFX,
	USoundBase* InExplosionSFX)
{
	if (GetNetMode() == NM_DedicatedServer)
	{
		return;
	}

	if (InExplosionVFX)
	{
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, InExplosionVFX, InExplosionLocation);
	}

	if (InExplosionSFX)
	{
		UGameplayStatics::PlaySoundAtLocation(this, InExplosionSFX, InExplosionLocation);
	}
}
