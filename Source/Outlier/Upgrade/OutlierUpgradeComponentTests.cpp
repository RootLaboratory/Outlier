#if WITH_DEV_AUTOMATION_TESTS

#include "Upgrade/OutlierUpgradeComponent.h"
#include "Engine/DataTable.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FInitiallyActivatedUpgradePrerequisiteTest,
	"Outlier.Upgrade.InitiallyActivatedPrerequisites",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FInitiallyActivatedUpgradePrerequisiteTest::RunTest(const FString& Parameters)
{
	UDataTable* Table = NewObject<UDataTable>();
	Table->RowStruct = FOutlierUpgradeNodeRow::StaticStruct();

	FOutlierUpgradeNodeRow Parent;
	Parent.bInitiallyActivated = true;
	Table->AddRow(FName(TEXT("DefaultParent")), Parent);

	FOutlierUpgradeNodeRow Child;
	Child.ParentId = FName(TEXT("DefaultParent"));
	Table->AddRow(FName(TEXT("Child")), Child);

	FOutlierUpgradeNodeRow Grandchild;
	Grandchild.ParentId = FName(TEXT("Child"));
	Table->AddRow(FName(TEXT("Grandchild")), Grandchild);

	FOutlierUpgradeNodeRow OrdinaryParent;
	Table->AddRow(FName(TEXT("OrdinaryParent")), OrdinaryParent);
	Child.ParentId = FName(TEXT("OrdinaryParent"));
	Table->AddRow(FName(TEXT("BlockedChild")), Child);
	Child.ParentId = FName(TEXT("DefaultParent"));
	Child.Cost = 1;
	Table->AddRow(FName(TEXT("UnaffordableChild")), Child);

	UOutlierUpgradeComponent* Component = NewObject<UOutlierUpgradeComponent>();
	Component->SetUpgradeDataTable(Table);
	TestTrue(TEXT("Default parent is active"), Component->IsNodeActivated(FName(TEXT("DefaultParent"))));
	TestTrue(TEXT("Default parent displays purchased state"),
		Component->GetNodeState(FName(TEXT("DefaultParent"))) == EOutlierUpgradeNodeState::Activated);
	TestFalse(TEXT("Default parent cannot be purchased again"), Component->CanActivateNode(FName(TEXT("DefaultParent"))));
	TestTrue(TEXT("Default parent unlocks child"), Component->CanActivateNode(FName(TEXT("Child"))));
	TestFalse(TEXT("Grandchild still requires child purchase"), Component->IsNodeUnlocked(FName(TEXT("Grandchild"))));
	TestFalse(TEXT("Ordinary unpurchased parent still blocks child"), Component->CanActivateNode(FName(TEXT("BlockedChild"))));
	TestFalse(TEXT("Default parent does not bypass cost"), Component->CanActivateNode(FName(TEXT("UnaffordableChild"))));
	TestTrue(TEXT("Child purchase succeeds"), Component->TryActivateNode(FName(TEXT("Child"))));
	TestTrue(TEXT("Purchased child unlocks grandchild"), Component->CanActivateNode(FName(TEXT("Grandchild"))));
	Component->RebuildNodeCache();
	Component->SyncFromPlayerState();
	TestTrue(TEXT("Default activation survives cache rebuild"), Component->IsNodeActivated(FName(TEXT("DefaultParent"))));
	TestTrue(TEXT("Purchased child survives cache rebuild"), Component->IsNodeActivated(FName(TEXT("Child"))));
	return true;
}

#endif
