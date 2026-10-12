#pragma once

#include "CoreMinimal.h"
#include "EnvironmentQuery/EnvQueryContext.h"
#include "EnvironmentQuery/EnvQueryTest.h"
#include "EnvironmentQuery/EnvQueryGenerator.h"
#include "EnemyEnvQuery.generated.h"

// 직접 시야로 선택한 타깃만 제공한다. 공유 위치/마지막 목격 좌표는 별도 수색 질의에서 사용한다.
UCLASS(meta = (DisplayName = "Enemy Visible Target"))
class OUTLIER_API UEnemyEnvQueryContext_VisibleTarget : public UEnvQueryContext
{
	GENERATED_BODY()

public:
	virtual void ProvideContext(FEnvQueryInstance& QueryInstance, FEnvQueryContextData& ContextData) const override;
};

// 타깃의 드론 쪽 반원에서 전투 거리 후보를 만든다. 시야/공간 검사는 EQS Test가 담당한다.
UCLASS(meta = (DisplayName = "Enemy Approach Candidates"))
class OUTLIER_API UEnemyEnvQueryGenerator_Approach : public UEnvQueryGenerator
{
	GENERATED_BODY()
public:
	UEnemyEnvQueryGenerator_Approach();
	virtual void GenerateItems(FEnvQueryInstance& QueryInstance) const override;
	UPROPERTY(EditDefaultsOnly, Category = Approach, meta = (ClampMin = "1000.0", ClampMax = "2000.0", Units = "cm"))
	float PreferredDistance = 1500.0f;
	UPROPERTY(EditDefaultsOnly, Category = Approach, meta = (ClampMin = "0.0", ClampMax = "500.0", Units = "cm"))
	float DistanceSpread = 250.0f;
	UPROPERTY(EditDefaultsOnly, Category = Approach, meta = (ClampMin = "0.0", ClampMax = "80.0"))
	float HalfAngleDegrees = 60.0f;
	UPROPERTY(EditDefaultsOnly, Category = Approach, meta = (ClampMin = "1", ClampMax = "9"))
	int32 AngularSamples = 5;
	UPROPERTY(EditDefaultsOnly, Category = Approach, meta = (ClampMin = "0.0", Units = "cm"))
	float FlightHeight = 150.0f;
	UPROPERTY(EditDefaultsOnly, Category = Approach, meta = (ClampMin = "1.0", Units = "cm"))
	float FloorProbeDistance = 10000.0f;
};

// Actor 목적지를 실제 이동 캡슐 중심으로 변환하고, 캡슐 공간과 초기 소속 방만 검사한다.
UCLASS(meta = (DisplayName = "Enemy Flight Destination"))
class OUTLIER_API UEnemyEnvQueryTest_FlightDestination : public UEnvQueryTest
{
	GENERATED_BODY()

public:
	UEnemyEnvQueryTest_FlightDestination();
	virtual void RunTest(FEnvQueryInstance& QueryInstance) const override;

	// 연결한 Fly Task의 PathClearance와 같은 값을 사용한다.
	UPROPERTY(EditDefaultsOnly, Category = "Flight", meta = (ClampMin = "0.0", Units = "cm"))
	float Clearance = 5.0f;
};
