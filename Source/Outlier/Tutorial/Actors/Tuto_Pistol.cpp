#include "Tutorial/Actors/Tuto_Pistol.h"

#include "FirstPerson/FirstPersonCharacter.h"
#include "Outlier.h"
#include "OutlierPlayerState.h"
#include "Save/OutlierSaveSubSystem.h"
#include "Tutorial/WorldGuide/OutlierWorldGuideTargetComponent.h"

ATuto_Pistol::ATuto_Pistol()
{
	WeaponType = EWeaponType::Pistol;
	EventTriggers.SetNum(1);
	WorldGuideTarget = CreateDefaultSubobject<UOutlierWorldGuideTargetComponent>(TEXT("WorldGuideTarget"));
	WorldGuideTarget->SetupAttachment(GetRootComponent());
}

void ATuto_Pistol::BeginPlay()
{
	Super::BeginPlay();
	if (!HasAuthority()) return;
	// 슈트처럼 sublevel 리로드는 pair가 권총을 소유하지 않은 동안에만 이 픽업을 다시 생성한다.
	// 서버 PS 로드아웃은 gameplay 레벨 리로드 전에 save 서브시스템에 주입된다.
	const UOutlierSaveSubSystem* Saves = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UOutlierSaveSubSystem>() : nullptr;
	const TSubclassOf<AWeaponBase> Granted = GetGrantedWeaponClass();
	if (Saves && Granted && Saves->IsCurrentWeaponOwned(Granted))
	{
		UE_LOG(LogOutlier, Log, TEXT("[EventTrigger] %s removed on BeginPlay: '%s' is already owned."),
			*GetName(), *GetNameSafe(Granted.Get()));
		Destroy();
	}
}

bool ATuto_Pistol::Interact(AFirstPersonCharacter* Interactor)
{
	if (!Super::Interact(Interactor)) return false;
	HandleWeaponCollected(Interactor->GetPlayerState<AOutlierPlayerState>());
	return true;
}

TSubclassOf<AWeaponBase> ATuto_Pistol::GetGrantedWeaponClass() const
{
	if (!GrantedWeaponClass || GrantedWeaponClass->IsChildOf(ATuto_Pistol::StaticClass()))
	{
		UE_LOG(LogOutlier, Error, TEXT("[EventTrigger] %s GrantedWeaponClass '%s' must be a non-Tuto pistol."),
			*GetName(), *GetNameSafe(GrantedWeaponClass.Get()));
		return nullptr;
	}
	return GrantedWeaponClass;
}

void ATuto_Pistol::HandleWeaponCollected(AOutlierPlayerState* SourcePlayerState)
{
	UOutlierWorldGuideTargetComponent::CompleteOnActor(this);
	TriggerEvent(0, SourcePlayerState);
}
