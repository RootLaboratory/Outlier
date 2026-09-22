#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Enemy/Death/OutlierDeathDebrisTypes.h"
#include "OutlierDeathDebrisActor.generated.h"

class UGeometryCollection;
class UGeometryCollectionComponent;

/**
 * Purely cosmetic Geometry Collection debris, spawned locally on each client when an enemy dies.
 *
 * Deliberately not replicated. The owning enemy is destroyed almost immediately after death
 * (see AEnemyBase::DeathDestroyDelay), so keeping the debris on the enemy actor would either
 * cut the debris short or force the corpse to stay a replicated actor for the whole debris
 * lifetime. Spawning a separate local actor decouples the two: the enemy is cleaned up on
 * schedule and the debris costs nothing on the network.
 *
 * Because each client simulates its own debris, piece positions diverge between machines.
 * That is fine for cosmetics — never drive gameplay off these pieces.
 */
UCLASS()
class OUTLIER_API AOutlierDeathDebrisActor : public AActor
{
	GENERATED_BODY()

public:
	AOutlierDeathDebrisActor();

	/**
	 * Spawns and starts one debris instance. Call only from code that already runs on every
	 * client (a Multicast implementation), never from server-only gameplay.
	 *
	 * SpawnTransform should be the source mesh's component transform at the moment of death.
	 * A Geometry Collection is pre-fractured rigid geometry with no skeleton, so the source
	 * bone pose is irrelevant — only the component transform matters.
	 */
	static AOutlierDeathDebrisActor* SpawnLocalDebris(
		UWorld* World,
		const UGeometryCollection* RestCollection,
		const FTransform& SpawnTransform,
		const FVector& InheritedVelocity,
		const FGeometryCollectionDeathProfile& Profile);

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Debris")
	TObjectPtr<UGeometryCollectionComponent> DebrisComponent;

private:
	bool BeginDebris(
		const UGeometryCollection* RestCollection,
		const FVector& InheritedVelocity,
		const FGeometryCollectionDeathProfile& Profile);

	// Chaos 는 물리 프록시 생성과 클러스터 해제에 각각 한 프레임씩 필요하다.
	// 그래서 활성화 -> ( 다음 틱 ) 클러스터 분해 -> ( 다음 틱 ) 임펄스 순으로 나눠 실행한다.
	void ReleaseClusters(const FVector& InheritedVelocity);
	void ApplyImpulses(const FVector& InheritedVelocity);

	FGeometryCollectionDeathProfile DebrisProfile;
};
