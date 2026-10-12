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
	BlockedSegment
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

	// 어느 한도든 넘으면 BudgetExceeded다. 경로가 물리적으로 없다는 판정과 구분한다.
	int32 MaxSearchNodes = 512;
	int32 MaxCollisionChecks = 2048;
	double MaxSearchMilliseconds = 4.0;

	// Enemy는 초기 소속 방을 명시하고, Partner는 방 제한을 사용하지 않는다.
	bool bRequireRoom = false;
	TWeakObjectPtr<ARoomVolume> Room;

	// 탐색은 동기 실행이다. 호출자는 타깃/상태 변경 등으로 요청이 무효해졌는지 검사할 수 있다.
	TFunction<bool()> ShouldCancel;
};

struct OUTLIER_API FFlightPathResult
{
	EFlightPathStatus Status = EFlightPathStatus::InvalidRequest;

	// 성공한 경우만 시작점부터 실제 목적지까지의 검증된 경유점을 제공한다.
	TArray<FVector> Waypoints;

	int32 SearchNodes = 0;
	int32 CollisionChecks = 0;
	double ElapsedMilliseconds = 0.0;
	double PathLength = 0.0;
	bool bStartBlocked = false;
	bool bGoalAdjusted = false;
	bool bUsedClearanceEscape = false;
};

// 상태/Tick/이동 입력을 소유하지 않는 공용 함수다. 호출자는 결과를 보관하고 이동을 실행한다.
namespace FlightPathfinding
{
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
		TFunctionRef<bool(const FVector&, const FVector&)> IsSegmentClear);
}
