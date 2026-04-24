#pragma once
#include "enums.h"
#include "../SDL3pp_rect.h"
#include "../SDL3pp_render.h"

namespace SDL {
namespace UI {

	// ==================================================================================
	// Value
	// ==================================================================================

	enum class Unit : Uint8 {
		Px   = 0,  ///< Pixel
		Ww   = 1,  ///< Percentage of Window Width
		Wh   = 2,  ///< Percentage of Window Height
		Pw   = 3,  ///< Percentage of Parent Width
		Ph   = 4,  ///< Percentage of Parent Height
		Rw   = 5,  ///< Percentage of Root Width
		Rh   = 6,  ///< Percentage of Root Height
		Pcw  = 7,  ///< Percentage of Parent Content Width
		Pch  = 8,  ///< Percentage of Parent Content Height
		Rcw  = 9,  ///< Percentage of Root Content Width
		Rch  = 10, ///< Percentage of Root Content Height
		Pfs  = 11, ///< Percentage of Parent Font Size
		Rfs  = 12, ///< Percentage of Root Font Size
		Auto = 13, ///< Shrink-to-content + optional px offset
		Grow = 14  ///< Grow factor in percentage, take a share of remaining space in the layout direction
	};

	/**
	 * @brief Immutable context passed down the widget tree during the Measure pass.
	 *
	 * Carries the resolved sizes of the window, root widget, and immediate parent so
	 * that percentage-based `Value` units (Pw, Ph, Rw, Ww, …) can be resolved without
	 * up-tree traversal.
	 */
	struct LayoutContext {
		FPoint windowSize     = {0.f, 0.f};

		FPoint rootSize       = {0.f, 0.f};
		FBox   rootPadding    = {0.f, 0.f, 0.f, 0.f};
		float  rootFontSize   = SDL::DEBUG_TEXT_FONT_CHARACTER_SIZE;

		FPoint parentSize     = {0.f, 0.f};
		FBox   parentPadding  = {0.f, 0.f, 0.f, 0.f};
		float  parentFontSize = SDL::DEBUG_TEXT_FONT_CHARACTER_SIZE;
	};

	struct Value {
		float val    = 0.f;
		Unit  unit   = Unit::Auto;
		float offset = 0.f;

		/** Pixel */
		static Value Px  (float v, float o = 0.f) { return {v, Unit::Px,   o}; }
		/** Percentage of Parent Width */
		static Value Pw  (float v, float o = 0.f) { return {v, Unit::Pw,   o}; }
		/** Percentage of Parent Height */
		static Value Ph  (float v, float o = 0.f) { return {v, Unit::Ph,   o}; }
		/** Percentage of Root Width */
		static Value Rw  (float v, float o = 0.f) { return {v, Unit::Rw,   o}; }
		/** Percentage of Root Height */
		static Value Rh  (float v, float o = 0.f) { return {v, Unit::Rh,   o}; }
		/** Percentage of Window Width */
		static Value Ww  (float v = 100.f, float o = 0.f) { return {v, Unit::Ww,   o}; }
		/** Percentage of Window Height */
		static Value Wh  (float v = 100.f, float o = 0.f) { return {v, Unit::Wh,   o}; }
		/** Percentage of Parent Content Width */
		static Value Pcw (float v, float o = 0.f) { return {v, Unit::Pcw,  o}; }
		/** Percentage of Parent Content Height */
		static Value Pch (float v, float o = 0.f) { return {v, Unit::Pch,  o}; }
		/** Percentage of Root Content Width */
		static Value Rcw (float v, float o = 0.f) { return {v, Unit::Rcw,  o}; }
		/** Percentage of Root Content Height */
		static Value Rch (float v, float o = 0.f) { return {v, Unit::Rch,  o}; }
		/** Percentage of Parent Font Size */
		static Value Pfs (float v, float o = 0.f) { return {v, Unit::Pfs,  o}; }
		/** Percentage of Root Font Size */
		static Value Rfs (float v, float o = 0.f) { return {v, Unit::Rfs,  o}; }
		/** Shrink-to-content */
        static Value Auto(float v = 0.f, float o = 0.f) { return {v, Unit::Auto, o}; }
		/** Grow factor: take a share of the remaining space in the layout direction.
		 *  .W(Value::Grow(g)) grows width in InLine parents; .H(Value::Grow(g)) grows height in InColumn parents. */
		static Value Grow(float g = 100.f, float o = 0.f) { return {g, Unit::Grow, o}; }

		/** @brief Returns true if this value is an absolute pixel quantity. */
		[[nodiscard]] bool IsPixel() const noexcept { return unit == Unit::Px; }
		/** @brief Returns true if this value uses shrink-to-content sizing. */
		[[nodiscard]] bool IsAuto() const noexcept { return unit == Unit::Auto; }
		/** @brief Returns true if this value grows to fill remaining space. */
		[[nodiscard]] bool IsGrow() const noexcept { return unit == Unit::Grow; }

		Value operator+(float o) const noexcept { return {val, unit, offset + o}; }
		Value operator-(float o) const noexcept { return {val, unit, offset - o}; }

		/**
		 * @brief Resolve this value to a pixel quantity using the given layout context.
		 * @param ctx  Layout context providing parent, root, and window sizes.
		 * @return     Resolved pixel value (plus any fixed offset).
		 */
		[[nodiscard]] float Resolve(const LayoutContext &ctx) const noexcept {
			float base = 0.f;
			switch (unit) {
				case Unit::Px:
				case Unit::Auto:
				case Unit::Grow:
					base = val;
					break;

				case Unit::Ww:
					base = (val / 100.f) * ctx.windowSize.x;
					break;
				case Unit::Wh:
					base = (val / 100.f) * ctx.windowSize.y;
					break;

				case Unit::Rw:
					base = (val / 100.f) * ctx.rootSize.x;
					break;
				case Unit::Rh:
					base = (val / 100.f) * ctx.rootSize.y;
					break;
				case Unit::Rcw:
					base = (val / 100.f) * (ctx.rootSize.x - ctx.rootPadding.GetH());
					break;
				case Unit::Rch:
					base = (val / 100.f) * (ctx.rootSize.y - ctx.rootPadding.GetV());
					break;
				case Unit::Rfs:
					base = (val / 100.0f) * ctx.rootFontSize;
					break;

				case Unit::Pw:
					base = (val / 100.f) * ctx.parentSize.x;
					break;
				case Unit::Ph:
					base = (val / 100.f) * ctx.parentSize.y;
					break;
				case Unit::Pcw:
					base = (val / 100.f) * (ctx.parentSize.x - ctx.parentPadding.GetH());
					break;
				case Unit::Pch:
					base = (val / 100.f) * (ctx.parentSize.y - ctx.parentPadding.GetV());
					break;
				case Unit::Pfs:
					base = (val / 100.f) * ctx.parentFontSize;
					break;
			}
			return base + offset;
		}
	};

} // namespace UI
} // namespace SDL
