#pragma once

#include "UIComponents.h"
#include "../SDL3pp_ecs.h"
#include "../SDL3pp_events.h"
#include "../SDL3pp_rect.h"
#include "../SDL3pp_clipboard.h"
#include "../SDL3pp_scancode.h"
#include "../SDL3pp_keycode.h"

#include <functional>
#include <cmath>
#include <algorithm>

namespace SDL::UI {

	class UILayoutSystem;
	class System;

	// ==================================================================================
	// UIEventSystem — pointer / keyboard / scroll dispatch
	// ==================================================================================
	//
	// Two-phase event flow:
	//   1. ProcessEvent feeds raw SDL events into the system (one per event).
	//      The system updates its internal mouse/keyboard mirror state.
	//   2. Process() runs once per frame to dispatch hover/press/click/drag/keypress
	//      events to the relevant entities.
	//
	// Dispatch is component-driven: instead of a switch on WidgetType, each
	// handler queries the components present on the entity (e.g. SliderData +
	// NumericValue → slider drag; KnobData + NumericValue → knob drag). Adding a
	// new draggable widget just means adding a new branch here.
	//
	// ==================================================================================

	class UIEventSystem {
	public:
		UIEventSystem(ECS::Context& ctx, UILayoutSystem& layout, System& sys);

		void Feed(const SDL::Event& ev);
		void Process(ECS::EntityId root);

		[[nodiscard]] ECS::EntityId GetFocused() const noexcept { return m_focused; }
		[[nodiscard]] ECS::EntityId GetHovered() const noexcept { return m_hovered; }
		[[nodiscard]] ECS::EntityId GetPressed() const noexcept { return m_pressed; }

		void SetFocused(ECS::EntityId e) noexcept { m_focused = e; }
		void ClearTransientState() noexcept;  ///< Reset per-frame transient pulses (clicks, scroll).

		void _AdvanceFocus(ECS::EntityId root, bool forward);

		/// Called by System after construction so _SetFocus can start/stop text input.
		std::function<void(bool)> onTextInputActive;

	private:
		ECS::Context&    m_ctx;
		UILayoutSystem&  m_layout;
		System&          m_sys;

		// ── Focus / hover / press tracking ───────────────────────────────────
		ECS::EntityId    m_focused = ECS::NullEntity;
		ECS::EntityId    m_hovered = ECS::NullEntity;
		ECS::EntityId    m_pressed = ECS::NullEntity;
		ECS::EntityId    m_root    = ECS::NullEntity; ///< Updated each frame by Process(root)

		// ── Pointer mirror ───────────────────────────────────────────────────
		FPoint           m_mousePos       = {};
		FPoint           m_mouseDelta     = {};
		bool             m_mouseDown      = false;
		bool             m_mousePressed   = false;
		bool             m_mouseReleased  = false;
		float            m_scrollX        = 0.f;
		float            m_scrollY        = 0.f;
		SDL::MouseButton m_lastButton     = static_cast<SDL::MouseButton>(0);
		int              m_clickCount     = 0;
		float            m_lastClickTime  = 0.f;

		// ── Per-handler dispatch ─────────────────────────────────────────────
		ECS::EntityId _HitTest      (ECS::EntityId root, FPoint p) const;
		void          _DispatchHover(ECS::EntityId root);
		void          _DispatchPress(ECS::EntityId target);
		void          _DispatchDrag (ECS::EntityId target);
		void          _DispatchRelease(ECS::EntityId target);
		void          _DispatchScroll(ECS::EntityId target, float dx, float dy);
		void          _DispatchKey  (const SDL::Event& ev);
		void          _HandleTextInput(ECS::EntityId e, const std::string& text);
		void          _HandleListBoxNavigation(ECS::EntityId e, SDL::Scancode scancode);
		void          _CollectFocusable(ECS::EntityId root, std::vector<ECS::EntityId>& out) const;

		void _SetFocus(ECS::EntityId e);
	};

	// ==================================================================================
	// Implementation: UIEventSystem
	// ==================================================================================

	inline UIEventSystem::UIEventSystem(ECS::Context& ctx, UILayoutSystem& layout, System& sys)
		: m_ctx(ctx), m_layout(layout), m_sys(sys) {}

	inline void UIEventSystem::Feed(const SDL::Event& ev) {
		// Update mouse state
		if (ev.type == SDL::EVENT_MOUSE_MOTION) {
			m_mouseDelta = {ev.motion.x - m_mousePos.x, ev.motion.y - m_mousePos.y};
			m_mousePos = {ev.motion.x, ev.motion.y};
		} else if (ev.type == SDL::EVENT_MOUSE_BUTTON_DOWN) {
			m_mousePressed = true;
			m_mouseDown = true;
			m_lastButton = ev.button.button;
		} else if (ev.type == SDL::EVENT_MOUSE_BUTTON_UP) {
			m_mouseReleased = true;
			m_mouseDown = false;
		} else if (ev.type == SDL::EVENT_MOUSE_WHEEL) {
			m_scrollX += ev.wheel.x;
			m_scrollY += ev.wheel.y;
		} else if (ev.type == SDL::EVENT_KEY_DOWN) {
			// Store key event for processing (handle immediately for special keys)
			_DispatchKey(ev);
		} else if (ev.type == SDL::EVENT_TEXT_INPUT) {
			// Handle typed text input (printable characters)
			if (m_focused != ECS::NullEntity && m_ctx.IsAlive(m_focused)) {
				auto *w = m_ctx.Get<Widget>(m_focused);
				if (w && (w->type == WidgetType::Input || w->type == WidgetType::TextArea)) {
					_HandleTextInput(m_focused, ev.text.text);
				}
			}
		}
	}

