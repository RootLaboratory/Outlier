// Impact push system — event registry (publisher side).
// 폭발/낙하 임팩트를 모아두고, 구독자(연기 Niagara)가 매 프레임 배열로 읽어감. (pull / gather-read)

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "ImpactFieldSubsystem.generated.h"

USTRUCT(BlueprintType)
struct FImpactEvent
{
	GENERATED_BODY()

	// 임팩트 중심 (월드 좌표)
	UPROPERTY(BlueprintReadWrite, Category = "Impact")
	FVector Position = FVector::ZeroVector;

	// 지향성 벡터 (컨테이너 흩어지는 방향 / 폭발 가상 벡터). 정규화됨.
	UPROPERTY(BlueprintReadWrite, Category = "Impact")
	FVector ScatterDir = FVector::UpVector;

	// 표면 노멀 (바닥 훑기용, 노멀 성분 제거에 사용). 정규화됨.
	UPROPERTY(BlueprintReadWrite, Category = "Impact")
	FVector Normal = FVector::UpVector;

	// 영향 반경 (암묵적 volume) — 이 밖 파티클은 early-cut
	UPROPERTY(BlueprintReadWrite, Category = "Impact")
	float Radius = 300.f;

	// 세기 (감쇠 전 기준값)
	UPROPERTY(BlueprintReadWrite, Category = "Impact")
	float Strength = 800.f;

	// 배열에 살아있는 시간(초). 지나면 자동 소멸. impulse면 짧게(0.2~0.5).
	UPROPERTY(BlueprintReadWrite, Category = "Impact")
	float Duration = 0.4f;

	// 내부용: 등록 시점 월드 시간
	float SpawnTime = 0.f;
};

/**
 * 활성 임팩트를 모아두는 월드 서브시스템.
 * - 퍼블리셔(컨테이너 착지 / 드론 사망 폭발)가 RegisterImpact 호출
 * - 구독자(UImpactSmokeSubscriberComponent)가 BuildArrays로 매 프레임 읽어 Niagara에 push
 * - Tick에서 Duration 지난 이벤트 자동 제거 → 동시 N을 억제 → early-cut 비용 상한
 */
UCLASS()
class OUTLIER_API UImpactFieldSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	// ---- Publisher API ----
	// BlueprintCallable: 컨테이너 BP에서도 호출 가능. C++(드론 사망)에서도 동일 호출.
	UFUNCTION(BlueprintCallable, Category = "Impact")
	void RegisterImpact(FVector Position, FVector ScatterDir, FVector Normal,
		float Radius = 300.f, float Strength = 800.f, float Duration = 0.4f);

	// ---- Subscriber API ----
	// Niagara Array DI에 넣을 병렬 배열을 채움. Ages = 현재시각 - SpawnTime.
	void BuildArrays(
		TArray<FVector>& OutPositions,
		TArray<FVector>& OutScatterDirs,
		TArray<FVector>& OutNormals,
		TArray<float>& OutRadii,
		TArray<float>& OutStrengths,
		TArray<float>& OutAges) const;

	int32 GetActiveCount() const { return ActiveImpacts.Num(); }

	// ---- UTickableWorldSubsystem ----
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override
	{
		RETURN_QUICK_DECLARE_CYCLE_STAT(UImpactFieldSubsystem, STATGROUP_Tickables);
	}

private:
	// Array DI 상한. 넘으면 가장 오래된 것부터 밀어냄. (분산 동시 폭발 대비)
	static constexpr int32 MaxImpacts = 16;

	TArray<FImpactEvent> ActiveImpacts;
};
