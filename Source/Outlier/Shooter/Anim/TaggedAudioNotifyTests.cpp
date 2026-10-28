#include "Shooter/Anim/AnimNotify_TaggedAudio.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTaggedAudioNotifyOwnershipTest,
	"Outlier.Audio.Animation.GameplayEventOwnership",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTaggedAudioNotifyOwnershipTest::RunTest(const FString& Parameters)
{
	// 애니메이션의 세부음은 허용하되 실제 성공 이벤트와 표면 Hit의 중복 요청은 막는다.
	for (const TCHAR* Name : {
		TEXT("Audio.Context.Player.01.Type.Pistol.Shoot"),
		TEXT("Audio.Context.Player.01.Type.Rifle.Shoot"),
		TEXT("Audio.Context.Player.02.Type.2P.Shoot"),
		TEXT("Audio.Context.Player.01.Type.Rifle.Empty"),
		TEXT("Audio.Context.Player.01.Type.Melee.Swing"),
		TEXT("Audio.Context.Player.01.Type.Melee.change"),
		TEXT("Audio.Context.Player.01.Type.Pistol.Change"),
		TEXT("Audio.Context.Player.01.Type.Rifle.Change"),
		TEXT("Audio.Context.Player.01.Movement.Jump.Plastic"),
		TEXT("Audio.Context.Player.01.Movement.Jump.Metal"),
		TEXT("Audio.Context.Player.01.Movement.Land.Plastic"),
		TEXT("Audio.Context.Player.01.Movement.Land.Metal"),
		TEXT("Audio.Context.Player.01.Movement.Sliding"),
		TEXT("Audio.Context.Player.01.Type.Pistol.Hit.Plastic"),
		TEXT("Audio.Context.Player.01.Type.Rifle.Hit.Glass"),
		TEXT("Audio.Context.Player.01.Type.Melee.Hit.Plastic"),
		TEXT("Audio.Context.Player.01.Type.Melee.Hit.Marble"),
		TEXT("Audio.Context.Player.01.Type.Melee.Hit.Metal"),
		TEXT("Audio.Context.Player.01.Type.Melee.Hit.Glass") })
	{
		const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(Name), false);
		TestTrue(FString::Printf(TEXT("Registered gameplay context: %s"), Name), Tag.IsValid());
		TestTrue(FString::Printf(TEXT("Notify skips gameplay-owned context: %s"), Name),
			UAnimNotify_TaggedAudio::IsGameplayOwnedContext(Tag));
	}

	for (const TCHAR* Name : {
		TEXT("Audio.Context.Player.01.Type.Pistol.Unload"),
		TEXT("Audio.Context.Player.01.Type.Rifle.Unload"),
		TEXT("Audio.Context.Player.01.Movement.Footstep.Marble"),
		TEXT("Audio.Context.Player.01.Type.Pistol.Load"),
		TEXT("Audio.Context.Player.01.Type.Rifle.Load"),
		TEXT("Audio.Context.Player.01.Movement.Footstep.Plastic"),
		TEXT("Audio.Context.Player.01.Movement.Footstep.Metal") })
	{
		const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(Name), false);
		TestTrue(FString::Printf(TEXT("Registered animation context: %s"), Name), Tag.IsValid());
		TestFalse(FString::Printf(TEXT("Notify permits animation context: %s"), Name),
			UAnimNotify_TaggedAudio::IsGameplayOwnedContext(Tag));
	}
	return true;
}
#endif