	inline void UIEventSystem::Process(ECS::EntityId root) {
		if (!m_ctx.IsAlive(root)) return;
		m_root = root;

		// Update hover state
		_DispatchHover(root);

		// Process press
		if (m_mousePressed) {
			ECS::EntityId target = _HitTest(root, m_mousePos);
			if (target != ECS::NullEntity && m_ctx.IsAlive(target)) {
				auto *w = m_ctx.Get<Widget>(target);
				if (w && Has(w->behavior, WidgetBehaviorFlag::Enable) && Has(w->behavior, WidgetBehaviorFlag::Selectable)) {
					_DispatchPress(target);
				}
			}
		}

		// Process drag
		if (m_mouseDown && m_pressed != ECS::NullEntity && m_ctx.IsAlive(m_pressed)) {
			_DispatchDrag(m_pressed);
		}

		// Process release
		if (m_mouseReleased && m_pressed != ECS::NullEntity && m_ctx.IsAlive(m_pressed)) {
			_DispatchRelease(m_pressed);
		}

		// Process scroll
		if (m_scrollX != 0.f || m_scrollY != 0.f) {
			ECS::EntityId target = _HitTest(root, m_mousePos);
			if (target != ECS::NullEntity) {
				_DispatchScroll(target, m_scrollX, m_scrollY);
			}
			m_scrollX = 0.f;
			m_scrollY = 0.f;
		}

		// Clear transient pulse flags
		m_mousePressed = false;
		m_mouseReleased = false;
	}

	inline void UIEventSystem::ClearTransientState() noexcept {
		m_mousePressed = false;
		m_mouseReleased = false;
		m_scrollX = 0.f;
		m_scrollY = 0.f;
		m_clickCount = 0;
	}

	inline void UIEventSystem::_SetFocus(ECS::EntityId e) {
		if (m_focused == e) return;

		// Unfocus previous
		if (m_focused != ECS::NullEntity && m_ctx.IsAlive(m_focused)) {
			auto *pw = m_ctx.Get<Widget>(m_focused);
			if (pw) pw->states &= ~WidgetStateFlag::Focused;
			if (auto *cb = m_ctx.Get<Callbacks>(m_focused); cb && cb->onFocusLose) {
				cb->onFocusLose();
			}
			// Stop text input when leaving an Input/TextArea
			if (pw && (pw->type == WidgetType::Input || pw->type == WidgetType::TextArea)) {
				if (onTextInputActive) onTextInputActive(false);
			}
		}

		// Focus new
		m_focused = e;
		if (m_focused != ECS::NullEntity && m_ctx.IsAlive(m_focused)) {
			auto *nw = m_ctx.Get<Widget>(m_focused);
			if (nw) nw->states |= WidgetStateFlag::Focused;
			if (auto *cb = m_ctx.Get<Callbacks>(m_focused); cb && cb->onFocusGain) {
				cb->onFocusGain();
			}
			// Start text input when focusing an Input/TextArea
			if (nw && (nw->type == WidgetType::Input || nw->type == WidgetType::TextArea)) {
				if (onTextInputActive) onTextInputActive(true);
			}
		}
	}

	inline ECS::EntityId UIEventSystem::_HitTest(ECS::EntityId root, FPoint p) const {
		if (!m_ctx.IsAlive(root)) return ECS::NullEntity;

		auto *w = m_ctx.Get<Widget>(root);
		auto *cr = m_ctx.Get<ComputedRect>(root);
		if (!w || !cr || !Has(w->behavior, WidgetBehaviorFlag::Visible)) return ECS::NullEntity;

		if (!cr->outer_clip.Contains(p)) return ECS::NullEntity;

		// Check children first (depth-first, reverse order for top-to-bottom layering)
		auto *ch = m_ctx.Get<Children>(root);
		if (ch) {
			for (int i = (int)ch->ids.size() - 1; i >= 0; --i) {
				ECS::EntityId result = _HitTest(ch->ids[i], p);
				if (result != ECS::NullEntity) return result;
			}
		}

		// Check self (only if hoverable)
		if (Has(w->behavior, WidgetBehaviorFlag::Hoverable)) {
			return root;
		}

		return ECS::NullEntity;
	}

	inline void UIEventSystem::_DispatchHover(ECS::EntityId root) {
		ECS::EntityId newHover = _HitTest(root, m_mousePos);

		// Clear old hover
		m_ctx.Each<Widget>([](ECS::EntityId, Widget &w) {
			w.states &= ~WidgetStateFlag::Hovered;
		});

		// Set new hover
		if (newHover != ECS::NullEntity && m_ctx.IsAlive(newHover)) {
			if (auto *w = m_ctx.Get<Widget>(newHover)) {
				w->states |= WidgetStateFlag::Hovered;
			}
		}

		// Trigger hover callbacks
		if (newHover != m_hovered) {
			if (m_hovered != ECS::NullEntity && m_ctx.IsAlive(m_hovered)) {
				if (auto *cb = m_ctx.Get<Callbacks>(m_hovered); cb && cb->onMouseLeave) {
					cb->onMouseLeave();
				}
			}
			if (newHover != ECS::NullEntity && m_ctx.IsAlive(newHover)) {
				if (auto *cb = m_ctx.Get<Callbacks>(newHover); cb && cb->onMouseEnter) {
					cb->onMouseEnter();
				}
			}
			m_hovered = newHover;
		}
	}

