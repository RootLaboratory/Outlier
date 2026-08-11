// Impact 시스템 테스트용 더미 적.
// AEnemyBase를 상속받아, 사망(HandleDeath) 시 자기 월드 위치로 임팩트 한 건을 주입한다.
// 실제 게임 로직(HandleDeath)은 건드리지 않고, 이 테스트 클래스에서만 퍼블리시.

#pragma once

#include "CoreMinimal.h"
#include "Enemy/EnemyBase.h"
#include "ImpactTestEnemy.generated.h"

UCLASS()
class OUTLIER_API AImpactTestEnemy : public AEnemyBase
{
	GENERATED_BODY()

public:
	// 사망 시 주입할 임팩트 파라미터 (에디터에서 튜닝)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Impact Test")
	float ImpactRadius = 400.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Impact Test")
	float ImpactStrength = 900.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Impact Test")
	float ImpactDuration = 0.4;

	// 가상 폭발 지향 벡터(로컬). 사망 시 월드로 변환해 ScatterDir로 주입.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Impact Test")
	FVector LocalScatterDir = FVector::UpVector;

protected:
	virtual void HandleDeath() override;
};
