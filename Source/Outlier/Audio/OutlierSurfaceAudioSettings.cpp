#include "Audio/OutlierSurfaceAudioSettings.h"

void FOutlierSurfaceContextMapping::GetCandidates(EPhysicalSurface Surface, TArray<FGameplayTag>& OutContexts) const
{
	OutContexts.Reset();
	FGameplayTag SurfaceContext;
	switch (Surface)
	{
	case SurfaceType1: SurfaceContext = MarbleContext.IsValid() ? MarbleContext : NoneMetalContext; break;
	case SurfaceType2: SurfaceContext = MetalContext; break;
	case SurfaceType3: SurfaceContext = GlassContext; break;
	case SurfaceType4: SurfaceContext = PlasticContext; break;
	case SurfaceType5: SurfaceContext = FabricContext; break;
	case SurfaceType6: SurfaceContext = PlantContext; break;
	case SurfaceType7: SurfaceContext = PaperContext; break;
	default: break;
	}
	if (SurfaceContext.IsValid()) OutContexts.Add(SurfaceContext);
	if (DefaultContext.IsValid()) OutContexts.AddUnique(DefaultContext);
}

void UOutlierSurfaceAudioSettings::BuildMappings(TMap<FGameplayTag, FOutlierSurfaceContextMapping>& OutMappings) const
{
	OutMappings.Reset();
	auto Tag = [](const TCHAR* Name)
	{
		return Name ? FGameplayTag::RequestGameplayTag(FName(Name), false) : FGameplayTag();
	};
	auto Add = [&](const TCHAR* Base, const TCHAR* Default, const TCHAR* Marble, const TCHAR* Metal, const TCHAR* Glass)
	{
		const FGameplayTag BaseTag = Tag(Base);
		if (!BaseTag.IsValid()) return;
		FOutlierSurfaceContextMapping Mapping;
		Mapping.DefaultContext = Tag(Default);
		Mapping.MarbleContext = Tag(Marble);
		Mapping.MetalContext = Tag(Metal);
		Mapping.GlassContext = Tag(Glass);
		Mapping.PlasticContext = Mapping.DefaultContext;
		OutMappings.Add(BaseTag, Mapping);
		// Notify에 Plastic을 지정해도 기준 이벤트와 같은 표면 매핑을 사용한다.
		if (Mapping.DefaultContext.IsValid()) OutMappings.Add(Mapping.DefaultContext, Mapping);
	};
	// 기존 태그가 있는 표면만 연결한다. Fabric/Plant/Paper 등 미설정 표면은 이벤트별 Plastic을 상속한다.
	Add(TEXT("Audio.Context.Player.01.Type.Rifle.Hit"), TEXT("Audio.Context.Player.01.Type.Rifle.Hit.Plastic"), TEXT("Audio.Context.Player.01.Type.Rifle.Hit.Marble"), TEXT("Audio.Context.Player.01.Type.Rifle.Hit.Metal"), TEXT("Audio.Context.Player.01.Type.Rifle.Hit.Glass"));
	Add(TEXT("Audio.Context.Player.01.Type.Pistol.Hit"), TEXT("Audio.Context.Player.01.Type.Pistol.Hit.Plastic"), nullptr, nullptr, nullptr);
	Add(TEXT("Audio.Context.Player.01.Type.Melee.Hit"), TEXT("Audio.Context.Player.01.Type.Melee.Hit.Plastic"), TEXT("Audio.Context.Player.01.Type.Melee.Hit.Marble"), TEXT("Audio.Context.Player.01.Type.Melee.Hit.Metal"), TEXT("Audio.Context.Player.01.Type.Melee.Hit.Glass"));
	Add(TEXT("Audio.Context.Player.01.Movement.Footstep"), TEXT("Audio.Context.Player.01.Movement.Footstep.Plastic"), TEXT("Audio.Context.Player.01.Movement.Footstep.Marble"), TEXT("Audio.Context.Player.01.Movement.Footstep.Metal"), nullptr);
	Add(TEXT("Audio.Context.Player.01.Movement.Jump"), TEXT("Audio.Context.Player.01.Movement.Jump.Plastic"), TEXT("Audio.Context.Player.01.Movement.Jump.Marble"), TEXT("Audio.Context.Player.01.Movement.Jump.Metal"), nullptr);
	Add(TEXT("Audio.Context.Player.01.Movement.Land"), TEXT("Audio.Context.Player.01.Movement.Land.Plastic"), TEXT("Audio.Context.Player.01.Movement.Land.Marble"), TEXT("Audio.Context.Player.01.Movement.Land.Metal"), nullptr);
	for (const auto& Pair : ContextMappings) OutMappings.Add(Pair.Key, Pair.Value);
}