	inline void UIEventSystem::_DispatchPress(ECS::EntityId target) {
		m_pressed = target;

		auto *w = m_ctx.Get<Widget>(target);
		if (!w) return;

		// Focus if focusable
		if (Has(w->behavior, WidgetBehaviorFlag::Focusable)) {
			_SetFocus(target);
		}

		// Begin drag for various widget types
		if (w->type == WidgetType::Slider) {
			if (auto *sd = m_ctx.Get<SliderData>(target)) {
				auto *nv = m_ctx.Get<NumericValue<float>>(target);
				auto *cr = m_ctx.Get<ComputedRect>(target);
				if (nv && cr) {
					sd->drag = true;
					sd->dragStartPos = (sd->orientation == Orientation::Horizontal) ? m_mousePos.x : m_mousePos.y;
					sd->dragStartVal = nv->value;
				}
			}
		} else if (w->type == WidgetType::ScrollBar) {
			if (auto *sb = m_ctx.Get<ScrollBarData>(target)) {
				sb->drag = true;
				sb->dragStartPos = (sb->orientation == Orientation::Vertical) ? m_mousePos.y : m_mousePos.x;
				sb->dragStartOff = sb->offset;
			}
		} else if (w->type == WidgetType::Knob) {
			if (auto *kd = m_ctx.Get<KnobData>(target)) {
				auto *cr = m_ctx.Get<ComputedRect>(target);
				auto *nv = m_ctx.Get<NumericValue<float>>(target);
				if (cr && nv) {
					kd->drag = true;
					kd->dragStartY = m_mousePos.y;
					kd->dragStartVal = nv->value;
					float cx = cr->absolute.x + cr->absolute.w * 0.5f;
					float cy = cr->absolute.y + cr->absolute.h * 0.5f;
					kd->dragStartAngle = std::atan2(m_mousePos.y - cy, m_mousePos.x - cx) * (180.f / 3.14159265f);
				}
			}
		} else if (w->type == WidgetType::Splitter) {
			if (auto *spl = m_ctx.Get<SplitterData>(target)) {
				spl->dragging = true;
				spl->dragStart = (spl->orientation == Orientation::Horizontal) ? m_mousePos.x : m_mousePos.y;
				spl->dragRatio = spl->ratio;
			}
		} else if (w->type == WidgetType::ColorPicker) {
			if (auto *cp = m_ctx.Get<ColorPickerData>(target)) {
				cp->dragging = true;
			}
		}
	}

	inline void UIEventSystem::_DispatchDrag(ECS::EntityId target) {
		auto *w = m_ctx.Get<Widget>(target);
		if (!w) return;

		// Slider drag
		if (w->type == WidgetType::Slider) {
			if (auto *sd = m_ctx.Get<SliderData>(target); sd && sd->drag) {
				auto *nv = m_ctx.Get<NumericValue<float>>(target);
				auto *cr = m_ctx.Get<ComputedRect>(target);
				if (nv && cr) {
					bool h = (sd->orientation == Orientation::Horizontal);
					SDL::FBox pad = {};
					if (auto* sps = m_ctx.Get<SpacingStyle>(target)) pad = sps->padding;
					float trackLen = h
						? (cr->absolute.w - pad.left - pad.right - 16.f)
						: (cr->absolute.h - pad.top  - pad.bottom - 16.f);
					if (trackLen > 0.f) {
						float cur = h ? m_mousePos.x : m_mousePos.y;
						float ddx = cur - sd->dragStartPos;
						float v = SDL::Clamp(
							(float)(sd->dragStartVal + ddx / trackLen * (nv->max - nv->min)),
							nv->min, nv->max);
						if (nv->step > 0.f) v = nv->min + std::round((v - nv->min) / nv->step) * nv->step;
						if (v != nv->value) {
							nv->value = v;
							if (auto *cb = m_ctx.Get<Callbacks>(target); cb && cb->onChange) {
								cb->onChange((float)v);
							}
						}
					}
				}
			}
		} else if (w->type == WidgetType::ScrollBar) {
			if (auto *sb = m_ctx.Get<ScrollBarData>(target); sb && sb->drag) {
				bool v = (sb->orientation == Orientation::Vertical);
				float cur = v ? m_mousePos.y : m_mousePos.x;
				float dx = cur - sb->dragStartPos;
				float ratio = (sb->viewSize > 0.f && sb->contentSize > sb->viewSize)
					? sb->viewSize / sb->contentSize : 1.f;
				float maxOff = SDL::Max(0.f, sb->contentSize - sb->viewSize);
				float doff = (ratio > 0.f) ? dx / ratio : 0.f;
				float noff = SDL::Clamp(sb->dragStartOff + doff, 0.f, maxOff);
				if (noff != sb->offset) {
					sb->offset = noff;
					if (auto *cb = m_ctx.Get<Callbacks>(target); cb && cb->onScroll) {
						cb->onScroll(noff);
					}
				}
			}
		} else if (w->type == WidgetType::Knob) {
			if (auto *kd = m_ctx.Get<KnobData>(target); kd && kd->drag) {
				auto *cr = m_ctx.Get<ComputedRect>(target);
				auto *nv = m_ctx.Get<NumericValue<float>>(target);
				if (cr && nv) {
					float cx = cr->absolute.x + cr->absolute.w * 0.5f;
					float cy = cr->absolute.y + cr->absolute.h * 0.5f;
					float curAngle = std::atan2(m_mousePos.y - cy, m_mousePos.x - cx) * (180.f / 3.14159265f);
					float dAngle = curAngle - kd->dragStartAngle;
					if (dAngle > 180.f) dAngle -= 360.f;
					if (dAngle < -180.f) dAngle += 360.f;
					double newVal = SDL::Clamp(kd->dragStartVal + dAngle / 270.0 * (nv->max - nv->min), nv->min, nv->max);
					if (newVal != nv->value) {
						nv->value = newVal;
						if (auto *cb = m_ctx.Get<Callbacks>(target); cb && cb->onChange) {
							cb->onChange((float)newVal);
						}
					}
					kd->dragStartAngle = curAngle;
					kd->dragStartVal = nv->value;
				}
			}
		} else if (w->type == WidgetType::Splitter) {
			if (auto *spl = m_ctx.Get<SplitterData>(target); spl && spl->dragging) {
				auto *cr = m_ctx.Get<ComputedRect>(target);
				if (cr) {
					bool horiz = (spl->orientation == Orientation::Horizontal);
					float cur = horiz ? m_mousePos.x : m_mousePos.y;
					float total = horiz ? cr->absolute.w : cr->absolute.h;
					float start = horiz ? cr->absolute.x : cr->absolute.y;
					float nr = SDL::Clamp((cur - start) / SDL::Max(1.f, total), spl->minRatio, spl->maxRatio);
					if (nr != spl->ratio) {
						spl->ratio = nr;
						if (auto *cb = m_ctx.Get<Callbacks>(target); cb && cb->onChange) {
							cb->onChange(nr);
						}
					}
				}
			}
		}
	}

