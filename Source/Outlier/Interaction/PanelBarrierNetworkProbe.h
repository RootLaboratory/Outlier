#pragma once

#include "CoreMinimal.h"
#include "FirstPerson/FirstPersonPlayerController.h"
#include "GameFramework/GameModeBase.h"
#include "PanelBarrierNetworkProbe.generated.h"

class AShooterCharacter;
class APartnerCharacter;
class APanelMover;
class ARoomCombatBarrier;
class ARoomVolume;
class UBoxComponent;
class FJsonObject;

UCLASS(Transient, NotBlueprintable)
class APanelBarrierProbeController : public AFirstPersonPlayerController
{
	GENERATED_BODY()
public:
	UFUNCTION(Server, Reliable) void ServerPanelBarrierReady();
	UFUNCTION(Server, Reliable) void ServerPanelBarrierObserved(int32 Index, bool bPassed);
	bool bProbeReady = false;
};

// 명시적인 개발 검증 URL에서만 사용한다. 실제 게임의 GameMode/Controller는 변경하지 않는다.
UCLASS(Transient, NotBlueprintable)
class APanelBarrierProbeGameMode : public AGameModeBase
{
	GENERATED_BODY()
public:
	APanelBarrierProbeGameMode();
	virtual void PostLogin(APlayerController* Player) override;
};

UCLASS(Transient, NotBlueprintable)
class APanelBarrierNetworkProbe : public AActor
{
	GENERATED_BODY()
public:
	APanelBarrierNetworkProbe();
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Props) const override;
	void ReceiveObservation(APanelBarrierProbeController* Sender, int32 Index, bool bPassed);
	UPROPERTY(Replicated) TObjectPtr<AShooterCharacter> Shooter;
	UPROPERTY(Replicated) TObjectPtr<APartnerCharacter> Partner;
private:
	UPROPERTY(VisibleAnywhere) TObjectPtr<UBoxComponent> Floor;
	UPROPERTY(Replicated) TObjectPtr<APanelMover> Panel;
	UPROPERTY(Replicated) TObjectPtr<ARoomCombatBarrier> Barrier;
	UPROPERTY(Transient) TObjectPtr<ARoomVolume> Room;
	UPROPERTY(Transient) TObjectPtr<AActor> Target;
	UPROPERTY(Replicated) int32 CaseIndex = -1;
	UPROPERTY(Replicated) int32 Phase = 0;
	UPROPERTY(Replicated) FVector ExpectedShooter = FVector::ZeroVector;
	UPROPERTY(Replicated) FVector ExpectedPartner = FVector::ZeroVector;
	UPROPERTY(Replicated) FVector ExpectedPanel = FVector::ZeroVector;
	UPROPERTY(Replicated) bool bExpectedBarrier = false;
	UPROPERTY(Replicated) bool bAllPassed = true;
	FVector BeforeShooter;
	FVector BeforePartner;
	FVector BeforePanel;
	float PhaseStart = 0;
	float ActionWaitSeconds = 4;
	float StableSince = -1;
	float LastMotionSample = -1;
	float PenetrationSince = -1;
	float MaxPenetrationSeconds = 0;
	int32 MotionSampleCase = -1;
	int32 MotionSamples = 0;
	int32 ObservedCase = -1;
	bool bConnected = false;
	bool bFinished = false;
	bool bActionAccepted = false;
	bool bServerPassed = false;
	TSet<TWeakObjectPtr<APanelBarrierProbeController>> Observers;
	void SetupCase();
	void PerformAction();
	void CaptureExpected();
	void ObserveLocal();
	void SampleMotion();
	TSharedRef<FJsonObject> Observe() const;
	void Write(const TSharedRef<FJsonObject>& Row) const;
};
