#if WITH_DEV_AUTOMATION_TESTS

#include "Drone/Partner/PartnerCharacter.h"
#include "Drone/Partner/PartnerSpriteAnimationComponent.h"
#include "AIController.h"
#include "Engine/Engine.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "TimerManager.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPartnerStandbyTest,
	"Outlier.Partner.Standby.StateAndEmotionPriority",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPartnerStandbyTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false,
		MakeUniqueObjectName(nullptr, UWorld::StaticClass(), TEXT("PartnerStandbyTest")));
	if (!TestNotNull(TEXT("World exists"), World)) return false;
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	ON_SCOPE_EXIT
	{
		World->DestroyWorld(false);
		GEngine->DestroyWorldContext(World);
		World->RemoveFromRoot();
	};
	World->InitializeActorsForPlay(FURL());
	APartnerCharacter* Partner = World->SpawnActor<APartnerCharacter>();
	AAIController* Controller = World->SpawnActor<AAIController>();
	if (!TestNotNull(TEXT("Partner exists"), Partner)
		|| !TestNotNull(TEXT("Controller exists"), Controller)) return false;
	// 입력과 대기 판정만 검사하므로 소유 변경에 따른 데이터 초기화는 실행하지 않는다.
	Partner->Controller = Controller;
	Controller->SetPawn(Partner);
	Partner->StandbyDelay = 3.0f;
	UPartnerSpriteAnimationComponent* Face = Partner->FaceSpriteAnimationComponent;
	if (!TestNotNull(TEXT("Sprite component exists"), Face)) return false;

	TestTrue(TEXT("Actor ticking is enabled for standby updates"), Partner->PrimaryActorTick.bCanEverTick);
	Partner->UpdateStandby(2.9f);
	TestFalse(TEXT("Does not enter before delay"), Partner->bIsStandby);
	Partner->UpdateStandby(0.2f);
	TestTrue(TEXT("Enters after inactivity delay"), Partner->bIsStandby);
	Partner->NotifyStandbyActivity();
	TestFalse(TEXT("Blocked input also wakes standby"), Partner->bIsStandby);
	Partner->UpdateStandby(2.9f);
	TestFalse(TEXT("Activity restarts the full delay"), Partner->bIsStandby);
	Partner->UpdateStandby(0.2f);
	Partner->GetCharacterMovement()->Velocity = FVector(0, 0, 20);
	Partner->UpdateStandby(0.1f);
	TestFalse(TEXT("Vertical movement wakes standby"), Partner->bIsStandby);
	Partner->GetCharacterMovement()->Velocity = FVector::ZeroVector;
	Partner->UpdateStandby(3.1f);
	Controller->SetControlRotation(FRotator(10, 0, 0));
	Partner->UpdateStandby(0.1f);
	TestFalse(TEXT("Aim changes wake standby"), Partner->bIsStandby);
	Partner->bScanning = true;
	Partner->UpdateStandby(10.0f);
	TestFalse(TEXT("Active skill blocks standby"), Partner->bIsStandby);
	Partner->bScanning = false;
	Partner->Controller = nullptr;
	Controller->SetPawn(nullptr);
	Partner->UpdateStandby(10.0f);
	TestFalse(TEXT("Unpossessed drone cannot enter standby"), Partner->bIsStandby);
	Partner->Controller = Controller;
	Controller->SetPawn(Partner);
	Partner->bStandbyAimInitialized = false;
	Partner->UpdateStandby(3.1f);
	Partner->Tick(0.125f);
	TestTrue(TEXT("Standby remains active after the actor update"), Partner->bIsStandby);

	// 렌더링 없이 서버의 표정 선정, 우선권, 타이머 수명만 검사한다.
	FPartnerSpriteAnimationData Data;
	Data.Texture = NewObject<UTexture2D>(Face);
	Face->EmotionSprites.Add(EPartnerEmotion::Default, Data);
	Face->EmotionSprites.Add(EPartnerEmotion::Happy, Data);
	Face->EmotionSprites.Add(EPartnerEmotion::Closed, Data);
	// 이 테스트는 BeginPlay를 생략하므로 평상시 깜빡임을 직접 시작한다.
	Partner->NotifyStandbyActivity();
	Face->SetBlinkingActive(true);
	TestFalse(TEXT("Normal blinking does not require standby"), Partner->bIsStandby);
	TestTrue(TEXT("Normal blinking schedules its timer"), World->GetTimerManager().TimerExists(Face->BlinkTimerHandle));
	Face->PlayBlink();
	TestEqual(TEXT("Standby blink closes the eyes"), Face->CurrentEmotion, EPartnerEmotion::Closed);
	Face->FinishBlink();
	TestEqual(TEXT("Blink finishes at Default"), Face->CurrentEmotion, EPartnerEmotion::Default);
	Face->PlayBlink();
	TestTrue(TEXT("Standby owns its blink"), Face->bOwnsBlink);
	Partner->NotifyStandbyActivity();
	TestEqual(TEXT("Activity does not interrupt a blink"), Face->CurrentEmotion, EPartnerEmotion::Closed);
	TestTrue(TEXT("Activity preserves the blink timer"), World->GetTimerManager().TimerExists(Face->BlinkTimerHandle));
	Partner->SetStandby(true);
	TestEqual(TEXT("Entering standby also preserves a blink"), Face->CurrentEmotion, EPartnerEmotion::Closed);
	Face->FinishBlink();
	TestEqual(TEXT("Blink reopens the eyes after activity"), Face->CurrentEmotion, EPartnerEmotion::Default);

	Partner->SetStandby(true);
	Face->PlayBlink();
	// 같은 표정을 외부에서 요청해도 대기 표정의 소유권을 넘겨야 한다.
	Face->SetEmotion(EPartnerEmotion::Closed);
	TestFalse(TEXT("Explicit request takes ownership even for the same emotion"), Face->bOwnsBlink);
	TestFalse(TEXT("Explicit emotion pauses idle changes"), World->GetTimerManager().TimerExists(Face->BlinkTimerHandle));
	Partner->SetStandby(false);
	TestEqual(TEXT("Waking preserves an explicit gameplay expression"), Face->CurrentEmotion, EPartnerEmotion::Closed);
	Partner->SetStandby(true);
	TestFalse(TEXT("Standby cannot overwrite an explicit expression"), World->GetTimerManager().TimerExists(Face->BlinkTimerHandle));
	Face->SetEmotion(EPartnerEmotion::Default);
	TestTrue(TEXT("Default releases priority and resumes the cycle"), World->GetTimerManager().TimerExists(Face->BlinkTimerHandle));
	Face->SetTemporaryEmotion(EPartnerEmotion::Happy);
	TestTrue(TEXT("Skill expression schedules automatic reset"), World->GetTimerManager().TimerExists(Face->TemporaryEmotionTimerHandle));
	Face->ResetTemporaryEmotion();
	TestEqual(TEXT("Skill reset restores Default"), Face->CurrentEmotion, EPartnerEmotion::Default);
	TestTrue(TEXT("Skill reset resumes standby expressions"), World->GetTimerManager().TimerExists(Face->BlinkTimerHandle));
	Face->SetTemporaryEmotion(EPartnerEmotion::Happy);
	Face->SetEmotion(EPartnerEmotion::Angry);
	TestFalse(TEXT("A newer explicit emotion cancels the old reset"), World->GetTimerManager().TimerExists(Face->TemporaryEmotionTimerHandle));
	Face->SetTemporaryEmotion(EPartnerEmotion::Surprised);
	Face->SetDeathEmotionActive(true);
	TestEqual(TEXT("Death closes the eyes immediately"), Face->CurrentEmotion, EPartnerEmotion::Closed);
	TestFalse(TEXT("Death cancels an old hit reset"), World->GetTimerManager().TimerExists(Face->TemporaryEmotionTimerHandle));
	Face->SetTemporaryEmotion(EPartnerEmotion::Angry);
	Face->ResetTemporaryEmotion();
	TestEqual(TEXT("Skills and delayed resets cannot override death"), Face->CurrentEmotion, EPartnerEmotion::Closed);
	Face->SetDeathEmotionActive(false);
	TestEqual(TEXT("Recovery restores Default"), Face->CurrentEmotion, EPartnerEmotion::Default);
	TestTrue(TEXT("Recovery resumes normal blinking"), World->GetTimerManager().TimerExists(Face->BlinkTimerHandle));
	Partner->NotifySkillExecuted();
	TestEqual(TEXT("Successful skill uses Angry"), Face->CurrentEmotion, EPartnerEmotion::Angry);
	return true;
}

#endif
