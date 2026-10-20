#if WITH_DEV_AUTOMATION_TESTS

#include "AbilityIconUI.h"
#include "GangTongMainUI.h"
#include "PartnerRightHudWidget.h"
#include "AbilitySlotUI.h"
#include "ShooterMainWidget.h"
#include "ShooterStatusBarWidget.h"
#include "TagDrivenUIGameplayTags.h"

#include "Components/Image.h"
#include "Components/CanvasPanel.h"
#include "Components/SizeBox.h"
#include "Engine/Texture2D.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/AutomationTest.h"
#include "UObject/UnrealType.h"

namespace ShooterChargeHUDTests
{
	struct FHUD
	{
		UShooterStatusBarWidget* StatusBar = NewObject<UShooterStatusBarWidget>();
		UAbilitySlotUI* Widget = NewObject<UAbilitySlotUI>();
		UAbilityIconUI* Icon = NewObject<UAbilityIconUI>(Widget);
		UTexture2D* OnTexture = NewObject<UTexture2D>(Widget);
		UTexture2D* OffTexture = NewObject<UTexture2D>(Widget);

		FHUD()
		{
			FObjectPropertyBase* Binding = FindFProperty<FObjectPropertyBase>(StatusBar->GetClass(), TEXT("CurrentAbilityUI"));
			check(Binding);
			Binding->SetObjectPropertyValue_InContainer(StatusBar, Widget);
			Widget->CurrentAbilityIcon = Icon;
			Widget->SkillCount_1 = NewObject<USizeBox>(Widget);
			Widget->SkillCount_2 = NewObject<USizeBox>(Widget);
			Widget->SkillCountOnTexture = OnTexture;
			Widget->SkillCountOffTexture = OffTexture;
			StatusBar->AbilityIconTextures.Add(TagDrivenUITags::Ability::Shooter::QuantumLeap(), NewObject<UTexture2D>(StatusBar));
			StatusBar->AbilityIconTextures.Add(TagDrivenUITags::Ability::Shooter::Stealth(), NewObject<UTexture2D>(StatusBar));
			StatusBar->AbilityIconTextures.Add(TagDrivenUITags::Ability::Shooter::BulletReflection(), NewObject<UTexture2D>(StatusBar));
			StatusBar->AbilityIconTextures.Add(TagDrivenUITags::Ability::Shooter::WeaponOvercharge(), NewObject<UTexture2D>(StatusBar));
			Icon->AbilityIcon = NewObject<UImage>(Icon);
			Icon->GlowImage = NewObject<UImage>(Icon);
			Icon->ResetChargeFeedback();
			Widget->RegisterSkillCountImages(NewObject<UImage>(Widget), NewObject<UImage>(Widget), NewObject<UImage>(Widget));
			Select(TagDrivenUITags::Ability::Shooter::QuantumLeap());
		}
		void Select(const FGameplayTag& AbilityTag) { StatusBar->SetCurrentAbilityPresentation(AbilityTag); }
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShooterChargeHUDBarsTest, "Outlier.UI.ShooterCharges.BarsAndSelection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterChargeHUDBarsTest::RunTest(const FString& Parameters)
{
	using namespace ShooterChargeHUDTests;
	FHUD HUD;
	UAbilitySlotUI* Widget = HUD.Widget;
	const FGameplayTag Leap = TagDrivenUITags::Ability::Shooter::QuantumLeap();
	const FGameplayTag Stealth = TagDrivenUITags::Ability::Shooter::Stealth();
	const auto CheckBars = [this, &HUD, Widget](const TCHAR* Context, bool bLeftOn, bool bRightOn)
	{
		TestTrue(FString::Printf(TEXT("%s: left bar uses its expected texture"), Context),
			Widget->SkillCountLeftImage->GetBrush().GetResourceObject() == (bLeftOn ? HUD.OnTexture : HUD.OffTexture));
		TestTrue(FString::Printf(TEXT("%s: right bar uses its expected texture"), Context),
			Widget->SkillCountRightImage->GetBrush().GetResourceObject() == (bRightOn ? HUD.OnTexture : HUD.OffTexture));
	};

	TestEqual(TEXT("An ability without charge information defaults to maximum one"), Widget->GetAbilitySkillCount(Leap), 1);
	TestTrue(TEXT("Default maximum one selects the single-slot container"), Widget->SkillCount_1->GetVisibility() == ESlateVisibility::Visible);
	TestTrue(TEXT("Default maximum one hides the double-slot container"), Widget->SkillCount_2->GetVisibility() == ESlateVisibility::Collapsed);

	Widget->SetAbilityChargeState(Leap, 0, 1, 8.0f, 6.0f, false);
	TestTrue(TEXT("An empty single charge keeps its container visible"), Widget->SkillCount_1->GetVisibility() == ESlateVisibility::Visible);
	TestTrue(TEXT("An empty single charge uses the off texture"), Widget->SkillCountSingleImage->GetBrush().GetResourceObject() == HUD.OffTexture);
	Widget->SetAbilityChargeState(Leap, 1, 1, 0.0f, 0.0f, false);
	TestTrue(TEXT("A ready single charge uses the on texture"), Widget->SkillCountSingleImage->GetBrush().GetResourceObject() == HUD.OnTexture);

	// 최대 횟수로 컨테이너를 고르고, 실제 잔여 횟수로 각 막대의 점등을 결정한다.
	Widget->SetAbilityChargeState(Leap, 2, 2, 0.0f, 0.0f, false);
	TestTrue(TEXT("Maximum two hides the single-slot container"), Widget->SkillCount_1->GetVisibility() == ESlateVisibility::Collapsed);
	TestTrue(TEXT("Maximum two selects the double-slot container"), Widget->SkillCount_2->GetVisibility() == ESlateVisibility::Visible);
	CheckBars(TEXT("Full"), true, true);
	Widget->SetAbilityChargeState(Leap, 1, 2, 8.0f, 8.0f, false);
	CheckBars(TEXT("One successful use leaves the left bar lit"), true, false);
	Widget->SetAbilityChargeState(Leap, 0, 2, 8.0f, 6.0f, false);
	CheckBars(TEXT("All charges spent"), false, false);
	TestTrue(TEXT("Zero remaining charges do not hide the double-slot container"), Widget->SkillCount_2->GetVisibility() == ESlateVisibility::Visible);
	Widget->SetAbilityChargeState(Leap, 1, 2, 8.0f, 8.0f, false);
	CheckBars(TEXT("First recovery"), true, false);
	Widget->SetAbilityChargeState(Leap, 2, 2, 0.0f, 0.0f, false);
	CheckBars(TEXT("Final recovery"), true, true);

	HUD.Select(Stealth);
	HUD.Icon->NativePreConstruct();
	TestTrue(TEXT("Repeated initialization preserves the selected skill texture"), HUD.Icon->IconTexture == HUD.StatusBar->GetAbilityIconTexture(TagDrivenUITags::Ability::Shooter::Stealth()));
	TestTrue(TEXT("Selecting another default ability restores its single-slot layout"), Widget->SkillCount_1->GetVisibility() == ESlateVisibility::Visible);
	Widget->SetAbilityChargeState(Leap, 0, 2, 8.0f, 4.0f, false);
	TestTrue(TEXT("A background ability update does not replace the selected layout"), Widget->SkillCount_1->GetVisibility() == ESlateVisibility::Visible);
	HUD.Select(Leap);
	CheckBars(TEXT("Reselecting an empty ability"), false, false);
	TestTrue(TEXT("Reselection uses the cached maximum, not its zero remaining count"), Widget->SkillCount_2->GetVisibility() == ESlateVisibility::Visible);

	UImage* ReplacementLeft = NewObject<UImage>(Widget);
	UImage* ReplacementRight = NewObject<UImage>(Widget);
	Widget->RegisterSkillCountImages(NewObject<UImage>(Widget), ReplacementLeft, ReplacementRight);
	TestTrue(TEXT("Registering BP images immediately applies the cached empty state"),
		ReplacementLeft->GetBrush().GetResourceObject() == HUD.OffTexture && ReplacementRight->GetBrush().GetResourceObject() == HUD.OffTexture);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShooterChargeHUDFeedbackTest, "Outlier.UI.ShooterCharges.RecoveryFeedback",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterChargeHUDFeedbackTest::RunTest(const FString& Parameters)
{
	using namespace ShooterChargeHUDTests;
	FHUD HUD;
	UAbilitySlotUI* Widget = HUD.Widget;
	UAbilityIconUI* Icon = HUD.Icon;
	const FGameplayTag Leap = TagDrivenUITags::Ability::Shooter::QuantumLeap();
	const FGameplayTag Stealth = TagDrivenUITags::Ability::Shooter::Stealth();
	const float HalfGlowDuration = Icon->FullChargeFeedbackDuration * 0.5f;
	const FVector2D OriginalLeftScale(0.8, 1.2);
	const FVector2D OriginalLeftPivot(0.2, 0.7);
	const FVector2D OriginalRightPivot(0.1, 0.3);
	Widget->SkillCountLeftImage->SetRenderScale(OriginalLeftScale);
	Widget->SkillCountLeftImage->SetRenderTransformPivot(OriginalLeftPivot);
	Widget->SkillCountRightImage->SetRenderTransformPivot(OriginalRightPivot);

	Widget->SetAbilityChargeState(Leap, 1, 1, 0.0f, 0.0f, true);
	Icon->UpdateChargeFeedback(HalfGlowDuration);
	TestTrue(TEXT("First full-state synchronization never plays glow"), FMath::IsNearlyZero(Icon->GlowImage->GetRenderOpacity()));
	Widget->SetAbilityChargeState(Leap, 2, 2, 0.0f, 0.0f, true);
	Icon->UpdateChargeFeedback(HalfGlowDuration);
	TestTrue(TEXT("A maximum-charge upgrade never masquerades as a recovery"), FMath::IsNearlyZero(Icon->GlowImage->GetRenderOpacity()));
	TestTrue(TEXT("An upgrade does not start the left bar pulse"), Widget->SkillCountLeftImage->GetRenderTransformPivot().Equals(OriginalLeftPivot));

	Widget->SetAbilityChargeState(Leap, 0, 2, 8.0f, 3.0f, false);
	Widget->SetAbilityChargeState(Leap, 1, 2, 8.0f, 8.0f, true);
	TestTrue(TEXT("First recovery pulses the newly lit left bar"), Widget->SkillCountLeftImage->GetRenderTransformPivot().Equals(FVector2D(0.5, 0.5)));
	TestTrue(TEXT("First recovery leaves the still-empty right bar alone"), Widget->SkillCountRightImage->GetRenderTransformPivot().Equals(OriginalRightPivot));
	Icon->UpdateChargeFeedback(HalfGlowDuration);
	TestTrue(TEXT("Partial recovery does not play full-charge glow"), FMath::IsNearlyZero(Icon->GlowImage->GetRenderOpacity()));

	Widget->SetAbilityChargeState(Leap, 2, 2, 0.0f, 0.0f, true);
	TestTrue(TEXT("Final recovery pulses the newly lit right bar"), Widget->SkillCountRightImage->GetRenderTransformPivot().Equals(FVector2D(0.5, 0.5)));
	TestTrue(TEXT("Changing the pulsing bar restores the prior bar pivot"), Widget->SkillCountLeftImage->GetRenderTransformPivot().Equals(OriginalLeftPivot));
	Icon->UpdateChargeFeedback(HalfGlowDuration);
	TestTrue(TEXT("Actual full recovery plays visible glow"), Icon->GlowImage->GetRenderOpacity() > 0.0f);
	TestTrue(TEXT("Glow uses the same skill icon texture"), Icon->GlowImage->GetBrush().GetResourceObject() == HUD.StatusBar->GetAbilityIconTexture(TagDrivenUITags::Ability::Shooter::QuantumLeap()));
	TestTrue(TEXT("Glow expands the effect image rather than the original icon"), Icon->GlowImage->GetRenderTransform().Scale.X > 1.0 && Icon->AbilityIcon->GetRenderTransform().Scale.Equals(FVector2D::UnitVector));
	Icon->UpdateChargeFeedback(Icon->FullChargeFeedbackDuration);
	TestTrue(TEXT("Completed glow returns to zero opacity"), FMath::IsNearlyZero(Icon->GlowImage->GetRenderOpacity()));
	TestTrue(TEXT("Completed glow restores its original scale"), Icon->GlowImage->GetRenderTransform().Scale.Equals(FVector2D::UnitVector));

	HUD.Select(Stealth);
	TestTrue(TEXT("Switching ability restores the interrupted bar scale"), Widget->SkillCountLeftImage->GetRenderTransform().Scale.Equals(OriginalLeftScale));
	TestTrue(TEXT("Switching ability restores the interrupted bar pivot"), Widget->SkillCountLeftImage->GetRenderTransformPivot().Equals(OriginalLeftPivot));
	TestTrue(TEXT("Switching ability restores the final recovery bar pivot"), Widget->SkillCountRightImage->GetRenderTransformPivot().Equals(OriginalRightPivot));
	TestTrue(TEXT("Switching updates the glow texture with the icon"), Icon->GlowImage->GetBrush().GetResourceObject() == HUD.StatusBar->GetAbilityIconTexture(TagDrivenUITags::Ability::Shooter::Stealth()));
	Widget->SetAbilityChargeState(Leap, 1, 2, 8.0f, 2.0f, false);
	Widget->SetAbilityChargeState(Leap, 2, 2, 0.0f, 0.0f, true);
	Icon->UpdateChargeFeedback(HalfGlowDuration);
	TestTrue(TEXT("Recovery of an unselected ability never plays glow on the selected icon"), FMath::IsNearlyZero(Icon->GlowImage->GetRenderOpacity()));
	HUD.Select(Leap);
	Icon->UpdateChargeFeedback(HalfGlowDuration);
	TestTrue(TEXT("Reselecting a recovered ability does not replay recovery glow"), FMath::IsNearlyZero(Icon->GlowImage->GetRenderOpacity()));

	Widget->SetAbilityChargeState(Leap, 1, 2, 8.0f, 1.0f, false);
	Widget->SetAbilityChargeState(Leap, 2, 2, 0.0f, 0.0f, true);
	Icon->UpdateChargeFeedback(HalfGlowDuration);
	HUD.Select(Stealth);
	TestTrue(TEXT("Switching during glow immediately hides it"), FMath::IsNearlyZero(Icon->GlowImage->GetRenderOpacity()));
	TestTrue(TEXT("Switching during glow restores its scale"), Icon->GlowImage->GetRenderTransform().Scale.Equals(FVector2D::UnitVector));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShooterAbilityCooldownBarTest, "Outlier.UI.ShooterCharges.AbilityCooldownBars",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterAbilityCooldownBarTest::RunTest(const FString& Parameters)
{
	using namespace ShooterChargeHUDTests;
	const FGameplayTag Abilities[] =
	{
		TagDrivenUITags::Ability::Shooter::Stealth(),
		TagDrivenUITags::Ability::Shooter::BulletReflection(),
		TagDrivenUITags::Ability::Shooter::WeaponOvercharge()
	};
	for (const FGameplayTag& Ability : Abilities)
	{
		FHUD HUD;
		UAbilitySlotUI* Widget = HUD.Widget;
		UAbilityIconUI* Icon = HUD.Icon;
		UImage* Bar = Widget->SkillCountSingleImage;
		const FVector2D OriginalPivot(0.2, 0.7);
		Bar->SetRenderTransformPivot(OriginalPivot);
		HUD.Select(Ability);
		Widget->ApplyCooldownIfMatches(Ability, 0.0f);
		TestTrue(TEXT("Initial ready synchronization does not pulse"), Bar->GetRenderTransformPivot().Equals(OriginalPivot));
		TestTrue(TEXT("A ready regular ability lights its bar"), Bar->GetBrush().GetResourceObject() == HUD.OnTexture);

		Widget->ApplyCooldownIfMatches(Ability, 8.0f);
		TestTrue(TEXT("Cooldown start turns the regular ability bar off"), Bar->GetBrush().GetResourceObject() == HUD.OffTexture);
		Widget->ApplyCooldownIfMatches(Ability, 0.0f);
		TestTrue(TEXT("Server cooldown completion lights the regular ability bar"), Bar->GetBrush().GetResourceObject() == HUD.OnTexture);
		TestTrue(TEXT("Server cooldown completion pulses the regular ability bar"), Bar->GetRenderTransformPivot().Equals(FVector2D(0.5, 0.5)));
		Icon->UpdateChargeFeedback(Icon->FullChargeFeedbackDuration * 0.5f);
		TestTrue(TEXT("Regular ability recovery plays full-charge glow"), Icon->GlowImage->GetRenderOpacity() > 0.0f);

		Widget->RegisterSkillCountImages(Bar, Widget->SkillCountLeftImage, Widget->SkillCountRightImage);
		Icon->ResetChargeFeedback();
		Widget->ApplyCooldownIfMatches(Ability, 0.0f);
		TestTrue(TEXT("Duplicate completion does not restart the bar pulse"), Bar->GetRenderTransformPivot().Equals(OriginalPivot));
		Icon->UpdateChargeFeedback(Icon->FullChargeFeedbackDuration * 0.5f);
		TestTrue(TEXT("Duplicate completion does not replay glow"), FMath::IsNearlyZero(Icon->GlowImage->GetRenderOpacity()));

		Widget->ApplyCooldownIfMatches(Ability, 8.0f);
		HUD.Select(TagDrivenUITags::Ability::Shooter::QuantumLeap());
		Widget->ApplyCooldownIfMatches(Ability, 0.0f);
		TestTrue(TEXT("A background completion does not pulse the selected bar"), Bar->GetRenderTransformPivot().Equals(OriginalPivot));
		HUD.Select(Ability);
		TestTrue(TEXT("Reselecting a recovered regular ability restores its lit bar"), Bar->GetBrush().GetResourceObject() == HUD.OnTexture);
		TestTrue(TEXT("Reselecting a recovered regular ability does not replay its pulse"), Bar->GetRenderTransformPivot().Equals(OriginalPivot));
		Icon->UpdateChargeFeedback(Icon->FullChargeFeedbackDuration * 0.5f);
		TestTrue(TEXT("Reselecting a recovered regular ability does not replay glow"), FMath::IsNearlyZero(Icon->GlowImage->GetRenderOpacity()));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShooterAutoAimRadiusVisibilityTest, "Outlier.UI.ShooterAutoAim.RadiusVisibility",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterAutoAimRadiusVisibilityTest::RunTest(const FString& Parameters)
{
	UShooterMainWidget* HUD = NewObject<UShooterMainWidget>();
	HUD->DefaultLayer = NewObject<UCanvasPanel>(HUD);
	UImage* RadiusImage = NewObject<UImage>(HUD);
	HUD->DefaultLayer->AddChild(RadiusImage);
	// BP의 BindWidget이 연결하는 private Image에 같은 참조를 주입해 직속 배치를 재현한다.
	FObjectPropertyBase* ImageBinding = FindFProperty<FObjectPropertyBase>(HUD->GetClass(), TEXT("AutoAimRadiusImage"));
	if (!TestNotNull(TEXT("The auto aim image binding exists"), ImageBinding)) return false;
	ImageBinding->SetObjectPropertyValue_InContainer(HUD, RadiusImage);

	HUD->SetAutoAimPresentation(false);
	TestTrue(TEXT("Initial inactive state overrides a designer-visible image"), RadiusImage->GetVisibility() == ESlateVisibility::Collapsed);
	HUD->ModulesControl(true);
	TestTrue(TEXT("Opening the HUD cannot expose an inactive auto aim image"), RadiusImage->GetVisibility() == ESlateVisibility::Collapsed);
	HUD->SetAutoAimPresentation(true);
	TestTrue(TEXT("Active auto aim shows the image without capturing input"), RadiusImage->GetVisibility() == ESlateVisibility::HitTestInvisible);
	HUD->ModulesControl(false);
	TestTrue(TEXT("Closing the HUD also hides an active auto aim image"), RadiusImage->GetVisibility() == ESlateVisibility::Collapsed);
	HUD->ModulesControl(true);
	TestTrue(TEXT("Reopening the HUD restores active auto aim visibility"), RadiusImage->GetVisibility() == ESlateVisibility::HitTestInvisible);
	HUD->SetAutoAimPresentation(false);
	RadiusImage->SetVisibility(ESlateVisibility::Visible);
	HUD->SetAutoAimPresentation(false);
	TestTrue(TEXT("Repeated inactive synchronization repairs visibility changed elsewhere"), RadiusImage->GetVisibility() == ESlateVisibility::Collapsed);
	HUD->NativePreConstruct();
	TestTrue(TEXT("Widget initialization preserves the inactive condition"), RadiusImage->GetVisibility() == ESlateVisibility::Collapsed);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShooterAbilityIconTextureTest, "Outlier.UI.ShooterCharges.IconTexture",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterAbilityIconTextureTest::RunTest(const FString& Parameters)
{
	UAbilityIconUI* Icon = NewObject<UAbilityIconUI>();
	UTexture2D* InitialTexture = NewObject<UTexture2D>(Icon);
	UTexture2D* SelectedTexture = NewObject<UTexture2D>(Icon);

	// 부모가 자식 Image 바인딩보다 먼저 Texture를 넘겨도 초기화 시 반영한다.
	Icon->SetIconTexture(InitialTexture);
	Icon->AbilityIcon = NewObject<UImage>(Icon);
	Icon->GlowImage = NewObject<UImage>(Icon);
	FSlateBrush DesignerBrush = Icon->AbilityIcon->GetBrush();
	DesignerBrush.SetImageSize(FVector2D(64.0, 48.0));
	Icon->AbilityIcon->SetBrush(DesignerBrush);
	Icon->NativePreConstruct();
	TestTrue(TEXT("Initialization applies the configured icon texture"), Icon->AbilityIcon->GetBrush().GetResourceObject() == InitialTexture);
	TestTrue(TEXT("Initialization applies the same texture to glow"), Icon->GlowImage->GetBrush().GetResourceObject() == InitialTexture);
	TestTrue(TEXT("Texture setup preserves the designer brush size"), Icon->AbilityIcon->GetBrush().GetImageSize() == DesignerBrush.GetImageSize());
	TestFalse(TEXT("Texture setup does not unlock the ability"), Icon->IsUnLock());

	// 기존 WBP는 새 속성을 지정하지 않아도 Image에 설정된 Texture를 그대로 사용한다.
	UAbilityIconUI* LegacyIcon = NewObject<UAbilityIconUI>();
	LegacyIcon->AbilityIcon = NewObject<UImage>(LegacyIcon);
	LegacyIcon->GlowImage = NewObject<UImage>(LegacyIcon);
	LegacyIcon->AbilityIcon->SetBrushFromTexture(InitialTexture);
	LegacyIcon->NativePreConstruct();
	TestTrue(TEXT("An existing texture brush remains the fallback"), LegacyIcon->DefaultIconBrush.GetResourceObject() == InitialTexture);
	TestTrue(TEXT("The legacy fallback also initializes glow"), LegacyIcon->GlowImage->GetBrush().GetResourceObject() == InitialTexture);

	// 프로젝트 에셋 로딩 없이 MID 재사용과 남은 시간 보존을 검사한다. 실제 마스크는 에디터에서 확인한다.
	Icon->M_AbilityIconMaster = UMaterial::GetDefaultMaterial(MD_UI);
	Icon->AbilityUnLock();
	Icon->SetCoolTime(8.0f);
	UMaterialInstanceDynamic* CooldownMID = Icon->AbilityMID;
	if (!TestNotNull(TEXT("Cooldown creates its display MID"), CooldownMID))
	{
		return false;
	}
	Icon->UpdateCoolTime(2.0f);
	Icon->SetIconTexture(SelectedTexture);
	Icon->NativePreConstruct();
	Icon->NativePreConstruct();
	TestTrue(TEXT("Changing texture and repeated initialization reuse the current MID"), Icon->AbilityMID == CooldownMID);
	TestTrue(TEXT("The icon keeps its cooldown material"), Icon->AbilityIcon->GetBrush().GetResourceObject() == CooldownMID);
	TestTrue(TEXT("The original brush contains the new texture rather than a MID"), Icon->DefaultIconBrush.GetResourceObject() == SelectedTexture);
	TestTrue(TEXT("Glow uses the new raw texture rather than a MID"), Icon->GlowImage->GetBrush().GetResourceObject() == SelectedTexture);
	TestTrue(TEXT("Texture changes preserve unlock state"), Icon->IsUnLock());
	TestTrue(TEXT("Texture changes do not affect another icon instance"), LegacyIcon->AbilityIcon->GetBrush().GetResourceObject() == InitialTexture);

	// 자식 Image가 다시 만들어져도 원본 Texture와 진행 중인 머티리얼을 복원한다.
	Icon->AbilityIcon = NewObject<UImage>(Icon);
	Icon->GlowImage = NewObject<UImage>(Icon);
	Icon->NativePreConstruct();
	Icon->NativeConstruct();
	TestTrue(TEXT("Reconstructed images reuse the cooldown MID"), Icon->AbilityIcon->GetBrush().GetResourceObject() == CooldownMID);
	TestTrue(TEXT("Reconstructed glow uses the selected texture"), Icon->GlowImage->GetBrush().GetResourceObject() == SelectedTexture);
	Icon->UpdateCoolTime(5.0f);
	TestTrue(TEXT("The original eight-second cooldown is still running at seven seconds"), Icon->IsCooldowning());
	Icon->UpdateCoolTime(1.0f);
	TestFalse(TEXT("The original cooldown completes at eight seconds despite texture changes"), Icon->IsCooldowning());
	TestTrue(TEXT("Cooldown completion restores the newly selected texture"), Icon->AbilityIcon->GetBrush().GetResourceObject() == SelectedTexture);

	// 잔여 횟수가 없는 상태는 게이지가 없어도 비활성 MID를 유지해야 한다.
	Icon->SetChargePresentation(false, 0.0f, 0.0f);
	UMaterialInstanceDynamic* UnavailableMID = Icon->AbilityMID;
	if (!TestNotNull(TEXT("Unavailable charge presentation creates its MID"), UnavailableMID))
	{
		return false;
	}
	Icon->SetIconTexture(InitialTexture);
	Icon->NativePreConstruct();
	TestTrue(TEXT("Texture changes preserve unavailable charge presentation"), Icon->AbilityMID == UnavailableMID);
	Icon->SetChargePresentation(true, 0.0f, 0.0f);
	TestTrue(TEXT("An available charge restores the updated texture"), Icon->AbilityIcon->GetBrush().GetResourceObject() == InitialTexture);
	Icon->GlowImage = nullptr;
	Icon->SetIconTexture(SelectedTexture);
	TestTrue(TEXT("The glow binding remains optional"), Icon->AbilityIcon->GetBrush().GetResourceObject() == SelectedTexture);
	Icon->NativeDestruct();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPartnerSharedAbilitySlotTest, "Outlier.UI.Partner.SharedAbilitySlots",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPartnerSharedAbilitySlotTest::RunTest(const FString& Parameters)
{
	using namespace ShooterChargeHUDTests;
	UMainUIBase* Main = NewObject<UGangTongMainUI>();
	UPartnerRightHudWidget* RightHUD = NewObject<UPartnerRightHudWidget>(Main);
	FObjectPropertyBase* RightBinding = FindFProperty<FObjectPropertyBase>(Main->GetClass(), TEXT("PartnerRightHUD"));
	if (!TestNotNull(TEXT("Partner HUD keeps its existing container binding"), RightBinding)) return false;
	RightBinding->SetObjectPropertyValue_InContainer(Main, RightHUD);
	const FGameplayTag Tags[] =
	{
		TagDrivenUITags::Ability::Partner::Shield(), TagDrivenUITags::Ability::Partner::Hacking(),
		TagDrivenUITags::Ability::Partner::Scan(), TagDrivenUITags::Ability::Partner::EMP()
	};
	const FName Names[] = { TEXT("ShieldAbilitySlot"), TEXT("HackingAbilitySlot"), TEXT("ScanAbilitySlot"), TEXT("EMPAbilitySlot") };
	TArray<UAbilitySlotUI*> Slots;
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Tags); ++Index)
	{
		FHUD HUD;
		HUD.Widget->IconTexture = NewObject<UTexture2D>(HUD.Widget);
		HUD.Widget->SetCurrentAbility(Tags[Index]);
		HUD.Widget->NativePreConstruct();
		TestTrue(TEXT("A fixed Partner slot uses its configured texture"),
			HUD.Icon->AbilityIcon->GetBrush().GetResourceObject() == HUD.Widget->IconTexture);
		TestTrue(TEXT("Glow uses the same configured texture"),
			HUD.Icon->GlowImage->GetBrush().GetResourceObject() == HUD.Widget->IconTexture);
		FObjectPropertyBase* Binding = FindFProperty<FObjectPropertyBase>(RightHUD->GetClass(), Names[Index]);
		if (!TestNotNull(TEXT("Partner supports the shared slot binding"), Binding)) return false;
		Binding->SetObjectPropertyValue_InContainer(RightHUD, HUD.Widget);
		Slots.Add(HUD.Widget);
	}
	Main->ModuleInit();
	for (int32 Index = 0; Index < Slots.Num(); ++Index)
	{
		UAbilitySlotUI* Slot = Slots[Index];
		TestTrue(TEXT("Partner registers the complete shared slot by ability tag"), Main->GetAbilitySlot(Tags[Index]) == Slot);
		TestTrue(TEXT("Partner registration preserves the per-slot texture"),
			Slot->CurrentAbilityIcon->IconTexture == Slot->IconTexture);
		TestTrue(TEXT("Existing icon lookup still returns the nested icon"), Main->GetAbilityIcon(Tags[Index]) == Slot->CurrentAbilityIcon);
		Slot->ApplyCooldownIfMatches(Tags[Index], 0.0f);
		Slot->CurrentAbilityIcon->UpdateChargeFeedback(0.1f);
		TestEqual(TEXT("Initial synchronization never plays recovery glow"), Slot->CurrentAbilityIcon->GlowImage->GetRenderOpacity(), 0.0f);
		Slot->ApplyCooldownIfMatches(Tags[Index], 5.0f);
		TestTrue(TEXT("Cooldown turns the single bar off"), Slot->SkillCountSingleImage->GetBrush().GetResourceObject() == Slot->SkillCountOffTexture);
		TestTrue(TEXT("Another ability cannot update this slot"), !Slot->ApplyCooldownIfMatches(Tags[(Index + 1) % Slots.Num()], 5.0f));
		Slot->ApplyCooldownIfMatches(Tags[Index], 0.0f);
		TestTrue(TEXT("Confirmed cooldown completion lights the bar"), Slot->SkillCountSingleImage->GetBrush().GetResourceObject() == Slot->SkillCountOnTexture);
		Slot->CurrentAbilityIcon->UpdateChargeFeedback(Slot->CurrentAbilityIcon->FullChargeFeedbackDuration * 0.5f);
		TestTrue(TEXT("Partner receives the same completion glow"), Slot->CurrentAbilityIcon->GlowImage->GetRenderOpacity() > 0.0f);
		TestTrue(TEXT("Partner receives the same bar pulse"), Slot->SkillCountSingleImage->GetRenderTransformPivot().Equals(FVector2D(0.5f, 0.5f)));
	}
	TestTrue(TEXT("Right HUD exposes the registered Shield slot"), RightHUD->GetShieldAbilitySlot() == Slots[0]);
	const FName LegacyBindings[] = { TEXT("AbilityShieldIcon"), TEXT("AbilityHackingIcon"), TEXT("AbilityScanIcon"), TEXT("AbilityEMPIcon") };
	for (const FName LegacyBinding : LegacyBindings)
	{
		TestNull(TEXT("Right HUD no longer binds legacy icons"),
			FindFProperty<FProperty>(UPartnerRightHudWidget::StaticClass(), LegacyBinding));
		TestNull(TEXT("Main HUD no longer caches legacy icons"),
			FindFProperty<FProperty>(UGangTongMainUI::StaticClass(), LegacyBinding));
	}
	Main->ResetAbilityCooldowns();
	TestEqual(TEXT("Reset clears the shared slot glow"), Slots[0]->CurrentAbilityIcon->GlowImage->GetRenderOpacity(), 0.0f);
	Main->AbilitySectionControl(false);
	TestTrue(TEXT("Section visibility hides bars along with the icon"), Slots[0]->GetVisibility() == ESlateVisibility::Collapsed);
	Main->AbilitySectionControl(true);
	TestTrue(TEXT("Section visibility restores the complete slot"), Slots[0]->GetVisibility() == ESlateVisibility::SelfHitTestInvisible);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAbilityHUDTextureRegistryTest, "Outlier.UI.AbilitySlots.ShooterStatusBarTextures",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAbilityHUDTextureRegistryTest::RunTest(const FString& Parameters)
{
	using namespace ShooterChargeHUDTests;
	FHUD HUD;
	const FGameplayTag Leap = TagDrivenUITags::Ability::Shooter::QuantumLeap();
	const FGameplayTag Stealth = TagDrivenUITags::Ability::Shooter::Stealth();
	HUD.Select(Stealth);
	TestTrue(TEXT("Changing selection obtains the next texture from the HUD"),
		HUD.Widget->IconTexture == HUD.StatusBar->GetAbilityIconTexture(Stealth));
	HUD.Widget->ApplyCooldownIfMatches(Stealth, 5.0f);
	HUD.Select(Stealth);
	TestTrue(TEXT("Reapplying the HUD texture preserves the active cooldown bar"),
		HUD.Widget->SkillCountSingleImage->GetBrush().GetResourceObject() == HUD.OffTexture);
	HUD.StatusBar->AbilityIconTextures.Remove(Leap);
	HUD.Select(Leap);
	TestNull(TEXT("An unconfigured tag clears the previous slot texture"), HUD.Widget->IconTexture.Get());
	TestTrue(TEXT("Missing texture never displays the previous skill icon"), HUD.Icon->GetVisibility() == ESlateVisibility::Collapsed);
	TestNull(TEXT("The common HUD does not own Shooter texture mappings"),
		FindFProperty<FProperty>(UMainUIBase::StaticClass(), TEXT("AbilityIconTextures")));
	TestNull(TEXT("The shared slot no longer has a Shooter-specific texture property"),
		FindFProperty<FProperty>(UAbilitySlotUI::StaticClass(), TEXT("QuantumLeapTexture")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShooterAbilityIconLayoutTest, "Outlier.UI.AbilitySlots.ShooterIconLayout",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterAbilityIconLayoutTest::RunTest(const FString& Parameters)
{
	using namespace ShooterChargeHUDTests;
	FHUD HUD;
	HUD.Widget->IconSizeBox = NewObject<USizeBox>(HUD.Widget);
	USizeBox* IconBox = HUD.Widget->IconSizeBox;
	IconBox->AddChild(HUD.Icon);
	IconBox->SetWidthOverride(100.0f);
	IconBox->SetHeightOverride(100.0f);
	IconBox->SetRenderTranslation(FVector2D(2.0f, 3.0f));
	HUD.Widget->SkillCount_1->SetRenderTranslation(FVector2D(5.0f, 6.0f));

	const FGameplayTag Leap = TagDrivenUITags::Ability::Shooter::QuantumLeap();
	const FGameplayTag Stealth = TagDrivenUITags::Ability::Shooter::Stealth();
	FShooterAbilityIconLayout& Layout = HUD.StatusBar->AbilityIconLayouts.Add(Leap);
	Layout.RenderScale = FVector2D(1.2f, 0.9f);
	Layout.PositionOffset = FVector2D(10.0f, -5.0f);
	HUD.Select(Leap);
	TestEqual(TEXT("Selected skill preserves the fixed layout width"), IconBox->GetWidthOverride(), 100.0f);
	TestEqual(TEXT("Selected skill preserves the fixed layout height"), IconBox->GetHeightOverride(), 100.0f);
	TestTrue(TEXT("Selected skill applies render scale"),
		IconBox->GetRenderTransform().Scale.Equals(FVector2D(1.2f, 0.9f)));
	TestTrue(TEXT("Icon scales around its center"), IconBox->GetRenderTransformPivot().Equals(FVector2D(0.5f, 0.5f)));
	TestTrue(TEXT("Position is relative to the Designer placement"),
		IconBox->GetRenderTransform().Translation.Equals(FVector2D(12.0f, -2.0f)));
	HUD.Select(Leap);
	TestTrue(TEXT("Repeated selection does not accumulate scale"),
		IconBox->GetRenderTransform().Scale.Equals(FVector2D(1.2f, 0.9f)));
	TestTrue(TEXT("Repeated selection does not accumulate offsets"),
		IconBox->GetRenderTransform().Translation.Equals(FVector2D(12.0f, -2.0f)));
	TestTrue(TEXT("Icon layout does not move charge bars"),
		HUD.Widget->SkillCount_1->GetRenderTransform().Translation.Equals(FVector2D(5.0f, 6.0f)));

	HUD.Select(Stealth);
	TestEqual(TEXT("Unconfigured skill keeps the fixed width"), IconBox->GetWidthOverride(), 100.0f);
	TestTrue(TEXT("Unconfigured skill restores Designer scale"),
		IconBox->GetRenderTransform().Scale.Equals(FVector2D::UnitVector));
	TestTrue(TEXT("Unconfigured skill restores Designer position"),
		IconBox->GetRenderTransform().Translation.Equals(FVector2D(2.0f, 3.0f)));

	Layout.RenderScale = FVector2D::UnitVector;
	HUD.Select(Leap);
	TestEqual(TEXT("Position-only settings preserve fixed width"), IconBox->GetWidthOverride(), 100.0f);
	TestTrue(TEXT("Position-only settings still apply the offset"),
		IconBox->GetRenderTransform().Translation.Equals(FVector2D(12.0f, -2.0f)));

	// 디자이너의 설정 변경 후 재선택 없이 사전 구성 경로로 바로 반영한다.
	Layout.RenderScale = FVector2D(1.4f, 1.1f);
	Layout.PositionOffset = FVector2D(-3.0f, 8.0f);
	HUD.StatusBar->NativePreConstruct();
	TestTrue(TEXT("PreConstruct applies edited render scale without reselection"),
		IconBox->GetRenderTransform().Scale.Equals(FVector2D(1.4f, 1.1f)));
	TestEqual(TEXT("Preview updates keep fixed layout width"), IconBox->GetWidthOverride(), 100.0f);
	TestEqual(TEXT("Preview updates keep fixed layout height"), IconBox->GetHeightOverride(), 100.0f);
	TestTrue(TEXT("PreConstruct applies edited position without accumulating offsets"),
		IconBox->GetRenderTransform().Translation.Equals(FVector2D(-1.0f, 11.0f)));

	// 이름 바인딩이 없으면 이전에 사용한 SizeBox에도 적용하지 않는다.
	HUD.Widget->IconSizeBox = nullptr;
	Layout.RenderScale = FVector2D(1.6f, 1.15f);
	HUD.StatusBar->NativePreConstruct();
	TestTrue(TEXT("Layout requires the named SizeBox binding"),
		IconBox->GetRenderTransform().Scale.Equals(FVector2D(1.4f, 1.1f)));
	return true;
}

#endif
