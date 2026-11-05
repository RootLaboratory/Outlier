#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "OutlierStencilSubsystem.generated.h"

class UMeshComponent;

// Scripts/Editor/ApplyOutlinePolicy.py와 공유한다. 관리 표식으로 ADS(3)와 기존
// 에셋 스텐실을 게임 외곽선 패스에서 제외한다. 하위 2비트는 스캔 유형을 나타낸다.
namespace OutlierStencil
{
	constexpr int32 ScanMask = 3;
	constexpr int32 Melee = 4;
	constexpr int32 Guide = 8;
	constexpr int32 Readability = 16;
	constexpr int32 Teammate = 32;
	constexpr int32 Managed = 128;
}

enum class EOutlierStencilEffect : uint8 { Scan, Melee, Guide };

struct FOutlierStencilRequest
{
	TWeakObjectPtr<AActor> Target;
	TWeakObjectPtr<UObject> Source;
	EOutlierStencilEffect Effect = EOutlierStencilEffect::Scan;
	int32 ScanType = 0;
};

struct FOutlierStencilMeshState
{
	bool bOriginalCustomDepth = false;
	int32 OriginalStencil = 0;
	ERendererStencilMask OriginalWriteMask = ERendererStencilMask::ERSM_Default;
	bool bReadability = false;
};

/** 게임 스텐실 효과의 쓰기를 전담한다. 다른 효과가 종료되어도 요청을 유지한다. */
UCLASS()
class OUTLIER_API UOutlierStencilSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual void Deinitialize() override;
	static bool IsEligibleMesh(const UMeshComponent* Mesh);
	void RegisterReadability(UMeshComponent* Mesh);
	void SetEffect(AActor* Target, UObject* Source, EOutlierStencilEffect Effect, int32 ScanType = 0);
	void ClearEffect(AActor* Target, UObject* Source, EOutlierStencilEffect Effect);
	void ClearSourceEffects(UObject* Source, EOutlierStencilEffect Effect);
	void ClearEffectsOfType(EOutlierStencilEffect Effect);
	/** 대상의 일시 요청과 push 팀원 등록을 모두 해제한다(사망/풀 반납). */
	void ClearActorEffects(AActor* Target);
	void ResetTransientEffects();
	bool HasEffect(EOutlierStencilEffect Effect) const;

	/** 플레이어 쌍 연결(캐릭터, 역할, 빙의)이 바뀌었을 때 호출한다. 다음 Tick에 한 번만 다시 찾는다. */
	void MarkTeammatesDirty();
	/** 해킹된 터렛처럼 플레이어 쌍 외의 팀원을 등록하거나 해제한다. ResetTransientEffects에 지워지지 않는다. */
	void SetActorTeammate(AActor* Target, bool bTeammate);

private:
	// 매 프레임 재계산하지 않는다. 이벤트가 놓친 메시 플래그 변경 등만 이 주기로 보정한다.
	static constexpr float FallbackReconcileInterval = 1.0f;

	TArray<FOutlierStencilRequest> Requests;
	// 적 빙의 중에도 로컬 PlayerState의 쌍 연결로 상대 플레이어를 찾는다.
	TSet<TWeakObjectPtr<AActor>> PairTeammates;
	// 플레이어 쌍 외에 push로 등록된 팀원(해킹된 터렛 등).
	TSet<TWeakObjectPtr<AActor>> RegisteredTeammates;
	TMap<TWeakObjectPtr<UMeshComponent>, FOutlierStencilMeshState> MeshStates;
	bool bTeammatesDirty = true;
	float FallbackElapsed = 0.f;
	bool RefreshPairTeammates();
	FOutlierStencilMeshState& TrackMesh(UMeshComponent* Mesh);
	void Reconcile();
	static void WriteMesh(UMeshComponent* Mesh, bool bEnabled, int32 Stencil, ERendererStencilMask WriteMask);
};
