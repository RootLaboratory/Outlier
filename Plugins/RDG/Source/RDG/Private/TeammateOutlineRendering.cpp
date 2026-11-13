#include "TeammateOutlineRendering.h"

#include "Components/PrimitiveComponent.h"
#include "CustomRenderPassSceneCapture.h"
#include "GlobalShader.h"
#include "GameFramework/Actor.h"
#include "HAL/IConsoleManager.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "Rendering/CustomRenderPass.h"
#include "SceneInterface.h"
#include "SceneRendering.h"
#include "ScreenPass.h"
#include "ShaderParameterStruct.h"

namespace
{
	constexpr float EmptyDepth = 1.0e8f;
	enum class EMask : uint8 { Target, FirstPerson, Count };
	constexpr int32 MaskCount = static_cast<int32>(EMask::Count);
	TAutoConsoleVariable<int32> CVarTeammateOutlineDebug(TEXT("rdg.TeammateOutline.Debug"), 0,
		TEXT("0: composite, 1: teammate coverage, 2: visible/first-person block/outline classification, 3: target depth."), ECVF_RenderThreadSafe);
}

struct FTeammateOutlineViewResources
{
	FTeammateOutlineViewData Settings;
	FRDGTextureRef Masks[MaskCount] = {};
	FIntRect MaskRects[MaskCount];
	const FRDGBuilder* Builder = nullptr;
};

namespace
{
	/** 별도의 대상 깊이로 그리므로 벽이나 다른 CustomStencil 쓰기가 실루엣을 지우지 않는다. */
	class FTeammateDepthPass final : public FCustomRenderPassBase
	{
	public:
		IMPLEMENT_CUSTOM_RENDER_PASS(FTeammateDepthPass);

		FTeammateDepthPass(const FTeammateOutlineResources& InResources, EMask InMask,
			FIntPoint Size, const FSceneView& MainView)
			// PrePass에 참여하지 않는 movable/비차폐 메시도 빠지지 않도록 최소 BasePass까지 실행한다.
			: FCustomRenderPassBase(TEXT("OutlierTeammateMask"), ERenderMode::DepthAndBasePass, ERenderOutput::SceneDepth, Size)
			, Resources(InResources), Mask(InMask), FirstPerson(MainView.SceneViewInitOptions.FirstPersonParams)
		{
			auto UserData = MakeUnique<FSceneCaptureCustomRenderPassUserData>();
			UserData->bMainViewFamily = true;
			UserData->bMainViewResolution = true;
			UserData->bMainViewCamera = true;
			SetUserData(MoveTemp(UserData));
		}

	protected:
		virtual void OnPreRender(FRDGBuilder& GraphBuilder) override
		{
			const int32 Index = static_cast<int32>(Mask);
			Resources->Builder = &GraphBuilder;
			Resources->MaskRects[Index] = Views[0]->ViewRect;
			const FIntPoint Extent = RenderTargetSize.ComponentMax(Views[0]->ViewRect.Max);
			RenderTargetTexture = GraphBuilder.CreateTexture(
				FRDGTextureDesc::Create2D(Extent, PF_R32_FLOAT, FClearValueBinding(FLinearColor(EmptyDepth, 0, 0, 0)),
					TexCreate_RenderTargetable | TexCreate_ShaderResource), TEXT("Outlier.TeammateDepth"));
			Resources->Masks[Index] = RenderTargetTexture;

			// CustomRenderPass의 기본 View에는 FP 보정이 복사되지 않는다.
			// 본 View와 같은 FOV/축소 변환으로 uniform을 다시 만들어 FP 무기 커버리지를 맞춘다.
			if (Mask == EMask::FirstPerson && FirstPerson.bUseParameters)
			{
				for (FViewInfo* CustomView : Views)
				{
					FSceneViewInitOptions Options = CustomView->SceneViewInitOptions;
					Options.FirstPersonParams = FirstPerson;
					const FVector2D Jitter = CustomView->ViewMatrices.GetTemporalAAJitter();
					CustomView->ViewMatrices = FViewMatrices(Options);
					// 재생성으로 초기화된 NoAA 캐시는 jitter를 더하기 전에 원본 투영으로 채운다.
					CustomView->ViewMatrices.SaveProjectionNoAAMatrix();
					CustomView->ViewMatrices.HackAddTemporalAAProjectionJitter(Jitter);
					// 캐시를 직접 갱신해 이후 참조와 실제 uniform에 같은 FP 보정값을 유지한다.
					FViewUniformShaderParameters& Parameters = *CustomView->CachedViewUniformShaderParameters;
					CustomView->SetupUniformBufferParameters(CustomView->ViewMatrices, CustomView->ViewMatrices, nullptr, 0, Parameters);
					CustomView->ViewUniformBuffer = TUniformBufferRef<FViewUniformShaderParameters>::CreateUniformBufferImmediate(
						Parameters, UniformBuffer_SingleFrame);
				}
			}
		}

