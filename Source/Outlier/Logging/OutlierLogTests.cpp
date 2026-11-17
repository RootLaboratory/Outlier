#include "Logging/OutlierLog.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "HAL/IConsoleManager.h"
#include "Misc/AutomationTest.h"

DEFINE_LOG_CATEGORY_STATIC(LogOutlierLogControlTest, Log, All);

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOutlierLogControlTest,
	"Outlier.Logging.ChannelControl",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOutlierLogControlTest::RunTest(const FString& Parameters)
{
	struct FSetting
	{
		OutlierLog::EChannel Channel;
		const TCHAR* Name;
	};
	const FSetting Settings[] = {
		{OutlierLog::EChannel::Core, TEXT("outlier.Log.Core")},
		{OutlierLog::EChannel::Network, TEXT("outlier.Log.Network")},
		{OutlierLog::EChannel::Save, TEXT("outlier.Log.Save")},
		{OutlierLog::EChannel::Shooter, TEXT("outlier.Log.Shooter")},
		{OutlierLog::EChannel::Animation, TEXT("outlier.Log.Animation")},
		{OutlierLog::EChannel::Weapon, TEXT("outlier.Log.Weapon")},
		{OutlierLog::EChannel::Enemy, TEXT("outlier.Log.Enemy")},
		{OutlierLog::EChannel::Partner, TEXT("outlier.Log.Partner")},
		{OutlierLog::EChannel::Flight, TEXT("outlier.Log.Flight")},
		{OutlierLog::EChannel::Ability, TEXT("outlier.Log.Ability")},
		{OutlierLog::EChannel::Room, TEXT("outlier.Log.Room")},
		{OutlierLog::EChannel::Interaction, TEXT("outlier.Log.Interaction")},
		{OutlierLog::EChannel::UI, TEXT("outlier.Log.UI")},
		{OutlierLog::EChannel::Audio, TEXT("outlier.Log.Audio")},
		{OutlierLog::EChannel::World, TEXT("outlier.Log.World")}
	};
	// Restore each setting at its original priority, including on an early return.
	struct FRestore
	{
		TArray<TPair<IConsoleVariable*, int32>> Values;
		~FRestore()
		{
			for (const auto& Entry : Values) Entry.Key->SetWithCurrentPriority(Entry.Value);
		}
		bool SaveAndDisable(const TCHAR* Name)
		{
			IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(Name);
			if (!Variable) return false;
			Values.Emplace(Variable, Variable->GetInt());
			Variable->SetWithCurrentPriority(0);
			return true;
		}
	} Restore;
	if (!TestTrue(TEXT("Global switch registered"), Restore.SaveAndDisable(TEXT("outlier.Log.All")))) return false;
	for (const FSetting& Setting : Settings)
	{
		if (!TestTrue(Setting.Name, Restore.SaveAndDisable(Setting.Name))) return false;
	}
	for (const FSetting& Setting : Settings)
	{
		TestFalse(TEXT("Disabled channel"), OutlierLog::IsEnabled(Setting.Channel));
		IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(Setting.Name);
		Variable->SetWithCurrentPriority(1);
		for (const FSetting& Other : Settings)
		{
			TestEqual(TEXT("Only selected channel enabled"), OutlierLog::IsEnabled(Other.Channel), Other.Channel == Setting.Channel);
		}
		Variable->SetWithCurrentPriority(0);
	}
	int32 Evaluations = 0;
	int32 Preparations = 0;
	auto PrepareAndLog = [&]()
	{
		if (OUTLIER_LOG_ACTIVE(Weapon, LogOutlierLogControlTest, Log))
		{
			const FString Prepared = FString::FromInt(++Preparations);
			OUTLIER_LOG(Weapon, LogOutlierLogControlTest, Log, TEXT("Prepared diagnostic: %s"), *Prepared);
		}
	};
	PrepareAndLog();
	TestEqual(TEXT("Disabled channel skips log-only preparation"), Preparations, 0);
	OUTLIER_LOG(Weapon, LogOutlierLogControlTest, Log, TEXT("Disabled argument: %d"), ++Evaluations);
	TestEqual(TEXT("Disabled logging does not evaluate arguments"), Evaluations, 0);
	IConsoleVariable* Global = IConsoleManager::Get().FindConsoleVariable(TEXT("outlier.Log.All"));
	Global->SetWithCurrentPriority(1);
	PrepareAndLog();
	TestEqual(TEXT("Enabled channel prepares exactly once"), Preparations, 1);
	{
		struct FRestoreVerbosity
		{
			ELogVerbosity::Type Value = LogOutlierLogControlTest.GetVerbosity();
			~FRestoreVerbosity() { LogOutlierLogControlTest.SetVerbosity(Value); }
		} RestoreVerbosity;
		LogOutlierLogControlTest.SetVerbosity(ELogVerbosity::Warning);
		PrepareAndLog();
		OUTLIER_LOG(Weapon, LogOutlierLogControlTest, Log, TEXT("Suppressed argument: %d"), ++Evaluations);
		TestEqual(TEXT("Suppressed category skips log-only preparation"), Preparations, 1);
		TestEqual(TEXT("Suppressed category skips arguments"), Evaluations, 0);
	}
	for (const FSetting& Setting : Settings) TestTrue(TEXT("Global enables channel"), OutlierLog::IsEnabled(Setting.Channel));
	OUTLIER_LOG(Weapon, LogOutlierLogControlTest, Log, TEXT("Enabled argument: %d"), ++Evaluations);
	TestEqual(TEXT("Enabled logging evaluates arguments once"), Evaluations, 1);
	Global->SetWithCurrentPriority(0);
	TestFalse(TEXT("Global off returns to individual settings"), OutlierLog::IsEnabled(OutlierLog::EChannel::Weapon));
	return true;
}
#endif
