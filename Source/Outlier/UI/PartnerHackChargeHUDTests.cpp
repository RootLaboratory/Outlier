#if WITH_DEV_AUTOMATION_TESTS

#include "AbilityIconUI.h"
#include "AbilitySlotUI.h"
#include "Components/Image.h"
#include "Components/SizeBox.h"
#include "Engine/Texture2D.h"
#include "Materials/Material.h"
#include "Misc/AutomationTest.h"
#include "TagDrivenUIGameplayTags.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPartnerHackChargeHUDRetryTest, "Outlier.UI.Partner.HackingChargeRetry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPartnerHackChargeHUDRetryTest::RunTest(const FString& Parameters)
{
	UAbilitySlotUI* Slot = NewObject<UAbilitySlotUI>();
	UAbilityIconUI* Icon = NewObject<UAbilityIconUI>(Slot);
	Slot->CurrentAbilityIcon = Icon;
	Slot->SkillCount_1 = NewObject<USizeBox>(Slot);
	Slot->SkillCount_2 = NewObject<USizeBox>(Slot);
	Slot->SkillCountOnTexture = NewObject<UTexture2D>(Slot);
	Slot->SkillCountOffTexture = NewObject<UTexture2D>(Slot);
	Icon->AbilityIcon = NewObject<UImage>(Icon);
	Icon->GlowImage = NewObject<UImage>(Icon);
	Icon->M_AbilityIconMaster = UMaterial::GetDefaultMaterial(MD_UI);
	Slot->SetIconTexture(NewObject<UTexture2D>(Slot));
	Slot->RegisterSkillCountImages(NewObject<UImage>(Slot), NewObject<UImage>(Slot), NewObject<UImage>(Slot));
	const FGameplayTag Hack = TagDrivenUITags::Ability::Partner::Hacking();
	Slot->SetCurrentAbility(Hack);
	Slot->SetAbilityChargeState(Hack, 1, 1, 0.0f, 0.0f, false);
	Slot->SetAbilityChargeState(Hack, 1, 1, 1.5f, 1.0f, false, true);
	TestTrue(TEXT("Single-charge retry turns its bar off without consuming the charge"),
		Slot->SkillCountSingleImage->GetBrush().GetResourceObject() == Slot->SkillCountOffTexture);
	TestTrue(TEXT("Single-charge retry displays its cooldown gauge"), Icon->IsCooldowning());
	Slot->SetAbilityChargeState(Hack, 1, 1, 0.0f, 0.0f, false, false);
	TestTrue(TEXT("Single-charge retry expiry restores its owned bar"),
		Slot->SkillCountSingleImage->GetBrush().GetResourceObject() == Slot->SkillCountOnTexture);
	Slot->NativeTick(FGeometry(), Slot->RecoveryPulseDuration * 0.5f);
	TestTrue(TEXT("Single-charge retry expiry pulses the restored bar"),
		Slot->SkillCountSingleImage->GetRenderTransform().Scale.X > 1.0f);
	TestTrue(TEXT("Retry expiry does not play full-charge glow"),
		FMath::IsNearlyZero(Icon->GlowImage->GetRenderOpacity()));
	// 중복 통보로 펄스가 다시 시작되지 않는지 완료 후 한 번 더 확인한다.
	Slot->NativeTick(FGeometry(), Slot->RecoveryPulseDuration);
	Slot->SetAbilityChargeState(Hack, 1, 1, 0.0f, 0.0f, false, false);
	Slot->NativeTick(FGeometry(), Slot->RecoveryPulseDuration * 0.5f);
	TestTrue(TEXT("Duplicate ready state does not restart the retry pulse"),
		FMath::IsNearlyEqual(Slot->SkillCountSingleImage->GetRenderTransform().Scale.X, 1.0f));
	Slot->SetAbilityChargeState(Hack, 2, 2, 0.0f, 0.0f, false);

	// 2회에서도 제한 중에는 막대를 끄고, 제한이 끝나면 실제 보유 횟수로 복구한다.
	Slot->SetAbilityChargeState(Hack, 2, 2, 1.5f, 1.0f, false, true);
	TestTrue(TEXT("Retry blocking turns both bars off"),
		Slot->SkillCountLeftImage->GetBrush().GetResourceObject() == Slot->SkillCountOffTexture
		&& Slot->SkillCountRightImage->GetBrush().GetResourceObject() == Slot->SkillCountOffTexture);
	TestNotNull(TEXT("Retry blocking applies the unavailable icon material without a recharge"), Icon->AbilityMID.Get());
	TestTrue(TEXT("Retry blocking displays its cooldown gauge"), Icon->IsCooldowning());
	Slot->SetAbilityChargeState(Hack, 2, 2, 0.0f, 0.0f, false, false);
	TestTrue(TEXT("Retry expiry restores both owned charges"),
		Slot->SkillCountLeftImage->GetBrush().GetResourceObject() == Slot->SkillCountOnTexture
		&& Slot->SkillCountRightImage->GetBrush().GetResourceObject() == Slot->SkillCountOnTexture);
	TestTrue(TEXT("Retry expiry restores the ready icon brush"),
		Icon->AbilityIcon->GetBrush().GetResourceObject() == Slot->IconTexture);
	Icon->UpdateChargeFeedback(Icon->FullChargeFeedbackDuration * 0.5f);
	TestTrue(TEXT("Retry expiry does not play recovery glow"), FMath::IsNearlyZero(Icon->GlowImage->GetRenderOpacity()));

	Slot->SetAbilityChargeState(Hack, 1, 2, 8.0f, 6.0f, false, true);
	TestTrue(TEXT("Retry blocking preserves the ongoing recharge gauge"), Icon->IsCooldowning());
	TestTrue(TEXT("Retry blocking also hides the remaining charge during recharge"),
		Slot->SkillCountLeftImage->GetBrush().GetResourceObject() == Slot->SkillCountOffTexture
		&& Slot->SkillCountRightImage->GetBrush().GetResourceObject() == Slot->SkillCountOffTexture);
	Slot->ApplyCooldownIfMatches(Hack, 0.0f);
	TestTrue(TEXT("Generic cooldown completion cannot clear a charged ability gauge"), Icon->IsCooldowning());
	Slot->SetAbilityChargeState(Hack, 1, 2, 8.0f, 4.0f, false, false);
	TestTrue(TEXT("Retry expiry retains the remaining charge and recharge gauge"), Icon->IsCooldowning());
	TestTrue(TEXT("Retry expiry restores only the remaining left bar"),
		Slot->SkillCountLeftImage->GetBrush().GetResourceObject() == Slot->SkillCountOnTexture
		&& Slot->SkillCountRightImage->GetBrush().GetResourceObject() == Slot->SkillCountOffTexture);

	Slot->SetAbilityChargeState(Hack, 2, 2, 0.0f, 0.0f, true);
	Icon->UpdateChargeFeedback(Icon->FullChargeFeedbackDuration * 0.5f);
	TestTrue(TEXT("Natural full recovery still plays glow"), Icon->GlowImage->GetRenderOpacity() > 0.0f);
	Slot->NativeDestruct();
	Slot->SetAbilityChargeState(Hack, 2, 2, 0.0f, 0.0f, true);
	Icon->UpdateChargeFeedback(Icon->FullChargeFeedbackDuration * 0.5f);
	TestTrue(TEXT("Recreated HUD treats its first state as synchronization"), FMath::IsNearlyZero(Icon->GlowImage->GetRenderOpacity()));
	return true;
}

#endif