	inline void UIEventSystem::_DispatchRelease(ECS::EntityId target) {
		auto *w = m_ctx.Get<Widget>(target);
		auto *cb = m_ctx.Get<Callbacks>(target);

		if (w) w->states &= ~WidgetStateFlag::Pressed;

		// Clear drag flags
		if (auto *sd = m_ctx.Get<SliderData>(target)) sd->drag = false;
		if (auto *sb = m_ctx.Get<ScrollBarData>(target)) sb->drag = false;
		if (auto *kd = m_ctx.Get<KnobData>(target)) kd->drag = false;
		if (auto *spl = m_ctx.Get<SplitterData>(target)) spl->dragging = false;
		if (auto *cp = m_ctx.Get<ColorPickerData>(target)) cp->dragging = false;

		// Trigger click callback if released on the pressed target
		if (m_hovered == target && w && Has(w->behavior, WidgetBehaviorFlag::Enable) && Has(w->behavior, WidgetBehaviorFlag::Selectable)) {
			// Toggle: flip checked state + fire onToggle
			if (w->type == WidgetType::Toggle) {
				if (auto* tog = m_ctx.Get<ToggleData>(target)) {
					tog->checked = !tog->checked;
					if (Has(w->states, WidgetStateFlag::Checked)) w->states &= ~WidgetStateFlag::Checked;
					else                                          w->states |=  WidgetStateFlag::Checked;
					if (cb && cb->onToggle) cb->onToggle(tog->checked);
				}
			}
			// Radio: set checked, deselect other members of same group
			else if (w->type == WidgetType::Radio) {
				if (auto* rd = m_ctx.Get<RadioData>(target); rd && !rd->checked) {
					// Deselect all other radios in the same group
					m_ctx.Each<RadioData>([&](ECS::EntityId other, RadioData& otherRd) {
						if (other != target && otherRd.group == rd->group && otherRd.checked) {
							otherRd.checked = false;
							if (auto* ow = m_ctx.Get<Widget>(other))
								ow->states &= ~WidgetStateFlag::Checked;
						}
					});
					rd->checked = true;
					w->states |= WidgetStateFlag::Checked;
					if (cb && cb->onToggle) cb->onToggle(true);
				}
			}

			if (cb && cb->onClick) {
				cb->onClick(m_lastButton);
			}
		}

		m_pressed = ECS::NullEntity;
	}

	inline void UIEventSystem::_DispatchScroll(ECS::EntityId target, float dx, float dy) {
		if (!m_ctx.IsAlive(target)) return;

		// Walk up the hierarchy to find the nearest scrollable container
		ECS::EntityId scrollable = ECS::NullEntity;
		ECS::EntityId cur = target;
		while (cur != ECS::NullEntity && m_ctx.IsAlive(cur)) {
			auto *w = m_ctx.Get<Widget>(cur);
			if (w && (Has(w->behavior, WidgetBehaviorFlag::ScrollableY) ||
			          Has(w->behavior, WidgetBehaviorFlag::ScrollableX))) {
				scrollable = cur;
				break;
			}
			auto *par = m_ctx.Get<Parent>(cur);
			cur = par ? par->id : ECS::NullEntity;
		}

		if (scrollable != ECS::NullEntity && m_ctx.IsAlive(scrollable)) {
			auto *lp = m_ctx.Get<LayoutProps>(scrollable);
			auto *w  = m_ctx.Get<Widget>(scrollable);
			if (lp && w) {
				if (Has(w->behavior, WidgetBehaviorFlag::ScrollableY)) {
					auto *cr = m_ctx.Get<ComputedRect>(scrollable);
					float viewH = cr ? cr->absolute.h : lp->height.val;
					float maxScrollY = SDL::Max(0.f, lp->contentH - viewH);
					lp->scrollY = SDL::Clamp(lp->scrollY - dy * 20.f, 0.f, maxScrollY);
				}
				if (Has(w->behavior, WidgetBehaviorFlag::ScrollableX)) {
					auto *cr = m_ctx.Get<ComputedRect>(scrollable);
					float viewW = cr ? cr->absolute.w : lp->width.val;
					float maxScrollX = SDL::Max(0.f, lp->contentW - viewW);
					lp->scrollX = SDL::Clamp(lp->scrollX - dx * 20.f, 0.f, maxScrollX);
				}
				w->dirty |= DirtyFlag::Layout | DirtyFlag::Render;
			}
			auto *cb = m_ctx.Get<Callbacks>(scrollable);
			if (cb && cb->onScroll) cb->onScroll(dy);
		} else {
			// No scrollable container found — still fire callback on original target
			auto *cb = m_ctx.Get<Callbacks>(target);
			if (cb && cb->onScroll) cb->onScroll(dy);
		}
	}

