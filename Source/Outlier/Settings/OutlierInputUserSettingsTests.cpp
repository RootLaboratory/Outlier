#if WITH_DEV_AUTOMATION_TESTS

#include "Settings/OutlierInputUserSettings.h"
#include "GameFramework/Character.h"
#include "OutlierPlayerState.h"
#include "EnhancedInputDeveloperSettings.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/AutomationTest.h"
#include "PlayerMappableKeySettings.h"
#include "UObject/UnrealType.h"

struct FOutlierSprintInputSettingsTestAccess
{
	static void Migrate(UOutlierInputUserSettings* Settings, UEnhancedInputUserSettings* OldSettings)
	{
		Settings->HandleSettingsClassChangedOnLoad_Impl(OldSettings);
	}
	static void SetInvalidShooterMode(UOutlierInputUserSettings* Settings)
	{
		Settings->ShooterSprintInputMode = static_cast<EOutlierSprintInputMode>(255);
	}
	static void SetInvalidPartnerMode(UOutlierInputUserSettings* Settings)
	{
		Settings->PartnerSprintInputMode = static_cast<EOutlierSprintInputMode>(255);
	}
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOutlierSprintInputSettingsTest,
	"Outlier.Settings.SprintInputMode",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOutlierSprintInputSettingsTest::RunTest(const FString& Parameters)
{
	using EMode = EOutlierSprintInputMode;
	using ERole = EOutlierPlayerRole;
	TestEqual(TEXT("Configured settings class"), GetDefault<UEnhancedInputDeveloperSettings>()->UserSettingsClass.Get(),
		UOutlierInputUserSettings::StaticClass());
	UOutlierInputUserSettings* Settings = NewObject<UOutlierInputUserSettings>();
	TestEqual(TEXT("Shooter defaults to Hold"), Settings->GetSprintInputMode(ERole::Shooter), EMode::Hold);
	TestEqual(TEXT("Partner defaults to Toggle"), Settings->GetSprintInputMode(ERole::Partner), EMode::Toggle);
	TestTrue(TEXT("Shooter accepts Hold"), Settings->SetSprintInputMode(ERole::Shooter, EMode::Hold));
	TestEqual(TEXT("Shooter change preserves Partner"), Settings->GetSprintInputMode(ERole::Partner), EMode::Toggle);
	TestFalse(TEXT("None is not a configurable role"), Settings->SetSprintInputMode(ERole::None, EMode::Hold));
	TestFalse(TEXT("Invalid mode rejected"), Settings->SetSprintInputMode(ERole::Partner, static_cast<EMode>(255)));

	// 디스크의 실제 사용자 파일을 건드리지 않고 동일한 SaveGame 직렬화 경로를 검증한다.
	auto RoundTrip = [this](UOutlierInputUserSettings* Source) -> UOutlierInputUserSettings*
	{
		TArray<uint8> Bytes;
		if (!TestTrue(TEXT("Save input settings to memory"), UGameplayStatics::SaveGameToMemory(Source, Bytes))) return nullptr;
		UOutlierInputUserSettings* Loaded = Cast<UOutlierInputUserSettings>(UGameplayStatics::LoadGameFromMemory(Bytes));
		TestNotNull(TEXT("Load input settings from memory"), Loaded);
		return Loaded;
	};
	if (UOutlierInputUserSettings* Loaded = RoundTrip(Settings))
	{
		TestEqual(TEXT("Shooter preference restored"), Loaded->GetSprintInputMode(ERole::Shooter), EMode::Hold);
		TestEqual(TEXT("Partner preference restored independently"), Loaded->GetSprintInputMode(ERole::Partner), EMode::Toggle);
		Loaded->SetSprintInputMode(ERole::Partner, EMode::Hold);
		Loaded->SetSprintInputMode(ERole::Shooter, EMode::Toggle);
		if (UOutlierInputUserSettings* Reloaded = RoundTrip(Loaded))
		{
			TestEqual(TEXT("Shooter Toggle restored"), Reloaded->GetSprintInputMode(ERole::Shooter), EMode::Toggle);
			TestEqual(TEXT("Partner Hold restored"), Reloaded->GetSprintInputMode(ERole::Partner), EMode::Hold);
		}
	}
	Settings->SetSprintInputMode(ERole::Partner, EMode::Hold);
	FOutlierSprintInputSettingsTestAccess::SetInvalidShooterMode(Settings);
	if (UOutlierInputUserSettings* Loaded = RoundTrip(Settings))
	{
		TestEqual(TEXT("Invalid Shooter value defaults to Hold"), Loaded->GetSprintInputMode(ERole::Shooter), EMode::Hold);
		TestEqual(TEXT("Invalid Shooter preserves Partner Hold"), Loaded->GetSprintInputMode(ERole::Partner), EMode::Hold);
	}
	Settings->SetSprintInputMode(ERole::Shooter, EMode::Hold);
	FOutlierSprintInputSettingsTestAccess::SetInvalidPartnerMode(Settings);
	if (UOutlierInputUserSettings* Loaded = RoundTrip(Settings))
	{
		TestEqual(TEXT("Invalid Partner value defaults to Toggle"), Loaded->GetSprintInputMode(ERole::Partner), EMode::Toggle);
		TestEqual(TEXT("Invalid Partner preserves Shooter Hold"), Loaded->GetSprintInputMode(ERole::Shooter), EMode::Hold);
	}
	UOutlierInputUserSettings* OtherUser = NewObject<UOutlierInputUserSettings>();
	TestEqual(TEXT("Another settings owner keeps its own default"), OtherUser->GetSprintInputMode(ERole::Partner), EMode::Toggle);

	UEnhancedInputUserSettings* OldSettings = NewObject<UEnhancedInputUserSettings>();
	FPlayerMappableKeyProfileCreationArgs ProfileArgs;
	ProfileArgs.ProfileStringIdentifier = TEXT("SprintSettingsTest.Profile");
	ProfileArgs.ProfileType = UEnhancedPlayerMappableKeyProfile::StaticClass();
	ProfileArgs.bSetAsCurrentProfile = true;
	if (!TestNotNull(TEXT("Legacy key profile created"), OldSettings->CreateNewKeyProfile(ProfileArgs))) return false;

	UInputMappingContext* Context = NewObject<UInputMappingContext>();
	UInputAction* Action = NewObject<UInputAction>();
	UPlayerMappableKeySettings* MappingSettings = NewObject<UPlayerMappableKeySettings>(Action);
	MappingSettings->Name = TEXT("SprintSettingsTest.Mapping");
	FObjectPropertyBase* MappingProperty = FindFProperty<FObjectPropertyBase>(UInputAction::StaticClass(), TEXT("PlayerMappableKeySettings"));
	if (!TestNotNull(TEXT("Action mappable settings property"), MappingProperty)) return false;
	MappingProperty->SetObjectPropertyValue_InContainer(Action, MappingSettings);
	Context->MapKey(Action, EKeys::LeftShift);
	TestTrue(TEXT("Legacy context registered"), OldSettings->RegisterInputMappingContext(Context));
	FMapPlayerKeyArgs MapArgs;
	MapArgs.MappingName = MappingSettings->Name;
	MapArgs.Slot = EPlayerMappableKeySlot::First;
	MapArgs.NewKey = EKeys::RightShift;
	FGameplayTagContainer FailureReasons;
	OldSettings->MapPlayerKey(MapArgs, FailureReasons);
	if (!TestTrue(TEXT("Legacy key remapped"), FailureReasons.IsEmpty())) return false;

	TArray<uint8> LegacyBytes;
	if (!TestTrue(TEXT("Legacy save serialized"), UGameplayStatics::SaveGameToMemory(OldSettings, LegacyBytes))) return false;
	UEnhancedInputUserSettings* LoadedLegacy = Cast<UEnhancedInputUserSettings>(UGameplayStatics::LoadGameFromMemory(LegacyBytes));
	if (!TestNotNull(TEXT("Legacy save loaded"), LoadedLegacy)) return false;
	UOutlierInputUserSettings* Migrated = NewObject<UOutlierInputUserSettings>();
	FOutlierSprintInputSettingsTestAccess::Migrate(Migrated, LoadedLegacy);
	TestEqual(TEXT("Legacy active profile preserved"), Migrated->GetActiveKeyProfileId(), ProfileArgs.ProfileStringIdentifier);
	TestEqual(TEXT("Legacy save without Shooter field defaults to Hold"), Migrated->GetSprintInputMode(ERole::Shooter), EMode::Hold);
	TestEqual(TEXT("Legacy save without Partner field defaults to Toggle"), Migrated->GetSprintInputMode(ERole::Partner), EMode::Toggle);
	Migrated->RegisterInputMappingContext(Context);
	const FPlayerKeyMapping* MigratedKey = Migrated->FindCurrentMappingForSlot(MapArgs.MappingName, MapArgs.Slot);
	if (TestNotNull(TEXT("Migrated key mapping exists"), MigratedKey))
	{
		TestEqual(TEXT("Migration preserves rebound key"), MigratedKey->GetCurrentKey(), EKeys::RightShift);
	}
	Migrated->SetSprintInputMode(ERole::Partner, EMode::Hold);
	if (UOutlierInputUserSettings* Reloaded = RoundTrip(Migrated))
	{
		Reloaded->RegisterInputMappingContext(Context);
		const FPlayerKeyMapping* ReloadedKey = Reloaded->FindCurrentMappingForSlot(MapArgs.MappingName, MapArgs.Slot);
		if (TestNotNull(TEXT("Rebound key survives subsequent save"), ReloadedKey))
		{
			TestEqual(TEXT("Sprint save preserves key preference"), ReloadedKey->GetCurrentKey(), EKeys::RightShift);
		}
		TestEqual(TEXT("Migrated role setting persists"), Reloaded->GetSprintInputMode(ERole::Partner), EMode::Hold);
	}
	return true;
}

#endif
