#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GameFramework/GameModeBase.h"
#include "Drone/Partner/PartnerPlayerController.h"
#include "HackTargetPointNetworkProbe.generated.h"

class APartnerCharacter;
class UPartnerHackComponent;
class FJsonObject;

// 관전자도 자기 Controller의 소유 연결을 통해 준비 완료를 서버에 알린다.
UCLASS(Transient, NotBlueprintable)
class AHackTargetPointProbeController : public APartnerPlayerController
{
	GENERATED_BODY()
public:
	UFUNCTION(Server, Reliable)
	void ServerProbeReady();
	bool bProbeReady = false;
};

// 명시적인 개발 검증 실행에서만 사용하는 GameMode다. 일반 맵의 GameMode는 바꾸지 않는다.
UCLASS(Transient, NotBlueprintable)
class AHackTargetPointProbeGameMode : public AGameModeBase
{
	GENERATED_BODY()
public:
	AHackTargetPointProbeGameMode();
	virtual void PostLogin(APlayerController* NewPlayer) override;
};

// 실제 소유 연결에서 기존 해킹 RPC를 호출하고 결과를 수집한다. 게임플레이 판정은 기존 컴포넌트가 수행한다.
UCLASS(Transient, NotBlueprintable)
class AHackTargetPointNetworkProbe : public AActor
{
	GENERATED_BODY()
public:
	AHackTargetPointNetworkProbe();
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	void Initialize(APartnerCharacter* InPartner);

	UFUNCTION(Server, Reliable)
	void ServerObserve(int32 ObservedCase, const FString& Observation);
	UFUNCTION(Client, Reliable)
	void ClientRequestHack(int32 RequestedCase);
	UFUNCTION(Client, Reliable)
	void ClientFinishHack(bool bSuccess);

private:
	UPROPERTY(Replicated)
	TObjectPtr<APartnerCharacter> Partner;
	UPROPERTY(Replicated)
	TObjectPtr<AActor> Target;
	UPROPERTY(Replicated)
	int32 CaseIndex = -1;
	UPROPERTY(Replicated)
	int32 Phase = 0;

	TObjectPtr<AActor> Wall;
	UPROPERTY(Transient)
	TObjectPtr<UPartnerHackComponent> HackComponent;
	TSharedPtr<FJsonObject> OwnerObservation;
	float PhaseStart = 0;
	int32 LocalCase = -1;
	int32 LocalPhase = -1;
	float LocalStart = 0;
	bool bSearchRequested = false;
	bool bObservationSent = false;
	bool bConnected = false;
	bool bStarted = false;
	bool bAllPassed = true;
	bool bResultPassed = false;
	bool bClientCleaned = false;
	bool bRestoreObserved = false;
	int32 DeploymentSampleCase = -1;
	int32 DeploymentSampleStage = -1;
	FString OriginalTargetPath;
	FString Output;
	FString Role;

	void StartCase();
	void ServerTick();
	void ClientTick();
	void ApplyFixedPointFixture();
	void SampleTurretDeployment();
	TSharedRef<FJsonObject> Observe();
	void Write(const TSharedRef<FJsonObject>& Row);
	void FinishResult(bool bPassed, const FString& Detail);
};
