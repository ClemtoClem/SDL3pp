#pragma once

#include "UIEventSystem.h"
#include "UILayoutSystem.h"
#include "UIRenderSystem.h"
#include "UIAnimationSystem.h"
#include "UIFocusSystem.h"
#include "UIStyleSystem.h"
#include "UIMeasureSystem.h"
#include "UIClipSystem.h"
#include "UICullingSystem.h"
#include "UIIndexSystem.h"
#include "UIBatchingSystem.h"

#include "../SDL3pp_ecs.h"
#include "../SDL3pp_render.h"
#include "../SDL3pp_mixer.h"
#include "../SDL3pp_resources.h"

#include <functional>

namespace SDL::UI {

	class Context;

	// ==================================================================================
	// UIPipeline — orchestrates the full UI update/render pipeline.
	//
	// Pipeline stages executed each frame in order:
	//   1. Input      (EventSystem)     — pointer / keyboard / touch → component state
	//   2. Focus      (FocusSystem)     — tab-key navigation between focusable widgets
	//   3. Animation  (AnimationSystem) — time-based animated properties
	//   4. Style      (StyleSystem)     — resolves computed styles / StyleBundle overrides
	//   5. Measure    (MeasureSystem)   — bottom-up intrinsic-size computation
	//   6. Layout     (LayoutSystem)    — top-down position/size assignment
	//   7. Clip       (ClipSystem)      — scissor rectangle propagation
	//   8. Culling    (CullingSystem)   — marks off-screen widgets
	//   9. Index      (IndexSystem)     — layer-ordered draw/event list (z-order planes)
	//  10. Batching   (BatchingSystem)  — groups draw calls by render state
	//  11. Render     (RenderSystem)    — GPU draw calls
	//
	// Input hit-testing (stage 1) and rendering (stage 11) both consume the single
	// ordered list produced by IndexSystem, so draw-order and event-order always agree.
	//
	// Member declaration order matches initialization order so that systems that hold
	// references to earlier systems (EventSystem → LayoutSystem, etc.) are always
	// initialized after their dependencies.
	// ==================================================================================

	class UIPipeline {
	public:
		UIPipeline(Context& uictx, ECS::Context& ecs,
		           RendererRef renderer, MixerRef mixer, ResourcePool& pool);

		// ── Per-event (call once per SDL event, before Update) ───────────────────
		void FeedEvent(const SDL::Event& ev);

		// ── Per-frame update (stages 1–8) ────────────────────────────────────────
		void Update(ECS::EntityId root, FRect viewport, float dt);

		// ── Render (stages 9–10) ─────────────────────────────────────────────────
		void Render(ECS::EntityId root);

		// ── Layout invalidation (forwarded from Context property setters) ────────
		void MarkLayoutDirty()                noexcept { m_layoutSystem.MarkDirty(); }
		void MarkLayoutDirty(ECS::EntityId e) noexcept { m_layoutSystem.MarkDirty(e); }

		// ── Focus navigation ─────────────────────────────────────────────────────
		void AdvanceFocus(ECS::EntityId root, bool forward) {
			m_inputSystem._AdvanceFocus(root, forward);
		}

		// ── Text input lifecycle (wired by Context) ───────────────────────────────
		std::function<void(bool)> onTextInputActive;

	private:
		// ── Declaration order = initialization order ─────────────────────────────
		// LayoutSystem and IndexSystem must come first: EventSystem, RenderSystem and
		// BatchingSystem all hold a reference to IndexSystem (draw/event order), and
		// the layout dirty flag drives the geometry IndexSystem reads.

		LayoutSystem    m_layoutSystem;   // 1 — no upstream deps
		IndexSystem     m_indexSystem;    // 2 — ordered draw/event list (no upstream deps)
		FocusSystem     m_focusSystem;    // 3 — focus-ring navigation (no upstream deps)
		EventSystem     m_inputSystem;    // 4 — needs m_indexSystem (hit-test) + m_focusSystem

		AnimationSystem m_animationSystem;
		StyleSystem     m_styleSystem;
		MeasureSystem   m_measureSystem;
		ClipSystem      m_clipSystem;
		CullingSystem   m_cullingSystem;

		BatchingSystem  m_batchingSystem; // needs m_indexSystem
		RenderSystem    m_renderSystem;   // needs m_indexSystem
	};

	// ── Implementation ────────────────────────────────────────────────────────────

	inline UIPipeline::UIPipeline(Context& uictx, ECS::Context& ecs,
	                               RendererRef renderer, MixerRef mixer,
	                               ResourcePool& pool)
		: m_layoutSystem(ecs, uictx)
		, m_indexSystem (ecs)
		, m_focusSystem (ecs)
		, m_inputSystem (ecs, m_indexSystem, m_focusSystem, m_layoutSystem, uictx)
		, m_animationSystem(ecs, m_layoutSystem)
		, m_styleSystem (ecs)
		, m_measureSystem(ecs)
		, m_clipSystem  (ecs)
		, m_cullingSystem(ecs)
		, m_batchingSystem(ecs, m_indexSystem)
		, m_renderSystem(ecs, renderer, pool, m_indexSystem, uictx)
	{
		(void)mixer; // reserved for future audio feedback in AnimationSystem
		m_inputSystem.onTextInputActive = [this](bool start) {
			if (onTextInputActive) onTextInputActive(start);
		};
	}

	inline void UIPipeline::FeedEvent(const SDL::Event& ev) {
		m_inputSystem.Feed(ev);
	}

	inline void UIPipeline::Update(ECS::EntityId root, FRect viewport, float dt) {
		if (root == ECS::NullEntity) return;

		// 1. Input & interaction
		m_inputSystem.Process(root);
		m_focusSystem.Update(root);

		// 2. State evolution
		m_animationSystem.Update(root, dt);
		m_styleSystem.Resolve(root);

		// 3. Geometry (measure → layout)
		const bool layoutWasDirty = m_layoutSystem.IsDirty();
		m_measureSystem.Update(root);
		m_layoutSystem.Process(root, viewport);

		// 4. Visibility & optimisation. Clip is computed inside LayoutSystem (shares its
		// padding + scrollbar model); ClipSystem stays a documented no-op. Culling and
		// indexing only depend on geometry, so they rerun together when layout changed.
		m_clipSystem.Update(root);
		if (layoutWasDirty) {
			m_cullingSystem.Update(root, viewport);
			m_indexSystem.Build(root);
		}

		// 6. Animation pass currently inside RenderSystem (will move to AnimationSystem)
		m_renderSystem.ProcessAnimate(dt, root);
	}

	inline void UIPipeline::Render(ECS::EntityId root) {
		if (root == ECS::NullEntity) return;

		// 9–10. Batching + render
		(void)m_batchingSystem.Generate(root);
		m_renderSystem.Process(root, ECS::NullEntity);
	}

} // namespace SDL::UI
