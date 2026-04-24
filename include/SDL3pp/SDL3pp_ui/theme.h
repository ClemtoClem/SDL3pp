#pragma once
#include "components.h"

namespace SDL {
namespace UI {

	// ==================================================================================
	// Forward declarations
	// ==================================================================================
	class UIManager;
	class System;
	struct Builder;

	// ==================================================================================
	// Theme
	// ==================================================================================

	/// @brief Collection of pre-built `Style` factories and global theme controls.
	struct Theme {
		static inline SDL::Color accentColor = {70, 130, 210, 255};
		/** @brief Switch the global accent colour to a dark-theme preset. */
		static void ApplyDark(SDL::UI::System &);
		/** @brief Switch the global accent colour to a light-theme preset. */
		static void ApplyLight(SDL::UI::System &);

		/** @brief Returns a dark-card style (slight elevation, rounded corners). */
		static Style Card() {
			Style s;
			s.bgColor       = {26, 28, 40, 255};
			s.bdColor       = {50, 54, 78, 255};
			s.bdHovered = s.bdPressed = s.bdDisabled = s.bdColor;
			s.borders       = SDL::FBox(1.f);
			s.radius        = SDL::FCorners(8.f);
			return s;
		}
		/**
		 * @brief Returns a filled accent-colored button style.
		 * @param a  Fill color (defaults to `accentColor`).
		 */
		static Style PrimaryButton(SDL::Color a = accentColor) {
			Style s;
			s.bgColor       = a;
			s.bgHovered     = a.Brighten(25);
			s.bgPressed     = a.Darken(35);
			s.bgDisabled    = {40, 42, 55, 160};
			s.bdColor       = a.Brighten(40);
			s.bdHovered = s.bdPressed = s.bdDisabled = s.bdColor;
			s.borders       = SDL::FBox(1.f);
			s.radius        = SDL::FCorners(6.f);
			s.textColor     = s.textHovered = {255, 255, 255, 255};
			return s;
		}
		/** @brief Returns a ghost (outline-only, transparent background) button style. */
		static Style GhostButton() {
			Style s;
			s.bgColor       = {0, 0, 0, 0};
			s.bgHovered     = {50, 55, 80, 80};
			s.bgPressed     = {20, 22, 36, 100};
			s.bdColor       = {70, 75, 100, 200};
			s.bdHovered = s.bdPressed = s.bdDisabled = s.bdColor;
			s.bdHovered     = {110, 120, 170, 255};
			s.borders       = SDL::FBox(1.f);
			s.radius        = SDL::FCorners(6.f);
			return s;
		}
		/** @brief Returns a fully transparent, border-less style (useful for invisible containers). */
		static Style Transparent() {
			Style s;
			s.bgColor       = {0, 0, 0, 0};
			s.borders       = SDL::FBox(0.f);
			s.radius        = SDL::FCorners(0.f);
			return s;
		}
		/** @brief Returns a transparent style with the accent color applied to the text (for section headings). */
		static Style SectionTitle() {
			Style s         = Transparent();
			s.textColor     = accentColor;
			return s;
		}
		/** @brief Returns a red-tinted primary button style for destructive actions. */
		static Style DangerButton() { return PrimaryButton({200, 55, 45, 255}); }
		/** @brief Returns a green-tinted primary button style for confirmatory actions. */
		static Style SuccessButton() { return PrimaryButton({50, 180, 90, 255}); }
		/** @brief Returns a light-theme card style (white/light-grey surface, suitable for light UIs). */
		static Style CardLight() {
			Style s;
			s.bgColor           = {248, 249, 252, 255};
			s.bdColor           = {210, 213, 225, 255};
			s.bdHovered = s.bdPressed = s.bdDisabled = s.bdColor;
			s.borders           = SDL::FBox(1.f);
			s.radius            = SDL::FCorners(8.f);
			s.textColor         = {30, 32, 42, 255};
			s.textHovered       = {10, 12, 22, 255};
			s.textDisabled      = {160, 163, 175, 200};
			s.textPlaceholder   = {160, 162, 175, 180};
			s.bgHovered         = {235, 238, 248, 255};
			s.bgPressed         = {215, 220, 238, 255};
			s.separator         = {210, 213, 225, 255};
			s.track             = {215, 218, 230, 255};
			s.fill              = {70, 130, 210, 255};
			s.thumb             = {100, 160, 230, 255};
			return s;
		}
	};

} // namespace UI
} // namespace SDL
