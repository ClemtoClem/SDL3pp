#pragma once

#include "UIComponents.h"
#include "UILayoutSystem.h"
#include "../SDL3pp_engine/ECS.h"

namespace SDL::UI {

	// ==================================================================================
	// AnimationSystem — drives time-based transitions, CSS-`transition` style.
	//
	// Each frame it advances the linear progress scalars on every widget's
	// AnimationState toward the target implied by its live Widget::states (and toggle
	// / expander logical state), at a rate of dt / AnimationStyle::duration. The
	// easing curve itself is applied where the value is consumed (DrawCtx colour
	// blend, toggle knob), so a value retargeted mid-transition still eases smoothly
	// in both directions.
	//
	// Channels (opt-in via AnimationStyle::channels):
	//   • Hover/Press/Focus/Checked → colour cross-fades (consumed by DrawCtx).
	//   • Toggle                    → ToggleAnim::animT slides the knob.
	//   • Expand                    → ExpanderData::animT reveals content.
	//   • Opacity                   → fade-in on show; fade-out before hide
	//                                 (AnimationState::closing defers Widget hide).
	//
	// A widget without an AnimationState is left untouched (it snaps instantly, the
	// pre-animation behaviour).
	// ==================================================================================

	class AnimationSystem {
	public:
		AnimationSystem(ECS::Context& ctx, LayoutSystem& layout)
			: m_ctx(ctx), m_layout(layout) {}

		void Update(ECS::EntityId root, float dt);

	private:
		ECS::Context& m_ctx;
		LayoutSystem& m_layout;

		// Advance @p v toward @p target by @p rate, clamped to [0,1].
		static void _Approach(float& v, float target, float rate) noexcept {
			if (v < target)      v = (v + rate >= target) ? target : v + rate;
			else if (v > target) v = (v - rate <= target) ? target : v - rate;
		}
	};

	inline void AnimationSystem::Update([[maybe_unused]] ECS::EntityId root, float dt) {
		if (dt <= 0.f) return;

		m_ctx.Each<AnimationState>([&](ECS::EntityId e, AnimationState& an) {
			auto* w = m_ctx.Get<Widget>(e);
			if (!w) return;

			AnimChannel channels = AnimChannel::Color | AnimChannel::Toggle | AnimChannel::Opacity;
			float duration   = 0.15f;
			float opaTime    = 0.15f;
			if (auto* as = m_ctx.Get<AnimationStyle>(e)) {
				channels = as->channels;
				duration = as->duration;
				opaTime  = as->duration;
			}
			const float rate    = duration > 0.f ? dt / duration : 1.f;
			const float opaRate = opaTime  > 0.f ? dt / opaTime  : 1.f;

			// ── Colour state channels ───────────────────────────────────────────
			_Approach(an.hoverT,   (Has(channels, AnimChannel::Hover)   && Has(w->states, WidgetStateFlag::Hovered)) ? 1.f : 0.f, rate);
			_Approach(an.pressT,   (Has(channels, AnimChannel::Press)   && Has(w->states, WidgetStateFlag::Pressed)) ? 1.f : 0.f, rate);
			_Approach(an.focusT,   (Has(channels, AnimChannel::Focus)   && Has(w->states, WidgetStateFlag::Focused)) ? 1.f : 0.f, rate);
			_Approach(an.checkedT, (Has(channels, AnimChannel::Checked) && Has(w->states, WidgetStateFlag::Checked)) ? 1.f : 0.f, rate);

			// ── Toggle knob ─────────────────────────────────────────────────────
			if (Has(channels, AnimChannel::Toggle)) {
				if (auto* ta = m_ctx.Get<ToggleAnim>(e)) {
					auto* tog = m_ctx.Get<ToggleState>(e);
					_Approach(ta->animT, (tog && tog->checked) ? 1.f : 0.f, rate);
				}
			}

			// ── Expander reveal ─────────────────────────────────────────────────
			if (Has(channels, AnimChannel::Expand)) {
				if (auto* ex = m_ctx.Get<ExpanderData>(e))
					_Approach(ex->animT, ex->expanded ? 1.f : 0.f, rate);
			}

			// ── Opacity fade (fade-in on show, fade-out then hide) ──────────────
			if (Has(channels, AnimChannel::Opacity)) {
				float target = an.closing ? 0.f : 1.f;
				_Approach(an.opacity, target, opaRate);
				if (an.closing && an.opacity <= 0.001f) {
					an.closing = false;
					if (Has(w->behavior, WidgetBehaviorFlag::Visible)) {
						w->behavior &= ~WidgetBehaviorFlag::Visible;
						m_layout.MarkDirty();   // drop the now-hidden widget from the index
					}
				}
			}
		});
	}

} // namespace SDL::UI