	private:
		FTeammateOutlineResources Resources;
		EMask Mask;
		FFirstPersonParameters FirstPerson;
	};

	TSet<FPrimitiveComponentId> GatherPrimitiveIds(const TArray<TWeakObjectPtr<UPrimitiveComponent>>& Meshes)
	{
		TSet<FPrimitiveComponentId> Result;
		Result.Reserve(Meshes.Num());
		for (const auto& WeakMesh : Meshes)
		{
			const UPrimitiveComponent* Mesh = WeakMesh.Get();
			const AActor* Owner = IsValid(Mesh) ? Mesh->GetOwner() : nullptr;
			if (IsValid(Mesh) && Mesh->IsRegistered() && Mesh->IsVisible() && !Mesh->bHiddenInGame
				&& IsValid(Owner) && !Owner->IsHidden() && !Owner->IsActorBeingDestroyed()
				&& Mesh->bRenderInMainPass && !Mesh->ComponentHasTag(TEXT("Outlier.StencilExclude")))
			{
				Result.Add(Mesh->GetPrimitiveSceneId());
			}
		}
		return Result;
	}
}

FTeammateOutlineResources FRDGTeammateOutlineRendering::QueueMasks(
	FSceneViewFamily& Family, const FSceneView& View, const FTeammateOutlineViewData& Data)
{
	check(IsInGameThread());
	// UE5.7의 main-view jitter 복사는 Views[0] 기준이다. 검증되지 않은 다중 View에는 출력하지 않는다.
	if (!Data.bEnabled || !Family.Scene || Family.Views.Num() != 1 || View.bIsSceneCapture || Data.TargetMeshes.IsEmpty()) return nullptr;
	TSet<FPrimitiveComponentId> Ids[MaskCount] = {
		GatherPrimitiveIds(Data.TargetMeshes),
		GatherPrimitiveIds(Data.FirstPersonMeshes) };
	if (Ids[0].IsEmpty()) return nullptr;

	auto Resources = MakeShared<FTeammateOutlineViewResources, ESPMode::ThreadSafe>();
	Resources->Settings = Data;
	// 렌더 스레드로 넘어가는 스냅샷에는 UObject 참조가 필요 없다.
	Resources->Settings.TargetMeshes.Reset();
	Resources->Settings.FirstPersonMeshes.Reset();
	Resources->Settings.OnMasksQueued.Unbind();
	for (int32 Index = 0; Index < MaskCount; ++Index)
	{
		if (Ids[Index].IsEmpty()) continue;
		FSceneInterface::FCustomRenderPassRendererInput Input;
		Input.ViewLocation = View.ViewMatrices.GetViewOrigin();
		Input.ViewRotationMatrix = View.ViewMatrices.GetViewMatrix().RemoveTranslation();
		// GT의 BeginRenderViewFamily에서는 NoAA 캐시가 아직 단위 행렬이다.
		// Custom View 생성 시 RHI 보정이 적용되므로 원본 카메라 투영 행렬을 전달한다.
		Input.ProjectionMatrix = View.SceneViewInitOptions.ProjectionMatrix;
		Input.ViewActor = View.ViewActor;
		Input.ShowOnlyPrimitives = MoveTemp(Ids[Index]);
		Input.HiddenPrimitives = View.HiddenPrimitives;
		Input.bUseMainViewFamilyShowFlags = true;
		Input.bIsSceneCapture = false; // OnlyOwnerSee/OwnerNoSee는 본 플레이어 View 기준으로 판정한다.
		Input.CustomRenderPass = new FTeammateDepthPass(Resources, static_cast<EMask>(Index), View.UnscaledViewRect.Max, View);
		if (!Family.Scene->AddCustomRenderPass(&Family, Input))
		{
			delete Input.CustomRenderPass;
			return nullptr;
		}
	}
	return Resources;
}

class FTeammateOutlineCS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FTeammateOutlineCS);
	SHADER_USE_PARAMETER_STRUCT(FTeammateOutlineCS, FGlobalShader);
	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SceneColorTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, SceneDepthTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, TargetDepthTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, FirstPersonDepthTexture)
		SHADER_PARAMETER(FIntPoint, OutputMin)
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER(FVector2f, OutputPixelScale)
		SHADER_PARAMETER(FIntPoint, SceneDepthMin)
		SHADER_PARAMETER(FIntPoint, SceneDepthSize)
		SHADER_PARAMETER(FIntPoint, MaskMin)
		SHADER_PARAMETER(FIntPoint, MaskSize)
		SHADER_PARAMETER(FIntPoint, FirstPersonMin)
		SHADER_PARAMETER(FIntPoint, FirstPersonSize)
		SHADER_PARAMETER(FVector4f, OutlineColor)
		SHADER_PARAMETER(FVector4f, Style)
		SHADER_PARAMETER(int32, DebugMode)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float4>, OutputTexture)
	END_SHADER_PARAMETER_STRUCT()
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(FTeammateOutlineCS, "/Plugin/RDG/OutlierTeammateOutline.usf", "MainCS", SF_Compute);

