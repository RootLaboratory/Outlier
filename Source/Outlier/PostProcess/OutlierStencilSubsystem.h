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
	void ClearActorEffects(AActor* Target);
	void ResetTransientEffects();
	bool HasEffect(EOutlierStencilEffect Effect) const;

private:
	TArray<FOutlierStencilRequest> Requests;
	// 적 빙의 중에도 로컬 PlayerState의 쌍 연결로 상대 플레이어를 찾는다.
	TSet<TWeakObjectPtr<AActor>> TeammateTargets;
	TMap<TWeakObjectPtr<UMeshComponent>, FOutlierStencilMeshState> MeshStates;
	void RefreshTeammateTargets();
	FOutlierStencilMeshState& TrackMesh(UMeshComponent* Mesh);
	void Reconcile();
	static void WriteMesh(UMeshComponent* Mesh, bool bEnabled, int32 Stencil, ERendererStencilMask WriteMask);
};
