#pragma once

#include "CoreMinimal.h"
#include "FlightPathfinding.generated.h"

class ARoomVolume;
class UCapsuleComponent;
class UWorld;

UENUM(BlueprintType)
enum class EFlightPathStatus : uint8
{
	Success,
	InvalidRequest,
	Unavailable,
	BlockedEndpoint,
	// 현재 탐색 범위/격자에서 경로를 찾지 못했다. 연속 공간의 절대 도달 불가능을 의미하지 않는다.
	NoPathWithinBounds,
	BudgetExceeded,
	Cancelled,
	// 이미 선정된 경로의 다음 직선 구간이 새 장애물로 막힌 경우다.
	BlockedSegment,
	InProgress
};

// 목적지 선정(EQS)과 실제 이동 사이에서 사용하는, 한 번의 제한된 탐색 요청이다.
struct OUTLIER_API FFlightPathRequest
{
	// 좌표는 Actor 원점이 아니라 이동 캡슐의 중심을 기준으로 한다.
	FVector Start = FVector::ZeroVector;
	FVector Goal = FVector::ZeroVector;

	// 목적지 주변의 제한된 공간만 탐색한다. CellSize가 작을수록 좁은 통로를 찾기 쉽지만 비용이 커진다.
	FBox SearchBounds = FBox(ForceInit);
	float CellSize = 100.0f;
	float Clearance = 5.0f;
	// 0이면 정확한 목적지만 사용한다. 막힌 목적지의 근처 접근점을 찾을 때만 지정한다.
	float GoalSearchRadius = 0.0f;
	// 1은 최단 경로 탐색이다. Partner 추종은 약간의 우회를 허용하고 목표 방향 탐색을 우선한다.
	double HeuristicScale = 1.0;
	// 0은 단일 기본 격자다. 고정된 큰 격자를 먼저 검사하는 호출자만 지정한다.
	float CoarseCellSize = 0.0f;
	bool bSimplifyPath = false;
	// 1은 기본 이웃만 사용한다. 적응형은 빈 영역의 간격, 기존 방식은 축 방향 긴 연결에 사용한다.
	int32 LongConnectionStride = 1;
	bool bLimitDistantGoalChecks = false;
	// 같은 캡슐/필터의 양방향 Sweep이 동일한 호출자만 사용한다. 출발점 탈출 구간은 제외한다.
	bool bReuseReverseSegmentChecks = false;
	// 빈 공간에서는 긴 연결을 허용한다. 영역 검사도 예산에 포함한다.
	// 동기 검색은 기본 격자로 보완하고, 일반 재개 검색은 작은 이웃을 함께 유지한다.
	bool bAdaptiveResolution = false;
	// 재개 검색 전용. 큰 간격의 경로를 먼저 찾고, 막힌 연결 주변에만 작은 이웃을 추가한다.
	// 정밀화 후보가 있으면 예산 절반에서 전환한다. 열린 공간의 큰 간격/충돌 캐시는 유지한다.
	bool bHierarchicalResolution = false;

	// 재개 검색 전용. 0은 전체 경로, 양수는 이 반경 밖의 검증된 경유지까지 탐색한다.
	// 최종 Goal은 방향 판단에 유지한다. 막힌 먼 Goal 때문에 지역 이동까지 거부하지 않는다.
	float TravelHorizon = 0.0f;
	TArray<FVector> RouteHistory;

	// 어느 한도든 넘으면 BudgetExceeded다. 경로가 물리적으로 없다는 판정과 구분한다.
	int32 MaxSearchNodes = 512;
	int32 MaxCollisionChecks = 2048;
	double MaxSearchMilliseconds = 4.0;

	// Enemy는 초기 소속 방을 명시하고, Partner는 방 제한을 사용하지 않는다.
	bool bRequireRoom = false;
	TWeakObjectPtr<ARoomVolume> Room;

	// 후퇴는 플레이어를 기준으로 드론 쪽 반공간 안에서만 탐색/이동한다.
	// Origin은 캡슐 좌표, Direction은 수평 단위 벡터다. 기본값은 기존 경로에 영향을 주지 않는다.
	bool bRequireAwayHalfSpace = false;
	FVector AwayOrigin = FVector::ZeroVector;
	FVector AwayDirection = FVector::ZeroVector;

	// 게임 스레드에서 동기/분할 실행한다. 분할 요청에 저장하는 콜백도 요청 수명 동안 유효해야 한다.
	TFunction<bool()> ShouldCancel;
};

struct OUTLIER_API FFlightPathResult
{
	EFlightPathStatus Status = EFlightPathStatus::InvalidRequest;

	// 성공한 경우만 검증된 경로를 제공한다. bPartialRoute이면 마지막 점은 지역 경유지다.
	TArray<FVector> Waypoints;