	inline void UIEventSystem::_DispatchKey(const SDL::Event& ev) {
		if (ev.type != SDL::EVENT_KEY_DOWN) return;
		if (m_focused == ECS::NullEntity || !m_ctx.IsAlive(m_focused)) return;

		auto *w = m_ctx.Get<Widget>(m_focused);
		if (!w) return;

		SDL::Scancode scancode = ev.key.scancode;
		SDL::Keymod mods = ev.key.mod;
		bool ctrlDown = (mods & SDL::KMOD_CTRL) != 0;
		bool shiftDown = (mods & SDL::KMOD_SHIFT) != 0;

		// ── Text editing widgets ─────────────────────────────────────────────────
		if (w->type == WidgetType::Input || w->type == WidgetType::TextArea) {
			auto *te = m_ctx.Get<TextEdit>(m_focused);
			auto *ts = m_ctx.Get<TextSelection>(m_focused);
			if (!te || !ts) return;

			// Skip read-only widgets
			if (te->readOnly &&
				scancode != SDL_SCANCODE_LEFT && scancode != SDL_SCANCODE_RIGHT &&
				scancode != SDL_SCANCODE_UP && scancode != SDL_SCANCODE_DOWN &&
				scancode != SDL_SCANCODE_HOME && scancode != SDL_SCANCODE_END &&
				scancode != SDL_SCANCODE_A) return;  // Allow selection in read-only

			// ── Backspace: delete char before cursor or selection ───────────────
			if (scancode == SDL_SCANCODE_BACKSPACE) {
				bool deleted = ts->DeleteFrom(te->text, te->cursor);
				if (!deleted && te->cursor > 0) {
					// Find the start of the previous UTF-8 character
					int newPos = te->cursor - 1;
					while (newPos > 0 && (te->text[newPos] & 0xC0) == 0x80) {
						newPos--;
					}
					te->text.erase(newPos, te->cursor - newPos);
					te->cursor = newPos;
					deleted = true;
				}
				if (deleted) {
					if (auto *w2 = m_ctx.Get<Widget>(m_focused)) {
						w2->dirty |= DirtyFlag::Layout | DirtyFlag::Render;
					}
					if (auto *cb = m_ctx.Get<Callbacks>(m_focused); cb && cb->onTextChange) {
						cb->onTextChange(te->text);
					}
				}
			}
			// ── Delete: delete char at cursor or selection ──────────────────────
			else if (scancode == SDL_SCANCODE_DELETE) {
				bool deleted = ts->DeleteFrom(te->text, te->cursor);
				if (!deleted && te->cursor < (int)te->text.size()) {
					// Find the end of the current UTF-8 character
					int endPos = te->cursor + 1;
					while (endPos < (int)te->text.size() && (te->text[endPos] & 0xC0) == 0x80) {
						endPos++;
					}
					te->text.erase(te->cursor, endPos - te->cursor);
					deleted = true;
				}
				if (deleted) {
					if (auto *w2 = m_ctx.Get<Widget>(m_focused)) {
						w2->dirty |= DirtyFlag::Layout | DirtyFlag::Render;
					}
					if (auto *cb = m_ctx.Get<Callbacks>(m_focused); cb && cb->onTextChange) {
						cb->onTextChange(te->text);
					}
				}
			}
			// ── Arrow keys: move cursor ──────────────────────────────────────────
			else if (scancode == SDL_SCANCODE_LEFT) {
				if (ts->HasSelection() && !shiftDown) {
					te->cursor = ts->Min();
					ts->Clear();
				} else if (te->cursor > 0) {
					int newPos = te->cursor - 1;
					while (newPos > 0 && (te->text[newPos] & 0xC0) == 0x80) {
						newPos--;
					}
					if (shiftDown) {
						if (ts->anchor < 0) ts->anchor = te->cursor;
						ts->focus = newPos;
					} else {
						te->cursor = newPos;
						ts->Clear();
					}
				}
			}
			else if (scancode == SDL_SCANCODE_RIGHT) {
				if (ts->HasSelection() && !shiftDown) {
					te->cursor = ts->Max();
					ts->Clear();
				} else if (te->cursor < (int)te->text.size()) {
					int newPos = te->cursor + 1;
					while (newPos < (int)te->text.size() && (te->text[newPos] & 0xC0) == 0x80) {
						newPos++;
					}
					if (shiftDown) {
						if (ts->anchor < 0) ts->anchor = te->cursor;
						ts->focus = newPos;
					} else {
						te->cursor = newPos;
						ts->Clear();
					}
				}
			}
			// ── Up/Down arrows in TextArea ───────────────────────────────────────
			else if (w->type == WidgetType::TextArea && (scancode == SDL_SCANCODE_UP || scancode == SDL_SCANCODE_DOWN)) {
				int curLine = te->LineOf(te->cursor);
				int curCol = te->ColOf(te->cursor);
				int newLine = curLine;
				if (scancode == SDL_SCANCODE_UP && curLine > 0) {
					newLine--;
				} else if (scancode == SDL_SCANCODE_DOWN && curLine < te->LineCount() - 1) {
					newLine++;
				}
				if (newLine != curLine) {
					int lineStart = te->LineStart(newLine);
					int lineEnd = te->LineEnd(newLine);
					int newPos = lineStart + std::min(curCol, lineEnd - lineStart);
					if (shiftDown) {
						if (ts->anchor < 0) ts->anchor = te->cursor;
						ts->focus = newPos;
					} else {
						te->cursor = newPos;
						ts->Clear();
					}
				}
			}
			// ── Home: go to line start ───────────────────────────────────────────
			else if (scancode == SDL_SCANCODE_HOME) {
				int lineStart = te->LineStart(te->LineOf(te->cursor));
				if (shiftDown) {
					if (ts->anchor < 0) ts->anchor = te->cursor;
					ts->focus = lineStart;
				} else {
					te->cursor = lineStart;
					ts->Clear();
				}
			}
			// ── End: go to line end ──────────────────────────────────────────────
			else if (scancode == SDL_SCANCODE_END) {
				int lineEnd = te->LineEnd(te->LineOf(te->cursor));
				if (shiftDown) {
					if (ts->anchor < 0) ts->anchor = te->cursor;
					ts->focus = lineEnd;
				} else {
					te->cursor = lineEnd;
					ts->Clear();
				}
			}
			// ── Ctrl+A: select all ──────────────────────────────────────────────
			else if (ctrlDown && scancode == SDL_SCANCODE_A) {
				ts->Set(0, (int)te->text.size(), (int)te->text.size());
			}
			// ── Ctrl+C: copy selection ──────────────────────────────────────────
			else if (ctrlDown && scancode == SDL_SCANCODE_C) {
				if (ts->HasSelection()) {
					std::string selected = ts->GetSelected(te->text);
					try {
						SDL::SetClipboardText(selected);
					} catch (...) {
						// Clipboard error - silently ignore
					}
				}
			}
			// ── Ctrl+V: paste from clipboard ────────────────────────────────────
			else if (ctrlDown && scancode == SDL_SCANCODE_V) {
				try {
					auto clipText = SDL::GetClipboardText();
					if (clipText) {
						_HandleTextInput(m_focused, std::string(clipText.data()));
					}
				} catch (...) {
					// Clipboard error - silently ignore
				}
			}
			// ── Return/Enter: depends on widget type ────────────────────────────
			else if (scancode == SDL_SCANCODE_RETURN) {
				if (w->type == WidgetType::Input) {
					// Fire onClick for Input widgets
					auto *cb = m_ctx.Get<Callbacks>(m_focused);
					if (cb && cb->onClick) {
						cb->onClick(SDL::BUTTON_LEFT);
					}
				} else {
					// Insert newline in TextArea
					_HandleTextInput(m_focused, "\n");
				}
			}
			// ── Tab in TextArea: insert tab spaces; Tab in Input: advance focus ──
			else if (scancode == SDL_SCANCODE_TAB) {
				if (w->type == WidgetType::TextArea) {
					std::string tab(te->tabSize, ' ');
					_HandleTextInput(m_focused, tab);
				} else {
					_AdvanceFocus(ECS::NullEntity, !shiftDown);
				}
			}
		}
		// ── Global TAB focus traversal (when no text widget is focused) ──────────────
		else if (scancode == SDL_SCANCODE_TAB) {
			_AdvanceFocus(ECS::NullEntity, !shiftDown);
		}
		// ── ListBox navigation ───────────────────────────────────────────────────────
		else if (w->type == WidgetType::ListBox) {
			auto *lb = m_ctx.Get<ListBoxData>(m_focused);
			auto *ilv = m_ctx.Get<ItemListView>(m_focused);
			if (lb && ilv) {
				_HandleListBoxNavigation(m_focused, scancode);
			}
		}
		// ── ComboBox keyboard: Enter/Space to toggle, Escape to close ────────────────
		else if (w->type == WidgetType::ComboBox) {
			if (auto *cbd = m_ctx.Get<ComboBoxData>(m_focused)) {
				if (scancode == SDL_SCANCODE_RETURN || scancode == SDL_SCANCODE_SPACE) {
					cbd->open = !cbd->open;
					if (cbd->overlay != ECS::NullEntity && m_ctx.IsAlive(cbd->overlay)) {
						auto *ow = m_ctx.Get<Widget>(cbd->overlay);
						if (ow) {
							if (cbd->open) ow->behavior |= WidgetBehaviorFlag::Visible;
							else           ow->behavior &= ~WidgetBehaviorFlag::Visible;
						}
					}
				} else if (scancode == SDL_SCANCODE_ESCAPE && cbd->open) {
					cbd->open = false;
					if (cbd->overlay != ECS::NullEntity && m_ctx.IsAlive(cbd->overlay)) {
						if (auto *ow = m_ctx.Get<Widget>(cbd->overlay))
							ow->behavior &= ~WidgetBehaviorFlag::Visible;
					}
				}
			}
		}
		// ── Expander keyboard: Enter/Space to toggle ─────────────────────────────────
		else if (w->type == WidgetType::Expander) {
			if (auto *exd = m_ctx.Get<ExpanderData>(m_focused)) {
				if (scancode == SDL_SCANCODE_RETURN || scancode == SDL_SCANCODE_SPACE) {
					exd->expanded = !exd->expanded;
					if (exd->contentEntity != ECS::NullEntity && m_ctx.IsAlive(exd->contentEntity)) {
						auto *cw = m_ctx.Get<Widget>(exd->contentEntity);
						if (cw) {
							if (exd->expanded) cw->behavior |= WidgetBehaviorFlag::Visible;
							else               cw->behavior &= ~WidgetBehaviorFlag::Visible;
						}
					}
				}
			}
		}
		// ── Button / Toggle / Radio: Enter/Space to activate ─────────────────────────
		else if (w->type == WidgetType::Button || w->type == WidgetType::Toggle || w->type == WidgetType::Radio) {
			if (scancode == SDL_SCANCODE_RETURN || scancode == SDL_SCANCODE_SPACE) {
				auto *cb = m_ctx.Get<Callbacks>(m_focused);
				if (w->type == WidgetType::Toggle) {
					if (auto* tog = m_ctx.Get<ToggleData>(m_focused)) {
						tog->checked = !tog->checked;
						if (tog->checked) w->states |=  WidgetStateFlag::Checked;
						else              w->states &= ~WidgetStateFlag::Checked;
						if (cb && cb->onToggle) cb->onToggle(tog->checked);
					}
				} else if (w->type == WidgetType::Radio) {
					if (auto* rd = m_ctx.Get<RadioData>(m_focused); rd && !rd->checked) {
						m_ctx.Each<RadioData>([&](ECS::EntityId other, RadioData& otherRd) {
							if (other != m_focused && otherRd.group == rd->group && otherRd.checked) {
								otherRd.checked = false;
								if (auto* ow = m_ctx.Get<Widget>(other))
									ow->states &= ~WidgetStateFlag::Checked;
							}
						});
						rd->checked = true;
						w->states |= WidgetStateFlag::Checked;
						if (cb && cb->onToggle) cb->onToggle(true);
					}
				}
				if (cb && cb->onClick) cb->onClick(SDL::BUTTON_LEFT);
			}
		}
	}

