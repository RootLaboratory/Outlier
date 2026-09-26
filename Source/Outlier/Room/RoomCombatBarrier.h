#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GameplayTagContainer.h"
#include "Room/RoomCombatSubsystem.h"
#include "RoomCombatBarrier.generated.h"

class UBoxComponent;
class UStaticMeshComponent;

UCLASS()
class OUTLIER_API ARoomCombatBarrier : public AActor
{
	GENERATED_BODY()

public:
	ARoomCombatBarrier();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	bool IsBlocked() const { return bBlocked; }
	void RefreshFromRoomCombat();
	bool ServesRoom(FGameplayTag InRoomTag) const;
	FVector GetJoinFallbackLocation() const;
	bool OverlapsJoinCapsule(const FVector& Location, float Radius, float HalfHeight) const;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Room Combat|Barrier")
	TObjectPtr<UBoxComponent> BlockingBox;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Room Combat|Barrier")
	TObjectPtr<UStaticMeshComponent> VisualPlane;

	// WP에서 RoomVolume보다 늦게 로드돼도 이 태그로 현재 차단 상태를 다시 조회한다.
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Room Combat|Barrier", meta = (Categories = "Room"))
	FGameplayTag RoomTag;

	// 공유 출입구라면 양쪽 Room 중 하나라도 차단 중일 때 계속 막는다.
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Room Combat|Barrier", meta = (Categories = "Room"))
	FGameplayTagContainer AdditionalRoomTags;

	// 출입구마다 방 안쪽 안전 지점을 지정한다. 맵 인스턴스의 3D 위젯으로 위치를 조정한다.
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category = "Room Combat|Barrier", meta = (MakeEditWidget))
	FVector JoinFallbackLocalOffset = FVector(200.0f, 0.0f, 0.0f);

private:
	UFUNCTION()
	void HandleCombatEvent(FGameplayTag EventRoomTag, ERoomCombatEvent Event,
		int32 CombatPhaseIndex, int32 GameplayGeneration);

	UFUNCTION()
	void OnRep_Blocked();

	void SetBlocked(bool bNewBlocked);
	void ApplyBlockedState();

	UPROPERTY(ReplicatedUsing = OnRep_Blocked)
	bool bBlocked = false;
};
