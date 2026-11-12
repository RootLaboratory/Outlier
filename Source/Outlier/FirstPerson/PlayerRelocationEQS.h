#pragma once

#include "CoreMinimal.h"
#include "EnvironmentQuery/EnvQueryGenerator.h"
#include "EnvironmentQuery/EnvQueryTest.h"
#include "PlayerRelocationEQS.generated.h"

class ACharacter;
class UEnvQuery;
struct FEnvQueryResult;

enum class EPlayerRelocationMode : uint8 { Ground, Flight };
enum class EPlayerRelocationPosture : uint8 { Standing, Crouched };
enum class EPlayerRelocationStatus : uint8
{
	Idle, Running, Success, NoCandidate, InvalidContext, Cancelled, TimedOut, BudgetExceeded
};

struct FPlayerRelocationRequest
{
	TWeakObjectPtr<ACharacter> MovingCharacter;
	TWeakObjectPtr<AActor> Anchor;
	FTransform Reference = FTransform::Identity;
	EPlayerRelocationMode Mode = EPlayerRelocationMode::Ground;
	bool bWantsCrouch = false;
	bool bExpandedOnly = false;
	bool bPreferOffset = false;
	bool bRequireAnchorSweep = false;
	FVector PreferredLocalOffset = FVector(0, 200, 0);
	// 비어 있으면 주변 격자, 지정하면 입력 순서의 출입구 후보만 검사한다. XY 반경 외 안전 조건은 동일하다.
	TArray<FVector> ExplicitCandidates;
	float InitialRadius = 200;
	float InitialDepth = 300;
	float ExpandedRadius = 300;
	float ExpandedDepth = 500;
	float GridSpacing = 50;
	int32 MaxCandidatesPerStage = 256;
	int32 MaxCollisionChecks = 2048;
	double TimeoutSeconds = 0.5;

	// 호출 측이 Room/차단막/전환 수명을 공급한다. 비동기 수명보다 짧은 스택 참조를 캡처하지 않는다.
	TFunction<bool()> IsContextCurrent;
	TFunction<bool(const FVector&, float, float)> AdditionalFilter;
};

struct FPlayerRelocationResult
{
	EPlayerRelocationStatus Status = EPlayerRelocationStatus::Idle;
	FVector Location = FVector::ZeroVector;
	EPlayerRelocationPosture Posture = EPlayerRelocationPosture::Standing;
	bool bExpanded = false;
	int32 CollisionChecks = 0;
	int32 GeneratedCandidates = 0;
};

// 요청 하나의 수명만 소유한다. 호출 측은 완료/취소까지 UPROPERTY 또는 강한 참조로 보관한다.
// 목적지 선정만 수행하며 Teleport, 자세 변경, 충전/지급/전투 확정은 호출 측 책임이다.
UCLASS()
class OUTLIER_API UPlayerRelocationQuery : public UObject
{
	GENERATED_BODY()
public:
	virtual UWorld* GetWorld() const override;
	bool Start(const FPlayerRelocationRequest& InRequest, TFunction<void(const FPlayerRelocationResult&)> InCompletion);
	void Cancel();
	const FPlayerRelocationResult& GetResult() const { return Result; }
	// 완료 결과도 실제 이동 직전에 다시 검사한다. 검사 중 캐릭터 자세나 위치를 바꾸지 않는다.
	bool Revalidate();
	bool RevalidateForCrouchIntent(bool bWantsCrouch);

private:
	friend class UPlayerRelocationGenerator;
	friend class UPlayerRelocationSafetyTest;
	bool IsContextValid() const;
	bool ConsumeCheck();
	bool IsWithinRange(const FVector& Location) const;
	void GetShape(float& Radius, float& HalfHeight) const;
	bool ProjectGround(FVector& Location);
	bool Validate(const FVector& Location);
	void Generate(FEnvQueryInstance& Query);
	void BeginStage();
	void OnQueryFinished(TSharedPtr<FEnvQueryResult> QueryResult);
	void OnTimeout();
	void Finish(EPlayerRelocationStatus Status);
	bool IsBetter(const FVector& Candidate, const FVector& Best) const;

	UPROPERTY(Transient)
	TObjectPtr<UEnvQuery> QueryTemplate;
	TWeakObjectPtr<UWorld> QueryWorld;
	FPlayerRelocationRequest Request;
	FPlayerRelocationResult Result;
	TFunction<void(const FPlayerRelocationResult&)> Completion;
	FTimerHandle TimeoutHandle;
	int32 QueryId = INDEX_NONE;
	int32 Stage = 0;
	int32 ExplicitCandidateIndex = 0;
	double Deadline = 0;
	bool bBudgetExceeded = false;
};

UCLASS()
class OUTLIER_API UPlayerRelocationGenerator : public UEnvQueryGenerator
{
	GENERATED_BODY()
public:
	UPlayerRelocationGenerator();
	virtual void GenerateItems(FEnvQueryInstance& QueryInstance) const override;
};

UCLASS()
class OUTLIER_API UPlayerRelocationSafetyTest : public UEnvQueryTest
{
	GENERATED_BODY()
public:
	UPlayerRelocationSafetyTest();
	virtual void RunTest(FEnvQueryInstance& QueryInstance) const override;
};