	// ── Additional specialized event handlers ─────────────────────────────────────

	// Handler for TextArea text input - could be called from Process()
	// when handling SDL_TEXTINPUT events
	inline void UIEventSystem::_HandleTextInput(ECS::EntityId e, const std::string& text) {
		auto *te = m_ctx.Get<TextEdit>(e);
		auto *ts = m_ctx.Get<TextSelection>(e);
		if (!te || !ts) return;

		// Check for read-only
		if (te->readOnly) return;

		// Delete selection if any
		ts->DeleteFrom(te->text, te->cursor);

		// Check InputFilterType filtering for Input widgets
		auto *w = m_ctx.Get<Widget>(e);
		auto *id = m_ctx.Get<InputData>(e);
		std::string toInsert = text;

		if (w && w->type == WidgetType::Input && id) {
			// Apply InputFilterType filtering
			std::string filtered;
			for (char c : toInsert) {
				bool allowed = false;
				switch (id->filter) {
					case InputFilterType::Text:
						allowed = true;  // All printable chars allowed
						break;
					case InputFilterType::Email: {
						// Basic email characters: alphanumeric, @, ., -, _, +
						allowed = (c >= '0' && c <= '9') ||
								 (c >= 'a' && c <= 'z') ||
								 (c >= 'A' && c <= 'Z') ||
								 c == '@' || c == '.' || c == '-' || c == '_' || c == '+';
						break;
					}
					case InputFilterType::URL: {
						// Basic URL characters: alphanumeric, :, /, ., -, _, ~, ?, &, =, +
						allowed = (c >= '0' && c <= '9') ||
								 (c >= 'a' && c <= 'z') ||
								 (c >= 'A' && c <= 'Z') ||
								 c == ':' || c == '/' || c == '.' || c == '-' || c == '_' ||
								 c == '~' || c == '?' || c == '&' || c == '=' || c == '+';
						break;
					}
					case InputFilterType::Integer: {
						// Digits and optional leading minus
						allowed = (c >= '0' && c <= '9') ||
								 (c == '-' && te->cursor == 0);
						break;
					}
					case InputFilterType::Float: {
						// Digits, optional minus at start, and single decimal point
						allowed = (c >= '0' && c <= '9') ||
								 (c == '-' && te->cursor == 0) ||
								 (c == '.' && te->text.find('.') == std::string::npos);
						break;
					}
					case InputFilterType::Multiline: {
						// Allow all printable chars and newlines
						allowed = (c >= 32 && c < 127) || c == '\n' || c == '\r';
						break;
					}
					case InputFilterType::Phone: {
						allowed = (c >= '0' && c <= '9') || c == '+' || c == '-' || c == '(' || c == ')' || c == ' ';
						break;
					}
					case InputFilterType::Username: {
						allowed = (c >= '0' && c <= '9') ||
								 (c >= 'a' && c <= 'z') ||
								 (c >= 'A' && c <= 'Z') ||
								 c == '_' || c == '-';
						break;
					}
					case InputFilterType::Hex: {
						allowed = (c >= '0' && c <= '9') ||
								 (c >= 'a' && c <= 'f') ||
								 (c >= 'A' && c <= 'F');
						break;
					}
					case InputFilterType::Digits: {
						allowed = (c >= '0' && c <= '9');
						break;
					}
					case InputFilterType::Alpha: {
						allowed = (c >= 'a' && c <= 'z') ||
								  (c >= 'A' && c <= 'Z');
						break;
					}
					case InputFilterType::Alnum: {
						allowed = (c >= '0' && c <= '9') ||
								 (c >= 'a' && c <= 'z') ||
								 (c >= 'A' && c <= 'Z');
						break;
					}
					case InputFilterType::Slug: {
						allowed = (c >= '0' && c <= '9') ||
								 (c >= 'a' && c <= 'z') ||
								 (c >= 'A' && c <= 'Z') ||
								 c == '-' || c == '_';
						break;
					}
					case InputFilterType::Filename: {
						// Basic filename characters: alphanumeric, ., -, _, and space
						allowed = (c >= '0' && c <= '9') ||
								 (c >= 'a' && c <= 'z') ||
								 (c >= 'A' && c <= 'Z') ||
								 c == '.' || c == '-' || c == '_' || c == ' ';
						break;
					}
					case InputFilterType::Custom: {
						if (id->customFilter) {
							allowed = id->customFilter(c);
						} else {
							allowed = true;  // No filter function means allow all
						}
						break;
					}
					default:
						allowed = true;  // Unknown filter type - allow all
						break;
				}
				if (allowed && c >= 32 && c < 127) {  // Printable ASCII only
					filtered += c;
				}
			}
			toInsert = filtered;
		}

		// Insert filtered text at cursor position
		if (!toInsert.empty()) {
			te->text.insert(te->cursor, toInsert);
			te->cursor += (int)toInsert.length();

			// Mark layout dirty
			if (w) {
				w->dirty |= DirtyFlag::Layout | DirtyFlag::Render;
			}

			// Fire onChange callback
			auto *cb = m_ctx.Get<Callbacks>(e);
			if (cb && cb->onTextChange) {
				cb->onTextChange(te->text);
			}
		}
	}

