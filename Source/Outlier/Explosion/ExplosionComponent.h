#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/DataTable.h"
#include "Explosion/ExplosionTypes.h"
#include "ExplosionComponent.generated.h"

class UCameraShakeBase;
class UNiagaraSystem;
class USoundBase;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnExplosionProcessed);

UCLASS(ClassGroup = (Gameplay), meta = (BlueprintSpawnableComponent))
class OUTLIER_API UExplosionComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UExplosionComponent();

	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// Detonate: 소유 Actor의 현재 위치에서 폭발을 요청한다.
	UFUNCTION(BlueprintCallable, Category = "Explosion")
	bool Detonate();

	// 지정 위치의 폭발 요청을 서버 Queue에 추가한다. 이미 요청한 컴포넌트라면 false를 반환한다.
	bool DetonateAt(const FVector& ExplosionLocation, AController* EventInstigator = nullptr);

	// 폭발 완료 상태를 초기화해 같은 컴포넌트가 다시 폭발할 수 있게 한다.
	UFUNCTION(BlueprintCallable, Category = "Explosion")
	void ResetExplosion();

	// Profile: 피해량, 반경, 차폐 감쇠와 반응 강도 수치가 들어 있는 DataTable Row를 적용한다.
	void SetExplosionProfileRow(const FDataTableRowHandle& InProfileRow);

	UFUNCTION(BlueprintPure, Category = "Explosion")
	bool HasDetonated() const { return bHasDetonated; }

	TSubclassOf<UCameraShakeBase> GetCameraShakeClass() const { return CameraShakeClass; }
	bool IsCameraShakeEnabled() const { return bEnableExplosionCameraShake; }
	bool AllowsCameraShakeForInactivePawn() const { return bAllowCameraShakeForInactivePawn; }

	UPROPERTY(BlueprintAssignable, Category = "Explosion")
	FOnExplosionProcessed OnExplosionProcessed;

protected:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Explosion")
	FDataTableRowHandle ExplosionProfileRow;

	// 연출 에셋은 CSV가 아닌 소유 BP에서 직접 지정한다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Explosion|Presentation")
	TObjectPtr<UNiagaraSystem> ExplosionVFX;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Explosion|Presentation")
	TObjectPtr<USoundBase> ExplosionSFX;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Explosion|Presentation")
	TSubclassOf<UCameraShakeBase> CameraShakeClass;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Explosion|Presentation")
	uint8 bEnableExplosionCameraShake : 1 = true;

	// 관전자 등 현재 Pawn을 직접 보고 있지 않을 때도 흔들림을 허용할지 결정한다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Explosion|Presentation")
	uint8 bAllowCameraShakeForInactivePawn : 1 = false;

	UPROPERTY(Replicated, VisibleInstanceOnly, BlueprintReadOnly, Category = "Explosion")
	bool bHasDetonated = false;

private:
	friend class UExplosionSubsystem;

	// DataTable Row를 런타임 값으로 복사한다. 유효한 Row가 없으면 false를 반환한다.
	bool InitializeProfile();

	// Subsystem의 피해 처리가 끝난 뒤 연출 RPC와 완료 이벤트를 발생시킨다.
	void NotifyExplosionProcessed(const FVector& ExplosionLocation, const FExplosionProfileRow& Profile);

	// Multicast: 서버에서 호출해 현재 접속한 모든 클라이언트에 폭발 연출을 재생한다.
	UFUNCTION(NetMulticast, Reliable)
	void MulticastPlayExplosionEffects(
		FVector_NetQuantize InExplosionLocation,
		UNiagaraSystem* InExplosionVFX,
		USoundBase* InExplosionSFX);

	FExplosionProfileRow* RuntimeProfile = nullptr;
	TOptional<FExplosionProfileRow> RuntimeProfileStorage;
};