	int32 SearchNodes = 0;
	int32 CollisionChecks = 0;
	int32 SegmentCacheHits = 0;
	int32 PrunedTraversals = 0;
	int32 RejectedPositions = 0;
	int32 RejectedSegments = 0;
	int32 OutOfBoundsNeighbours = 0;
	int32 ClearStartConnections = 0;
	int32 GoalConnectionChecks = 0;
	int32 ClearGoalConnections = 0;
	int32 SkippedGoalConnections = 0;
	int32 WideExpansionNodes = 0;
	int32 FineExpansionNodes = 0;
	int32 SearchPasses = 0;
	int32 CoarseSearchNodes = 0;
	int32 CoarseCollisionChecks = 0;
	int32 RefinementCandidates = 0;
	int32 RefinedSearchNodes = 0;
	int32 CoarseProgressNodes = 0;
	int32 FallbackRefinedNodes = 0;
	int32 WideRegion3DNodes = 0;
	int32 WideRegion2DNodes = 0;
	int32 RawWaypointCount = 0;
	float CellSizeUsed = 0.0f;
	FVector ResolvedGoal = FVector::ZeroVector;
	double ElapsedMilliseconds = 0.0;
	int32 SearchSlices = 0;
	double PeakSliceMilliseconds = 0.0;
	double WallMilliseconds = 0.0;
	double PathLength = 0.0;
	bool bStartBlocked = false;
	bool bGoalAdjusted = false;
	bool bUsedClearanceEscape = false;
	bool bPartialRoute = false;
};

class OUTLIER_API FFlightPathSearch : public TSharedFromThis<FFlightPathSearch>
{
public:
	virtual ~FFlightPathSearch() = default;
	// 완료이면 true다. A* 노드는 원자적으로 처리하며 시간 한도는 노드 사이의 soft boundary다.
	virtual bool Advance(int32 MaxChecks, double MaxMilliseconds) = 0;
	virtual const FFlightPathResult& GetResult() const = 0;
	virtual const FFlightPathRequest& GetRequest() const = 0;
};

// 상태/Tick/이동 입력을 소유하지 않는 공용 함수다. 호출자는 결과를 보관하고 이동을 실행한다.
namespace FlightPathfinding
{
	OUTLIER_API TSharedPtr<FFlightPathSearch> BeginSearch(
		UWorld& World, const UCapsuleComponent& MovementCapsule, const FFlightPathRequest& Request);
	// 테스트/독립 검사 주입용이다. 함수와 요청을 소유하지만 콜백이 참조하는 외부 데이터는 소유하지 않는다.
	OUTLIER_API TSharedPtr<FFlightPathSearch> BeginSearchWithChecks(
		const FFlightPathRequest& Request, TFunction<bool(const FVector&)> IsPositionClear,
		TFunction<bool(const FVector&, const FVector&)> IsSegmentClear,
		TFunction<bool(const FVector&, const FVector&)> IsRegionClear = {});
	// 게임 스레드 전용. 월드 공유 예산/FIFO와 세션당 프레임 한 번 제한을 적용한다.
	// A* 슬라이스에는 최소 68 checks가 필요하다. CoarseCellSize는 재개 검색에서 사용하지 않는다.
	OUTLIER_API bool AdvanceSearch(UWorld& World, FFlightPathSearch& Search, int32 SliceChecks, double SliceMs);
#if WITH_DEV_AUTOMATION_TESTS
	// 테스트 월드의 Task 예산만 교체한다. 에셋과 다른 PIE 월드에는 적용하지 않는다.
	struct FTestCost
	{
		int64 Paths = 0;
		int64 Failures = 0;
		int64 Budgets = 0;
		int64 Checks = 0;
		int64 Nodes = 0;
		int64 QueriesStarted = 0;
		int64 QueriesFinished = 0;
		double TotalMs = 0.0;
		double PeakMs = 0.0;
	};
	OUTLIER_API void BeginTestBudget(UWorld& World, int32 CollisionChecks);
	OUTLIER_API void EndTestBudget();
	OUTLIER_API int32 GetTestBudget(UWorld& World, int32 AssetBudget);
	OUTLIER_API FTestCost GetTestCost();
	OUTLIER_API void RecordTestQuery(UWorld* World, bool bFinished);
#endif
	OUTLIER_API bool IsInsideAwayHalfSpace(const FFlightPathRequest& Request, const FVector& Position);
	// 회전된 박스가 아니라 캡슐 중심축과 구형 끝부분으로 월드 경계 범위를 계산한다.
	OUTLIER_API FVector GetCapsuleWorldExtent(float Radius, float HalfHeight, const FQuat& Rotation);

	// EQS 후보의 캡슐 공간/방 경계만 검사한다. 우회 경로를 찾거나 목적지를 보정하지 않는다.
	OUTLIER_API EFlightPathStatus CheckDestination(
		UWorld& World, const UCapsuleComponent& MovementCapsule, const FFlightPathRequest& Request);

	// 경유점 추종 중 실제 위치에서 다음 지점까지 검사한다. A*는 실행하지 않는다.
	OUTLIER_API EFlightPathStatus CheckSegment(
		UWorld& World, const UCapsuleComponent& MovementCapsule, const FFlightPathRequest& Request);

	// 서버 게임 스레드에서 실제 캡슐과 선택적 방 경계를 검증한 뒤 경로를 반환한다.
	OUTLIER_API FFlightPathResult FindPath(
		UWorld& World, const UCapsuleComponent& MovementCapsule, const FFlightPathRequest& Request);

	// 충돌 환경을 주입할 수 있는 순수 탐색 진입점. Predicate는 위치/구간의 통과 가능 여부를 반환한다.
	// 게임에서의 권한, 실제 충돌 및 RoomVolume 검사는 위 FindPath 진입점에서 처리한다.
	OUTLIER_API FFlightPathResult FindPathWithChecks(
		const FFlightPathRequest& Request,
		TFunctionRef<bool(const FVector&)> IsPositionClear,
		TFunctionRef<bool(const FVector&, const FVector&)> IsSegmentClear,
		TFunction<bool(const FVector&, const FVector&)> IsRegionClear = {});
}
