#include "Enemy/SelfDestructDrone.h"

#include "Components/SceneComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Explosion/ExplosionComponent.h"

ASelfDestructDrone::ASelfDestructDrone()
{
	SetDefaultEnemyType(EEnemyType::Melee);

	MountedExplosiveRoot = CreateDefaultSubobject<USceneComponent>(TEXT("MountedExplosiveRoot"));
	MountedExplosiveRoot->SetupAttachment(ThirdPersonTiltRoot);

	MountedExplosiveMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MountedExplosiveMesh"));
	MountedExplosiveMesh->SetupAttachment(MountedExplosiveRoot);
	MountedExplosiveMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	MountedExplosiveHitbox = CreateDefaultSubobject<USphereComponent>(TEXT("MountedExplosiveHitbox"));
	MountedExplosiveHitbox->SetupAttachment(MountedExplosiveRoot);
	MountedExplosiveHitbox->InitSphereRadius(30.0f);
	MountedExplosiveHitbox->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	MountedExplosiveHitbox->SetCollisionResponseToAllChannels(ECR_Ignore);
	MountedExplosiveHitbox->SetCollisionResponseToChannel(ECC_PhysicsBody, ECR_Block);
	MountedExplosiveHitbox->SetGenerateOverlapEvents(false);

	ExplosionComponent = CreateDefaultSubobject<UExplosionComponent>(TEXT("ExplosionComponent"));
}

float ASelfDestructDrone::GetWeakPointDamageMultiplier(const UPrimitiveComponent* HitComponent) const
{
	if (HitComponent == MountedExplosiveHitbox)
	{
		// 부착 폭발물은 별도 HP 없이 DataTable 배율로 본체 HP에 크리티컬 피해를 준다.
		return FMath::Max(GetRuntimeStat().ExplosiveWeakPointMultiplier, 1.0f);
	}

	return Super::GetWeakPointDamageMultiplier(HitComponent);
}

void ASelfDestructDrone::TriggerSelfDestruct()
{
	if (!HasAuthority() || bDeathHandling)
	{
		return;
	}

	CurrentHealth = 0.0f;
	HandleDeath();
}

void ASelfDestructDrone::HandleDeath()
{
	if (!HasAuthority() || bDeathHandling)
	{
		return;
	}

	bDeathHandling = true;
	// EnemyBase가 Actor를 제거하기 전에 폭발 Queue와 클라이언트 연출을 먼저 확정한다.
	if (ExplosionComponent)
	{
		ExplosionComponent->DetonateAt(GetActorLocation(), GetController());
	}

	Super::HandleDeath();
}