	// Handler for ListBox item selection via keyboard
	inline void UIEventSystem::_HandleListBoxNavigation(ECS::EntityId e, SDL::Scancode scancode) {
		auto *ilv = m_ctx.Get<ItemListView>(e);
		if (!ilv || ilv->items.empty()) return;

		int prevIdx = ilv->selectedIndex;

		if (scancode == SDL_SCANCODE_UP) {
			// Move selection up
			if (ilv->selectedIndex <= 0) {
				ilv->selectedIndex = 0;
			} else {
				ilv->selectedIndex--;
			}
		}
		else if (scancode == SDL_SCANCODE_DOWN) {
			// Move selection down
			int maxIdx = (int)ilv->items.size() - 1;
			if (ilv->selectedIndex >= maxIdx) {
				ilv->selectedIndex = maxIdx;
			} else {
				ilv->selectedIndex++;
			}
		}
		else if (scancode == SDL_SCANCODE_RETURN || scancode == SDL_SCANCODE_SPACE) {
			// Activate current selection
			auto *cb = m_ctx.Get<Callbacks>(e);
			if (cb && cb->onClick) {
				cb->onClick(SDL::BUTTON_LEFT);
			}
			return;  // Don't fire onTreeSelect for activation
		}

		// Fire onTreeSelect if index changed
		if (ilv->selectedIndex != prevIdx) {
			auto *cb = m_ctx.Get<Callbacks>(e);
			if (cb && cb->onTreeSelect) {
				cb->onTreeSelect(ilv->selectedIndex, false);
			}
		}
	}

