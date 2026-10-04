#pragma once

#include "CoreMinimal.h"
#include "Components/StaticMeshComponent.h"
#include "FirstPerson/FirstPersonPlayerController.h"
#include "Interaction/SuitInteraction.h"
#include "Shooter/ShooterCharacter.h"
#include "Weapon/RangedWeaponBase.h"
#include "SuitInteractionTestActors.generated.h"

UCLASS(Transient, NotBlueprintable)
class ASuitTransitionTestPlayerController : public AFirstPersonPlayerController
{
	GENERATED_BODY()

public:
	// 실제 화면/LocalPlayer 없이 요청 전달만 관찰한다. 완료 응답은 테스트 본문에서 명시적으로 넣는다.
	virtual bool IsLocalController() const override { return bTestLocalController; }
	bool bTestLocalController = true;
	bool bNotifyDuringCleanup = false;
	bool bCancelDuringFadeOut = false;
	int32 FadeOutRequests = 0;
	int32 PresentationRequests = 0;
	int32 FadeInRequests = 0;
	int32 CleanupRequests = 0;
	FGuid LastRequestedId;
	FGuid LastCleanupId;
	float LastFadeDuration = 0.0f;

protected:
	virtual void OnPossess(APawn* InPawn) override
	{
		// PlayerState/Pair는 테스트 본문에서 구성한다. GameMode의 자동 Pair 등록은 이 경계 테스트에서 제외한다.
		APlayerController::OnPossess(InPawn);
	}

	virtual void RequestSuitFadeOut(const FGuid& TransitionId, float Duration) override
	{
		Super::RequestSuitFadeOut(TransitionId, Duration);
		++FadeOutRequests;
		LastRequestedId = TransitionId;
		LastFadeDuration = Duration;
		if (bCancelDuringFadeOut)
		{
			if (AShooterCharacter* Shooter = Cast<AShooterCharacter>(GetPawn()))
			{
				Shooter->CancelSuitTransition();
			}
		}
	}

	virtual void RequestSuitPresentationReady(const FGuid& TransitionId) override
	{
		Super::RequestSuitPresentationReady(TransitionId);
		++PresentationRequests;
		LastRequestedId = TransitionId;
	}

	virtual void RequestSuitFadeIn(const FGuid& TransitionId, float Duration) override
	{
		Super::RequestSuitFadeIn(TransitionId, Duration);
		++FadeInRequests;
		LastRequestedId = TransitionId;
		LastFadeDuration = Duration;
	}

	virtual void RequestSuitTransitionCleanup(const FGuid& TransitionId) override
	{
		Super::RequestSuitTransitionCleanup(TransitionId);
		++CleanupRequests;
		LastCleanupId = TransitionId;
		if (bNotifyDuringCleanup)
		{
			NotifySuitFadeOutFinished(TransitionId);
		}
	}
};

UCLASS(Transient, NotBlueprintable)
class ASuitInteractionTestRifle : public ARangedWeaponBase
{
	GENERATED_BODY()

public:
	ASuitInteractionTestRifle()
	{
		WeaponType = EWeaponType::Rifle;
	}

	void SetTestWeaponType(EWeaponType InWeaponType) { WeaponType = InWeaponType; }

	void ConfigureProceduralValues(UProceduralAnimValues* Common, UProceduralAnimValues* PreSuit, UProceduralAnimValues* Suit)
	{
		FirstPersonProceduralValues = Common;
		PreSuitProceduralValues = PreSuit;
		SuitProceduralValues = Suit;
	}

	void SetProceduralTestOwner(ACharacter* NewOwner)
	{
		WeaponOwner = NewOwner;
		OnRep_EquippedState();
	}
};

UCLASS(Transient, NotBlueprintable)
class ASuitInteractionTestPartnerWeapon : public ARangedWeaponBase
{
	GENERATED_BODY()
};

UCLASS(Transient, NotBlueprintable)
class ASuitInteractionTestActor : public ASuitInteraction
{
	GENERATED_BODY()

public:
	void Configure(
		UStaticMesh* DisplayMesh,
		USkeletalMesh* FirstPersonMesh,
		USkeletalMesh* ThirdPersonMesh)
	{
		SuitDisplayMesh->SetStaticMesh(DisplayMesh);
		ShooterFirstPersonMesh = FirstPersonMesh;
		ShooterThirdPersonMesh = ThirdPersonMesh;
		ShooterRifleClass = ASuitInteractionTestRifle::StaticClass();
		PartnerWeaponClass = ASuitInteractionTestPartnerWeapon::StaticClass();
	}
};