FScreenPassTexture FRDGTeammateOutlineRendering::Composite(
	FRDGBuilder& GraphBuilder, const FSceneView& View, FScreenPassTexture SceneColor,
	FRDGTextureRef SceneDepth, const FTeammateOutlineResources& Resources)
{
	if (!Resources || Resources->Builder != &GraphBuilder || !Resources->Masks[0] || !SceneDepth || !SceneColor.IsValid()) return SceneColor;
	const FViewInfo& RenderView = static_cast<const FViewInfo&>(View);
	auto* Parameters = GraphBuilder.AllocParameters<FTeammateOutlineCS::FParameters>();
	Parameters->View = View.ViewUniformBuffer;
	Parameters->SceneColorTexture = SceneColor.Texture;
	Parameters->SceneDepthTexture = SceneDepth;
	Parameters->TargetDepthTexture = Resources->Masks[0];
	Parameters->OutputMin = SceneColor.ViewRect.Min;
	Parameters->OutputSize = SceneColor.ViewRect.Size();
	Parameters->OutputPixelScale = FVector2f(
		float(SceneColor.ViewRect.Width()) / FMath::Max(View.UnscaledViewRect.Width(), 1),
		float(SceneColor.ViewRect.Height()) / FMath::Max(View.UnscaledViewRect.Height(), 1));
	Parameters->SceneDepthMin = RenderView.ViewRect.Min;
	Parameters->SceneDepthSize = RenderView.ViewRect.Size();
	Parameters->MaskMin = Resources->MaskRects[0].Min;
	Parameters->MaskSize = Resources->MaskRects[0].Size();

	FRDGTextureRef Empty = nullptr;
	auto MaskOrEmpty = [&](int32 Index, FIntPoint& Min, FIntPoint& Size)
	{
		if (Resources->Masks[Index])
		{
			Min = Resources->MaskRects[Index].Min;
			Size = Resources->MaskRects[Index].Size();
			return Resources->Masks[Index];
		}
		if (!Empty)
		{
			Empty = GraphBuilder.CreateTexture(FRDGTextureDesc::Create2D(FIntPoint(1, 1), PF_R32_FLOAT,
				FClearValueBinding(FLinearColor(EmptyDepth, 0, 0, 0)), TexCreate_RenderTargetable | TexCreate_ShaderResource), TEXT("Outlier.EmptyMask"));
			AddClearRenderTargetPass(GraphBuilder, Empty, FLinearColor(EmptyDepth, 0, 0, 0));
		}
		Min = FIntPoint::ZeroValue;
		Size = FIntPoint(1, 1);
		return Empty;
	};
	Parameters->FirstPersonDepthTexture = MaskOrEmpty(static_cast<int32>(EMask::FirstPerson),
		Parameters->FirstPersonMin, Parameters->FirstPersonSize);
	const auto& Settings = Resources->Settings;
	Parameters->OutlineColor = FVector4f(Settings.Color);
	Parameters->Style = FVector4f(FMath::Clamp(Settings.VisibleThickness, 0.0f, 4.0f),
		FMath::Clamp(Settings.OccludedThickness, 0.0f, 4.0f), FMath::Clamp(Settings.VisibleStrength, 0.0f, 1.0f),
		FMath::Clamp(Settings.OccludedStrength, 0.0f, 1.0f));
	const int32 DebugOverride = CVarTeammateOutlineDebug.GetValueOnRenderThread();
	Parameters->DebugMode = DebugOverride != 0 ? DebugOverride : Settings.DebugMode;
	// Tonemap 출력 포맷이 BGRA/외부 타깃이어도 UAV 쓰기가 가능한 내부 포맷으로 합성한다.
	FRDGTextureDesc Desc = FRDGTextureDesc::Create2D(SceneColor.Texture->Desc.Extent, PF_FloatRGBA,
		FClearValueBinding::None, TexCreate_ShaderResource | TexCreate_RenderTargetable | TexCreate_UAV);
	FRDGTextureRef Output = GraphBuilder.CreateTexture(Desc, TEXT("Outlier.TeammateOutlineColor"));
	Parameters->OutputTexture = GraphBuilder.CreateUAV(Output);
	TShaderMapRef<FTeammateOutlineCS> Shader(GetGlobalShaderMap(View.GetFeatureLevel()));
	FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("OutlierTeammateOutline"), Shader, Parameters,
		FComputeShaderUtils::GetGroupCount(SceneColor.ViewRect.Size(), FIntPoint(8, 8)));
	return FScreenPassTexture(Output, SceneColor.ViewRect);
}