	inline void UIEventSystem::_CollectFocusable(ECS::EntityId e, std::vector<ECS::EntityId>& out) const {
		if (!m_ctx.IsAlive(e)) return;
		auto *w = m_ctx.Get<Widget>(e);
		if (!w || !Has(w->behavior, WidgetBehaviorFlag::Visible) || !Has(w->behavior, WidgetBehaviorFlag::Enable))
			return;
		if (Has(w->behavior, WidgetBehaviorFlag::Focusable))
			out.push_back(e);
		auto *ch = m_ctx.Get<Children>(e);
		if (ch) {
			for (ECS::EntityId cid : ch->ids)
				_CollectFocusable(cid, out);
		}
	}

	inline void UIEventSystem::_AdvanceFocus(ECS::EntityId root, bool forward) {
		ECS::EntityId treeRoot = (root != ECS::NullEntity) ? root : m_root;
		if (!m_ctx.IsAlive(treeRoot)) return;

		std::vector<ECS::EntityId> focusable;
		_CollectFocusable(treeRoot, focusable);
		if (focusable.empty()) return;

		if (!forward) std::reverse(focusable.begin(), focusable.end());

		// Find current position and advance
		auto it = std::find(focusable.begin(), focusable.end(), m_focused);
		ECS::EntityId next = ECS::NullEntity;
		if (it == focusable.end() || std::next(it) == focusable.end()) {
			next = focusable.front();
		} else {
			next = *std::next(it);
		}
		_SetFocus(next);
	}

} // namespace SDL::UI
