#if WITH_DEV_AUTOMATION_TESTS

#include "Upgrade/OutlierUpgradeComponent.h"
#include "Engine/DataTable.h"
#include "Misc/AutomationTest.h"

#if WITH_EDITOR
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GAS/OutlierAbilitySystemComponent.h"
#include "GAS/Attributes/OutlierWeaponAttributeSet.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Shooter/ShooterCharacter.h"
#endif

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

#if WITH_EDITOR
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FOutlierShooterRecoilAndReflectionProjectionTest,
	"Outlier.Upgrade.Shooter.RecoilAndReflectionProjection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FOutlierShooterRecoilAndReflectionProjectionTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	// 테스트 전용 수치를 복제하지 않고 실제 CSV의 네 행을 기존 투영 경로로 검증한다.
	FString CSV;
	if (!TestTrue(TEXT("Shooter effect source CSV is readable"), FFileHelper::LoadFileToString(CSV,
		*FPaths::Combine(FPaths::ProjectContentDir(),
			TEXT("Datas/Character/Upgrade/SourceCSV/Effect/DT_ShooterUpgradeEffect.csv")))))
	{
		return false;
	}
	UDataTable* SourceEffects = NewObject<UDataTable>();
	SourceEffects->RowStruct = FOutlierUpgradeEffectRow::StaticStruct();
	const TArray<FString> ImportErrors = SourceEffects->CreateTableFromCSVString(CSV);
	for (const FString& Error : ImportErrors)
	{
		AddError(Error);
	}
	if (!ImportErrors.IsEmpty())
	{
		return false;
	}

	UDataTable* Nodes = NewObject<UDataTable>();
	Nodes->RowStruct = FOutlierUpgradeNodeRow::StaticStruct();
	UDataTable* Effects = NewObject<UDataTable>();
	Effects->RowStruct = FOutlierUpgradeEffectRow::StaticStruct();
	const FName EffectNames[] = {
		TEXT("Combat_002_Recoil"), TEXT("Combat_004_Recoil"),
		TEXT("Reflect_003A_ReflectDamage"), TEXT("Reflect_004A_ReflectDamage")
	};
	TArray<FName> NodeNames;
	for (const FName& EffectName : EffectNames)
	{
		const FOutlierUpgradeEffectRow* Effect = SourceEffects->FindRow<FOutlierUpgradeEffectRow>(
			EffectName, TEXT("RecoilAndReflectionTest"));
		if (!TestNotNull(*EffectName.ToString(), Effect))
		{
			return false;
		}
		Effects->AddRow(EffectName, *Effect);
		FOutlierUpgradeNodeRow Node;
		Node.Role = EOutlierUpgradeRole::Shooter;
		Nodes->AddRow(Effect->NodeRowName, Node);
		NodeNames.Add(Effect->NodeRowName);
	}

	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false,
		MakeUniqueObjectName(nullptr, UWorld::StaticClass()), GetTransientPackage());
	if (!TestNotNull(TEXT("Projection test world is created"), World))
	{
		return false;
	}
	World->AddToRoot();
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	ON_SCOPE_EXIT
	{
		World->DestroyWorld(true);
		World->SetPhysicsScene(nullptr);
		GEngine->DestroyWorldContext(World);
		World->RemoveFromRoot();
	};
	// Spawn 시 ASC의 InitializeComponent가 실행되어 기본 AttributeSet이 등록되도록 한다.
	World->InitializeActorsForPlay(FURL());

	// 네이티브 Shooter는 abstract다. 기존 BP를 생성하되 BeginPlay 전이라 테스트 설정만 적용한다.
	UClass* ShooterClass = LoadClass<AShooterCharacter>(nullptr,
		TEXT("/Game/Blueprints/Shooter/BP_ShooterCharacter.BP_ShooterCharacter_C"));
	AShooterCharacter* Shooter = ShooterClass ? World->SpawnActor<AShooterCharacter>(ShooterClass) : nullptr;
	if (!TestNotNull(TEXT("Shooter is spawned"), Shooter))
	{
		return false;
	}
	UOutlierAbilitySystemComponent* ASC = Shooter->GetOutlierAbilitySystemComponent();
	ASC->InitializeForPawn(Shooter);
	TestTrue(TEXT("Base suit config is accepted"), ASC->ConfigureShooterSuitAbilities(FOutlierShooterSuitConfig()));
	UOutlierUpgradeComponent* Component = Shooter->GetUpgradeComponent();
	Component->SetUpgradeRole(EOutlierUpgradeRole::Shooter);
	Component->SetUpgradeDataTable(Nodes);
	Component->SetUpgradeEffectDataTable(Effects);

	TestEqual(TEXT("Recoil starts at its baseline"), Shooter->GetWeaponRecoilMultiplier(), 1.0f);
	TestTrue(TEXT("First recoil upgrade activates"), Component->TryActivateNode(NodeNames[0]));
	TestTrue(TEXT("First recoil upgrade yields 90%"), FMath::IsNearlyEqual(Shooter->GetWeaponRecoilMultiplier(), 0.9f));
	TestTrue(TEXT("Second recoil upgrade activates"), Component->TryActivateNode(NodeNames[1]));
	TestTrue(TEXT("Recoil reductions add to 80%"), FMath::IsNearlyEqual(Shooter->GetWeaponRecoilMultiplier(), 0.8f));
	TestTrue(TEXT("First reflection upgrade activates"), Component->TryActivateNode(NodeNames[2]));
	TestTrue(TEXT("First reflection upgrade yields 140%"),
		FMath::IsNearlyEqual(ASC->GetShooterSuitConfig().BulletReflection.ReflectDamageMult, 1.4f));
	TestTrue(TEXT("Second reflection upgrade activates"), Component->TryActivateNode(NodeNames[3]));
	TestTrue(TEXT("Reflection upgrades add to 200%"),
		FMath::IsNearlyEqual(ASC->GetShooterSuitConfig().BulletReflection.ReflectDamageMult, 2.0f));

	Component->ReconcileUpgradeProjection();
	Component->RebuildNodeCache();
	TestTrue(TEXT("Reconciliation does not stack recoil twice"),
		FMath::IsNearlyEqual(Shooter->GetWeaponRecoilMultiplier(), 0.8f));
	TestTrue(TEXT("Reconciliation does not stack reflection twice"),
		FMath::IsNearlyEqual(ASC->GetShooterSuitConfig().BulletReflection.ReflectDamageMult, 2.0f));
	TestEqual(TEXT("Recoil base stays unchanged"),
		ASC->GetNumericAttributeBase(UOutlierWeaponAttributeSet::GetRecoilMultiplierAttribute()), 1.0f);

	// 효과 테이블 제거 시 이전 GE와 config 델타가 함께 사라져야 한다.
	Component->SetUpgradeEffectDataTable(nullptr);
	TestEqual(TEXT("Removing effects restores recoil"), Shooter->GetWeaponRecoilMultiplier(), 1.0f);
	TestEqual(TEXT("Removing effects restores reflection"), ASC->GetShooterSuitConfig().BulletReflection.ReflectDamageMult, 1.0f);
	return true;
}
#endif

#endif
