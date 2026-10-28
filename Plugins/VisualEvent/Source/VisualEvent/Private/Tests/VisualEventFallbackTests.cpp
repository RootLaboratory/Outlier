#if WITH_DEV_AUTOMATION_TESTS

#include "VisualEventSubsystem.h"
#include "Misc/AutomationTest.h"
#include "NativeGameplayTags.h"
#include "Materials/Material.h"
#include "Particles/ParticleSystem.h"

UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_VisualEventTest_Surface, "VisualEvent.Test.Surface");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_VisualEventTest_Default, "VisualEvent.Test.Default");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TAG_VisualEventTest_Base, "VisualEvent.Test");

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVisualEventIndependentFallbackTest,
	"VisualEvent.TaggedEffects.IndependentFallback",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVisualEventIndependentFallbackTest::RunTest(const FString& Parameters)
{
	UProjectionMarkDefinition* SurfaceDecal = NewObject<UProjectionMarkDefinition>();
	SurfaceDecal->DecalMaterial = NewObject<UMaterial>();
	UTrailEffectDefinition* EmptySurfaceFX = NewObject<UTrailEffectDefinition>();
	UTrailEffectDefinition* DefaultFX = NewObject<UTrailEffectDefinition>();
	DefaultFX->FXAsset = NewObject<UParticleSystem>();
	UProjectionMarkDefinition* DefaultDecal = NewObject<UProjectionMarkDefinition>();
	DefaultDecal->DecalMaterial = NewObject<UMaterial>();

	TMap<FGameplayTag, FVisualEventSet> Definitions;
	FVisualEventSet& Surface = Definitions.Add(TAG_VisualEventTest_Surface);
	Surface.DecalDef = SurfaceDecal;
	Surface.TrailEffectDef = EmptySurfaceFX;
	Surface.SoundDef = NewObject<USoundDefinition>();
	FVisualEventSet& Default = Definitions.Add(TAG_VisualEventTest_Default);
	Default.DecalDef = DefaultDecal;
	Default.TrailEffectDef = DefaultFX;

	FVisualEventSet Fallback;
	Fallback.SoundDef = NewObject<USoundDefinition>();
	const FVisualEventSet Result = UVisualEventSubsystem::ResolveTaggedEffects(
		{ TAG_VisualEventTest_Surface, TAG_VisualEventTest_Default }, Definitions, Fallback);
	TestTrue(TEXT("Surface decal wins before Default"), Result.DecalDef == SurfaceDecal);
	TestTrue(TEXT("Empty inner FX falls through independently"), Result.TrailEffectDef == DefaultFX);
	TestNull(TEXT("Tagged visual lookup never returns fallback sound"), Result.SoundDef.Get());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVisualEventExactFallbackTest,
	"VisualEvent.TaggedEffects.ExactContextAndLegacyFallback",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVisualEventExactFallbackTest::RunTest(const FString& Parameters)
{
	UProjectionMarkDefinition* LegacyDecal = NewObject<UProjectionMarkDefinition>();
	LegacyDecal->DecalMaterial = NewObject<UMaterial>();
	UTrailEffectDefinition* BaseFX = NewObject<UTrailEffectDefinition>();
	BaseFX->FXAsset = NewObject<UParticleSystem>();
	TMap<FGameplayTag, FVisualEventSet> Definitions;
	Definitions.Add(TAG_VisualEventTest_Surface).DecalDef = NewObject<UProjectionMarkDefinition>();
	Definitions.Add(TAG_VisualEventTest_Base).TrailEffectDef = BaseFX;
	FVisualEventSet Fallback;
	Fallback.DecalDef = LegacyDecal;

	FVisualEventSet Result = UVisualEventSubsystem::ResolveTaggedEffects(
		{ TAG_VisualEventTest_Surface, TAG_VisualEventTest_Default }, Definitions, Fallback);
	TestTrue(TEXT("Empty inner decal reaches legacy fallback"), Result.DecalDef == LegacyDecal);
	TestNull(TEXT("Parent tag is not implicitly selected"), Result.TrailEffectDef.Get());

	Result = UVisualEventSubsystem::ResolveTaggedEffects(
		{ TAG_VisualEventTest_Surface, TAG_VisualEventTest_Default, TAG_VisualEventTest_Base }, Definitions, Fallback);
	TestTrue(TEXT("Explicit base Context is selected"), Result.TrailEffectDef == BaseFX);
	TestTrue(TEXT("Legacy decal and base FX are independent"), Result.DecalDef == LegacyDecal);

	Result = UVisualEventSubsystem::ResolveTaggedEffects({}, {}, {});
	TestNull(TEXT("Empty settings and fallback omit decal"), Result.DecalDef.Get());
	TestNull(TEXT("Empty settings and fallback omit FX"), Result.TrailEffectDef.Get());
	return true;
}

#endif
