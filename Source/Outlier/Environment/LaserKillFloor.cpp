#include "Environment/LaserKillFloor.h"

#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Damage/OutlierDamageReceiver.h"
#include "GameplayTags/OutlierGameplayTags.h"
#include "Net/UnrealNetwork.h"
#include "Outlier.h"

ALaserKillFloor::ALaserKillFloor()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;

	// Box는 Pawn의 진입만 감지하고 이동을 막지 않는다.
	KillVolume = CreateDefaultSubobject<UBoxComponent>(TEXT("KillVolume"));
	SetRootComponent(KillVolume);
	KillVolume->SetBoxExtent(FVector(200.0f, 200.0f, 20.0f));
	KillVolume->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	KillVolume->SetCollisionObjectType(ECC_WorldDynamic);
	KillVolume->SetCollisionResponseToAllChannels(ECR_Ignore);
	KillVolume->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	KillVolume->SetGenerateOverlapEvents(true);

	// 표시용 Plane은 착지나 피해 판정에 관여하지 않는다.
	LaserPlane = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("LaserPlane"));
	LaserPlane->SetupAttachment(KillVolume);
	LaserPlane->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	LaserPlane->SetGenerateOverlapEvents(false);
}

void ALaserKillFloor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ALaserKillFloor, bHazardEnabled);
}

void ALaserKillFloor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	if (LaserMaterial)
	{
		LaserPlane->SetMaterial(0, LaserMaterial);
	}
}

void ALaserKillFloor::BeginPlay()
{
	Super::BeginPlay();
	KillVolume->OnComponentBeginOverlap.AddDynamic(
		this,
		&ALaserKillFloor::HandleKillVolumeBeginOverlap);
	KillVolume->OnComponentEndOverlap.AddDynamic(
		this,
		&ALaserKillFloor::HandleKillVolumeEndOverlap);
	ApplyHazardState();

	if (HasAuthority() && bHazardEnabled)
	{
		DamageCurrentOverlaps();
	}
}

void ALaserKillFloor::SetHazardEnabled(bool bEnabled)
{
	if (!HasAuthority() || bHazardEnabled == bEnabled)
	{
		return;
	}

	bHazardEnabled = bEnabled;
	if (!bHazardEnabled)
	{
		DamagedActorsInVolume.Reset();
	}
	ApplyHazardState();
	if (bHazardEnabled)
	{
		DamageCurrentOverlaps();
	}
	ForceNetUpdate();
}

void ALaserKillFloor::HandleKillVolumeBeginOverlap(
	UPrimitiveComponent* OverlappedComponent,
	AActor* OtherActor,
	UPrimitiveComponent* OtherComp,
	int32 OtherBodyIndex,
	bool bFromSweep,
	const FHitResult& SweepResult)
{
	(void)OverlappedComponent;
	(void)OtherComp;
	(void)OtherBodyIndex;
	(void)bFromSweep;
	ApplyInstantKillDamage(OtherActor, SweepResult);
}

void ALaserKillFloor::HandleKillVolumeEndOverlap(
	UPrimitiveComponent* OverlappedComponent,
	AActor* OtherActor,
	UPrimitiveComponent* OtherComp,
	int32 OtherBodyIndex)
{
	(void)OverlappedComponent;
	(void)OtherComp;
	(void)OtherBodyIndex;
	if (OtherActor && !KillVolume->IsOverlappingActor(OtherActor))
	{
		DamagedActorsInVolume.Remove(TWeakObjectPtr<AActor>(OtherActor));
	}
}

void ALaserKillFloor::OnRep_HazardEnabled()
{
	ApplyHazardState();
}

void ALaserKillFloor::ApplyHazardState()
{
	KillVolume->SetCollisionEnabled(bHazardEnabled
		? ECollisionEnabled::QueryOnly
		: ECollisionEnabled::NoCollision);
	if (bVisibleOnlyWhenEnabled)
	{
		LaserPlane->SetHiddenInGame(!bHazardEnabled);
	}
}

void ALaserKillFloor::ApplyInstantKillDamage(AActor* TargetActor, const FHitResult& HitResult)
{
	if (!HasAuthority() || !bHazardEnabled || !IsValid(TargetActor)
		|| TargetActor == this || InstantKillDamage <= 0.0f
		|| DamagedActorsInVolume.Contains(TWeakObjectPtr<AActor>(TargetActor)))
	{
		return;
	}
	DamagedActorsInVolume.Add(TWeakObjectPtr<AActor>(TargetActor));

	FOutlierDamageRequest DamageRequest;
	DamageRequest.DamageAmount = InstantKillDamage;
	DamageRequest.DamageTag = OutlierGameplayTags::Damage::Environment();
	DamageRequest.DamageOrigin = GetActorLocation();
	DamageRequest.DamageCauser = this;
	DamageRequest.HitResult = HitResult;

	// 대상 Receiver가 면역, 보호막, 사망 처리를 결정한다.
	const float AppliedDamage = OutlierDamage::Apply(TargetActor, DamageRequest);
	if (AppliedDamage > 0.0f)
	{
		UE_LOG(
			LogOutlier,
			Verbose,
			TEXT("[LaserKillFloor] Applied instant damage Target=%s Damage=%.2f Hazard=%s"),
			*GetNameSafe(TargetActor),
			AppliedDamage,
			*GetNameSafe(this));
	}
}

void ALaserKillFloor::DamageCurrentOverlaps()
{
	// 시작 시점이나 재활성화 시 이미 영역 안에 있는 대상은 BeginOverlap이 없을 수 있다.
	TArray<AActor*> OverlappingActors;
	KillVolume->GetOverlappingActors(OverlappingActors);
	for (AActor* Actor : OverlappingActors)
	{
		FHitResult SyntheticHit;
		SyntheticHit.HitObjectHandle = FActorInstanceHandle(Actor);
		SyntheticHit.bBlockingHit = false;
		SyntheticHit.Location = Actor ? Actor->GetActorLocation() : GetActorLocation();
		SyntheticHit.ImpactPoint = SyntheticHit.Location;
		SyntheticHit.ImpactNormal = (GetActorLocation() - SyntheticHit.Location).GetSafeNormal();
		ApplyInstantKillDamage(Actor, SyntheticHit);
	}
}
