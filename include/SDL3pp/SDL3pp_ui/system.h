#pragma once
#include "theme.h"
#include "../SDL3pp_mouse.h"
#include "../SDL3pp_keyboard.h"
#include "../SDL3pp_resources.h"
#include "../SDL3pp_clipboard.h"
#include "../SDL3pp_log.h"
#include "../SDL3pp_mixer.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <concepts>
#include <cstdio>
#include <memory>
#include <numbers>
#include <regex>
#include <type_traits>
#include <unordered_map>

#if defined(SDL3PP_ENABLE_TTF)
#define UI_HAS_TTF 1
#else
#define UI_HAS_TTF 0
#endif
#if defined(SDL3PP_ENABLE_MIXER)
#define UI_HAS_MIXER 1
#else
#define UI_HAS_MIXER 0
#endif

namespace SDL {
namespace UI {

	/// @brief Concept matching arithmetic types (excluding @c bool) usable as
	///        the value type of a numeric Input widget.
	template<class T>
	concept is_numeric_value =
		std::is_arithmetic_v<T> && !std::is_same_v<T, bool>;

	// ==================================================================================
	// System
	// ==================================================================================

	class System {
	public: // TODO: Make this private, UIManager is the new public API
		/**
		 * Construct the UI system.
		 *
		 * @param w    ECS Context that owns all widget entities.
		 * @param r    Renderer used for all drawing.
		 * @param m    Mixer used for audio.
		 * @param pool ResourcePool that holds **all** UI assets — textures 
		 *             (`SDL::Texture`), fonts (`SDL::TTF::Font`), and sounds
		 *             (`SDL::Audio`).  The pool must outlive the System.
		 *
		 * ```cpp
		 * // In main: 
		 * SDL::ResourceManager rm;
		 * SDL::ResourcePool& uiPool = *rm.CreatePool("ui");
		 * uiPool.Add<SDL::Texture>("hero", SDL::LoadTexture(renderer, "hero.png"));
		 * SDL::UI::System ui(rm, renderer, uiPool);
		 * ```
		 */
		System(ECS::Context &ctx, RendererRef r, MixerRef m, ResourcePool &pool)
			: m_ctx(ctx), m_renderer(r), m_mixer(m), m_pool(pool)
		{
		}

		/**
		 * Destructor — must destroy all SDL::Text objects (TextCache ECS
		 * components) *before* the RendererTextEngine is destroyed.
		 *
		 * SDL_ttf requires every TTF_Text that was created with an engine to be
		 * destroyed before that engine is freed.  Because the engine is now owned
		 * by System (not the pool), we can guarantee the correct order here:
		 *   1. Iterate all TextCache ECS components and reset their SDL::Text.
		 *   2. m_engine optional<> is then destroyed at end of destructor.
		 */
		~System() {
#if UI_HAS_TTF
			// Step 1 — release all SDL::Text objects while the engine is still live.
			m_ctx.Each<TextCache>([](ECS::EntityId, TextCache &tc) {
				tc.text = SDL::Text{};  // calls TTF_DestroyText safely
			});
			// Step 2 — engine is destroyed when m_engine optional goes out of scope.
			m_engine.reset();
#endif
		}

		System(const System &) = delete;
		System &operator=(const System &) = delete;

		/// Direct access to the resource pool for runtime additions.
		[[nodiscard]] ResourcePool &GetPool() noexcept { return m_pool; }

		/**
		 * Set a font applied to every widget.
		 *
		 * Widgets that already set their own `fontKey`/`fontSize` keep them.
		 * Pass an empty path to clear the default.
		 *
		 * ```cpp 
		 * #if _HAS_TTF
		 *     ui.SetDefaultFont("assets/Roboto-Regular.ttf", 15.f);
		 * #endif
		 * ```
		 */
		void SetDefaultFont(const std::string &path, float ptsize) {
			m_usedDebugFontPerDefault = false;
			m_defaultFontPath = path;
			m_defaultFontSize = ptsize;
		}

		/** @brief Use default font applied to every widget created */
		void UseDebugFont(float ptsize) {
			m_usedDebugFontPerDefault = true;
			m_defaultFontSize = ptsize;
		}

		/** @brief Returns the path of the current default font (empty if none). */
		[[nodiscard]] const std::string &GetDefaultFontPath() const { return m_defaultFontPath; }
		
		/** @brief Returns the point size of the current default font (0 if unset). */
		[[nodiscard]] float GetDefaultFontSize() const { return m_defaultFontSize; }

		// ── Entity factories ──────────────────────────────────────────────────────────

		/** @brief Create a Container entity and return its ID. */
		ECS::EntityId MakeContainer(const std::string &n = "Container") { return _Make(n, WidgetType::Container); }

		/**
		 * @brief Create a Label entity with the given display text.
		 * @param n  Widget name (for debugging).
		 * @param t  Initial text content.
		 */
		ECS::EntityId MakeLabel(const std::string &n, const std::string &t = "") {
			ECS::EntityId e = _Make(n, WidgetType::Label);
			m_ctx.Get<EditableContent>(e)->text = t;
			auto &l = *m_ctx.Get<LayoutProps>(e);
			l.padding.top = l.padding.bottom = 2.f;
			return e;
		}
		/**
		 * @brief Create a Button entity with the given label text.
		 * @param n  Widget name.
		 * @param t  Button label text.
		 */
		ECS::EntityId MakeButton(const std::string &n, const std::string &t = "") {
			ECS::EntityId e = _Make(n, WidgetType::Button);
			m_ctx.Get<Widget>(e)->behavior |= BehaviorFlag::Hoverable | BehaviorFlag::Selectable | BehaviorFlag::Focusable;
			m_ctx.Get<EditableContent>(e)->text = t;
			return e;
		}
		/**
		 * @brief Create a Toggle (on/off switch) entity.
		 * @param n  Widget name.
		 * @param t  Label text displayed beside the switch.
		 */
		ECS::EntityId MakeToggle(const std::string &n, const std::string &t = "") {
			ECS::EntityId e = _Make(n, WidgetType::Toggle);
			m_ctx.Get<EditableContent>(e)->text = t;
			m_ctx.Get<Widget>(e)->behavior |= BehaviorFlag::Hoverable | BehaviorFlag::Selectable | BehaviorFlag::Focusable;
			m_ctx.Get<LayoutProps>(e)->height = Value::Px(28.f);
			m_ctx.Add<ToggleData>(e);
			return e;
		}
		/**
		 * @brief Create a RadioButton entity belonging to the given group.
		 * @param n      Widget name.
		 * @param group  Named group; only one radio button per group can be checked.
		 * @param t      Label text displayed beside the button.
		 */
		ECS::EntityId MakeRadioButton(const std::string &n, const std::string &group, const std::string &t = "") {
			ECS::EntityId e = _Make(n, WidgetType::RadioButton);
			m_ctx.Get<EditableContent>(e)->text = t;
			m_ctx.Get<Widget>(e)->behavior |= BehaviorFlag::Hoverable | BehaviorFlag::Selectable | BehaviorFlag::Focusable;
			m_ctx.Get<LayoutProps>(e)->height = Value::Px(24.f);
			m_ctx.Add<RadioData>(e, {group, false});
			return e;
		}
		/**
		 * @brief Create a Slider entity.
		 * @param n   Widget name.
		 * @param mn  Minimum value.
		 * @param mx  Maximum value.
		 * @param v   Initial value (clamped to [mn, mx]).
		 * @param o   Orientation (Horizontal or Vertical).
		 */
		template<is_numeric_value T>
		ECS::EntityId MakeSlider(const std::string &n,
		                         T mn = T(0), T mx = T(1), T v = T(0), T step = T(0),
		                         Orientation o = Orientation::Horizontal) {
			ECS::EntityId e = _Make(n, WidgetType::Slider);
			SliderData sd;
			sd.orientation = o;
			sd.min  = static_cast<double>(mn);
			sd.max  = static_cast<double>(mx);
			sd.val  = std::clamp(static_cast<double>(v), sd.min, sd.max);
			sd.step = static_cast<double>(step);
			sd.type = typeid(T);
			m_ctx.Add<SliderData>(e, sd);

			m_ctx.Get<Widget>(e)->behavior |= BehaviorFlag::Hoverable
			                                | BehaviorFlag::Selectable
			                                | BehaviorFlag::Focusable;
			auto &lp = *m_ctx.Get<LayoutProps>(e);
			if (o == Orientation::Horizontal) lp.height = Value::Px(24.f);
			else                              lp.width  = Value::Px(24.f);
			return e;
		}
		/**
		 * @brief Create a ScrollBar entity.
		 * @param n   Widget name.
		 * @param cs  Total content size.
		 * @param vs  Visible viewport size.
		 * @param o   Orientation (Vertical by default).
		 */
		ECS::EntityId MakeScrollBar(const std::string &n, float cs = 0.f, float vs = 0.f,
							   Orientation o = Orientation::Vertical) {
			ECS::EntityId e = _Make(n, WidgetType::ScrollBar);
			ScrollBarData sd;
			sd.contentSize = cs;
			sd.viewSize = vs;
			sd.orientation = o;
			m_ctx.Add<ScrollBarData>(e, sd);
			m_ctx.Get<Widget>(e)->behavior |= BehaviorFlag::Hoverable | BehaviorFlag::Selectable | BehaviorFlag::Focusable;
			auto &lp = *m_ctx.Get<LayoutProps>(e);
			if (o == Orientation::Vertical)
				lp.width = Value::Px(10.f);
			else
				lp.height = Value::Px(10.f);
			return e;
		}
		/**
		 * @brief Create a Progress bar entity.
		 * @param n   Widget name.
		 * @param v   Initial value (clamped to [0, mx]).
		 * @param mx  Maximum value.
		 */
		ECS::EntityId MakeProgress(const std::string &n, float v = 0.f, float mx = 1.f) {
			ECS::EntityId e = _Make(n, WidgetType::Progress);
			m_ctx.Add<SliderData>(e, SliderData{.min=0.f, .max=mx, .val=SDL::Clamp(v, 0.f, mx), .markers={}});
			m_ctx.Get<LayoutProps>(e)->height = Value::Px(18.f);
			return e;
		}
		/** @brief Create a 1-pixel horizontal separator entity. */
		ECS::EntityId MakeSeparator(const std::string &n = "sep") {
			ECS::EntityId e = _Make(n, WidgetType::Separator);
			auto &lp = *m_ctx.Get<LayoutProps>(e);
			lp.height = Value::Px(1.f);
			lp.margin.top = lp.margin.bottom = 6.f;
			return e;
		}
		/**
		 * @brief Create a single-line text Input entity.
		 * @param n   Widget name.
		 * @param ph  Placeholder text shown when empty and unfocused.
		 */
		ECS::EntityId MakeInput(const std::string &n, const std::string &ph = "") {
			ECS::EntityId e = _Make(n, WidgetType::Input);
			m_ctx.Get<EditableContent>(e)->placeholder = ph;
			m_ctx.Get<Widget>(e)->behavior |= BehaviorFlag::Hoverable | BehaviorFlag::Selectable | BehaviorFlag::Focusable;
			m_ctx.Get<LayoutProps>(e)->height = Value::Px(30.f);
			return e;
		}
		/// @brief Create a numeric Input entity (replaces InputValue).
		///        @c T may be any integral or floating-point type.
		///        Selects @ref InputType::IntegerValue for integers and
		///        @ref InputType::FloatValue for floating-point.
		template<is_numeric_value T>
		ECS::EntityId MakeInputValue(const std::string &n,
		                             T minValue = T(0), T maxValue = T(100),
		                             T value    = T(0), T step     = T(1))
		{
			ECS::EntityId e = _Make(n, WidgetType::Input);
			m_ctx.Get<Widget>(e)->behavior |= BehaviorFlag::Hoverable
			                                | BehaviorFlag::Selectable
			                                | BehaviorFlag::Focusable;
			m_ctx.Get<LayoutProps>(e)->height = Value::Px(30.f);

			InputData d{};
			d.type = std::is_integral_v<T> ? InputType::IntegerValue
			                               : InputType::FloatValue;
			m_ctx.Add<InputData>(e, d);

			// Typed numeric value via the AnyValue<T> wrapper; the data is
			// stored as NumericValue (uniform double + type_index) so widgets
			// can dispatch on type without per-T template instantiation.
			NumericValue &nv = m_ctx.Add<NumericValue>(e, AnyValue<T>(minValue, maxValue, value, step));
			m_ctx.Get<EditableContent>(e)->text = _FormatNumeric(nv);
			return e;
		}

		/// @brief Create a text Input restricted to the given @ref InputType
		///        (Mail or Url filtering, etc.). Numeric types should use
		///        @ref MakeInputValue instead.
		ECS::EntityId MakeInputFiltered(const std::string &n, InputType type,
		                                const std::string &ph = "")
		{
			ECS::EntityId e = MakeInput(n, ph);
			InputData d{};
			d.type = type;
			m_ctx.Add<InputData>(e, d);
			return e;
		}
		/**
		 * @brief Create a circular Knob entity.
		 * @param n   Widget name.
		 * @param mn  Minimum value.
		 * @param mx  Maximum value.
		 * @param v   Initial value (clamped to [mn, mx]).
		 */
		ECS::EntityId MakeKnob(const std::string &n, float mn = 0.f, float mx = 1.f, float v = 0.5f, KnobShape shape = KnobShape::Arc) {
			ECS::EntityId e = _Make(n, WidgetType::Knob);
			
			// Initialisation explicite de tous les champs pour éviter le garbage memory
			KnobData kd;
			kd.min = mn;
			kd.max = mx;
			kd.val = SDL::Clamp(v, mn, mx);
			kd.drag = false;
			kd.dragStartY = 0.f;
			kd.dragStartVal = 0.f;
			kd.shape = shape;
			m_ctx.Add<KnobData>(e, kd);
			
			auto *w = m_ctx.Get<Widget>(e);
			if (w) w->behavior |= BehaviorFlag::Hoverable | BehaviorFlag::Selectable | BehaviorFlag::Focusable;

			auto &lp = *m_ctx.Get<LayoutProps>(e);
			lp.width = lp.height = Value::Px(56.f); // Taille par défaut sécurisée
			return e;
		}
		/**
		 * @brief Create an Image entity.
		 * @param n    Widget name.
		 * @param key  Resource-pool key of the texture to display.
		 * @param fit  How the image is scaled to fit the widget rect.
		 */
		ECS::EntityId MakeImage(const std::string &n, const std::string &key = "", ImageFit fit = ImageFit::Contain) {
			ECS::EntityId e = _Make(n, WidgetType::Image);
			m_ctx.Add<ImageData>(e, {key, fit});
			return e;
		}
		/**
		 * @brief Create a Canvas entity with custom callbacks.
		 * @param n         Widget name.
		 * @param cb_event  Called by `ProcessEvent` when the canvas is hit or focused.
		 * @param cb_update Called every frame before layout with the frame delta time.
		 * @param cb_render Called during the render pass with the renderer and screen rect.
		 */
		ECS::EntityId MakeCanvas(const std::string &n,
			std::function<void(SDL::Event&)> cb_event = nullptr,
			std::function<void(float)> cb_update = nullptr,
			std::function<void(RendererRef, FRect)> cb_render = nullptr) {
			ECS::EntityId e = _Make(n, WidgetType::Canvas);
			m_ctx.Get<Widget>(e)->behavior |= BehaviorFlag::Hoverable | BehaviorFlag::Selectable | BehaviorFlag::Focusable;
			m_ctx.Add<CanvasData>(e, {std::move(cb_event), std::move(cb_update), std::move(cb_render)});
			return e; 
		}
		/**
		 * @brief Create a ListBox entity pre-populated with items.
		 * @param n      Widget name.
		 * @param items  Initial list of text items.
		 */
		ECS::EntityId MakeListBox(const std::string &n,
								  const std::vector<std::string>& items = {}) {
			ECS::EntityId e = _Make(n, WidgetType::ListBox);
			m_ctx.Get<Widget>(e)->behavior |= BehaviorFlag::Hoverable
											  | BehaviorFlag::Selectable
											  | BehaviorFlag::Focusable
											  | BehaviorFlag::AutoScrollableY
											  | BehaviorFlag::AutoScrollableX;
			auto &lb = m_ctx.Add<ListBoxData>(e);
			lb.items = items;
			m_ctx.Get<LayoutProps>(e)->padding = {2.f, 2.f, 2.f, 2.f};
			return e;
		}
		/** @brief Create an empty Graph (graduated data plot) entity. */
		ECS::EntityId MakeGraph(const std::string &n) {
			ECS::EntityId e = _Make(n, WidgetType::Graph);
			m_ctx.Add<GraphData>(e);
			m_ctx.Get<LayoutProps>(e)->padding = {0.f, 0.f, 0.f, 0.f};
			return e;
		}
		/**
		 * @brief Create a multi-line TextArea entity.
		 * @param n     Widget name.
		 * @param text  Initial document content.
		 * @param ph    Placeholder text shown when the document is empty and unfocused.
		 */
		ECS::EntityId MakeTextArea(const std::string &n, const std::string &text = "", const std::string &ph = "") {
			ECS::EntityId e = _Make(n, WidgetType::TextArea);
			m_ctx.Get<Widget>(e)->behavior |= BehaviorFlag::Hoverable | BehaviorFlag::Selectable | BehaviorFlag::Focusable;
			auto &ta = m_ctx.Add<TextAreaData>(e);
			ta.text = text;
			auto &lp = *m_ctx.Get<LayoutProps>(e);
			lp.padding = {6.f, 6.f, 6.f, 6.f};
			if (!ph.empty()) {
				// Store placeholder in EditableContent component
				m_ctx.Get<EditableContent>(e)->placeholder = ph;
			}
			return e;
		}

		/** @brief Create a ComboBox entity with a list of items. */
		ECS::EntityId MakeComboBox(const std::string &n, const std::vector<std::string>& items = {}, int sel = 0) {
			ECS::EntityId e = _Make(n, WidgetType::ComboBox);
			auto &d = m_ctx.Add<ComboBoxData>(e);
			d.items         = items;
			d.selectedIndex = items.empty() ? -1 : SDL::Clamp(sel, 0, (int)items.size() - 1);
			return e;
		}
		/** @brief Create a TabView entity. */
		ECS::EntityId MakeTabView(const std::string &n) {
			ECS::EntityId e = _Make(n, WidgetType::TabView);
			m_ctx.Add<TabViewData>(e);
			return e;
		}
		/** @brief Create an Expander entity with a header label. */
		ECS::EntityId MakeExpander(const std::string &n, const std::string &label = "", bool expanded = false) {
			ECS::EntityId e = _Make(n, WidgetType::Expander);
			auto &d = m_ctx.Add<ExpanderData>(e);
			d.label    = label;
			d.expanded = expanded;
			d.animT    = expanded ? 1.f : 0.f;
			return e;
		}
		/** @brief Create a Splitter entity (horizontal or vertical). */
		ECS::EntityId MakeSplitter(const std::string &n, Orientation o = Orientation::Horizontal, float ratio = 0.5f) {
			ECS::EntityId e = _Make(n, WidgetType::Splitter);
			auto &d = m_ctx.Add<SplitterData>(e);
			d.orientation = o; d.ratio = SDL::Clamp(ratio, d.minRatio, d.maxRatio);
			return e;
		}
		/** @brief Create an animated Spinner entity. */
		ECS::EntityId MakeSpinner(const std::string &n, float speed = 3.f) {
			ECS::EntityId e = _Make(n, WidgetType::Spinner);
			auto &d = m_ctx.Add<SpinnerData>(e);
			d.speed = speed;
			return e;
		}
		/** @brief Create a Badge entity displaying a short text count. */
		ECS::EntityId MakeBadge(const std::string &n, const std::string &text = "0") {
			ECS::EntityId e = _Make(n, WidgetType::Badge);
			auto &d = m_ctx.Add<BadgeData>(e);
			d.text = text;
			return e;
		}
		/** @brief Create a ColorPicker entity.
		 *  @param palette  Palette type (Grayscale, RGB8, RGBFloat, GradientAB).
		 *  @param step     Quantization step used in RGBFloat mode (default 1/255). */
		ECS::EntityId MakeColorPicker(const std::string &n,
		                              ColorPickerPalette palette = ColorPickerPalette::RGB8,
		                              float step = 1.f / 255.f) {
			ECS::EntityId e = _Make(n, WidgetType::ColorPicker);
			auto &d = m_ctx.Add<ColorPickerData>(e);
			d.palette       = palette;
			d.precisionStep = step;
			return e;
		}
		/** @brief Create a Popup (floating window) entity.
		 *
		 *  Position it via builder: `.Fixed(x,y).W(w).H(h)`.
		 *  Children added via `.Child(...)` or `.Children(...)` appear in the content area.
		 */
		ECS::EntityId MakePopup(const std::string &n,
		                        const std::string &title = "",
		                        bool closable  = true,
		                        bool draggable = true,
		                        bool resizable = false) {
			ECS::EntityId e = _Make(n, WidgetType::Popup);
			auto &d  = m_ctx.Add<PopupData>(e);
			d.title    = title;
			d.closable = closable;
			d.draggable= draggable;
			d.resizable= resizable;
			// Reserve top padding for the header bar
			if (auto *lp = m_ctx.Get<LayoutProps>(e)) {
				lp->attach  = AttachLayout::Fixed;
				lp->padding = {4.f, 4.f, 4.f, d.headerH + 4.f};
			}
			return e;
		}
		/** @brief Create a Tree widget entity.
		 *
		 *  Populate it with nodes via `GetTreeData(e)->nodes.push_back(...)` or the
		 *  builder helpers `.TreeNode(...)`. */
		ECS::EntityId MakeTree(const std::string &n) {
			ECS::EntityId e = _Make(n, WidgetType::Tree);
			m_ctx.Add<TreeData>(e);
			auto &lp = *m_ctx.Get<LayoutProps>(e);
			lp.padding = {2.f, 2.f, 2.f, 2.f};
			return e;
		}

		/** @brief Create a MenuBar entity.  Populate it with menus via GetMenuBarData()->menus. */
		ECS::EntityId MakeMenuBar(const std::string &n = "menubar") {
			ECS::EntityId e = _Make(n, WidgetType::MenuBar);
			m_ctx.Add<MenuBarData>(e);
			auto &lp = *m_ctx.Get<LayoutProps>(e);
			lp.height = Value::Px(26.f);
			lp.padding = {0.f, 0.f, 0.f, 0.f};
			return e;
		}

		// ── MenuBar accessors ─────────────────────────────────────────────────────────

		/** @brief Returns the MenuBarData of entity @p e, or nullptr. */
		MenuBarData* GetMenuBarData(ECS::EntityId e) { return m_ctx.Get<MenuBarData>(e); }
		/** @brief Close the currently-open dropdown of a MenuBar, if any. */
		void CloseMenuBar(ECS::EntityId e) {
			if (auto *d = m_ctx.Get<MenuBarData>(e)) {
				d->openMenu = -1;
				d->hovItem  = -1;
			}
			if (m_menuBarOpen == e) m_menuBarOpen = ECS::NullEntity;
		}

		// ── ColorPicker accessors ─────────────────────────────────────────────────────

		/** @brief Returns the ColorPickerData of entity @p e, or nullptr. */
		ColorPickerData* GetColorPickerData(ECS::EntityId e) { return m_ctx.Get<ColorPickerData>(e); }
		/** @brief Returns the current color selected by a ColorPicker widget. */
		[[nodiscard]] SDL::Color GetPickedColor(ECS::EntityId e) const {
			if (auto *d = m_ctx.Get<ColorPickerData>(e)) return d->currentColor;
			return {};
		}
		/** @brief Set the current color of a ColorPicker widget (updates internal HSV state). */
		void SetPickedColor(ECS::EntityId e, SDL::Color c) {
			if (auto *d = m_ctx.Get<ColorPickerData>(e)) {
				d->currentColor = c;
				_RgbToHsv(c, d->hue, d->sat, d->val);
			}
		}

		// ── Popup accessors ───────────────────────────────────────────────────────────

		/** @brief Returns the PopupData of entity @p e, or nullptr. */
		PopupData* GetPopupData(ECS::EntityId e) { return m_ctx.Get<PopupData>(e); }
		/** @brief Open or close a Popup widget. Closing also hides the widget. */
		void SetPopupOpen(ECS::EntityId e, bool open) {
			if (auto *d = m_ctx.Get<PopupData>(e)) {
				d->open = open;
				SetVisible(e, open);
			}
		}
		/** @brief Set the title of a Popup widget. */
		void SetPopupTitle(ECS::EntityId e, const std::string &title) {
			if (auto *d = m_ctx.Get<PopupData>(e)) d->title = title;
		}
		/** @brief Add a custom icon button to the Popup header. */
		void AddPopupHeaderButton(ECS::EntityId e, const std::string &iconKey, std::function<void()> cb) {
			if (auto *d = m_ctx.Get<PopupData>(e)) d->headerButtons.push_back({iconKey, std::move(cb)});
		}

		// ── Tree accessors ────────────────────────────────────────────────────────────

		/** @brief Returns the TreeData of entity @p e, or nullptr. */
		TreeData* GetTreeData(ECS::EntityId e) { return m_ctx.Get<TreeData>(e); }
		/** @brief Append a node to a Tree widget. */
		void AddTreeNode(ECS::EntityId e, const TreeNodeData &node) {
			if (auto *d = m_ctx.Get<TreeData>(e)) d->nodes.push_back(node);
		}
		/** @brief Clear all nodes from a Tree widget. */
		void ClearTreeNodes(ECS::EntityId e) {
			if (auto *d = m_ctx.Get<TreeData>(e)) { d->nodes.clear(); d->selectedIndex = -1; }
		}
		/** @brief Returns the currently selected node index in a Tree, or -1 if none. */
		[[nodiscard]] int GetTreeSelection(ECS::EntityId e) const {
			if (auto *d = m_ctx.Get<TreeData>(e)) return d->selectedIndex;
			return -1;
		}

		/** @brief Force a full layout recompute on the next frame (e.g. after inserting tree nodes). */
		void MarkLayoutDirty() { m_layoutDirty = true; }

		// ── BgGradient accessors ──────────────────────────────────────────────────────

		/** @brief Attach or replace the BgGradient component on entity @p e. */
		void SetBgGradient(ECS::EntityId e, BgGradient grad) {
			auto *g = m_ctx.Get<BgGradient>(e);
			if (g) *g = std::move(grad);
			else m_ctx.Add<BgGradient>(e, std::move(grad));
		}
		/** @brief Remove the BgGradient component from entity @p e (reverts to solid color). */
		void RemoveBgGradient(ECS::EntityId e) { m_ctx.Remove<BgGradient>(e); }

		// ── Builder factories (defined after Builder) ─────────────────────────────────

		/** @brief Create a Container and return a Builder for it. */
		inline Builder Container(const std::string &n = "Container");
		/** @brief Create a Label and return a Builder for it. */
		inline Builder Label(const std::string &n, const std::string &t = "");
		/** @brief Create a Button and return a Builder for it. */
		inline Builder Button(const std::string &n, const std::string &t = "");
		/** @brief Create a Toggle switch and return a Builder for it. */
		inline Builder Toggle(const std::string &n, const std::string &t = "");
		/** @brief Create a RadioButton and return a Builder for it. */
		inline Builder Radio(const std::string &n, const std::string &grp, const std::string &t = "");
		/** @brief Create a Slider and return a Builder for it.
		 *  Templated on the arithmetic value type @c T (int / float / double …). */
		template <is_numeric_value T = float>
		inline Builder Slider(const std::string &n, T mn = T(0), T mx = T(1), T v = T(0),
		                      Orientation o = Orientation::Horizontal);
		/** @brief Create a ScrollBar and return a Builder for it. */
		inline Builder ScrollBar(const std::string &n, float cs = 0.f, float vs = 0.f,
								   Orientation o = Orientation::Vertical);
		/** @brief Create a Progress bar and return a Builder for it. */
		inline Builder Progress(const std::string &n, float v = 0.f, float mx = 1.f);
		/** @brief Create a Separator and return a Builder for it. */
		inline Builder Separator(const std::string &n = "sep");
		/** @brief Create a single-line Input and return a Builder for it. */
		inline Builder Input(const std::string &n, const std::string &ph = "");
		/** @brief Create a Knob and return a Builder for it. */
		inline Builder Knob(const std::string &n, float mn = 0.f, float mx = 1.f, float v = 0.5f, KnobShape shape = KnobShape::Arc);
		/** @brief Create an Image widget and return a Builder for it. */
		inline Builder ImageWidget(const std::string &n, const std::string &p = "", ImageFit f = ImageFit::Contain);
		/** @brief Create a Canvas widget and return a Builder for it. */
		inline Builder CanvasWidget(const std::string &n,
			std::function<void(SDL::Event&)> cb_event = nullptr,
			std::function<void(float)> cb_update = nullptr,
			std::function<void(RendererRef, FRect)> cb_render = nullptr);
		/** @brief Create a multi-line TextArea and return a Builder for it. */
		inline Builder TextArea(const std::string &n, const std::string &text = "", const std::string &ph = "");
		/** @brief Create a ListBox and return a Builder for it. */
		inline Builder ListBoxWidget(const std::string &n, const std::vector<std::string>& items = {});
		/** @brief Create a Graph (graduated data plot) and return a Builder for it. */
		inline Builder GradedGraph(const std::string &n);
		/** @brief Create a ComboBox and return a Builder for it. */
		inline Builder ComboBox(const std::string &n, const std::vector<std::string>& items = {}, int sel = 0);
		/** @brief Create a numeric Input (replaces SpinBox). Returns a Builder. */
		template <is_numeric_value T>
		Builder InputValue(const std::string &n,
		                   T minValue = T(0), T maxValue = T(100),
		                   T value    = T(0), T step     = T(1));
		/** @brief Create a TabView and return a Builder for it. */
		inline Builder TabView(const std::string &n);
		/** @brief Create an Expander and return a Builder for it. */
		inline Builder Expander(const std::string &n, const std::string &label = "", bool expanded = false);
		/** @brief Create a Splitter and return a Builder for it. */
		inline Builder Splitter(const std::string &n, Orientation o = Orientation::Horizontal, float ratio = 0.5f);
		/** @brief Create a Spinner and return a Builder for it. */
		inline Builder Spinner(const std::string &n, float speed = 3.f);
		/** @brief Create a Badge and return a Builder for it. */
		inline Builder Badge(const std::string &n, const std::string &text = "0");
		/** @brief Create a ColorPicker and return a Builder for it. */
		inline Builder ColorPicker(const std::string &n,
		                           ColorPickerPalette palette = ColorPickerPalette::RGB8,
		                           float step = 1.f / 255.f);
		/** @brief Create a Popup window and return a Builder for it. */
		inline Builder Popup(const std::string &n, const std::string &title = "",
		                     bool closable = true, bool draggable = true, bool resizable = false);
		/** @brief Create a Tree widget and return a Builder for it. */
		inline Builder Tree(const std::string &n);
		/** @brief Create a MenuBar widget and return a Builder for it. */
		inline Builder MenuBar(const std::string &n = "menubar");
		
		/** @brief Create a vertical Column container (InColumn layout) and return a Builder for it. */
		inline Builder Column(const std::string &n = "col", float gap = 4.f, float pad = 8.f, float marg = 0.f);
		/** @brief Create a horizontal Row container (InLine layout) and return a Builder for it. */
		inline Builder Row(const std::string &n = "row", float gap = 8.f, float pad = 0.f, float marg = 0.f);
		/** @brief Create a Card container (Column with card styling) and return a Builder for it. */
		inline Builder Card(const std::string &n = "card", float gap = 8.f, float marg = 0.f);
		/** @brief Create a wrapping Stack container and return a Builder for it. */
		inline Builder Stack(const std::string &n = "stack", float gap = 0.f, float pad = 0.f, float marg = 0.f);
		/** @brief Create an accent-colored section title Label and return a Builder for it. */
		inline Builder SectionTitle(const std::string &text, SDL::Color color = {70, 130, 210, 255});
		/** @brief Create a vertical ScrollView (Column with auto vertical scrollbar) and return a Builder for it. */
		inline Builder ScrollView(const std::string &n, float gap = 4.f);
		/// Grid container: children placed on a `columns × auto-rows` grid.
		/// Use `.GridCols(n)`, `.GridRows(n)`, `.GridColSizing(...)` etc. on the returned Builder
		/// to customise, and `.Cell(col, row)` on each child to set its grid position.
		inline Builder Grid(const std::string &n = "grid", int columns = 2, float gap = 4.f, float pad = 8.f);

		// ── Tree management ───────────────────────────────────────────────────────────

		/** @brief Set the root widget entity; it is laid out to fill the full viewport. */
		void SetRoot(ECS::EntityId e) { m_root = e; }
		/** @brief Returns the current root entity (ECS::NullEntity if none). */
		[[nodiscard]] ECS::EntityId GetRootId() const { return m_root; }

		/**
		 * @brief Append child @p c to parent @p p.
		 * @param p  Parent entity (must be alive).
		 * @param c  Child entity (must be alive).
		 */
		void AppendChild(ECS::EntityId p, ECS::EntityId c) {
			if (!m_ctx.IsAlive(p) || !m_ctx.IsAlive(c)) 
				return;
			m_ctx.Get<Children>(p)->Add(c);
			m_ctx.Get<Parent>(c)->id = p;
		}
		/**
		 * @brief Remove child @p c from parent @p p.
		 * @param p  Parent entity.
		 * @param c  Child entity to detach.
		 */
		void RemoveChild(ECS::EntityId p, ECS::EntityId c) {
			if (!m_ctx.IsAlive(p))
				return;
			m_ctx.Get<Children>(p)->Remove(c);
			if (m_ctx.IsAlive(c))
				m_ctx.Get<Parent>(c)->id = ECS::NullEntity;
		}

		/** @brief Return a Builder wrapping an existing entity (for post-creation styling). */
		inline Builder GetBuilder(ECS::EntityId e);

		// ── Component accessors ───────────────────────────────────────────────────────

		/** @brief Direct access to the Style component of entity @p e. */
		Style &GetStyle(ECS::EntityId e) { return *m_ctx.Get<Style>(e); }
		/** @brief Direct access to the LayoutProps component of entity @p e. */
		LayoutProps &GetLayout(ECS::EntityId e) { return *m_ctx.Get<LayoutProps>(e); }
		/** @brief Direct access to the EditableContent component of entity @p e. */
		EditableContent &GetContent(ECS::EntityId e) { return *m_ctx.Get<EditableContent>(e); }

		// ── Setters ───────────────────────────────────────────────────────────────────

		/**
		 * @brief Set the display text of a Label, Button, or Input widget.
		 * @param e  Target entity.
		 * @param t  New text content; cursor is moved to the end.
		 */
		void SetText(ECS::EntityId e, const std::string &t) {
			if (auto *c = m_ctx.Get<EditableContent>(e)) {
				c->text = t;
				c->cursor = (int)t.size();
			}
		}

		/**
		 * @brief Set the numeric value of a Slider or Knob widget (clamped to [min, max]).
		 * @param e  Target entity.
		 * @param v  New value.
		 */
		template <typename T = float>
		void SetValue(ECS::EntityId e, T v) {
			const double dv = static_cast<double>(v);
			if (auto *s = m_ctx.Get<SliderData>(e))
				s->val = std::clamp(dv, s->min, s->max);
			if (auto *k = m_ctx.Get<KnobData>(e))
				k->val = SDL::Clamp(static_cast<float>(dv), k->min, k->max);
			if (auto *nv = m_ctx.Get<NumericValue>(e)) {
				nv->val = std::clamp(dv, nv->min, nv->max);
				if (nv->isIntegral()) nv->val = std::round(nv->val);
				if (auto *c = m_ctx.Get<EditableContent>(e); c && m_focused != e)
					c->text = _FormatNumeric(*nv);
			}
		}

		/// Set normalized marker positions [0,1] on a Slider (e.g. chapter positions).
		void SetSliderMarkers(ECS::EntityId e, std::vector<float> markers) {
			if (auto *sd = m_ctx.Get<SliderData>(e))
				sd->markers = std::move(markers);
		}

		// ── ListBox accessors ─────────────────────────────────────────────────────────

		/**
		 * @brief Replace the item list of a ListBox widget.
		 *
		 * Resets the scroll offset and clears the selection if it is out of range.
		 * @param e      Target entity.
		 * @param items  New item list.
		 */
		void SetListBoxItems(ECS::EntityId e, std::vector<std::string> items) {
			auto *lb = m_ctx.Get<ListBoxData>(e);
			auto *lp = m_ctx.Get<LayoutProps>(e);
			if (lb && lp) {
				lb->items = std::move(items);
				lp->scrollY = 0.f;
				if (lb->selectedIndex >= (int)lb->items.size())
					lb->selectedIndex = -1;
			}
		}
		/**
		 * @brief Return the index of the selected ListBox item, or -1 if none.
		 * @param e  Target entity.
		 */
		[[nodiscard]] int GetListBoxSelection(ECS::EntityId e) const {
			if (auto *lb = m_ctx.Get<ListBoxData>(e)) return lb->selectedIndex;
			return -1;
		}
		/**
		 * @brief Programmatically select a ListBox item.
		 * @param e    Target entity.
		 * @param idx  Item index to select, or -1 to clear the selection.
		 */
		void SetListBoxSelection(ECS::EntityId e, int idx) {
			if (auto *lb = m_ctx.Get<ListBoxData>(e))
				lb->selectedIndex = (idx >= 0 && idx < (int)lb->items.size()) ? idx : -1;
		}
		/** @brief Return a pointer to the ListBoxData component of @p e, or nullptr. */
		ListBoxData* GetListBoxData(ECS::EntityId e) { return m_ctx.Get<ListBoxData>(e); }

		/** @brief Enable or disable drag-to-reorder on a ListBox. */
		void SetListBoxReorderable(ECS::EntityId e, bool v) {
			if (auto *lb = m_ctx.Get<ListBoxData>(e)) lb->reorderable = v;
		}
		/** @brief Set the reorder callback (called with fromIdx, toIdx after a drag-reorder). */
		void SetListBoxOnReorder(ECS::EntityId e, std::function<void(int,int)> cb) {
			if (auto *lb = m_ctx.Get<ListBoxData>(e)) lb->onReorder = std::move(cb);
		}

		// ── Tooltip accessors ─────────────────────────────────────────────────────────

		/// Attache (ou met à jour) un tooltip sur le widget @p e.
		void SetTooltip(ECS::EntityId e, const std::string &text, float delay = 1.f) {
			auto *td = m_ctx.Get<TooltipData>(e);
			if (!td) td = &m_ctx.Add<TooltipData>(e);
			td->text  = text;
			td->delay = delay;
		}
		/// Supprime le tooltip du widget @p e.
		void RemoveTooltip(ECS::EntityId e) { m_ctx.Remove<TooltipData>(e); }

		// ── Graph accessors ───────────────────────────────────────────────────────────

		/**
		 * @brief Replace the Y-data samples of a Graph widget.
		 * @param e     Target entity.
		 * @param data  New Y-values to plot (one per X sample).
		 */
		void SetGraphData(ECS::EntityId e, std::vector<float> data) {
			if (auto *gd = m_ctx.Get<GraphData>(e)) gd->data = std::move(data);
		}
		/**
		 * @brief Set the Y-axis display range of a Graph widget.
		 * @param e     Target entity.
		 * @param minV  Y axis minimum.
		 * @param maxV  Y axis maximum.
		 */
		void SetGraphRange(ECS::EntityId e, float minV, float maxV) {
			if (auto *gd = m_ctx.Get<GraphData>(e)) { gd->minVal = minV; gd->maxVal = maxV; }
		}
		/**
		 * @brief Set the X-axis display range of a Graph widget (used for tick labels).
		 * @param e     Target entity.
		 * @param xMin  X axis start value.
		 * @param xMax  X axis end value.
		 */
		void SetGraphXRange(ECS::EntityId e, float xMin, float xMax) {
			if (auto *gd = m_ctx.Get<GraphData>(e)) { gd->xMin = xMin; gd->xMax = xMax; }
		}
		/** @brief Return a pointer to the GraphData component of @p e, or nullptr. */
		GraphData* GetGraphData(ECS::EntityId e) { return m_ctx.Get<GraphData>(e); }

		/**
		 * @brief Programmatically set the scroll offset of a ScrollBar widget.
		 * @param e    Target entity.
		 * @param off  New offset (clamped to [0, contentSize - viewSize]).
		 */
		void SetScrollOffset(ECS::EntityId e, float off) {
			if (auto *sb = m_ctx.Get<ScrollBarData>(e)) {
				float mx = SDL::Max(0.f, sb->contentSize - sb->viewSize);
				sb->offset = SDL::Clamp(off, 0.f, mx);
			}
		}

		/**
		 * @brief Set the checked state of a Toggle or RadioButton widget.
		 * @param e  Target entity.
		 * @param b  New checked state.
		 */
		void SetChecked(ECS::EntityId e, bool b) {
			if (auto *t = m_ctx.Get<ToggleData>(e)) {
				t->checked = b;
				t->animT = b ? 1.f : 0.f;
			}
			if (auto *r = m_ctx.Get<RadioData>(e))
				r->checked = b;
		}

		/**
		 * @brief Enable or disable a widget (disabled widgets are not interactive but remain visible).
		 * @param e  Target entity.
		 * @param b  True to enable, false to disable.
		 */
		void SetEnabled(ECS::EntityId e, bool b) {
			if (auto *w = m_ctx.Get<Widget>(e)) {
				if (b) w->behavior |= BehaviorFlag::Enable;
				else w->behavior &= (~BehaviorFlag::Enable);
			}
		}

		/**
		 * @brief Show or hide a widget (hidden widgets take no space and receive no input).
		 *
		 * Plays `showSound` / `hideSound` when the state actually changes and marks the
		 * layout dirty so the tree is re-measured next frame.
		 * @param e  Target entity.
		 * @param b  True to make visible, false to hide.
		 */
		void SetVisible(ECS::EntityId e, bool b) {
			if (auto *w = m_ctx.Get<Widget>(e)) {
				bool wasVisible = Has(w->behavior, BehaviorFlag::Visible);
				if (b && !wasVisible) {
					w->behavior |= BehaviorFlag::Visible;
					m_layoutDirty = true;
					if (auto *s = m_ctx.Get<Style>(e); s && !s->showSound.empty()) {
						if (auto sh = _EnsureAudio(s->showSound)) _PlayAudio(sh);
					}
				} else if (!b && wasVisible) {
					w->behavior &= (~BehaviorFlag::Visible);
					m_layoutDirty = true;
					if (auto *s = m_ctx.Get<Style>(e); s && !s->hideSound.empty()) {
						if (auto sh = _EnsureAudio(s->hideSound)) _PlayAudio(sh);
					}
				}
			}
		}

		/** @brief Enable or disable the Hoverable behavior flag on @p e. */
		void SetHoverable(ECS::EntityId e, bool b) {
			if (auto *w = m_ctx.Get<Widget>(e)) {
				if (b) w->behavior |= BehaviorFlag::Hoverable;
				else w->behavior &= (~BehaviorFlag::Hoverable);
			}
		}

		/** @brief Enable or disable the Selectable behavior flag on @p e. */
		void SetSelectable(ECS::EntityId e, bool b) {
			if (auto *w = m_ctx.Get<Widget>(e)) {
				if (b) w->behavior |= BehaviorFlag::Selectable;
				else w->behavior &= (~BehaviorFlag::Selectable);
			}
		}

		/** @brief Enable or disable the Focusable behavior flag on @p e. */
		void SetFocusable(ECS::EntityId e, bool b) {
			if (auto *w = m_ctx.Get<Widget>(e)) {
				if (b) w->behavior |= BehaviorFlag::Focusable;
				else w->behavior &= (~BehaviorFlag::Focusable);
			}
		}

		/** @brief Enable or disable the permanent horizontal scrollbar on @p e. */
		void SetScrollableX(ECS::EntityId e, bool b) {
			if (auto *w = m_ctx.Get<Widget>(e)) {
				if (b) w->behavior |= BehaviorFlag::ScrollableX;
				else w->behavior &= (~BehaviorFlag::ScrollableX);
			}
		}

		/** @brief Enable or disable the permanent vertical scrollbar on @p e. */
		void SetScrollableY(ECS::EntityId e, bool b) {
			if (auto *w = m_ctx.Get<Widget>(e)) {
				if (b) w->behavior |= BehaviorFlag::ScrollableY;
				else w->behavior &= (~BehaviorFlag::ScrollableY);
			}
		}

		/** @brief Shorthand to set both permanent scrollbar axes at once. */
		void SetScrollable(ECS::EntityId e, bool bx, bool by) {
			SetScrollableX(e, bx);
			SetScrollableY(e, by);
		}

		/// Scrollbar automatique horizontal (visible seulement si le contenu déborde).
		void SetAutoScrollableX(ECS::EntityId e, bool b) {
			if (auto *w = m_ctx.Get<Widget>(e)) {
				if (b) w->behavior |= BehaviorFlag::AutoScrollableX;
				else w->behavior &= (~BehaviorFlag::AutoScrollableX);
			}
		}

		/// Scrollbar automatique vertical (visible seulement si le contenu déborde).
		void SetAutoScrollableY(ECS::EntityId e, bool b) {
			if (auto *w = m_ctx.Get<Widget>(e)) {
				if (b) w->behavior |= BehaviorFlag::AutoScrollableY;
				else w->behavior &= (~BehaviorFlag::AutoScrollableY);
			}
		}

		/// Active les deux scrollbars automatiques.
		void SetAutoScrollable(ECS::EntityId e, bool bx, bool by) {
			SetAutoScrollableX(e, bx);
			SetAutoScrollableY(e, by);
		}

		/// Épaisseur (px) des scrollbars inline dessinées dans le container.
		void SetScrollbarThickness(ECS::EntityId e, float t) {
			if (auto *lp = m_ctx.Get<LayoutProps>(e))
				lp->scrollbarThickness = SDL::Max(4.f, t);
		}

		// ── Grid layout helpers ───────────────────────────────────────────────────

		/// Ensure a LayoutGridProps component exists on `e` and return a reference to it.
		LayoutGridProps &EnsureGridProps(ECS::EntityId e) {
			if (auto *gp = m_ctx.Get<LayoutGridProps>(e))
				return *gp;
			return m_ctx.Add<LayoutGridProps>(e);
		}

		/** @brief Set the column count for a Layout::InGrid container. */
		void SetGridCols(ECS::EntityId e, int n) { EnsureGridProps(e).columns = SDL::Max(1, n); }
		/** @brief Set the fixed row count for a Layout::InGrid container (0 = auto). */
		void SetGridRows(ECS::EntityId e, int n) { EnsureGridProps(e).rows    = SDL::Max(0, n); }
		/** @brief Set the column sizing mode for a Layout::InGrid container. */
		void SetGridColSizing(ECS::EntityId e, GridSizing s) { EnsureGridProps(e).colSizing = s; }
		/** @brief Set the row sizing mode for a Layout::InGrid container. */
		void SetGridRowSizing(ECS::EntityId e, GridSizing s) { EnsureGridProps(e).rowSizing = s; }
		/** @brief Set which separator lines are drawn inside a Layout::InGrid container. */
		void SetGridLines(ECS::EntityId e, GridLines l)      { EnsureGridProps(e).lines     = l; }
		/** @brief Set the color of the separator lines in a Layout::InGrid container. */
		void SetGridLineColor(ECS::EntityId e, SDL::Color c) { EnsureGridProps(e).lineColor = c; }
		/** @brief Set the thickness of the separator lines in a Layout::InGrid container. */
		void SetGridLineThickness(ECS::EntityId e, float t)  { EnsureGridProps(e).lineThickness = SDL::Max(0.f, t); }

		/// Ensure a GridCell component exists on `e` and set its position/span.
		void SetGridCell(ECS::EntityId e, int col, int row, int colSpan = 1, int rowSpan = 1) {
			GridCell *gc = m_ctx.Get<GridCell>(e);
			if (!gc) gc = &m_ctx.Add<GridCell>(e);
			gc->col     = SDL::Max(0, col);
			gc->row     = SDL::Max(0, row);
			gc->colSpan = SDL::Max(1, colSpan);
			gc->rowSpan = SDL::Max(1, rowSpan);
		}

		/**
		 * @brief Register (or replace) the event callback on a Canvas widget.
		 * @param e   Target entity.
		 * @param cb  New event handler.
		 */
		void OnEventCanvas(ECS::EntityId e, std::function<void(SDL::Event&)> cb) {
			if (auto *c = m_ctx.Get<CanvasData>(e))
				c->eventCb = std::move(cb);
		}

		/**
		 * @brief Register (or replace) the per-frame update callback on a Canvas widget.
		 * @param e   Target entity.
		 * @param cb  Callback receiving the frame delta time in seconds.
		 */
		void OnUpdateCanvas(ECS::EntityId e, std::function<void(float)> cb) {
			if (auto *c = m_ctx.Get<CanvasData>(e))
				c->updateCb = std::move(cb);
		}

		/**
		 * @brief Register (or replace) the render callback on a Canvas widget.
		 * @param e   Target entity.
		 * @param cb  Callback receiving the renderer and the screen-space rect of the canvas.
		 */
		void OnRenderCanvas(ECS::EntityId e, std::function<void(RendererRef, FRect)> cb) {
			if (auto *c = m_ctx.Get<CanvasData>(e))
				c->renderCb = std::move(cb);
		}

		// ── Tileset skin ──────────────────────────────────────────────────────────────

		/**
		 * Attach (or replace) a tileset 9-slice skin on widget `e`.
		 * Pass a default-constructed `TilesetStyle` with an empty `textureKey`
		 * to remove the skin.
		 */
		void SetTilesetStyle(ECS::EntityId e, TilesetStyle ts) {
			if (!m_ctx.IsAlive(e)) return;
			m_ctx.Add<TilesetStyle>(e, std::move(ts));
		}

		/** @brief Return a pointer to the TilesetStyle component of @p e, or nullptr. */
		[[nodiscard]] TilesetStyle* GetTilesetStyle(ECS::EntityId e) {
			return m_ctx.Get<TilesetStyle>(e);
		}

		/** @brief Remove the TilesetStyle component from @p e (restores default rendering). */
		void RemoveTilesetStyle(ECS::EntityId e) {
			m_ctx.Remove<TilesetStyle>(e);
		}

		/**
		 * @brief Change the texture key and fit mode of an Image widget at runtime.
		 * @param e    Target entity.
		 * @param key  New resource-pool key.
		 * @param f    New fit mode.
		 */
		void SetImageKey(ECS::EntityId e, const std::string &key, ImageFit f = ImageFit::Contain) {
			if (auto *d = m_ctx.Get<ImageData>(e)) {
				d->key = key;
				d->fit = f;
			}
		}

		/// Return a reference to the IconData component, creating it if absent.
		IconData &GetOrAddIconData(ECS::EntityId e) {
			if (auto *ic = m_ctx.Get<IconData>(e)) return *ic;
			return m_ctx.Add<IconData>(e);
		}

		// ── TextArea accessors ────────────────────────────────────────────────────────

		/**
		 * @brief Replace the entire document content of a TextArea widget.
		 *
		 * Resets the cursor, selection, and vertical scroll to the top.
		 * @param e  Target entity.
		 * @param t  New document text (LF line endings).
		 */
		void SetTextAreaContent(ECS::EntityId e, const std::string &t) {
			if (auto *ta = m_ctx.Get<TextAreaData>(e)) {
				ta->text = t;
				ta->cursorPos = 0;
				ta->ClearSelection();
				ta->scrollY = 0.f;
			}
		}
		/**
		 * @brief Return the document content of a TextArea widget.
		 * @param e  Target entity.
		 * @return   Reference to the document string, or an empty static string if not found.
		 */
		[[nodiscard]] const std::string &GetTextAreaContent(ECS::EntityId e) const {
			static const std::string empty;
			const auto *ta = m_ctx.Get<TextAreaData>(e);
			return ta ? ta->text : empty;
		}
		/** @brief Set the selection highlight color of a TextArea widget. */
		void SetTextAreaHighlightColor(ECS::EntityId e, SDL::Color c) {
			if (auto *ta = m_ctx.Get<TextAreaData>(e)) ta->highlightColor = c;
		}
		/** @brief Set the tab-stop size (in character columns) for a TextArea widget. */
		void SetTextAreaTabSize(ECS::EntityId e, int sz) {
			if (auto *ta = m_ctx.Get<TextAreaData>(e)) ta->tabSize = SDL::Max(1, sz);
		}
		/**
		 * @brief Add a styled span to a TextArea widget (rich text highlight).
		 * @param e      Target entity.
		 * @param start  Byte offset of the span start (inclusive).
		 * @param end    Byte offset of the span end (exclusive).
		 * @param style  Style to apply over the byte range.
		 */
		void AddTextAreaSpan(ECS::EntityId e, int start, int end, TextSpanStyle style) {
			if (auto *ta = m_ctx.Get<TextAreaData>(e)) ta->AddSpan(start, end, style);
		}
		/** @brief Remove all rich-text spans from a TextArea widget. */
		void ClearTextAreaSpans(ECS::EntityId e) {
			if (auto *ta = m_ctx.Get<TextAreaData>(e)) ta->ClearSpans();
		}
		/** @brief Return a pointer to the TextAreaData component of @p e, or nullptr. */
		[[nodiscard]] TextAreaData* GetTextAreaData(ECS::EntityId e) {
			return m_ctx.Get<TextAreaData>(e);
		}
		/**
		 * @brief Set whether a TextArea is read-only (selection and copy still work).
		 * @param e   Target entity.
		 * @param ro  True to prevent editing.
		 */
		void SetTextAreaReadOnly(ECS::EntityId e, bool ro) {
			if (auto *ta = m_ctx.Get<TextAreaData>(e)) ta->readOnly = ro;
		}
		/** @brief Return true if the TextArea widget at @p e is in read-only mode. */
		[[nodiscard]] bool GetTextAreaReadOnly(ECS::EntityId e) const {
			const auto *ta = m_ctx.Get<TextAreaData>(e);
			return ta && ta->readOnly;
		}

		// ── Getters ───────────────────────────────────────────────────────────────────

		/**
		 * @brief Return the display text of a Label, Button, or Input widget.
		 * @param e  Target entity.
		 * @return   Const reference to the text string, or an empty static string.
		 */
		[[nodiscard]] const std::string &GetText(ECS::EntityId e) const {
			static const std::string empty;
			const auto *c = m_ctx.Get<EditableContent>(e);
			return c ? c->text : empty;
		}

		/** @brief Return the current numeric value of a Slider widget (0 if not found). */
		[[nodiscard]] float GetValue(ECS::EntityId e) const {
			const auto *s = m_ctx.Get<SliderData>(e);
			return s ? s->val : 0.f;
		}

		/** @brief Return the current scroll offset of a ScrollBar widget (0 if not found). */
		[[nodiscard]] float GetScrollOffset(ECS::EntityId e) const {
			const auto *sb = m_ctx.Get<ScrollBarData>(e);
			return sb ? sb->offset : 0.f;
		}

		/** @brief Return true if a Toggle or RadioButton widget is currently checked. */
		[[nodiscard]] bool IsChecked(ECS::EntityId e) const {
			if (const auto *t = m_ctx.Get<ToggleData>(e))
				return t->checked;
			if (const auto *r = m_ctx.Get<RadioData>(e))
				return r->checked;
			return false;
		}

		/** @brief Return true if widget @p e has the Enable behavior flag set. */
		[[nodiscard]] bool IsEnabled(ECS::EntityId e) const {
			const auto *w = m_ctx.Get<Widget>(e);
			return w && Has(w->behavior, BehaviorFlag::Enable);
		}

		/** @brief Return true if widget @p e has the Visible behavior flag set. */
		[[nodiscard]] bool IsVisible(ECS::EntityId e) const {
			const auto *w = m_ctx.Get<Widget>(e);
			return w && Has(w->behavior, BehaviorFlag::Visible);
		}

		/** @brief Return true if widget @p e has the Hoverable behavior flag set. */
		[[nodiscard]] bool IsHoverable(ECS::EntityId e) const {
			const auto *w = m_ctx.Get<Widget>(e);
			return w && Has(w->behavior, BehaviorFlag::Hoverable);
		}

		/** @brief Return true if widget @p e has the Selectable behavior flag set. */
		[[nodiscard]] bool IsSelectable(ECS::EntityId e) const {
			const auto *w = m_ctx.Get<Widget>(e);
			return w && Has(w->behavior, BehaviorFlag::Selectable);
		}

		/** @brief Return true if widget @p e has the Focusable behavior flag set. */
		[[nodiscard]] bool IsFocusable(ECS::EntityId e) const {
			const auto *w = m_ctx.Get<Widget>(e);
			return w && Has(w->behavior, BehaviorFlag::Focusable);
		}

		/** @brief Return true if widget @p e has the ScrollableX behavior flag set. */
		[[nodiscard]] bool IsScrollableX(ECS::EntityId e) const {
			const auto *w = m_ctx.Get<Widget>(e);
			return w && Has(w->behavior, BehaviorFlag::ScrollableX);
		}

		/** @brief Return true if widget @p e has the ScrollableY behavior flag set. */
		[[nodiscard]] bool IsScrollableY(ECS::EntityId e) const {
			const auto *w = m_ctx.Get<Widget>(e);
			return w && Has(w->behavior, BehaviorFlag::ScrollableY);
		}

		/** @brief Return true if the mouse cursor is currently over widget @p e. */
		[[nodiscard]] bool IsHovered(ECS::EntityId e) const {
			const auto *s = m_ctx.Get<WidgetState>(e);
			return s && s->hovered;
		}

		/** @brief Return true if widget @p e currently holds keyboard focus. */
		[[nodiscard]] bool IsFocused(ECS::EntityId e) const {
			return m_focused == e;
		}
		
		/** @brief Return true if widget @p e is currently being pressed (mouse button held). */
		[[nodiscard]] bool IsPressed(ECS::EntityId e) const {
			const auto *s = m_ctx.Get<WidgetState>(e);
			return s && s->pressed;
		}

		/**
		 * @brief Return the screen-space rectangle of widget @p e as computed in the last layout pass.
		 * @param e  Target entity.
		 * @return   Screen rect, or a zero-sized rect if the entity has no ComputedRect.
		 */
		[[nodiscard]] FRect GetScreenRect(ECS::EntityId e) const {
			const auto *c = m_ctx.Get<ComputedRect>(e);
			return c ? c->screen : FRect{};
		}

		/**
		 * @brief Pre-load a texture into the resource pool under the given key.
		 * @param key   Pool key used to retrieve the texture later.
		 * @param path  File path of the image.
		 */
		void LoadTexture(const std::string& key, const std::string& path) {
			_EnsureTexture(key, path);
		}

		/**
		 * @brief Pre-load a font into the resource pool under the given key (size 8 pt).
		 * @param key   Pool key used to retrieve the font later.
		 * @param path  File path of the TTF/OTF font.
		 */
		void LoadFont(const std::string& key, const std::string& path) {
			_EnsureFont(key, 8.0f, path);
		}

		/**
		 * @brief Pre-load an audio clip into the resource pool under the given key.
		 * @param key   Pool key used to retrieve the audio later.
		 * @param path  File path of the audio file.
		 */
		void LoadAudio(const std::string& key, const std::string& path) {
			_EnsureAudio(key, path);
		}

		// ── Callback registration ─────────────────────────────────────────────────────

		/** @brief Register (or replace) the click callback on widget @p e. */
		void OnClick(ECS::EntityId e, std::function<void()> cb) { m_ctx.Get<Callbacks>(e)->onClick = std::move(cb); }
		/** @brief Register (or replace) the double-click callback on widget @p e. */
		void OnDoubleClick(ECS::EntityId e, std::function<void()> cb) { m_ctx.Get<Callbacks>(e)->onDoubleClick = std::move(cb); }
		/** @brief Enable or disable event propagation to parent for widget @p e (default: true). */
		void SetDispatchEvent(ECS::EntityId e, bool b) { if (auto *w = m_ctx.Get<Widget>(e)) w->dispatchEvent = b; }
		/** @brief Register the value-change callback on widget @p e.
		 *  Templated on the value type @c T; the callback is wrapped so the
		 *  internal float-based delivery still works for any arithmetic T. */
		template <typename T>
		void OnChange(ECS::EntityId e, std::function<void(T)> cb) {
			if constexpr (std::is_same_v<T, float>) {
				m_ctx.Get<Callbacks>(e)->onChange = std::move(cb);
			} else {
				auto wrapped = [fn = std::move(cb)](float v) {
					fn(static_cast<T>(v));
				};
				m_ctx.Get<Callbacks>(e)->onChange = std::move(wrapped);
			}
		}
		/** @brief Register (or replace) the text-change callback on widget @p e (Input, TextArea). */
		void OnTextChange(ECS::EntityId e, std::function<void(const std::string &)> cb) { m_ctx.Get<Callbacks>(e)->onTextChange = std::move(cb); }
		/** @brief Register (or replace) the toggle callback on widget @p e (Toggle, RadioButton). */
		void OnToggle(ECS::EntityId e, std::function<void(bool)> cb) { m_ctx.Get<Callbacks>(e)->onToggle = std::move(cb); }
		/** @brief Register (or replace) the scroll callback on widget @p e (Container, ScrollBar). */
		void OnScroll(ECS::EntityId e, std::function<void(float)> cb) { m_ctx.Get<Callbacks>(e)->onScroll = std::move(cb); }
		/** @brief Register (or replace) the hover-enter callback on widget @p e. */
		void OnHoverEnter(ECS::EntityId e, std::function<void()> cb) { m_ctx.Get<Callbacks>(e)->onHoverEnter = std::move(cb); }
		/** @brief Register (or replace) the hover-leave callback on widget @p e. */
		void OnHoverLeave(ECS::EntityId e, std::function<void()> cb) { m_ctx.Get<Callbacks>(e)->onHoverLeave = std::move(cb); }
		/** @brief Register (or replace) the focus-gain callback on widget @p e. */
		void OnFocusGain(ECS::EntityId e, std::function<void()> cb) { m_ctx.Get<Callbacks>(e)->onFocusGain = std::move(cb); }
		/** @brief Register (or replace) the focus-lose callback on widget @p e. */
		void OnFocusLose(ECS::EntityId e, std::function<void()> cb) { m_ctx.Get<Callbacks>(e)->onFocusLose = std::move(cb); }
		/// Register a callback fired when a Tree node is selected (nodeIndex, hasChildren).
		void OnTreeSelect(ECS::EntityId e, std::function<void(int, bool)> cb) { m_ctx.Get<Callbacks>(e)->onTreeSelect = std::move(cb); }

		/** @brief Return a reference to the underlying ECS context (for advanced use). */
		[[nodiscard]] ECS::Context &GetECSContext() { return m_ctx; }

		// ── Frame pipeline ────────────────────────────────────────────────────────────

		/**
		 * @brief Feed an SDL event into the UI system.
		 *
		 * Must be called once per event from the application event loop.  Handles
		 * mouse, keyboard, text-input, mouse-wheel, and drop-text events.
		 * @param ev  The SDL event to process.
		 */
		void ProcessEvent(const SDL::Event &ev) {
			switch (ev.type) {
				case SDL::EVENT_WINDOW_RESIZED:
				case SDL::EVENT_WINDOW_PIXEL_SIZE_CHANGED:
					m_layoutDirty = true; // Déclenche le recalcul complet
					break;
				case SDL::EVENT_MOUSE_BUTTON_DOWN:
					if (ev.button.button == SDL::BUTTON_LEFT) {
						m_mouseDown    = true;
						m_mousePressed = true;
					}
					break;
				case SDL::EVENT_MOUSE_BUTTON_UP:
					if (ev.button.button == SDL::BUTTON_LEFT) {
						m_mouseDown     = false;
						m_mouseReleased = true;
					}
					break;
				case SDL::EVENT_MOUSE_MOTION:
					m_mousePos   = {ev.motion.x, ev.motion.y};
					m_mouseDelta = {ev.motion.xrel, ev.motion.yrel};
					break;
				case SDL::EVENT_TEXT_INPUT:
					_HandleTextInput(ev.text.text);
					break;
				case SDL::EVENT_KEY_DOWN:
					_HandleKeyDown(ev.key.key, ev.key.mod);
					break;
				case SDL::EVENT_MOUSE_WHEEL:
					_HandleScroll(ev.wheel.x, ev.wheel.y);
					break;
				case SDL::EVENT_DROP_TEXT:
					if (m_focused != ECS::NullEntity && m_ctx.IsAlive(m_focused)) {
						auto *w = m_ctx.Get<Widget>(m_focused);
						if (w && Has(w->behavior, BehaviorFlag::Enable)) {
							auto processDrop = [&]<typename T>(T* data) {
								if (data) _InsertText(data, std::string_view(ev.drop.data));
							};
							if (w->type == WidgetType::Input) processDrop(m_ctx.Get<EditableContent>(m_focused));
							else if (w->type == WidgetType::TextArea) processDrop(m_ctx.Get<TextAreaData>(m_focused));
						}
					}
					break;
				default:
					break;
			}
			_CanvasEvent(ev);
		}

		/**
		 * @brief Run one full UI frame: layout → input → render → animate.
		 *
		 * Must be called once per game frame after all `ProcessEvent` calls.
		 * @param dt  Frame delta time in seconds.
		 */
		void Iterate(float dt) {
			m_pool.Update();

			m_dt = dt;
			m_timeSinceLastClick += dt;
			if (m_root == ECS::NullEntity || !m_ctx.IsAlive(m_root)) {
				_ResetOneShots();
				return;
			}

			SDL::Point sz    = m_renderer.GetWindow().GetSize();
			SDL::FRect newVp = {0.f, 0.f, SDL::Round((float)sz.x), SDL::Round((float)sz.y)};
			if (newVp.w != m_viewport.w || newVp.h != m_viewport.h)
				m_layoutDirty = true;
			m_viewport = newVp;

			// Canvas update callbacks run first so game logic sees current dt.
			_ProcessCanvasUpdate(dt);

			_ProcessLayout();
			_ProcessInput();
			_ProcessTooltip(dt);
			_ProcessRender();
			_ProcessAnimate(dt);
			_ResetOneShots();
			m_mouseDelta  = {};
			m_layoutDirty = false;
		}

	private:
		ECS::Context& m_ctx;
		RendererRef   m_renderer;
		MixerRef      m_mixer;
		ResourcePool& m_pool;
#if UI_HAS_TTF
		// Owned TTF text engine.  Declared *after* m_ctx/m_pool (references)
		// so that its destruction order is controlled explicitly in ~System():
		// all TextCache ECS components are cleared first, then this resets.
		std::optional<SDL::RendererTextEngine> m_engine;
#endif
		bool        m_usedDebugFontPerDefault = false;
		std::string m_defaultFontPath;
		float       m_defaultFontSize = 0.f;
		ECS::EntityId m_root = ECS::NullEntity, m_focused = ECS::NullEntity, m_hovered = ECS::NullEntity, m_pressed = ECS::NullEntity;
		float m_dt = 0.f;
		// ── Double-click tracking ──────────────────────────────────────────────────────
		float         m_timeSinceLastClick  = 1e9f;
		ECS::EntityId m_lastClickEntity     = ECS::NullEntity;
		int           m_clickCount          = 0;

		// ── Tooltip state ─────────────────────────────────────────────────────────────
		ECS::EntityId m_tooltipEntity  = ECS::NullEntity; ///< Entité dédiée au cache de texte du tooltip.
		ECS::EntityId m_tooltipTarget  = ECS::NullEntity; ///< Entité actuellement survol-minutée.
		float         m_tooltipTimer   = 0.f;             ///< Temps de survol accumulé (secondes).
		bool          m_tooltipVisible = false;           ///< Vrai si le tooltip est visible ce frame.
		FRect m_viewport = {};
		FPoint m_mousePos = {}, m_mouseDelta = {};
		bool m_mouseDown = false, m_mousePressed = false, m_mouseReleased = false;
		bool m_layoutDirty = true; ///< true whenever a resize or structural change requires a full re-layout
		ECS::EntityId m_comboOpen   = ECS::NullEntity; ///< Currently-open ComboBox overlay (or NullEntity).
		ECS::EntityId m_menuBarOpen = ECS::NullEntity; ///< MenuBar entity with an open dropdown (or NullEntity).
		void _ResetOneShots() { m_mousePressed = m_mouseReleased = false; }

		// ── Texture ──────────────────────────────────────────────────────────────────

		SDL::TextureRef _EnsureTexture(const std::string &key, const std::string &path = "") {
			if (key.empty()) return nullptr; 

			auto h = m_pool.Get<SDL::Texture>(key);
			if (h) return *h.get();

			SDL::Texture wrapper;
			try {
				wrapper = SDL::Texture(m_renderer, path.empty() ? key : path);
			} catch (...) { 
				SDL::LogWarn(SDL::LOG_CATEGORY_APPLICATION, std::format("UI::System: cannot open font '{}'", path.empty() ? key : path));
				return nullptr;
			}

			m_pool.Add<SDL::Texture>(key, std::move(wrapper));
			auto h2 = m_pool.Get<SDL::Texture>(key);
			return h2 ? SDL::TextureRef(*h2.get()) : nullptr;
		}

		// ── Font ──────────────────────────────────────────────────────────────────────

		/// Ensure the RendererTextEngine exists as an owned member; return a pointer.
		/// The engine is lazy-initialised on first use and destroyed in ~System()
		/// *after* all TextCache ECS components have been cleared, which guarantees
		/// that no SDL::Text outlives its engine (avoiding the use-after-free that
		/// occurred when the engine was stored in the external ResourcePool).
		SDL::RendererTextEngine *_EnsureEngine() {
			if (!m_engine.has_value()) {
				try {
					m_engine.emplace(m_renderer);
				} catch (...) {
					SDL::LogWarn(SDL::LOG_CATEGORY_APPLICATION,
								 "UI::System: failed to create RendererTextEngine");
					return nullptr;
				}
			}
			return &m_engine.value();
		}

		SDL::FontRef _EnsureFont(const std::string &key, float ptsize, const std::string& path = "") { 
			if (key.empty() || ptsize <= 0.f)
				return nullptr;

			auto h = m_pool.Get<SDL::Font>(key);
			if (h) {
				if (h->GetSize() != ptsize) {
					h->SetSize(ptsize);
				}
				return *h.get();
			}

			// Slow key: load and insert.
			SDL::Font wrapper;
			try {
				wrapper = SDL::Font(path.empty() ? key : path, ptsize);
			} catch (...) { 
				SDL::LogWarn(SDL::LOG_CATEGORY_APPLICATION, std::format("UI::System: cannot open font '{}' at {:.2f}pt", path.empty() ? key : path, ptsize));
				return nullptr;
			}

			// Store even when invalid — acts as a "failed" sentinel so we don't retry.
			m_pool.Add<SDL::Font>(key, std::move(wrapper));
			auto h2 = m_pool.Get<SDL::Font>(key);
			if (h2) {
				if (h2->GetSize() != ptsize) {
					h2->SetSize(ptsize);
				}
				return SDL::FontRef(*h2.get());
			}
			return nullptr;
		}

		SDL::TextRef _EnsureText(ECS::EntityId e, SDL::FontRef font, const std::string& text) { 
			if (!font || text.empty()) return nullptr;

			auto* engine = _EnsureEngine();
			if (!engine) return nullptr;

			auto* cache = m_ctx.Get<TextCache>(e);
			if (!cache) {
				cache = &m_ctx.Add<TextCache>(e);
			}

			if (cache->text) {
				if (cache->text.GetFont().Get() != font.Get()) {
					cache->text = engine->CreateText(font, text);
				} else {
					cache->text.SetString(text); 
				}

			} else {
				try {
					cache->text = engine->CreateText(font, text);
				} catch (const std::exception& ex) { 
				SDL::LogWarn(SDL::LOG_CATEGORY_APPLICATION,
							 std::format("UI::System: Failed to create text for entity {}: {}", e, ex.what()));
				return nullptr;
				}
			}
			return SDL::TextRef(cache->text.Get());
		}
		
		// ── Font resolution ───────────────────────────────────────────────────────────

		struct ResolvedFont {
			std::string key;
			float size   = 0.f;
			bool isDebug = false;
		};

		/// Walk the FontType chain for entity @p e and return the effective font.
		///
		/// - FontType::Self    → own fontKey/fontSize (falls through to Inherited if unset).
		/// - FontType::Inherited → walks up parents until a Self ancestor is found, then Default.
		/// - FontType::Root    → uses root widget's Self font, then Default.
		/// - FontType::Default → system SetDefaultFont() path, or debug if none configured.
		/// - FontType::Debug   → always SDL3 built-in debug font.
		ResolvedFont _ResolveFont(ECS::EntityId e) {
			for (ECS::EntityId cur = e; cur != ECS::NullEntity && m_ctx.IsAlive(cur);) {
				auto* s = m_ctx.Get<Style>(cur);
				if (!s) break;

				switch (s->usedFont) {
					case FontType::Debug:
						return {"", s->fontSize > 0.f ? s->fontSize : m_defaultFontSize, true};
					case FontType::Default:
						if (!m_defaultFontPath.empty())
							return {m_defaultFontPath, m_defaultFontSize, false};
						return {"", m_defaultFontSize, m_usedDebugFontPerDefault};
					case FontType::Self:
						if (!s->fontKey.empty() && s->fontSize > 0.f)
							return {s->fontKey, s->fontSize, false};
						break; // Self but unconfigured → walk up
					case FontType::Root: {
						ECS::EntityId root = cur;
						while (true) {
							auto* p = m_ctx.Get<Parent>(root);
							if (!p || p->id == ECS::NullEntity) break;
							root = p->id;
						}
						if (root != cur) {
							auto* rs = m_ctx.Get<Style>(root);
							if (rs && rs->usedFont == FontType::Self &&
								!rs->fontKey.empty() && rs->fontSize > 0.f)
								return {rs->fontKey, rs->fontSize, false};
						}
						// Root has no Self font → system default
						if (!m_defaultFontPath.empty())
							return {m_defaultFontPath, m_defaultFontSize, false};
						return {"", m_defaultFontSize, m_usedDebugFontPerDefault};
					}
					case FontType::Inherited:
						break; // walk up
				}

				auto* p = m_ctx.Get<Parent>(cur);
				if (!p || p->id == ECS::NullEntity) break;
				cur = p->id;
			}
			// No configured ancestor → system default
			if (!m_defaultFontPath.empty())
				return {m_defaultFontPath, m_defaultFontSize, false};
			return {"", m_defaultFontSize, m_usedDebugFontPerDefault};
		}

		// ── Audio ─────────────────────────────────────────────────────────────────────

		SDL::AudioRef _EnsureAudio(const std::string& key, const std::string& path = "") { 
			if (key.empty())
				return nullptr;

			auto h = m_pool.Get<SDL::Audio>(key);
			if (h) return *h.get();

			SDL::Audio wrapper;
			try {
				wrapper = SDL::Audio(m_mixer, path.empty() ? key : path, false);
			} catch (...) { 
				SDL::LogWarn(SDL::LOG_CATEGORY_APPLICATION, std::format("UI::System: cannot open audio '{}'", path.empty() ? key : path));
				return nullptr;
			}

			// Store even when invalid — acts as a "failed" sentinel so we don't retry.
			m_pool.Add<SDL::Audio>(key, std::move(wrapper));
			auto h2 = m_pool.Get<SDL::Audio>(key);
			return h2 ? SDL::AudioRef(*h2.get()) : nullptr;
		}

		void _PlayAudio(SDL::AudioRef audio) { 
			m_mixer.PlayAudio(audio);
		}


		// ── Make widget ─────────────────────────────────────────────────────────────────────────────
		ECS::EntityId _Make(const std::string &n, WidgetType k) {
			ECS::EntityId e = m_ctx.CreateEntity();

			// Compute the correct behavior flags for this widget type.
			// All widgets start Enabled + Visible.  Interactive widgets additionally
			// get Hoverable / Selectable / Focusable as appropriate.
			BehaviorFlag beh = BehaviorFlag::Enable | BehaviorFlag::Visible;
			switch (k) { 
				case WidgetType::Button:
				case WidgetType::Toggle:
				case WidgetType::RadioButton:
				case WidgetType::Slider:
				case WidgetType::Knob:
				case WidgetType::ScrollBar:
				case WidgetType::Input:
				case WidgetType::Canvas:
				case WidgetType::TabView:
				case WidgetType::Expander:
				case WidgetType::ColorPicker:
				case WidgetType::ComboBox:
					beh |= BehaviorFlag::Hoverable | BehaviorFlag::Selectable | BehaviorFlag::Focusable;
					break;
				case WidgetType::Graph:
				case WidgetType::Splitter:
					beh |= BehaviorFlag::Hoverable | BehaviorFlag::Selectable;
					break;
				case WidgetType::Popup:
					beh |= BehaviorFlag::Hoverable | BehaviorFlag::Selectable | BehaviorFlag::Resizable | BehaviorFlag::Draggable;
					break;
				case WidgetType::MenuBar:
					beh |= BehaviorFlag::Hoverable | BehaviorFlag::Selectable;
					break;
				case WidgetType::Container:
					beh |= BehaviorFlag::AutoScrollableX | BehaviorFlag::AutoScrollableY;
					break;
				case WidgetType::ListBox:
				case WidgetType::TextArea:
				case WidgetType::Tree:
					beh |= BehaviorFlag::Hoverable | BehaviorFlag::Selectable | BehaviorFlag::Focusable;
					beh |= BehaviorFlag::AutoScrollableX | BehaviorFlag::AutoScrollableY;
					break;
				default:
					break;
			}

			m_ctx.Add<Widget>(e, {n, k, beh, DirtyFlag::All});
			auto &style = m_ctx.Add<Style>(e);
			m_ctx.Add<LayoutProps>(e);
			m_ctx.Add<EditableContent>(e);
			m_ctx.Add<WidgetState>(e);
			m_ctx.Add<Callbacks>(e);
			m_ctx.Add<ComputedRect>(e);
			m_ctx.Add<Children>(e);
			m_ctx.Add<Parent>(e);

			// Containers get a scroll-drag state component for their inline scrollbars.
			if (k == WidgetType::Container || k == WidgetType::TextArea || k == WidgetType::ListBox || k == WidgetType::Tree)
				m_ctx.Add<ContainerScrollState>(e);

			style.usedFont = FontType::Default;
			return e;
		}

		/// Build a child LayoutContext from the current viewport + root info + the
		/// resolved parent content area and layout props.
		LayoutContext _MakeChildCtx(const LayoutContext &parentCtx,
									const FPoint& contentSize,
									const LayoutProps &lp) const noexcept { 
			LayoutContext cc;
			cc.windowSize     = parentCtx.windowSize;
			cc.rootSize       = parentCtx.rootSize;
			cc.rootPadding    = parentCtx.rootPadding;
			cc.rootFontSize   = parentCtx.rootFontSize;
			cc.parentSize     = contentSize;
			cc.parentPadding  = lp.padding;
			cc.parentFontSize = parentCtx.rootFontSize; 
			return cc;
		}

		/// Calcule quelles scrollbars inline doivent être affichées pour un container.
		///
		/// ScrollableX/Y  → toujours affiché (permanent).
		/// AutoScrollableX/Y → affiché seulement si le contenu déborde.
		/// La compensation de l'axe croisé est prise en compte (une barre visible
		/// rétrécit l'autre axe et peut faire apparaître la deuxième barre).
		///
		/// @param w      Widget (BehaviorFlags).
		/// @param lp     LayoutProps (contentW/H, scrollbarThickness).
		/// @param viewW  Largeur intérieure disponible (padding déjà soustrait).
		/// @param viewH  Hauteur intérieure disponible (padding déjà soustrait).
		/// @param showX  [out] vrai si la barre horizontale doit être dessinée.
		/// @param showY  [out] vrai si la barre verticale doit être dessinée.
		static void _ContainerScrollbars(const Widget &w, const LayoutProps &lp,
										 float viewW, float viewH,
										 bool &showX, bool &showY) noexcept { 
			const bool wantX  = Has(w.behavior, BehaviorFlag::ScrollableX | BehaviorFlag::AutoScrollableX);
			const bool wantY  = Has(w.behavior, BehaviorFlag::ScrollableY | BehaviorFlag::AutoScrollableY);
			const bool autoX  = Has(w.behavior, BehaviorFlag::AutoScrollableX)
							 && !Has(w.behavior, BehaviorFlag::ScrollableX);
			const bool autoY  = Has(w.behavior, BehaviorFlag::AutoScrollableY)
							 && !Has(w.behavior, BehaviorFlag::ScrollableY);

			// Première passe — sans tenir compte de l'axe croisé.
			showX = wantX && (!autoX || lp.contentW > viewW);
			showY = wantY && (!autoY || lp.contentH > viewH);

			// Deuxième passe — une barre visible réduit l'espace de l'autre axe.
			// Pour le mode auto, on garde le même seuil que la première passe (viewW/viewH)
			// afin d'éviter de déclencher une barre croisée quand contentW == viewW.
			if (showY && !showX && wantX)
				showX = !autoX || (lp.contentW > viewW);
			if (showX && !showY && wantY)
				showY = !autoY || (lp.contentH > viewH);
		}

		// ── Text Edition Helpers ──────────────────────────────────────────────────────

		/// Unifie l'accès au curseur entre EditableContent et TextAreaData
		template <typename T>
		int& _GetCursor(T* data) {
			if constexpr (std::is_same_v<T, TextAreaData>) return data->cursorPos;
			else return data->cursor;
		}

		// ── Input filtering / numeric helpers ─────────────────────────────────
		/// Returns the per-frame "permissive" regex used to validate the
		/// prospective text after an insertion. Compiled once per pattern.
		static const std::regex& _GetInputRegex(InputType t) {
			static const std::regex reText(".*", std::regex::optimize);
			static const std::regex reInt (R"(-?\d*)",          std::regex::optimize);
			static const std::regex reFlt (R"(-?\d*\.?\d*)",    std::regex::optimize);
			static const std::regex reMail(R"([A-Za-z0-9._@+\-]*)",        std::regex::optimize);
			static const std::regex reUrl (R"([A-Za-z0-9._~:/?#\[\]@!$&'()*+,;=%\-]*)", std::regex::optimize);
			switch (t) {
				case InputType::IntegerValue: return reInt;
				case InputType::FloatValue:   return reFlt;
				case InputType::Mail:         return reMail;
				case InputType::Url:          return reUrl;
				case InputType::Text:
				default:                      return reText;
			}
		}
		/// Strict regex used on commit/blur: requires a fully-formed value.
		static const std::regex& _GetInputStrictRegex(InputType t) {
			static const std::regex reInt (R"(-?\d+)",                       std::regex::optimize);
			static const std::regex reFlt (R"(-?(\d+\.?\d*|\.\d+))",         std::regex::optimize);
			static const std::regex reMail(R"([A-Za-z0-9._%+\-]+@[A-Za-z0-9.\-]+\.[A-Za-z]{2,})", std::regex::optimize);
			return (t == InputType::IntegerValue) ? reInt
			     : (t == InputType::FloatValue)   ? reFlt
			     : (t == InputType::Mail)         ? reMail
			     : _GetInputRegex(t);
		}
		/// Format a NumericValue into its display string, integer or float
		/// formatting per the original arithmetic type stored in @c v.type.
		static std::string _FormatNumeric(const NumericValue& v) {
			char buf[32];
			if (v.isIntegral())
				std::snprintf(buf, sizeof(buf), "%lld", (long long)std::llround(v.val));
			else
				std::snprintf(buf, sizeof(buf), "%.*f", std::max(0, v.decimals), v.val);
			return buf;
		}
		/// Parse the given text into @c v.val using a strict regex selected
		/// by @p type. Returns true if a value was committed.
		static bool _ParseNumeric(NumericValue& v, InputType type, const std::string& text) {
			if (text.empty() || text == "-" || text == "." || text == "-.") return false;
			if (!std::regex_match(text, _GetInputStrictRegex(type))) return false;
			try {
				double d = std::stod(text);
				if (v.isIntegral()) d = std::round(d);
				d = std::clamp(d, v.min, v.max);
				if (d != v.val) { v.val = d; return true; }
			} catch (...) {}
			return false;
		}

		/// Insère du texte en écrasant la sélection éventuelle
		template <typename T>
		void _InsertText(T* data, std::string_view text) {
			if constexpr (std::is_same_v<T, TextAreaData>) {
				data->Insert(text); // TextAreaData s'occupe des Spans
			} else {
				if (data->HasSelection()) data->DeleteSelection();
				data->text.insert(_GetCursor(data), text);
				_GetCursor(data) += (int)text.size();
				data->ClearSelection();
			}
		}

		/// Déplace le curseur et gère la sélection continue (touche Shift)
		template <typename T>
		void _MoveTextCursor(T* data, int newPos, bool shift) {
			int& cursor = _GetCursor(data);
			if (shift) {
				if (!data->HasSelection()) data->selAnchor = cursor;
				cursor = newPos;
				data->selFocus = newPos;
			} else {
				cursor = newPos;
				data->ClearSelection();
			}
		}

		[[nodiscard]] static int _TextWordLeft(const std::string& text, int pos) noexcept {
			if (pos <= 0) return 0; 
			--pos;
			while (pos > 0 && !std::isalnum((unsigned char)text[pos - 1])) --pos;
			while (pos > 0 && std::isalnum((unsigned char)text[pos - 1])) --pos;
			return pos;
		}

		[[nodiscard]] static int _TextWordRight(const std::string& text, int pos) noexcept {
			int sz = (int)text.size();
			while (pos < sz && !std::isalnum((unsigned char)text[pos])) ++pos;
			while (pos < sz && std::isalnum((unsigned char)text[pos])) ++pos;
			return pos;
		}

		/// Gère tous les raccourcis communs (Ctrl+C, Ctrl+V, Flèches, Suppr...)
		template <typename T>
		bool _HandleCommonTextKeys(T* data, SDL::Keycode k, SDL::Keymod mod, Callbacks* cb, bool allowEdit = true) {
			int& cursor = _GetCursor(data);
			const bool ctrl  = (mod & SDL::KMOD_CTRL)  != 0;
			const bool shift = (mod & SDL::KMOD_SHIFT) != 0;

			switch (k) {
				case SDL::KEYCODE_BACKSPACE:
					if (!allowEdit) return true;
					if (data->HasSelection()) {
						data->DeleteSelection();
					} else if (cursor > 0) {
						data->text.erase((size_t)(cursor - 1), 1);
						if constexpr (std::is_same_v<T, TextAreaData>) data->_ShiftSpans(cursor - 1, -1);
						--cursor;
					}
					if (cb && cb->onTextChange) cb->onTextChange(data->text);
					return true;

				case SDL::KEYCODE_DELETE:
					if (!allowEdit) return true;
					if (data->HasSelection()) {
						data->DeleteSelection();
					} else if (cursor < (int)data->text.size()) {
						data->text.erase((size_t)cursor, 1);
						if constexpr (std::is_same_v<T, TextAreaData>) data->_ShiftSpans(cursor, -1);
					}
					if (cb && cb->onTextChange) cb->onTextChange(data->text);
					return true;

				case SDL::KEYCODE_LEFT:
					if (!shift && data->HasSelection()) _MoveTextCursor(data, data->SelMin(), false);
					else if (ctrl) _MoveTextCursor(data, _TextWordLeft(data->text, cursor), shift);
					else _MoveTextCursor(data, SDL::Max(0, cursor - 1), shift);
					return true;

				case SDL::KEYCODE_RIGHT:
					if (!shift && data->HasSelection()) _MoveTextCursor(data, data->SelMax(), false);
					else if (ctrl) _MoveTextCursor(data, _TextWordRight(data->text, cursor), shift);
					else _MoveTextCursor(data, SDL::Min((int)data->text.size(), cursor + 1), shift);
					return true;

				case SDL::KEYCODE_HOME:
					if constexpr (std::is_same_v<T, TextAreaData>) {
						if (ctrl) _MoveTextCursor(data, 0, shift);
						else      _MoveTextCursor(data, data->LineStart(data->LineOf(cursor)), shift);
					} else {
						_MoveTextCursor(data, 0, shift);
					}
					return true;

				case SDL::KEYCODE_END:
					if constexpr (std::is_same_v<T, TextAreaData>) {
						if (ctrl) _MoveTextCursor(data, (int)data->text.size(), shift);
						else      _MoveTextCursor(data, data->LineEnd(data->LineOf(cursor)), shift);
					} else {
						_MoveTextCursor(data, (int)data->text.size(), shift);
					}
					return true;

				case SDL::KEYCODE_A:
					if (ctrl) {
						data->SetSelection(0, (int)data->text.size());
						cursor = (int)data->text.size();
					}
					return true;

				case SDL::KEYCODE_C:
					if (ctrl && data->HasSelection()) {
						try { SDL::SetClipboardText(data->GetSelectedText().c_str()); } catch (...) {}
					}
					return true;

				case SDL::KEYCODE_X:
					if (ctrl && data->HasSelection()) {
						try { SDL::SetClipboardText(data->GetSelectedText().c_str()); } catch (...) {}
						if (allowEdit) {
							data->DeleteSelection();
							if (cb && cb->onTextChange) cb->onTextChange(data->text);
						}
					}
					return true;

				case SDL::KEYCODE_V:
					if (!allowEdit) return true;
					if (ctrl) {
						try {
							if (SDL::HasClipboardText()) {
								auto clip = SDL::GetClipboardText();
								_InsertText(data, static_cast<std::string_view>(clip));
								if (cb && cb->onTextChange) cb->onTextChange(data->text);
							}
						} catch (...) {}
					}
					return true;
			}
			return false;
		}

		// Helper pour fusionner les logiques de Clic et de Drag
		void _ProcessTextClickOrDrag(ECS::EntityId e, bool isDrag) {
			auto *w   = m_ctx.Get<Widget>(e);
			auto *s   = m_ctx.Get<Style>(e);
			auto *cr  = m_ctx.Get<ComputedRect>(e);
			auto *lp  = m_ctx.Get<LayoutProps>(e);
			if (!w || !s || !cr || !lp) return;

			float relX = m_mousePos.x - (cr->screen.x + lp->padding.left);
			float relY = m_mousePos.y - (cr->screen.y + lp->padding.top);

			auto applyHit = [&](auto* data, int hitPos) {
				if (isDrag && !data->selectDragging) return;
				_GetCursor(data) = hitPos;
				if (isDrag) data->selFocus = hitPos;
				else { data->SetSelection(hitPos, hitPos); data->selectDragging = true; }
			};

			if (w->type == WidgetType::TextArea) {
				if (auto *ta = m_ctx.Get<TextAreaData>(e)) applyHit(ta, _TextAreaHitPos(ta, relX, relY, e));
			} else if (w->type == WidgetType::Input) {
				if (auto *c = m_ctx.Get<EditableContent>(e)) applyHit(c, _InputHitPos(c, relX, e));
			}
		}

		// ── Multi-click selection ─────────────────────────────────────────────────────

		/// Double-click (count==2) on Input  → select all.
		/// Double-click (count==2) on TextArea → select current line.
		/// Triple-click or more (count>=3) on TextArea → select all text.
		void _HandleMultiClick(ECS::EntityId e, int count) {
			auto *w  = m_ctx.Get<Widget>(e);
			auto *cb = m_ctx.Get<Callbacks>(e);
			if (!w) return;

			if (w->type == WidgetType::Input) {
				if (auto *c = m_ctx.Get<EditableContent>(e)) {
					c->SetSelection(0, (int)c->text.size());
					c->cursor = (int)c->text.size();
					if (cb && cb->onDoubleClick) cb->onDoubleClick();
				}
			} else if (w->type == WidgetType::TextArea) {
				if (auto *ta = m_ctx.Get<TextAreaData>(e)) {
					if (count >= 3) {
						// Select all
						ta->SetSelection(0, (int)ta->text.size());
						ta->cursorPos = (int)ta->text.size();
					} else {
						// Select the line under the cursor
						auto *cr = m_ctx.Get<ComputedRect>(e);
						auto *lp = m_ctx.Get<LayoutProps>(e);
						if (cr && lp) {
							float relY  = m_mousePos.y - (cr->screen.y + lp->padding.top);
							float lineH = _TH(e) + 2.f;
							int   line  = std::clamp((int)((relY + ta->scrollY) / lineH), 0, ta->LineCount() - 1);
							ta->SetSelection(ta->LineStart(line), ta->LineEnd(line));
							ta->cursorPos = ta->LineEnd(line);
						}
					}
					if (cb && cb->onDoubleClick) cb->onDoubleClick();
				}
			}
		}

		// ── Layout ────────────────────────────────────────────────────────────────────

		void _ProcessLayout() {
			if (!m_ctx.IsAlive(m_root)) return; 

			auto *rootLp = m_ctx.Get<LayoutProps>(m_root);
			if (!rootLp) return;

			// Calcul de la taille racine sans écraser la valeur d'origine (Value::Auto)
			float rootW = SDL::Round(rootLp->width.IsAuto()
							? m_viewport.w
							: rootLp->width.Resolve({{m_viewport.w, m_viewport.h}, {m_viewport.w, m_viewport.h}, rootLp->padding}));
			float rootH = SDL::Round(rootLp->height.IsAuto()
							? m_viewport.h
							: rootLp->height.Resolve({{m_viewport.w, m_viewport.h}, {m_viewport.w, m_viewport.h}, rootLp->padding}));

			float rootFs = (m_defaultFontSize > 0.f) ? m_defaultFontSize : (float)SDL::DEBUG_TEXT_FONT_CHARACTER_SIZE;

			LayoutContext rc;
			rc.windowSize     = {m_viewport.w, m_viewport.h};
			rc.rootSize       = {rootW, rootH};
			rc.rootPadding    = rootLp->padding;
			rc.rootFontSize   = rootFs;
			rc.parentSize     = {rootW, rootH};
			rc.parentPadding  = rootLp->padding;
			rc.parentFontSize = rootFs;

			_Measure(m_root, rc);

			auto *rootCr = m_ctx.Get<ComputedRect>(m_root);
			if (rootCr)
				rootCr->screen = {0.f, 0.f, rootW, rootH};

			_Place(m_root);
			_UpdateClips(m_root, m_viewport);
		}

		FPoint _Measure(ECS::EntityId e, const LayoutContext &ctx) {
			if (!m_ctx.IsAlive(e)) 
				return {};
			auto *w  = m_ctx.Get<Widget>(e);
			auto *lp = m_ctx.Get<LayoutProps>(e);
			auto *cr = m_ctx.Get<ComputedRect>(e);
			if (!w || !lp || !cr)
				return {};
			if (!Has(w->behavior, BehaviorFlag::Visible)) {
				cr->measured = {};
				return {};
			}

			// Resolve explicit dimensions (Auto/Grow → 0 ici ; on élargit ci-dessous).
			bool wa = lp->width.IsAuto()  || lp->width.IsGrow();
			bool ha = lp->height.IsAuto() || lp->height.IsGrow();
			float fw = wa ? 0.f : lp->width.Resolve(ctx);
			float fh = ha ? 0.f : lp->height.Resolve(ctx);

			// Espace contenu brut disponible pour les enfants.
			float cW = SDL::Max(0.f, (wa ? ctx.parentSize.x : fw) - lp->padding.left - lp->padding.right);
			float cH = SDL::Max(0.f, (ha ? ctx.parentSize.y : fh) - lp->padding.top  - lp->padding.bottom);

			if (w->type == WidgetType::ListBox) {
				if (auto *lb = m_ctx.Get<ListBoxData>(e)) {
					lp->contentH = (float)lb->items.size() * lb->itemHeight;
					// Largeur max des items pour la scrollbar horizontale
					{
						float maxW = 0.f;
						for (const auto &item : lb->items)
							maxW = SDL::Max(maxW, _TW(item, e));
						lp->contentW = maxW;
					}
				}
			} else if (w->type == WidgetType::TextArea) {
				if (auto *ta = m_ctx.Get<TextAreaData>(e)) {
					float lineH = _TH(e) + 2.f;
					lp->contentH = ta->LineCount() * lineH;

					float maxW = 0.f;
					for (int i = 0; i < ta->LineCount(); ++i) {
						float lw = _TextAreaLineX(ta, i, ta->LineEnd(i) - ta->LineStart(i), e);
						maxW = SDL::Max(maxW, lw);
					}
					lp->contentW = maxW;
				}
			}

			// Pour les containers avec scrollbars automatiques, on doit pré-calculer
			// si les barres seront visibles afin de réserver leur place dans l'espace
			// contenu.  On utilise les données contentW/H du frame précédent (première
			// frame = 0, ce qui est correct car le contenu ne déborde pas encore).
			if (w->type == WidgetType::Container || w->type == WidgetType::ListBox || w->type == WidgetType::TextArea || w->type == WidgetType::Tree) {
				bool showX = false, showY = false;
				_ContainerScrollbars(*w, *lp, cW, cH, showX, showY);
				if (showY) cW = SDL::Max(0.f, cW - lp->scrollbarThickness);
				if (showX) cH = SDL::Max(0.f, cH - lp->scrollbarThickness);
			}

			FPoint intr = _IntrinsicSize(e);

			// Contexte transmis aux enfants.
			LayoutContext cc = _MakeChildCtx(ctx, {cW, cH}, *lp);

			float chW = 0.f, chH = 0.f;
			float curLineW = 0.f, curLineH = 0.f;
			int vis = 0, lineVis = 0;
			auto *ch = m_ctx.Get<Children>(e);
			if (ch) {
				if (lp->layout == Layout::InColumn || lp->layout == Layout::InLine) {
					// Two-pass: measure non-grow children first to compute the grow
					// budget, then re-measure grow children with the correct axis size
					// in their context so that Ph/Pw in grandchildren resolve correctly.
					// Grow factor: read from child's width (InLine) or height (InColumn).
					const bool isCol = (lp->layout == Layout::InColumn);
					auto growOf = [&](const LayoutProps* cl) -> float {
						return 0.01f * (isCol ? (cl->height.IsGrow() ? cl->height.val : 0.f)
									 : (cl->width.IsGrow() ? cl->width.val  : 0.f));
					};
					std::vector<ECS::EntityId> flow;
					float tFixed = 0.f, tGrow = 0.f;

					for (ECS::EntityId cid : ch->ids) {
						if (!m_ctx.IsAlive(cid)) continue;
						auto *cw2 = m_ctx.Get<Widget>(cid);
						auto *cl2 = m_ctx.Get<LayoutProps>(cid);
						if (!cw2 || !cl2 || !Has(cw2->behavior, BehaviorFlag::Visible)) continue;
						if (cl2->attach == AttachLayout::Absolute || cl2->attach == AttachLayout::Fixed) {
							_Measure(cid, cc);
							continue;
						}
						flow.push_back(cid);
						float g = growOf(cl2);
						tGrow += g;
						// Count margins for ALL flow children — matches _Place tFixed logic.
						if (isCol) tFixed += cl2->margin.top  + cl2->margin.bottom;
						else       tFixed += cl2->margin.left + cl2->margin.right;
						if (g == 0.f) {
							FPoint csz = _Measure(cid, cc);
							if (isCol) {
								chW = SDL::Max(chW, csz.x + cl2->margin.left + cl2->margin.right);
								tFixed += csz.y;
							} else {
								chH = SDL::Max(chH, csz.y + cl2->margin.top + cl2->margin.bottom);
								tFixed += csz.x;
							}
						}
					}

					int fvis    = (int)flow.size();
					float avail   = isCol ? cH : cW;
					float gBudget = SDL::Max(0.f, avail - tFixed - lp->gap * SDL::Max(0, fvis - 1));
					float gUnit   = (tGrow > 0.f) ? gBudget / tGrow : 0.f;

					for (int i = 0; i < fvis; ++i) {
						ECS::EntityId cid = flow[i];
						auto *cl2  = m_ctx.Get<LayoutProps>(cid);
						auto *ccr2 = m_ctx.Get<ComputedRect>(cid);
						float g = growOf(cl2);

						if (g > 0.f) {
							float growSz = SDL::Max(0.f, gUnit * g);
							LayoutContext growCtx = cc;
							if (isCol) growCtx.parentSize.y = growSz;
							else       growCtx.parentSize.x = growSz;
							_Measure(cid, growCtx);
							// Store grow-resolved size so Ph/Pw in children resolve correctly.
							if (isCol) ccr2->measured.y = growSz;
							else       ccr2->measured.x = growSz;
						}

						float mw = ccr2->measured.x + cl2->margin.left + cl2->margin.right;
						float mh = ccr2->measured.y + cl2->margin.top  + cl2->margin.bottom;
						if (isCol) {
							chW  = SDL::Max(chW, mw);
							chH += mh + (i > 0 ? lp->gap : 0.f);
						} else {
							chH  = SDL::Max(chH, mh);
							chW += mw + (i > 0 ? lp->gap : 0.f);
						}
					}
					vis = fvis;

				} else {
					for (ECS::EntityId cid : ch->ids) {
						if (!m_ctx.IsAlive(cid)) continue;
						auto *cw  = m_ctx.Get<Widget>(cid);
						auto *cl  = m_ctx.Get<LayoutProps>(cid);
						if (!cw || !cl || !Has(cw->behavior, BehaviorFlag::Visible)) continue;

						if (cl->attach == AttachLayout::Absolute || cl->attach == AttachLayout::Fixed) {
							_Measure(cid, cc);
							continue;
						}

						FPoint csz = _Measure(cid, cc);
						float mw = csz.x + cl->margin.left + cl->margin.right;
						float mh = csz.y + cl->margin.top  + cl->margin.bottom;

						if (lp->layout == Layout::Stack) {
							float flowW = mw;
							if (lineVis > 0 && cW > 0.f && curLineW + lp->gap + flowW > cW) {
								chW = SDL::Max(chW, curLineW);
								chH += curLineH + lp->gap;
								curLineW = flowW;
								curLineH = mh;
								lineVis = 1;
							} else {
								curLineW += flowW + (lineVis > 0 ? lp->gap : 0.f);
								curLineH = SDL::Max(curLineH, mh);
								lineVis++;
							}
						}
						++vis;
					}
					if (lp->layout == Layout::Stack && lineVis > 0) {
						chW = SDL::Max(chW, curLineW);
						chH += curLineH;
					}
				}

				// ── InGrid: post-loop track computation ──────────────────────────────
				if (lp->layout == Layout::InGrid) {
					auto *gp     = m_ctx.Get<LayoutGridProps>(e);
					int numCols  = gp ? SDL::Max(1, gp->columns) : 2;
					float gap    = lp->gap;

					// Determine effective number of rows (explicit or auto from children)
					int numRows  = gp ? gp->rows : 0;
					if (numRows <= 0) {
						int autoIdx = 0;
						for (ECS::EntityId cid : ch->ids) {
							if (!m_ctx.IsAlive(cid)) continue;
							auto *cw2 = m_ctx.Get<Widget>(cid);
							auto *cl  = m_ctx.Get<LayoutProps>(cid);
							if (!cw2 || !cl || !Has(cw2->behavior, BehaviorFlag::Visible)) continue;
							if (cl->attach == AttachLayout::Absolute || cl->attach == AttachLayout::Fixed) continue;
							auto *gc  = m_ctx.Get<GridCell>(cid);
							int r = gc ? gc->row : (autoIdx / numCols);
							int rs = gc ? SDL::Max(1, gc->rowSpan) : 1;
							numRows = SDL::Max(numRows, r + rs);
							if (!gc) ++autoIdx;
						}
						numRows = SDL::Max(1, numRows);
					}

					GridSizing cSiz = gp ? gp->colSizing : GridSizing::Fixed;
					GridSizing rSiz = gp ? gp->rowSizing : GridSizing::Fixed;
					float baseCellW = SDL::Max(1.f, (cW - gap * (float)(numCols - 1)) / (float)numCols);
					float baseCellH = (!ha && numRows > 0) ? SDL::Max(1.f, (cH - gap * (float)(numRows - 1)) / (float)numRows) : 0.f;

					std::vector<float> colW(numCols, cSiz == GridSizing::Fixed ? baseCellW : 0.f);
					std::vector<float> rowH(numRows, (rSiz == GridSizing::Fixed && baseCellH > 0.f) ? baseCellH : 0.f);

					// Content-based sizing: derive track sizes from already-measured children
					if (cSiz == GridSizing::Content || rSiz == GridSizing::Content || baseCellH == 0.f) {
						int autoIdx = 0;
						for (ECS::EntityId cid : ch->ids) {
							if (!m_ctx.IsAlive(cid)) continue;
							auto *cw2 = m_ctx.Get<Widget>(cid);
							auto *cl  = m_ctx.Get<LayoutProps>(cid);
							auto *gc  = m_ctx.Get<GridCell>(cid);
							auto *ccr = m_ctx.Get<ComputedRect>(cid);
							if (!cw2 || !cl || !ccr || !Has(cw2->behavior, BehaviorFlag::Visible)) { if (!gc) ++autoIdx; continue; }
							if (cl->attach == AttachLayout::Absolute || cl->attach == AttachLayout::Fixed) continue;

							int c  = gc ? SDL::Clamp(gc->col, 0, numCols - 1) : ((autoIdx % numCols));
							int r  = gc ? SDL::Clamp(gc->row, 0, numRows - 1) : ((autoIdx / numCols));
							int cs = gc ? SDL::Max(1, gc->colSpan) : 1;
							int rs = gc ? SDL::Max(1, gc->rowSpan) : 1;
							if (!gc) ++autoIdx;

							if (cSiz == GridSizing::Content) {
								float childW = ccr->measured.x + cl->margin.left + cl->margin.right;
								float perCol = SDL::Max(0.f, (childW - gap * (float)(cs - 1)) / (float)cs);
								for (int ci = c; ci < SDL::Min(c + cs, numCols); ++ci)
									colW[ci] = SDL::Max(colW[ci], perCol);
							}
							if (rSiz == GridSizing::Content || baseCellH == 0.f) {
								float childH = ccr->measured.y + cl->margin.top + cl->margin.bottom;
								float perRow = SDL::Max(0.f, (childH - gap * (float)(rs - 1)) / (float)rs);
								for (int ri = r; ri < SDL::Min(r + rs, numRows); ++ri)
									rowH[ri] = SDL::Max(rowH[ri], perRow);
							}
						}
					}

					// Store computed tracks for use in _Place and _DrawContainer
					if (gp) {
						gp->colWidths  = colW;
						gp->rowHeights = rowH;
					}

					// Total grid content size
					chW = 0.f;
					for (int ci = 0; ci < numCols; ++ci)
						chW += colW[ci] + (ci > 0 ? gap : 0.f);
					chH = 0.f;
					for (int ri = 0; ri < numRows; ++ri)
						chH += rowH[ri] + (ri > 0 ? gap : 0.f);
				}
				// ── end InGrid ───────────────────────────────────────────────────────

				// Ne pas écraser le contenu déjà calculé pour ListBox / TextArea
				if (w->type != WidgetType::ListBox && w->type != WidgetType::TextArea) {
					lp->contentW = chW;
					lp->contentH = chH;
				}
			}

			float bW = wa ? SDL::Max(intr.x, chW) + lp->padding.left + lp->padding.right : fw;
			float bH = ha ? SDL::Max(intr.y, chH) + lp->padding.top  + lp->padding.bottom : fh;
			// Expander/TabView: the header/tab bar replaces padding.top — height is header + children + bottom padding.
			if (ha) {
				if (w->type == WidgetType::Expander) {
					if (auto *exd = m_ctx.Get<ExpanderData>(e))
						bH = exd->headerH + chH + lp->padding.bottom;
				} else if (w->type == WidgetType::TabView) {
					if (auto *tvd = m_ctx.Get<TabViewData>(e))
						bH = tvd->tabHeight + chH + lp->padding.bottom;
				}
			}

			// Apply min/max size constraints (Px(-1) = no constraint).
			// Convention CSS : maxWidth appliqué d'abord, puis minWidth prime sur maxWidth.
			float rMinW = lp->minWidth.Resolve(ctx);
			float rMinH = lp->minHeight.Resolve(ctx);
			float rMaxW = lp->maxWidth.Resolve(ctx);
			float rMaxH = lp->maxHeight.Resolve(ctx);
			if (rMaxW >= 0.f) bW = SDL::Min(bW, rMaxW);
			if (rMaxH >= 0.f) bH = SDL::Min(bH, rMaxH);
			if (rMinW >= 0.f) bW = SDL::Max(bW, rMinW);
			if (rMinH >= 0.f) bH = SDL::Max(bH, rMinH);

			cr->measured = {bW, bH};
			return cr->measured;
		}

		[[nodiscard]] FPoint _IntrinsicSize(ECS::EntityId e) {
			auto *w = m_ctx.Get<Widget>(e);
			if (!w) return {};
			float ch = _TH(e);
			switch (w->type) {
			case WidgetType::Label:
			case WidgetType::Button: {
				auto *c = m_ctx.Get<EditableContent>(e);
				if (!c || c->text.empty())
					return {60.f, ch + 4.f};
				return {_TW(c->text, e), ch + 4.f};
			}
			case WidgetType::Toggle:
				return {80.f, 28.f};
			case WidgetType::RadioButton:
				return {80.f, 24.f};
			case WidgetType::Slider:
				return {80.f, 24.f};
			case WidgetType::ScrollBar:
				return {10.f, 80.f};
			case WidgetType::Input: {
				// Numeric Input adds two stacked ↑/↓ arrows on the right
				// edge — reserve their width here so a row layout sizes the
				// widget correctly and the text area is not squeezed.
				const float arrowsW = m_ctx.Get<NumericValue>(e) ? 20.f : 0.f;
				return {80.f + arrowsW, SDL::Max(30.f, ch + 8.f)};
			}
			case WidgetType::TextArea:
				return {160.f, SDL::Max(80.f, ch * 4.f + 8.f)};
			case WidgetType::ListBox:
				return {160.f, SDL::Max(80.f, ch * 4.f + 8.f)};
			case WidgetType::Graph:
				return {200.f, 120.f};
			case WidgetType::Progress:
				return {80.f, 18.f};
			case WidgetType::Separator:
				return {0.f, 1.f};
			case WidgetType::Knob:
				return {56.f, 56.f};
			case WidgetType::ComboBox:
				return {120.f, SDL::Max(28.f, ch + 8.f)};
			case WidgetType::TabView: {
				auto *tvd = m_ctx.Get<TabViewData>(e);
				return {200.f, tvd ? tvd->tabHeight : 32.f};
			}
			case WidgetType::Expander: {
				auto *exd = m_ctx.Get<ExpanderData>(e);
				return {120.f, exd ? exd->headerH : 28.f};
			}
			case WidgetType::Splitter:
				return {200.f, 200.f};
			case WidgetType::Spinner:
				return {32.f, 32.f};
			case WidgetType::Badge:
				return {SDL::Max(20.f, _TW("0", e) + 10.f), SDL::Max(18.f, ch + 4.f)};
			case WidgetType::ColorPicker:
				return {220.f, 240.f};
			case WidgetType::Popup: {
				auto *pd = m_ctx.Get<PopupData>(e);
				return {320.f, 240.f + (pd ? pd->headerH : 28.f)};
			}
			case WidgetType::Tree:
				return {160.f, SDL::Max(80.f, ch * 4.f + 8.f)};
			case WidgetType::MenuBar:
				return {400.f, ch + 8.f};
			default:
				return {};
			}
		}

		void _Place(ECS::EntityId e) {
			if (!m_ctx.IsAlive(e)) return; 

			auto *w  = m_ctx.Get<Widget>(e);
			auto *lp = m_ctx.Get<LayoutProps>(e);
			auto *cr = m_ctx.Get<ComputedRect>(e);
			auto *ch = m_ctx.Get<Children>(e);
			if (!w || !lp || !cr || !ch || ch->ids.empty())
				return;

			const FRect &self = cr->screen;
			float cw       = self.w - lp->padding.left - lp->padding.right;
			// Expander/TabView: children start below the header/tab bar, not at padding.top.
			float topInset = lp->padding.top;
			if (w->type == WidgetType::Expander) {
				if (auto *exd = m_ctx.Get<ExpanderData>(e)) topInset = exd->headerH;
			} else if (w->type == WidgetType::TabView) {
				if (auto *tvd = m_ctx.Get<TabViewData>(e); tvd && !tvd->tabsBottom) topInset = tvd->tabHeight;
			} else if (w->type == WidgetType::Popup) {
				if (auto *pd = m_ctx.Get<PopupData>(e)) topInset = pd->headerH;
			}
			float ch2 = SDL::Max(0.f, self.h - topInset - lp->padding.bottom);

			// Réserver la place des scrollbars inline pour les containers.
			if (w->type == WidgetType::Container || w->type == WidgetType::ListBox || w->type == WidgetType::TextArea || w->type == WidgetType::Tree) {
				bool showX = false, showY = false;
				_ContainerScrollbars(*w, *lp, cw, ch2, showX, showY);
				if (showY) cw  = SDL::Max(0.f, cw  - lp->scrollbarThickness);
				if (showX) ch2 = SDL::Max(0.f, ch2 - lp->scrollbarThickness);
			}

			// Splitter: place first child at ratio, second at 1-ratio.
			if (w->type == WidgetType::Splitter) {
				auto *spl = m_ctx.Get<SplitterData>(e);
				if (spl && ch->ids.size() >= 2) {
					bool horiz = (spl->orientation == Orientation::Horizontal);
					float ox = self.x + lp->padding.left;
					float oy = self.y + lp->padding.top;
					float first  = horiz ? cw * spl->ratio  : ch2 * spl->ratio;
					float second = horiz ? cw - first - spl->handleSize : ch2 - first - spl->handleSize;
					second = SDL::Max(0.f, second);
					// Child 0
					if (m_ctx.IsAlive(ch->ids[0])) {
						auto *cc0 = m_ctx.Get<ComputedRect>(ch->ids[0]);
						if (cc0) {
							cc0->screen = horiz
								? FRect{ox, oy, first, ch2}
								: FRect{ox, oy, cw, first};
							_Place(ch->ids[0]);
						}
					}
					// Child 1
					if (m_ctx.IsAlive(ch->ids[1])) {
						auto *cc1 = m_ctx.Get<ComputedRect>(ch->ids[1]);
						if (cc1) {
							cc1->screen = horiz
								? FRect{ox + first + spl->handleSize, oy, second, ch2}
								: FRect{ox, oy + first + spl->handleSize, cw, second};
							_Place(ch->ids[1]);
						}
					}
				}
				return;
			}

			// Point de départ du flux, décalé par le scroll.
			float cx  = self.x + lp->padding.left - lp->scrollX;
			float cy  = self.y + topInset         - lp->scrollY;

			// First pass: compute grow budget over flow children only.
			// Placer les éléments Absolute/Fixed en priorité pour les sortir du flux
			for (ECS::EntityId cid : ch->ids) {
				if (!m_ctx.IsAlive(cid)) continue; 
				auto *cw2 = m_ctx.Get<Widget>(cid);
				auto *cl  = m_ctx.Get<LayoutProps>(cid);
				auto *cc  = m_ctx.Get<ComputedRect>(cid);
				if (!cw2 || !cl || !cc || !Has(cw2->behavior, BehaviorFlag::Visible)) continue;

				if (cl->attach == AttachLayout::Absolute || cl->attach == AttachLayout::Fixed) {
					float ox = (cl->attach == AttachLayout::Fixed) ? 0.f : self.x;
					float oy = (cl->attach == AttachLayout::Fixed) ? 0.f : self.y;
					
					LayoutContext absCtx;
					absCtx.windowSize     = {m_viewport.w, m_viewport.h};
					absCtx.rootSize       = {m_viewport.w, m_viewport.h};
					absCtx.rootFontSize   = m_defaultFontSize > 0.f ? m_defaultFontSize : (float)SDL::DEBUG_TEXT_FONT_CHARACTER_SIZE;
					absCtx.parentSize     = {self.w, self.h};
					absCtx.parentPadding  = lp->padding;
					absCtx.parentFontSize = absCtx.rootFontSize;

					cc->screen = {ox + cl->absX.Resolve(absCtx), oy + cl->absY.Resolve(absCtx), cc->measured.x, cc->measured.y};
					_Place(cid);
				}
			}

			if (lp->layout == Layout::InColumn || lp->layout == Layout::InLine) {
				const bool isColP = (lp->layout == Layout::InColumn);
				auto growOfP = [&](const LayoutProps* cl) -> float {
					return 0.01f * (isColP ? (cl->height.IsGrow() ? cl->height.val : 0.f)
								  : (cl->width.IsGrow() ? cl->width.val : 0.f));
				};
				std::vector<ECS::EntityId> flowChildren;
				float tFixed = 0.f, tGrow = 0.f;

				// 1. Collecte et détermination de l'espace alloué
				for (ECS::EntityId cid : ch->ids) {
					if (!m_ctx.IsAlive(cid)) continue;
					auto *cw2 = m_ctx.Get<Widget>(cid);
					auto *cl  = m_ctx.Get<LayoutProps>(cid);
					auto *cc  = m_ctx.Get<ComputedRect>(cid);
					if (!cw2 || !cl || !cc || !Has(cw2->behavior, BehaviorFlag::Visible)) continue;
					if (cl->attach == AttachLayout::Absolute || cl->attach == AttachLayout::Fixed) continue;

					flowChildren.push_back(cid);
					float g = growOfP(cl);
					tGrow += g;
					if (lp->layout == Layout::InColumn) {
						tFixed += cl->margin.top + cl->margin.bottom;
						if (g == 0.f) tFixed += cc->measured.y;
					} else {
						tFixed += cl->margin.left + cl->margin.right;
						if (g == 0.f) tFixed += cc->measured.x;
					}
				}

				int vis = (int)flowChildren.size();
				float avail   = (lp->layout == Layout::InColumn) ? ch2 : cw;
				float gBudget = SDL::Max(0.f, avail - tFixed - lp->gap * SDL::Max(0, vis - 1));
				float gUnit   = (tGrow > 0.f) ? gBudget / tGrow : 0.f;

				// 2. Trouver le dernier élément qui grandit pour fermer la brèche exactement
				int lastGrowIdx = -1;
				for (int i = 0; i < vis; ++i) {
					auto *cl = m_ctx.Get<LayoutProps>(flowChildren[i]);
					if (growOfP(cl) > 0.f) lastGrowIdx = i;
				}

				std::vector<FRect> computed(vis);

				// 3. Pré-calcul de l'axe principal et application de l'étirement (Stretch) sur l'axe croisé
				for (int i = 0; i < vis; ++i) {
					ECS::EntityId cid = flowChildren[i];
					auto *cl  = m_ctx.Get<LayoutProps>(cid);
					auto *cc  = m_ctx.Get<ComputedRect>(cid);

					float childW = cc->measured.x;
					float childH = cc->measured.y;
					float g = growOfP(cl);

					if (g > 0.f) {
						if (lp->layout == Layout::InColumn) childH = gUnit * g;
						else                                childW = gUnit * g;
					}
					
					if (lp->layout == Layout::InColumn) {
						if (cl->alignSelfH == Align::Stretch)
							childW = SDL::Max(0.f, cw - cl->margin.left - cl->margin.right);
					} else {
						if (cl->alignSelfV == Align::Stretch)
							childH = SDL::Max(0.f, ch2 - cl->margin.top - cl->margin.bottom);
					}
					computed[i] = {0.f, 0.f, childW, childH};
				}

				float currentX = cx;
				float currentY = cy;
				float rightX = cx + cw;
				float bottomY = cy + ch2;

				// 4. Placement BI-DIRECTIONNEL sur l'axe principal
				if (lastGrowIdx == -1) {
					// Aucun grow : placement standard de gauche à droite / haut en bas
					for (int i = 0; i < vis; ++i) {
						auto *cl = m_ctx.Get<LayoutProps>(flowChildren[i]);
						if (lp->layout == Layout::InColumn) {
							computed[i].y = currentY + cl->margin.top;
							currentY += computed[i].h + cl->margin.top + cl->margin.bottom + lp->gap;
						} else {
							computed[i].x = currentX + cl->margin.left;
							currentX += computed[i].w + cl->margin.left + cl->margin.right + lp->gap;
						}
					}
				} else {
					// Placement depuis la Droite / Bas (éléments APRES le dernier grow)
					for (int i = vis - 1; i > lastGrowIdx; --i) {
						auto *cl = m_ctx.Get<LayoutProps>(flowChildren[i]);
						if (lp->layout == Layout::InColumn) {
							bottomY -= cl->margin.bottom;
							bottomY -= computed[i].h;
							computed[i].y = bottomY;
							bottomY -= (cl->margin.top + lp->gap);
						} else {
							rightX -= cl->margin.right;
							rightX -= computed[i].w;
							computed[i].x = rightX;
							rightX -= (cl->margin.left + lp->gap);
						}
					}
					
					// Placement depuis la Gauche / Haut (éléments AVANT et JUSQU'AU dernier grow)
					for (int i = 0; i <= lastGrowIdx; ++i) {
						auto *cl = m_ctx.Get<LayoutProps>(flowChildren[i]);
						if (lp->layout == Layout::InColumn) {
							computed[i].y = currentY + cl->margin.top;
							
							// Comble exactement le trou restant pour effacer les erreurs de float
							if (i == lastGrowIdx) 
								computed[i].h = SDL::Max(0.f, bottomY - computed[i].y - cl->margin.bottom);
							
							currentY += computed[i].h + cl->margin.top + cl->margin.bottom + lp->gap;
						} else {
							computed[i].x = currentX + cl->margin.left;
							
							// Comble exactement le trou restant
							if (i == lastGrowIdx) 
								computed[i].w = SDL::Max(0.f, rightX - computed[i].x - cl->margin.right);
							
							currentX += computed[i].w + cl->margin.left + cl->margin.right + lp->gap;
						}
					}
				}

				// 5. Application de l'alignement de l'axe secondaire et appel récursif
				for (int i = 0; i < vis; ++i) {
					ECS::EntityId cid = flowChildren[i];
					auto *cl  = m_ctx.Get<LayoutProps>(cid);
					auto *cc  = m_ctx.Get<ComputedRect>(cid);
					
					if (lp->layout == Layout::InColumn) {
						float px = cx + cl->margin.left;
						switch (cl->alignSelfH) {
							case Align::Center:  px = cx + (cw - computed[i].w) * 0.5f; break;
							case Align::End:     px = cx + cw - computed[i].w - cl->margin.right; break;
							default: break;
						}
						computed[i].x = px;
					} else {
						float py = cy + cl->margin.top;
						switch (cl->alignSelfV) {
							case Align::Center:  py = cy + (ch2 - computed[i].h) * 0.5f; break;
							case Align::End:     py = cy + ch2 - computed[i].h - cl->margin.bottom; break;
							default: break;
						}
						computed[i].y = py;
					}
					
					cc->screen = computed[i];
					_Place(cid);
				}
			} else if (lp->layout == Layout::Stack) {
				float startX = cx;
				size_t i = 0;
				while (i < ch->ids.size()) {
					size_t j = i;
					float rowFixedW = 0.f, rowGrow = 0.f, rowMaxH = 0.f;
					int rowItems = 0;

					// Chercher le nombre d'éléments qui tiennent sur cette ligne
					while (j < ch->ids.size()) {
						ECS::EntityId cid = ch->ids[j];
						if (!m_ctx.IsAlive(cid)) { j++; continue; } 
						auto *cw2 = m_ctx.Get<Widget>(cid);
						auto *cl  = m_ctx.Get<LayoutProps>(cid);
						auto *cc  = m_ctx.Get<ComputedRect>(cid);
						
						if (!cw2 || !cl || !cc || !Has(cw2->behavior, BehaviorFlag::Visible) || cl->attach == AttachLayout::Absolute || cl->attach == AttachLayout::Fixed) { 
							j++; continue; 
						}
						
						float itemGrow   = cl->width.IsGrow() ? 0.01f * cl->width.val : 0.f;
						float itemFixedW = cl->margin.left + cl->margin.right + (itemGrow == 0.f ? cc->measured.x : 0.f);
						float itemH = cc->measured.y + cl->margin.top + cl->margin.bottom;

						if (rowItems > 0 && cw > 0.f && rowFixedW + lp->gap + itemFixedW > cw) {
							break; // Wrap ! On passe à la ligne suivante
						}

						rowFixedW += itemFixedW + (rowItems > 0 ? lp->gap : 0.f);
						rowGrow += itemGrow;
						rowMaxH = SDL::Max(rowMaxH, itemH);
						rowItems++;
						j++;
					}

					if (rowItems == 0) { i++; continue; }

					float gBudget = SDL::Max(0.f, cw - rowFixedW);
					float gUnit   = (rowGrow > 0.f) ? gBudget / rowGrow : 0.f;

					// Placer et aligner les éléments sur la ligne trouvée
					bool firstInRow = true;
					for (size_t k = i; k < j; ++k) {
						ECS::EntityId cid = ch->ids[k]; 
						if (!m_ctx.IsAlive(cid)) continue;
						auto *cw2 = m_ctx.Get<Widget>(cid);
						auto *cl  = m_ctx.Get<LayoutProps>(cid);
						auto *cc  = m_ctx.Get<ComputedRect>(cid);
						
						if (!cw2 || !cl || !cc || !Has(cw2->behavior, BehaviorFlag::Visible) || cl->attach == AttachLayout::Absolute || cl->attach == AttachLayout::Fixed) continue;

						float childW = cc->measured.x, childH = cc->measured.y;
						float cg = cl->width.IsGrow() ? 0.01f * cl->width.val : 0.f;
						if (cg > 0.f) childW = gUnit * cg;

						if (!firstInRow) cx += lp->gap;
						firstInRow = false;

						float px = cx + cl->margin.left;
						float py = cy + cl->margin.top;

						switch (cl->alignSelfV) {
							case Align::Stretch: childH = SDL::Max(0.f, rowMaxH - cl->margin.top - cl->margin.bottom); [[fallthrough]];
							case Align::Start:   break;
							case Align::Center:  py = cy + (rowMaxH - childH) * 0.5f; break;
							case Align::End:     py = cy + rowMaxH - childH - cl->margin.bottom; break;
						}

						cc->screen = {px, py, childW, childH};
						cx += childW + cl->margin.left + cl->margin.right;
						_Place(cid);
					}

					cx = startX;
					cy += rowMaxH + lp->gap;
					i = j;
				}
			} else if (lp->layout == Layout::InGrid) {
				// ── InGrid placement ────────────────────────────────────────────────
				auto *gp    = m_ctx.Get<LayoutGridProps>(e);
				int numCols = gp ? SDL::Max(1, gp->columns) : 2;

				// Rebuild row count (same logic as _Measure)
				int numRows = gp ? gp->rows : 0;
				if (numRows <= 0) {
					int autoIdx = 0;
					for (ECS::EntityId cid : ch->ids) {
						if (!m_ctx.IsAlive(cid)) continue;
						auto *cw2 = m_ctx.Get<Widget>(cid);
						auto *cl  = m_ctx.Get<LayoutProps>(cid);
						if (!cw2 || !cl || !Has(cw2->behavior, BehaviorFlag::Visible)) continue;
						if (cl->attach == AttachLayout::Absolute || cl->attach == AttachLayout::Fixed) continue;
						auto *gc  = m_ctx.Get<GridCell>(cid);
						int r  = gc ? gc->row : (autoIdx / numCols);
						int rs = gc ? SDL::Max(1, gc->rowSpan) : 1;
						numRows = SDL::Max(numRows, r + rs);
						if (!gc) ++autoIdx;
					}
					numRows = SDL::Max(1, numRows);
				}

				// Retrieve precomputed track sizes (fallback to uniform if not available)
				float gap = lp->gap;
				const std::vector<float> *colWidths  = (gp && (int)gp->colWidths.size()  == numCols) ? &gp->colWidths  : nullptr;
				const std::vector<float> *rowHeights = (gp && (int)gp->rowHeights.size() == numRows) ? &gp->rowHeights : nullptr;

				float uniformColW = SDL::Max(1.f, (cw - gap * (float)(numCols - 1)) / (float)numCols);
				float uniformRowH = SDL::Max(1.f, (ch2 - gap * (float)(numRows - 1)) / (float)numRows);

				// Pre-compute cumulative column X and row Y origins (relative to content origin)
				std::vector<float> colX(numCols), rowY(numRows);
				float accX = 0.f;
				for (int ci = 0; ci < numCols; ++ci) {
					colX[ci] = accX;
					accX += (colWidths ? (*colWidths)[ci] : uniformColW) + gap;
				}
				float accY = 0.f;
				for (int ri = 0; ri < numRows; ++ri) {
					rowY[ri] = accY;
					accY += (rowHeights ? (*rowHeights)[ri] : uniformRowH) + gap;
				}

				// Place each flow child at its grid cell
				int autoIdx = 0;
				for (ECS::EntityId cid : ch->ids) {
					if (!m_ctx.IsAlive(cid)) continue;
					auto *cw2 = m_ctx.Get<Widget>(cid);
					auto *cl  = m_ctx.Get<LayoutProps>(cid);
					auto *cc  = m_ctx.Get<ComputedRect>(cid);
					if (!cw2 || !cl || !cc || !Has(cw2->behavior, BehaviorFlag::Visible)) continue;
					if (cl->attach == AttachLayout::Absolute || cl->attach == AttachLayout::Fixed) continue;

					auto *gc  = m_ctx.Get<GridCell>(cid);
					int c  = gc ? SDL::Clamp(gc->col, 0, numCols - 1) : (autoIdx % numCols);
					int r  = gc ? SDL::Clamp(gc->row, 0, numRows - 1) : (autoIdx / numCols);
					int cs = gc ? SDL::Max(1, gc->colSpan) : 1;
					int rs = gc ? SDL::Max(1, gc->rowSpan) : 1;
					if (!gc) ++autoIdx;

					// Compute span pixel dimensions
					float cellW = 0.f;
					for (int ci = c; ci < SDL::Min(c + cs, numCols); ++ci)
						cellW += (colWidths ? (*colWidths)[ci] : uniformColW) + (ci > c ? gap : 0.f);
					float cellH = 0.f;
					for (int ri = r; ri < SDL::Min(r + rs, numRows); ++ri)
						cellH += (rowHeights ? (*rowHeights)[ri] : uniformRowH) + (ri > r ? gap : 0.f);

					// Child size: Stretch fills the cell, other alignments keep measured size
					float childW = cc->measured.x, childH = cc->measured.y;
					if (cl->alignSelfH == Align::Stretch)
						childW = SDL::Max(0.f, cellW - cl->margin.left - cl->margin.right);
					if (cl->alignSelfV == Align::Stretch)
						childH = SDL::Max(0.f, cellH - cl->margin.top  - cl->margin.bottom);

					float px = cx + colX[c] + cl->margin.left;
					float py = cy + rowY[r] + cl->margin.top;

					// Alignment inside cell (non-Stretch cases)
					switch (cl->alignSelfH) {
						case Align::Center: px = cx + colX[c] + (cellW - childW) * 0.5f; break;
						case Align::End:    px = cx + colX[c] + cellW - childW - cl->margin.right; break;
						default: break;
					}
					switch (cl->alignSelfV) {
						case Align::Center: py = cy + rowY[r] + (cellH - childH) * 0.5f; break;
						case Align::End:    py = cy + rowY[r] + cellH - childH - cl->margin.bottom; break;
						default: break;
					}

					cc->screen = {px, py, childW, childH};
					_Place(cid);
				}
			}

			if (w->type == WidgetType::Container) {
				float maxContentX = 0.f;
				float maxContentY = 0.f;

				for (ECS::EntityId cid : ch->ids) {
					if (!m_ctx.IsAlive(cid)) continue;
					auto *cw2 = m_ctx.Get<Widget>(cid);
					auto *cc  = m_ctx.Get<ComputedRect>(cid);
					auto *cl  = m_ctx.Get<LayoutProps>(cid);
					if (!cw2 || !cc || !cl || !Has(cw2->behavior, BehaviorFlag::Visible)) continue;
					if (cl->attach == AttachLayout::Fixed) continue; // Fixed ne scrolle pas

					// On calcule la position de l'enfant relativement au coin (0,0) du contenu
					// On rajoute lp->scrollX/Y car cr->screen est déjà décalé par l'affichage
					float childRelativeRight  = (cc->screen.x + cc->screen.w + cl->margin.right)  - (self.x + lp->padding.left) + lp->scrollX;
					float childRelativeBottom = (cc->screen.y + cc->screen.h + cl->margin.bottom) - (self.y + lp->padding.top)  + lp->scrollY;

					maxContentX = SDL::Max(maxContentX, childRelativeRight);
					maxContentY = SDL::Max(maxContentY, childRelativeBottom);
				}

				lp->contentW = maxContentX;
				lp->contentH = maxContentY;
			}
		}

		void _UpdateClips(ECS::EntityId e, FRect parentClip) {
			if (!m_ctx.IsAlive(e)) return; 
			
			auto *w   = m_ctx.Get<Widget>(e);
			auto *lp  = m_ctx.Get<LayoutProps>(e);
			auto *s   = m_ctx.Get<Style>(e);
			auto *cr  = m_ctx.Get<ComputedRect>(e);
			if (!w || !lp || !cr) return;

			// 1. Calcul du clip de base du widget lui-même
			cr->clip = cr->screen.GetIntersection(parentClip);
			cr->outer_clip = cr->clip.Extend(s->borders);

			FRect childClip = cr->clip; 
			
			// 2. Si c'est un container, la zone allouée aux enfants est plus petite
			if (w->type == WidgetType::Container || w->type == WidgetType::ListBox || w->type == WidgetType::TextArea || w->type == WidgetType::Tree) {
				float innerW = cr->screen.w - lp->padding.left - lp->padding.right;
				float innerH = cr->screen.h - lp->padding.top  - lp->padding.bottom;
				
				// On vérifie quelles scrollbars sont physiquement présentes
				bool showX = false, showY = false;
				_ContainerScrollbars(*w, *lp, innerW, innerH, showX, showY);

				// On soustrait les paddings ET l'épaisseur des scrollbars
				childClip = cr->screen;
				childClip.x += lp->padding.left;
				childClip.y += lp->padding.top;
				childClip.w = SDL::Round(innerW - (showY ? lp->scrollbarThickness : 0.f));
				childClip.h = SDL::Round(innerH - (showX ? lp->scrollbarThickness : 0.f));
				
				// On restreint au clip du parent
				childClip = childClip.GetIntersection(parentClip);
			} else if (w->type == WidgetType::Expander) {
				if (auto *exd = m_ctx.Get<ExpanderData>(e)) {
					childClip = cr->screen;
					childClip.y += exd->headerH + s->borders.GetH();
					childClip.h = SDL::Max(0.f, childClip.h - exd->headerH);
					childClip = childClip.GetIntersection(parentClip);
				}
			} else if (w->type == WidgetType::TabView) {
				if (auto *tvd = m_ctx.Get<TabViewData>(e)) {
					childClip = cr->screen;
					if (!tvd->tabsBottom) {
						childClip.y += tvd->tabHeight + s->borders.GetH();
						childClip.h = SDL::Max(0.f, childClip.h - tvd->tabHeight);
					} else {
						childClip.h = SDL::Max(0.f, childClip.h - tvd->tabHeight);
					}
					childClip = childClip.GetIntersection(parentClip);
				}
			}

			auto *ch = m_ctx.Get<Children>(e);
			if (ch) {
				for (ECS::EntityId c : ch->ids) {
					_UpdateClips(c, childClip);
				}
			}
		}

		// ── Input ─────────────────────────────────────────────────────────────────────
		
		/// Convertit une position pixel X (relative à la zone de texte) en index de caractère pour Input
		[[nodiscard]] int _InputHitPos(const EditableContent *c, float px, ECS::EntityId e) {
			if (!c || c->text.empty() || px <= 0.f) return 0;

			// Use prefix-width measurement to match exactly how the cursor is drawn
			// (_TW on the full prefix, including kerning between all characters).
			float bestDist = px; // distance from x=0 at i=0
			int   bestIdx  = 0;

			for (int i = 1; i <= (int)c->text.size(); ++i) {
				float xOff = _TW(c->text.substr(0, (size_t)i), e);
				float dist = SDL::Abs(px - xOff);
				if (dist < bestDist) { bestDist = dist; bestIdx = i; }
				if (xOff > px + 40.f) break; // early exit when far past the click
			}
			return bestIdx;
		}

		void _ProcessInput() {
			// ── Scrollbars inline des containers (drag en cours) ──────────────────
			// On traite le drag des thumbs de container AVANT le hit-test normal
			// car la souris peut sortir du thumb pendant le drag.
			_UpdateContainerScrollDrags();

			ECS::EntityId nh = _HitTest(m_root, m_mousePos);

			// Reset all hover flags.
			m_ctx.Each<WidgetState>([](ECS::EntityId, WidgetState &s) {
				s.wasHovered = s.hovered;
				s.hovered    = false;
			});

			// Only mark as hovered if the widget actually supports it.
			if (nh != ECS::NullEntity && m_ctx.IsAlive(nh)) { 
				auto *w = m_ctx.Get<Widget>(nh);
				if (w && Has(w->behavior, BehaviorFlag::Enable) && Has(w->behavior, BehaviorFlag::Hoverable)) {
					m_ctx.Get<WidgetState>(nh)->hovered = true;
				} else {
					nh = ECS::NullEntity;
				}
			}

			// Hover enter / leave callbacks + hover sound.
			if (nh != m_hovered) {
				if (m_hovered != ECS::NullEntity && m_ctx.IsAlive(m_hovered)) { 
					auto *cb = m_ctx.Get<Callbacks>(m_hovered);
					if (cb && cb->onHoverLeave) cb->onHoverLeave();
				}
				if (nh != ECS::NullEntity && m_ctx.IsAlive(nh)) { 
					auto *cb = m_ctx.Get<Callbacks>(nh);
					if (cb && cb->onHoverEnter) cb->onHoverEnter();
					
					auto *s = m_ctx.Get<Style>(nh);
					if (s && !s->hoverSound.empty())
						if (auto sh = _EnsureAudio(s->hoverSound))
							_PlayAudio(sh);
				}
				m_hovered = nh;
			}

			// ── Press ─────────────────────────────────────────────────────────────
			if (m_mousePressed) {
				// MenuBar dropdown intercept — must run before normal hit-test.
				if (_CheckMenuBarOverlayPress()) goto end_press;
				// ComboBox overlay intercept — must run before normal hit-test.
				if (_CheckComboOverlayPress()) goto end_press;
				// Vérifier d'abord si le clic tombe sur un thumb de scrollbar inline.
				// Si oui, on initie le drag et on consomme l'événement.
				if (!_TryBeginContainerScrollDrag()) {
					if (m_hovered == ECS::NullEntity)
						_SetFocus(ECS::NullEntity);

					if (m_hovered != ECS::NullEntity && m_ctx.IsAlive(m_hovered)) {
						auto *pw = m_ctx.Get<Widget>(m_hovered);
						if (pw && Has(pw->behavior, BehaviorFlag::Enable) && Has(pw->behavior, BehaviorFlag::Selectable)) {
							// ── Comptage des clics (double-clic) ─────────────────────────
							if (m_hovered == m_lastClickEntity && m_timeSinceLastClick < 0.4f)
								++m_clickCount;
							else {
								m_clickCount = 1;
								m_lastClickEntity = m_hovered;
							}
							m_timeSinceLastClick = 0.f;

							m_pressed = m_hovered;
							if (auto *st = m_ctx.Get<WidgetState>(m_pressed)) st->pressed = true;

							bool wantFocus = Has(pw->behavior, BehaviorFlag::Focusable);
							_SetFocus(wantFocus ? m_pressed : ECS::NullEntity);

							// Begin drag for interactive controls.
							if (pw->type == WidgetType::Slider) {
								auto *sd = m_ctx.Get<SliderData>(m_pressed);
								auto *cr2 = m_ctx.Get<ComputedRect>(m_pressed);
								auto *lp2 = m_ctx.Get<LayoutProps>(m_pressed);
								if (sd && cr2 && lp2) {
									sd->drag = true;
									bool h = (sd->orientation == Orientation::Horizontal);
									sd->dragStartPos = h ? m_mousePos.x : m_mousePos.y;
									// Jump thumb to click position immediately
									float nv;
									if (h) {
										float tx = cr2->screen.x + lp2->padding.left;
										float tw = cr2->screen.w - lp2->padding.left - lp2->padding.right;
										nv = SDL::Clamp(sd->min + (m_mousePos.x - tx) / SDL::Max(1.f, tw) * (sd->max - sd->min), sd->min, sd->max);
									} else {
										float ty = cr2->screen.y + lp2->padding.top;
										float th = cr2->screen.h - lp2->padding.top - lp2->padding.bottom;
										nv = SDL::Clamp(sd->min + (1.f - (m_mousePos.y - ty) / SDL::Max(1.f, th)) * (sd->max - sd->min), sd->min, sd->max);
									}
									if (nv != sd->val) {
										sd->val = nv;
										auto *cb2 = m_ctx.Get<Callbacks>(m_pressed);
										if (cb2 && cb2->onChange) cb2->onChange(nv);
									}
									sd->dragStartVal = sd->val;
								}
							} else if (pw->type == WidgetType::ScrollBar) {
								if (auto *sb = m_ctx.Get<ScrollBarData>(m_pressed)) {
									sb->drag        = true;
									sb->dragStartPos = (sb->orientation == Orientation::Vertical)
														 ? m_mousePos.y : m_mousePos.x;
									sb->dragStartOff = sb->offset;
								}
							} else if (pw->type == WidgetType::Knob) {
								auto *kd  = m_ctx.Get<KnobData>(m_pressed);
								auto *cr2 = m_ctx.Get<ComputedRect>(m_pressed);
								if (kd && cr2) {
									kd->drag         = true;
									kd->dragStartY   = m_mousePos.y;
									kd->dragStartVal = kd->val;
									float cx_ = cr2->screen.x + cr2->screen.w * 0.5f;
									float cy_ = cr2->screen.y + cr2->screen.h * 0.5f;
									kd->dragStartAngle = SDL::Atan2(m_mousePos.y - cy_, m_mousePos.x - cx_) * (180.f / SDL::PI_F);
								}
							} else if (pw->type == WidgetType::ListBox) {
								if (auto *lb2 = m_ctx.Get<ListBoxData>(m_pressed); lb2 && lb2->reorderable) {
									auto *cr2 = m_ctx.Get<ComputedRect>(m_pressed);
									auto *lp2 = m_ctx.Get<LayoutProps>(m_pressed);
									if (cr2 && lp2) {
										float iy = cr2->screen.y + lp2->padding.top;
										int   idx = (int)((m_mousePos.y - iy + lp2->scrollY) / lb2->itemHeight);
										if (idx >= 0 && idx < (int)lb2->items.size()) {
											lb2->dragSrcIdx  = idx;
											lb2->dragDstIdx  = idx;
											lb2->dragY       = m_mousePos.y;
											lb2->dragStartY  = m_mousePos.y;
											lb2->dragMoved   = false;
											lb2->dragActive  = false; // activated after threshold
										}
									}
								}
							} else if (pw->type == WidgetType::Splitter) {
								if (auto *spl = m_ctx.Get<SplitterData>(m_pressed)) {
									spl->dragging = true;
									spl->dragStart   = (spl->orientation == Orientation::Horizontal)
										? m_mousePos.x : m_mousePos.y;
									spl->dragRatio = spl->ratio;
								}
							} else if (pw->type == WidgetType::TextArea || pw->type == WidgetType::Input) {
								// Numeric Input: check for arrow-button click before
								// falling through to text edit click handling.
								bool consumed = false;
								if (pw->type == WidgetType::Input) {
									auto *d  = m_ctx.Get<InputData>(m_pressed);
									auto *nv = m_ctx.Get<NumericValue>(m_pressed);
									if (d && nv) {
										auto *cr2 = m_ctx.Get<ComputedRect>(m_pressed);
										auto *st2 = m_ctx.Get<Style>(m_pressed);
										if (cr2 && st2) {
											const float bw = 20.f;
											const float br = SDL::Max(0.f, st2->borders.right);
											const float bt = SDL::Max(0.f, st2->borders.top);
											const float bb = SDL::Max(0.f, st2->borders.bottom);
											const float innerH = SDL::Max(0.f, cr2->screen.h - bt - bb);
											const float upH    = std::floor(innerH * 0.5f);
											const float downH  = innerH - upH;
											FRect up   = {cr2->screen.x + cr2->screen.w - bw - br,
											              cr2->screen.y + bt, bw, upH};
											FRect down = {up.x, up.y + up.h, bw, downH};
											auto bump = [&](double dir){
												double nx = std::clamp(nv->val + dir * nv->step, nv->min, nv->max);
												if (nv->isIntegral()) nx = std::round(nx);
												nv->val = nx;
												if (auto *c = m_ctx.Get<EditableContent>(m_pressed))
													c->text = _FormatNumeric(*nv);
												if (auto *cb = m_ctx.Get<Callbacks>(m_pressed); cb && cb->onChange)
													cb->onChange((float)nv->val);
											};
											if (up.Contains(m_mousePos))   { d->pressUp   = true; bump(+1.0); consumed = true; }
											else if (down.Contains(m_mousePos)) { d->pressDown = true; bump(-1.0); consumed = true; }
										}
									}
								}
								if (!consumed) {
									if (m_clickCount >= 2)
										_HandleMultiClick(m_pressed, m_clickCount);
									else
										_ProcessTextClickOrDrag(m_pressed, false);
								}
							} else if (pw->type == WidgetType::ColorPicker) {
								auto *cr2 = m_ctx.Get<ComputedRect>(m_pressed);
								if (auto *cp = m_ctx.Get<ColorPickerData>(m_pressed); cp && cr2) {
									cp->dragging = true;
									_UpdateColorPicker(m_pressed, cr2->screen, cp, m_mousePos);
								}
							} else if (pw->type == WidgetType::Popup) {
								if (auto *pd = m_ctx.Get<PopupData>(m_pressed)) {
									auto *cr2 = m_ctx.Get<ComputedRect>(m_pressed);
									if (cr2) {
										FRect hdrR    = {cr2->screen.x, cr2->screen.y, cr2->screen.w, pd->headerH};
										FRect resizeR = {cr2->screen.x + cr2->screen.w - 16.f,
										                 cr2->screen.y + cr2->screen.h - 16.f, 16.f, 16.f};
										pd->pressPos = m_mousePos;
										if (pd->draggable && hdrR.Contains(m_mousePos)) {
											pd->dragging   = true;
											pd->dragOffset = {m_mousePos.x - cr2->screen.x,
											                   m_mousePos.y - cr2->screen.y};
										} else if (pd->resizable && resizeR.Contains(m_mousePos)) {
											pd->resizing        = true;
											pd->resizeStart     = m_mousePos;
											pd->resizeStartSize = {cr2->screen.w, cr2->screen.h};
										}
									}
								}
							}
						}
					}
				}
			}
			end_press:;

			// ── Drag ──────────────────────────────────────────────────────────────
			if (m_mouseDown && m_pressed != ECS::NullEntity && m_ctx.IsAlive(m_pressed)) { 
				// TextArea drag-selection and Input drag-cursor-move can continue even if the mouse goes out of the widget bounds, so we check them before the hit-test of the current mouse position.
				{
					auto *pw2 = m_ctx.Get<Widget>(m_pressed);
					if (pw2 && (pw2->type == WidgetType::TextArea || pw2->type == WidgetType::Input)) {
						_ProcessTextClickOrDrag(m_pressed, true); // true = c'est un drag
					}
				}
				auto *pw = m_ctx.Get<Widget>(m_pressed);
				if (pw) {
					if (pw->type == WidgetType::Slider) {
						if (auto *sd = m_ctx.Get<SliderData>(m_pressed); sd && sd->drag) {
							auto *cr = m_ctx.Get<ComputedRect>(m_pressed);
							auto *lp = m_ctx.Get<LayoutProps>(m_pressed);
							if (cr && lp) {
								bool h = (sd->orientation == Orientation::Horizontal);
								float tl = h
									? (cr->screen.w - lp->padding.left - lp->padding.right - 16.f)
									: (cr->screen.h - lp->padding.top  - lp->padding.bottom - 16.f);
								if (tl > 0.f) {
									float cur = h ? m_mousePos.x : m_mousePos.y;
									float dx  = cur - sd->dragStartPos;
									float nv  = SDL::Clamp(
										sd->dragStartVal + dx / tl * (sd->max - sd->min),
										sd->min, sd->max);
									if (nv != sd->val) {
										sd->val = nv;
										auto *cb = m_ctx.Get<Callbacks>(m_pressed);
										if (cb && cb->onChange) cb->onChange(nv);
									}
								}
							}
						}
					} else if (pw->type == WidgetType::ScrollBar) {
						if (auto *sb = m_ctx.Get<ScrollBarData>(m_pressed); sb && sb->drag) {
							bool v    = (sb->orientation == Orientation::Vertical);
							float cur = v ? m_mousePos.y : m_mousePos.x;
							float dx  = cur - sb->dragStartPos;
							float ratio = (sb->viewSize > 0.f && sb->contentSize > sb->viewSize)
											? sb->viewSize / sb->contentSize : 1.f;
							float maxO = SDL::Max(0.f, sb->contentSize - sb->viewSize);
							float doff = (ratio > 0.f) ? dx / ratio : 0.f;
							float noff = SDL::Clamp(sb->dragStartOff + doff, 0.f, maxO);
							if (noff != sb->offset) {
								sb->offset = noff;
								auto *cb = m_ctx.Get<Callbacks>(m_pressed);
								if (cb && cb->onScroll) cb->onScroll(noff);
								if (cb && cb->onChange) cb->onChange(noff);
							}
						}
					} else if (pw->type == WidgetType::Knob) {
						auto *kd  = m_ctx.Get<KnobData>(m_pressed);
						auto *cr2 = m_ctx.Get<ComputedRect>(m_pressed);
						if (kd && kd->drag && cr2) {
							float range = SDL::Max(0.0001f, kd->max - kd->min);
							float cx_ = cr2->screen.x + cr2->screen.w * 0.5f;
							float cy_ = cr2->screen.y + cr2->screen.h * 0.5f;
							float curAngle = SDL::Atan2(m_mousePos.y - cy_, m_mousePos.x - cx_) * (180.f / SDL::PI_F);
							// Incremental angle delta with wrap-around handling
							float dAngle = curAngle - kd->dragStartAngle;
							if (dAngle >  180.f) dAngle -= 360.f;
							if (dAngle < -180.f) dAngle += 360.f;
							float nv = SDL::Clamp(kd->dragStartVal + dAngle / 270.f * range, kd->min, kd->max);
							if (nv != kd->val) {
								kd->val = nv;
								auto *cb = m_ctx.Get<Callbacks>(m_pressed);
								if (cb && cb->onChange) cb->onChange(nv);
							}
							// Update start for next frame (incremental)
							kd->dragStartAngle = curAngle;
							kd->dragStartVal   = kd->val;
						}
						
					} else if (pw->type == WidgetType::Splitter) {
						if (auto *spl = m_ctx.Get<SplitterData>(m_pressed); spl && spl->dragging) {
							auto *cr = m_ctx.Get<ComputedRect>(m_pressed);
							if (cr) {
								bool horiz = (spl->orientation == Orientation::Horizontal);
								float cur   = horiz ? m_mousePos.x : m_mousePos.y;
								float total = horiz ? cr->screen.w  : cr->screen.h;
								float start = horiz ? cr->screen.x  : cr->screen.y;
								float nr = SDL::Clamp((cur - start) / SDL::Max(1.f, total), spl->minRatio, spl->maxRatio);
								if (nr != spl->ratio) {
									spl->ratio = nr;
									auto *cb2 = m_ctx.Get<Callbacks>(m_pressed);
									if (cb2 && cb2->onChange) cb2->onChange(nr);
									m_layoutDirty = true;
								}
							}
						}
					} else if (pw->type == WidgetType::ColorPicker) {
						if (auto *cp = m_ctx.Get<ColorPickerData>(m_pressed); cp && cp->dragging) {
							auto *cr = m_ctx.Get<ComputedRect>(m_pressed);
							if (cr) _UpdateColorPicker(m_pressed, cr->screen, cp, m_mousePos);
						}
					} else if (pw->type == WidgetType::Popup) {
						if (auto *pd = m_ctx.Get<PopupData>(m_pressed)) {
							if (pd->dragging) {
								auto *lp = m_ctx.Get<LayoutProps>(m_pressed);
								if (lp) {
									lp->absX = Value::Px(m_mousePos.x - pd->dragOffset.x);
									lp->absY = Value::Px(m_mousePos.y - pd->dragOffset.y);
									m_layoutDirty = true;
								}
							} else if (pd->resizing) {
								auto *lp = m_ctx.Get<LayoutProps>(m_pressed);
								if (lp) {
									float nw = pd->resizeStartSize.x + (m_mousePos.x - pd->resizeStart.x);
									float nh = pd->resizeStartSize.y + (m_mousePos.y - pd->resizeStart.y);
									lp->width  = Value::Px(SDL::Max(100.f, nw));
									lp->height = Value::Px(SDL::Max(pd->headerH + 20.f, nh));
									m_layoutDirty = true;
								}
							}
						}
					} else if (pw->type == WidgetType::ListBox) {
						if (auto *lb2 = m_ctx.Get<ListBoxData>(m_pressed); lb2 && lb2->reorderable && lb2->dragSrcIdx >= 0) {
							auto *cr2 = m_ctx.Get<ComputedRect>(m_pressed);
							auto *lp2 = m_ctx.Get<LayoutProps>(m_pressed);
							if (cr2 && lp2) {
								float dy = m_mousePos.y - lb2->dragStartY;
								if (!lb2->dragMoved && dy * dy > 16.f)
									lb2->dragMoved = lb2->dragActive = true;
								if (lb2->dragActive) {
									lb2->dragY = m_mousePos.y;
									float iy = cr2->screen.y + lp2->padding.top;
									int dst = (int)((m_mousePos.y - iy + lp2->scrollY + lb2->itemHeight * 0.5f) / lb2->itemHeight);
									lb2->dragDstIdx = SDL::Clamp(dst, 0, (int)lb2->items.size() - 1);
								}
							}
						}
					} else if (pw->type == WidgetType::Tree) {
						if (auto *css = m_ctx.Get<ContainerScrollState>(m_pressed); css && css->dragY) {
							auto *d  = m_ctx.Get<TreeData>(m_pressed);
							auto *lp = m_ctx.Get<LayoutProps>(m_pressed);
							auto *cr = m_ctx.Get<ComputedRect>(m_pressed);
							if (d && lp && cr) {
								float viewH  = cr->screen.h - lp->padding.top - lp->padding.bottom;
								float totalH = (float)_GetVisibleTreeNodes(d->nodes).size() * d->itemHeight;
								d->scrollY = _ApplyScrollDrag(viewH, totalH, lp->scrollbarThickness,
								                              css->dragStartY_, css->dragStartOffY,
								                              m_mousePos.y);
							}
						}
					}
				}
			}

			// ── Release ───────────────────────────────────────────────────────────
			if (m_mouseReleased) {
				// Relâcher les drags de scrollbars inline.
				_EndContainerScrollDrags();

				if (m_pressed != ECS::NullEntity && m_ctx.IsAlive(m_pressed)) { 
					if (auto *st = m_ctx.Get<WidgetState>(m_pressed))
						st->pressed = false;
					if (m_pressed == m_hovered) {
						auto *pw = m_ctx.Get<Widget>(m_pressed);
						// Suppress click for reorderable ListBox that was actively dragged.
						bool suppressClick = false;
						if (pw && pw->type == WidgetType::ListBox)
							if (auto *lb2 = m_ctx.Get<ListBoxData>(m_pressed))
								suppressClick = lb2->dragActive;
						if (pw && !suppressClick
						       && Has(pw->behavior, BehaviorFlag::Enable)
							   && Has(pw->behavior, BehaviorFlag::Selectable))
							_OnClick(m_pressed, *pw);
					}
					if (auto *sd  = m_ctx.Get<SliderData>(m_pressed))    sd->drag  = false;
					if (auto *sb  = m_ctx.Get<ScrollBarData>(m_pressed)) sb->drag  = false;
					if (auto *kd  = m_ctx.Get<KnobData>(m_pressed))      kd->drag  = false;
					if (auto *sp  = m_ctx.Get<InputData>(m_pressed)) {
						sp->drag      = false;
						sp->pressUp   = false;
						sp->pressDown = false;
					}
					if (auto *spl = m_ctx.Get<SplitterData>(m_pressed))  spl->dragging = false;
					if (auto *ta  = m_ctx.Get<TextAreaData>(m_pressed))  ta->selectDragging = false;
					if (auto *cp  = m_ctx.Get<ColorPickerData>(m_pressed)) { cp->dragging = false; cp->draggingHue = false; }
					if (auto *pd  = m_ctx.Get<PopupData>(m_pressed))     { pd->dragging = false; pd->resizing = false; }
					if (auto *lb2 = m_ctx.Get<ListBoxData>(m_pressed)) {
						if (lb2->dragActive && lb2->dragSrcIdx >= 0
						    && lb2->dragDstIdx != lb2->dragSrcIdx
						    && lb2->dragDstIdx >= 0 && lb2->dragDstIdx < (int)lb2->items.size()) {
							int src = lb2->dragSrcIdx, dst = lb2->dragDstIdx;
							auto it = lb2->items.begin();
							std::string item = lb2->items[(size_t)src];
							lb2->items.erase(it + src);
							lb2->items.insert(lb2->items.begin() + dst, item);
							lb2->selectedIndex = dst;
							if (lb2->onReorder) lb2->onReorder(src, dst);
							auto *cb2 = m_ctx.Get<Callbacks>(m_pressed);
							if (cb2 && cb2->onChange) cb2->onChange((float)dst);
						}
						lb2->dragActive = false;
						lb2->dragMoved  = false;
						lb2->dragSrcIdx = -1;
					}
					m_pressed = ECS::NullEntity;
				}
				if (auto *c = m_ctx.Get<EditableContent>(m_pressed)) {
					c->selectDragging = false;
				}
			}
		}

		// ── Helpers : drag des scrollbars inline de containers ────────────────────

		/// Parcourt tous les containers avec ContainerScrollState et met à jour
		/// scrollX/Y en fonction du déplacement de la souris (si drag en cours).
		void _UpdateContainerScrollDrags() {
			if (!m_mouseDown) return;
			m_ctx.Each<ContainerScrollState>([this](ECS::EntityId e, ContainerScrollState &css) {
				auto *lp = m_ctx.Get<LayoutProps>(e);
				auto *cr = m_ctx.Get<ComputedRect>(e);
				auto *w  = m_ctx.Get<Widget>(e);
				if (!lp || !cr || !w) return;

				const float innerW = cr->screen.w - lp->padding.left - lp->padding.right;
				const float innerH = cr->screen.h - lp->padding.top  - lp->padding.bottom;
				bool showX = false, showY = false;
				_ContainerScrollbars(*w, *lp, innerW, innerH, showX, showY);
				const float viewW = showY ? SDL::Max(0.f, innerW - lp->scrollbarThickness) : innerW;
				const float viewH = showX ? SDL::Max(0.f, innerH - lp->scrollbarThickness) : innerH;

				if (css.dragY && showY && lp->contentH > viewH) {
					lp->scrollY = _ApplyScrollDrag(viewH, lp->contentH, lp->scrollbarThickness,
					                               css.dragStartY_, css.dragStartOffY, m_mousePos.y);
					auto *cb = m_ctx.Get<Callbacks>(e);
					if (cb && cb->onScroll) cb->onScroll(lp->scrollY);
				}
				if (css.dragX && showX && lp->contentW > viewW) {
					lp->scrollX = _ApplyScrollDrag(viewW, lp->contentW, lp->scrollbarThickness,
					                               css.dragStartX, css.dragStartOff, m_mousePos.x);
					auto *cb = m_ctx.Get<Callbacks>(e);
					if (cb && cb->onScroll) cb->onScroll(lp->scrollX);
				}
			});
		}

		/// Teste si le clic courant tombe sur le thumb d'une scrollbar inline ;
		/// si oui, initialise le drag et retourne true (l'événement est consommé).
		bool _TryBeginContainerScrollDrag() {
			bool consumed = false;
			m_ctx.Each<ContainerScrollState>([this, &consumed](ECS::EntityId e, ContainerScrollState &css) {
				if (consumed) return;
				auto *lp = m_ctx.Get<LayoutProps>(e);
				if (!lp) return;

				if (css.thumbY.w > 0.f && css.thumbY.h > 0.f && css.thumbY.Contains(m_mousePos)) {
					css.dragY        = true;
					css.dragStartY_  = m_mousePos.y;
					css.dragStartOffY = lp->scrollY;
					consumed = true;
					return;
				}
				if (css.thumbX.w > 0.f && css.thumbX.h > 0.f && css.thumbX.Contains(m_mousePos)) {
					css.dragX       = true;
					css.dragStartX  = m_mousePos.x;
					css.dragStartOff = lp->scrollX;
					consumed = true;
				}
			});
			return consumed;
		}

		/// Relâche tous les drags de scrollbars inline.
		void _EndContainerScrollDrags() {
			m_ctx.Each<ContainerScrollState>([](ECS::EntityId, ContainerScrollState &css) {
				css.dragX = false;
				css.dragY = false;
			});
		}

		void _OnClick(ECS::EntityId e, const Widget &w) {
			auto *s = m_ctx.Get<Style>(e);
			if (s && !s->clickSound.empty())
				if (auto sh = _EnsureAudio(s->clickSound))
					_PlayAudio(sh);
			auto *cb = m_ctx.Get<Callbacks>(e);
			switch (w.type) {
				case WidgetType::Toggle:
					if (auto *t = m_ctx.Get<ToggleData>(e)) {
						t->checked = !t->checked;
						if (cb && cb->onToggle)
							cb->onToggle(t->checked);
						if (cb && cb->onClick)
							cb->onClick();
					}
					break;
				case WidgetType::RadioButton:
					if (auto *r = m_ctx.Get<RadioData>(e); r && !r->checked) {
						m_ctx.Each<RadioData>([&](ECS::EntityId eid, RadioData &rd) { if(rd.group==r->group) rd.checked=(eid==e); });
						if (cb && cb->onToggle)
							cb->onToggle(true);
						if (cb && cb->onClick)
							cb->onClick();
					}
					break;
				case WidgetType::ListBox: 
					_OnClickListBox(e);
					break;
				case WidgetType::ComboBox:
					_OnClickComboBox(e);
					break;
				case WidgetType::TabView:
					_OnClickTabView(e);
					break;
				case WidgetType::Expander:
					_OnClickExpander(e);
					break;
				case WidgetType::Popup: {
					auto *pd = m_ctx.Get<PopupData>(e);
					auto *cr = m_ctx.Get<ComputedRect>(e);
					if (!pd || !cr) break;
					// Ignore if the mouse actually moved (drag release, not a click)
					{
						float dx = m_mousePos.x - pd->pressPos.x;
						float dy = m_mousePos.y - pd->pressPos.y;
						if (dx*dx + dy*dy > 16.f) break; // > 4px threshold
					}
					// Close button
					if (pd->closable) {
						float hs = pd->headerH;
						FRect closeR = {cr->screen.x + cr->screen.w - hs, cr->screen.y, hs, hs};
						if (closeR.Contains(m_mousePos)) {
							pd->open = false;
							SetVisible(e, false);
							if (cb && cb->onClick) cb->onClick();
							break;
						}
					}
					// Custom header buttons
					float btnX = cr->screen.x + cr->screen.w - pd->headerH * (pd->closable ? 2.f : 1.f);
					for (int i = (int)pd->headerButtons.size() - 1; i >= 0; --i) {
						FRect btnR = {btnX - pd->headerH, cr->screen.y, pd->headerH, pd->headerH};
						if (btnR.Contains(m_mousePos)) {
							if (pd->headerButtons[i].onClick) pd->headerButtons[i].onClick();
							break;
						}
						btnX -= pd->headerH;
					}
					if (cb && cb->onClick) cb->onClick();
					break;
				}
				case WidgetType::Tree:
					_OnClickTree(e);
					break;
				case WidgetType::MenuBar:
					_OnClickMenuBar(e);
					break;
				default:
					if (cb && cb->onClick)
						cb->onClick();
					break;
			}
			// Fire onDoubleClick on every widget type when click count >= 2.
			if (m_clickCount >= 2 && cb && cb->onDoubleClick)
				cb->onDoubleClick();
		}

		void _SetFocus(ECS::EntityId nf) {
			if (nf == m_focused)
				return;
			if (m_focused != ECS::NullEntity && m_ctx.IsAlive(m_focused)) {
				if (auto *st = m_ctx.Get<WidgetState>(m_focused))
					st->focused = false;
				// Numeric Input: on blur, reformat text from the canonical value
				// (commits any in-progress edit that was strictly valid).
				if (auto *w2 = m_ctx.Get<Widget>(m_focused);
				    w2 && w2->type == WidgetType::Input) {
					auto *id = m_ctx.Get<InputData>(m_focused);
					auto *nv = m_ctx.Get<NumericValue>(m_focused);
					auto *c  = m_ctx.Get<EditableContent>(m_focused);
					if (id && nv && c) {
						double prev = nv->val;
						if (_ParseNumeric(*nv, id->type, c->text) && nv->val != prev)
							if (auto *cb2 = m_ctx.Get<Callbacks>(m_focused); cb2 && cb2->onChange)
								cb2->onChange((float)nv->val);
						c->text = _FormatNumeric(*nv);
						c->cursor = (int)c->text.size();
						c->ClearSelection();
					}
				}
				auto *cb = m_ctx.Get<Callbacks>(m_focused);
				if (cb && cb->onFocusLose)
					cb->onFocusLose();
			}
			m_focused = nf;
			if (m_focused != ECS::NullEntity && m_ctx.IsAlive(m_focused)) {
				if (auto *st = m_ctx.Get<WidgetState>(m_focused))
					st->focused = true;
				// On focus-gain of a numeric Input, prime the text from val.
				if (auto *w2 = m_ctx.Get<Widget>(m_focused);
				    w2 && w2->type == WidgetType::Input) {
					auto *nv = m_ctx.Get<NumericValue>(m_focused);
					auto *c  = m_ctx.Get<EditableContent>(m_focused);
					if (nv && c) {
						c->text = _FormatNumeric(*nv);
						c->cursor = (int)c->text.size();
						c->ClearSelection();
					}
				}
				auto *cb = m_ctx.Get<Callbacks>(m_focused);
				if (cb && cb->onFocusGain)
					cb->onFocusGain();
			}
		}

		[[nodiscard]] ECS::EntityId _HitTest(ECS::EntityId e, FPoint p) const {
			if (!m_ctx.IsAlive(e)) 
				return ECS::NullEntity;
			auto *w = m_ctx.Get<Widget>(e);
			auto *cr = m_ctx.Get<ComputedRect>(e);
			if (!w || !cr || !Has(w->behavior, BehaviorFlag::Visible))
				return ECS::NullEntity;
			if (!cr->clip.Contains(p))
				return ECS::NullEntity;
			auto *ch = m_ctx.Get<Children>(e);
			if (ch)
				for (int i = (int)ch->ids.size() - 1; i >= 0; --i) {
					ECS::EntityId h = _HitTest(ch->ids[i], p);
					if (h != ECS::NullEntity)
						return h;
				}
			return cr->screen.Contains(p) ? e : ECS::NullEntity;
		}

		void _HandleTextInput(const char *txt) {
			if (m_focused == ECS::NullEntity || !m_ctx.IsAlive(m_focused)) return;
			auto *w = m_ctx.Get<Widget>(m_focused);
			if (!w || !Has(w->behavior, BehaviorFlag::Enable | BehaviorFlag::Focusable)) return;

			// Filter against the InputType regex if a typed-Input filter is set.
			std::string_view tv{txt};
			if (w->type == WidgetType::Input) {
				if (auto *id = m_ctx.Get<InputData>(m_focused);
				    id && id->type != InputType::Text) {
					if (auto *c = m_ctx.Get<EditableContent>(m_focused)) {
						// Build the prospective text (replacing selection if any).
						std::string prospective = c->text;
						int a = c->HasSelection() ? c->SelMin() : c->cursor;
						int b = c->HasSelection() ? c->SelMax() : c->cursor;
						a = std::clamp(a, 0, (int)prospective.size());
						b = std::clamp(b, 0, (int)prospective.size());
						prospective.replace(a, b - a, txt);
						if (!std::regex_match(prospective, _GetInputRegex(id->type)))
							return; // reject the keystroke
					}
				}
			}

			auto processInput = [&]<typename T>(T* data) {
				if (data) {
					_InsertText(data, tv);
					if (auto *cb = m_ctx.Get<Callbacks>(m_focused); cb && cb->onTextChange)
						cb->onTextChange(data->text);
				}
			};

			if (w->type == WidgetType::Input) {
				auto *c = m_ctx.Get<EditableContent>(m_focused);
				processInput(c);
				// For numeric Input, try to parse and sync val + onChange.
				auto *id = m_ctx.Get<InputData>(m_focused);
				auto *nv = m_ctx.Get<NumericValue>(m_focused);
				if (id && nv && c) {
					double prev = nv->val;
					if (_ParseNumeric(*nv, id->type, c->text)) {
						if (nv->val != prev)
							if (auto *cb = m_ctx.Get<Callbacks>(m_focused); cb && cb->onChange)
								cb->onChange((float)nv->val);
					}
				}
			}
			else if (w->type == WidgetType::TextArea) {
				auto* ta = m_ctx.Get<TextAreaData>(m_focused);
				if (ta && !ta->readOnly) processInput(ta);
			}
		}

		void _HandleKeyDownInput(SDL::Keycode k, SDL::Keymod mod) {
			auto *c = m_ctx.Get<EditableContent>(m_focused);
			if (!c) return;

			if (k == SDL::KEYCODE_ESCAPE) { _SetFocus(ECS::NullEntity); return; }

			// Numeric Input: arrow keys nudge by step; Enter commits and blurs.
			{
				auto *id = m_ctx.Get<InputData>(m_focused);
				auto *nv = m_ctx.Get<NumericValue>(m_focused);
				if (id && nv) {
					if (k == SDL::KEYCODE_UP || k == SDL::KEYCODE_DOWN) {
						double prev = nv->val;
						double delta = (k == SDL::KEYCODE_UP) ? nv->step : -nv->step;
						nv->val = std::clamp(nv->val + delta, nv->min, nv->max);
						if (nv->isIntegral()) nv->val = std::round(nv->val);
						c->text = _FormatNumeric(*nv);
						c->cursor = (int)c->text.size();
						c->ClearSelection();
						if (nv->val != prev)
							if (auto *cb = m_ctx.Get<Callbacks>(m_focused); cb && cb->onChange)
								cb->onChange((float)nv->val);
						return;
					}
					if (k == SDL::KEYCODE_RETURN || k == SDL::KEYCODE_RETURN2
					    || k == SDL::KEYCODE_KP_ENTER) {
						double prev = nv->val;
						if (_ParseNumeric(*nv, id->type, c->text) && nv->val != prev)
							if (auto *cb = m_ctx.Get<Callbacks>(m_focused); cb && cb->onChange)
								cb->onChange((float)nv->val);
						c->text = _FormatNumeric(*nv);
						c->cursor = (int)c->text.size();
						_SetFocus(ECS::NullEntity);
						return;
					}
				}
			}

			_HandleCommonTextKeys(c, k, mod, m_ctx.Get<Callbacks>(m_focused));

			// After common-key edits (backspace, paste, etc.), re-validate numeric.
			{
				auto *id = m_ctx.Get<InputData>(m_focused);
				auto *nv = m_ctx.Get<NumericValue>(m_focused);
				if (id && nv) {
					double prev = nv->val;
					if (_ParseNumeric(*nv, id->type, c->text) && nv->val != prev)
						if (auto *cb = m_ctx.Get<Callbacks>(m_focused); cb && cb->onChange)
							cb->onChange((float)nv->val);
				}
			}
		}

		void _HandleKeyDownTextArea(SDL::Keycode k, SDL::Keymod mod) {
			auto *ta = m_ctx.Get<TextAreaData>(m_focused);
			auto *cb = m_ctx.Get<Callbacks>(m_focused);
			if (!ta) return;

			if (k == SDL::KEYCODE_ESCAPE) { _SetFocus(ECS::NullEntity); return; }

			const bool editable = !ta->readOnly;

			// Si le Helper a géré une touche standard (Copier, Flèches, Suppr...), on s'arrête
			if (_HandleCommonTextKeys(ta, k, mod, cb, editable)) return;

			// Sinon, on traite les spécificités multilignes du TextArea
			const bool ctrl  = (mod & SDL::KMOD_CTRL)  != 0;
			const bool shift = (mod & SDL::KMOD_SHIFT) != 0;

			switch (k) { 
				case SDL::KEYCODE_TAB:
					if (ctrl) break; // Laisse passer pour basculer le focus du widget
					if (!editable) return;
					if (shift) {
						int ls = ta->LineStart(ta->LineOf(ta->cursorPos));
						int spaces = 0;
						while (ls + spaces < (int)ta->text.size() && ta->text[ls + spaces] == ' ' && spaces < ta->tabSize) ++spaces;
						if (spaces > 0) {
							ta->text.erase(ls, spaces);
							ta->_ShiftSpans(ls, -spaces);
							ta->cursorPos = std::max(ls, ta->cursorPos - spaces);
							ta->ClearSelection();
							if (cb && cb->onTextChange) cb->onTextChange(ta->text);
						}
					} else {
						_InsertText(ta, "\t");
						if (cb && cb->onTextChange) cb->onTextChange(ta->text);
					}
					return;

				case SDL::KEYCODE_RETURN:
				case SDL::KEYCODE_RETURN2:
				case SDL::KEYCODE_KP_ENTER:
					if (!editable) return;
					_InsertText(ta, "\n");
					if (cb && cb->onTextChange) cb->onTextChange(ta->text);
					return;

				case SDL::KEYCODE_UP: {
					int line = ta->LineOf(ta->cursorPos);
					if (line > 0) {
						int col = ta->ColOf(ta->cursorPos);
						int ns = ta->LineStart(line - 1);
						int ne = ta->LineEnd(line - 1);
						_MoveTextCursor(ta, std::min(ns + col, ne), shift);
					}
					return;
				}
				case SDL::KEYCODE_DOWN: {
					int line = ta->LineOf(ta->cursorPos);
					if (line < ta->LineCount() - 1) {
						int col = ta->ColOf(ta->cursorPos);
						int ns = ta->LineStart(line + 1);
						int ne = ta->LineEnd(line + 1);
						_MoveTextCursor(ta, std::min(ns + col, ne), shift);
					}
					return;
				}

				case SDL::KEYCODE_PAGEUP: {
					float lineH = _TH(m_focused) + 2.f;
					int linesPerPage = SDL::Max(1, (int)(_TextAreaViewH() / lineH));
					int line = SDL::Max(0, ta->LineOf(ta->cursorPos) - linesPerPage);
					_MoveTextCursor(ta, ta->LineStart(line), shift);
					return;
				}
				case SDL::KEYCODE_PAGEDOWN: {
					float lineH = _TH(m_focused) + 2.f;
					int linesPerPage = SDL::Max(1, (int)(_TextAreaViewH() / lineH));
					int line = SDL::Min(ta->LineCount() - 1, ta->LineOf(ta->cursorPos) + linesPerPage);
					_MoveTextCursor(ta, ta->LineStart(line), shift);
					return;
				}
				case SDL::KEYCODE_Z:
					if (ctrl && !shift) ta->ClearSelection(); // Undo not implemented
					return;

				default: break;
			}
		}

		// TextArea helpers ──────────────────────────────────────────────────────────

		/// View height of the focused TextArea (0 if not a TextArea).
		[[nodiscard]] float _TextAreaViewH() const noexcept {
			if (m_focused == ECS::NullEntity || !m_ctx.IsAlive(m_focused)) return 0.f; 
			auto *cr = m_ctx.Get<ComputedRect>(m_focused);
			auto *lp = m_ctx.Get<LayoutProps>(m_focused);
			if (!cr || !lp) return 0.f;
			return SDL::Max(0.f, cr->screen.h - lp->padding.top - lp->padding.bottom);
		}

		/// Pixel X offset for a column within a line (tabs expanded).
		[[nodiscard]] float _TextAreaLineX(const TextAreaData *ta, int line, int col, ECS::EntityId e) {
			int lineStart = ta->LineStart(line);
			col = std::clamp(col, 0, ta->LineEnd(line) - lineStart);

			// Measure space width for tab expansion (matches the renderer).
			auto rf = _ResolveFont(e);
			float charW = (rf.size > 0.f) ? rf.size : (float)SDL::DEBUG_TEXT_FONT_CHARACTER_SIZE;
#if UI_HAS_TTF
			if (!rf.isDebug && !rf.key.empty() && rf.size > 0.f) {
				if (auto font = _EnsureFont(rf.key, rf.size)) {
					int sw = 0, sh = 0;
					font.GetStringSize(" ", &sw, &sh);
					charW = (float)sw;
				}
			}
#endif
			// Accumulate non-tab characters into runs and measure each run with _TW,
			// exactly as the renderer does — this includes inter-character kerning.
			float x = 0.f;
			int   colCount = 0;
			std::string run;
			for (int i = 0; i < col; ++i) {
				char ch = ta->text[lineStart + i];
				if (ch == '\t') {
					if (!run.empty()) { x += _TW(run, e); run.clear(); }
					int spaces = ta->tabSize - (colCount % ta->tabSize);
					if (spaces == 0) spaces = ta->tabSize;
					x += spaces * charW;
					colCount += spaces;
				} else {
					run += ch;
					++colCount;
				}
			}
			if (!run.empty()) x += _TW(run, e);
			return x;
		}

		/// Convert a pixel position (relative to content area origin) to a document offset.
		[[nodiscard]] int _TextAreaHitPos(const TextAreaData *ta, float px, float py, ECS::EntityId e) {
			float lineH = _TH(e) + 2.f;
			float pyDoc = py + ta->scrollY;
			int line = std::clamp((int)(pyDoc / lineH), 0, ta->LineCount() - 1);
			int lineStart = ta->LineStart(line);
			int lineEnd   = ta->LineEnd(line);

			auto rf = _ResolveFont(e);
			float charW = (rf.size > 0.f) ? rf.size : (float)SDL::DEBUG_TEXT_FONT_CHARACTER_SIZE;
			int best = lineStart;
			float bestDist = SDL::Abs(px);
			for (int i = lineStart; i <= lineEnd; ++i) {
				float xOff = _TextAreaLineX(ta, line, i - lineStart, e);
				float dist = SDL::Abs(px - xOff);
				if (dist < bestDist) { bestDist = dist; best = i; }
				if (xOff > px + charW * 3.f) break;
			}
			return best;
		}

		/// Scroll the TextArea so the cursor is visible.
		void _TextAreaScrollToCursor(TextAreaData *ta, const ComputedRect *cr, const LayoutProps *lp, ECS::EntityId e) {
			if (!ta || !cr || !lp) return;
			float lineH = _TH(e) + 2.f;
			float viewH = cr->screen.h - lp->padding.top - lp->padding.bottom;
			int   curLine = ta->LineOf(ta->cursorPos);
			float curY    = curLine * lineH;
			if (curY < ta->scrollY)
				ta->scrollY = curY;
			else if (curY + lineH > ta->scrollY + viewH)
				ta->scrollY = curY + lineH - viewH;
			float maxScroll = SDL::Max(0.f, ta->LineCount() * lineH - viewH);
			ta->scrollY = SDL::Clamp(ta->scrollY, 0.f, maxScroll);
		}

		void _HandleKeyDown(SDL::Keycode k, SDL::Keymod mod) {
			if (m_focused != ECS::NullEntity && m_ctx.IsAlive(m_focused)) { 
				auto *fw = m_ctx.Get<Widget>(m_focused);
				if (fw) {
					if (fw->type == WidgetType::TextArea) {
						if (Has(fw->behavior, BehaviorFlag::Enable | BehaviorFlag::Focusable)) {
							_HandleKeyDownTextArea(k, mod);
							return;
						}   
					} else if (fw->type == WidgetType::Input) {
						if (Has(fw->behavior, BehaviorFlag::Enable | BehaviorFlag::Focusable)) {
							_HandleKeyDownInput(k, mod);
							return;
						}
					} else if (fw->type == WidgetType::ListBox) {
						if (Has(fw->behavior, BehaviorFlag::Enable | BehaviorFlag::Focusable)) {
							_HandleKeyDownListBox(k);
							return;
						}
					}
				}
			}

			if (k == SDL::KEYCODE_TAB) {
				bool shiftPressed = (mod & SDL::KMOD_SHIFT);
				_CycleFocus(shiftPressed);
				return;
			}
			if (m_focused == ECS::NullEntity || !m_ctx.IsAlive(m_focused)) return; 

			auto *w = m_ctx.Get<Widget>(m_focused);
			if (!w || w->type != WidgetType::Input || !Has(w->behavior, BehaviorFlag::Enable | BehaviorFlag::Focusable)) return;

			auto *c = m_ctx.Get<EditableContent>(m_focused);
			auto *cb = m_ctx.Get<Callbacks>(m_focused);
			if (!c) return;

			switch (k) { 
				case SDL::KEYCODE_BACKSPACE:
					if (c->cursor > 0) {
						c->text.erase((size_t)(c->cursor - 1), 1);
						--c->cursor;
						if (cb && cb->onTextChange) cb->onTextChange(c->text);
					}
					break;
				case SDL::KEYCODE_DELETE:
					if (c->cursor < (int)c->text.size()) {
						c->text.erase((size_t)c->cursor, 1);
						if (cb && cb->onTextChange) cb->onTextChange(c->text);
					}
					break;
				case SDL::KEYCODE_LEFT:
					c->cursor = SDL::Max(0, c->cursor - 1);
					break;
				case SDL::KEYCODE_RIGHT:
					c->cursor = SDL::Min((int)c->text.size(), c->cursor + 1);
					break;
				case SDL::KEYCODE_HOME:
					c->cursor = 0;
					break;
				case SDL::KEYCODE_END:
					c->cursor = (int)c->text.size();
					break;
				case SDL::KEYCODE_ESCAPE:
					_SetFocus(ECS::NullEntity);
					break;
				default:
					break;
			}
		}

		void _HandleScroll(float dx, float dy) {
			ECS::EntityId e = _HitTest(m_root, m_mousePos);
			while (e != ECS::NullEntity && m_ctx.IsAlive(e)) {
				auto *w  = m_ctx.Get<Widget>(e);
				auto *lp = m_ctx.Get<LayoutProps>(e);

				// ListBox has its own internal scroll.
				if (w && w->type == WidgetType::ListBox) {
					bool consumed = false;
					if (auto *lb = m_ctx.Get<ListBoxData>(e)) {
						auto *cr2 = m_ctx.Get<ComputedRect>(e);
						auto *lp2 = m_ctx.Get<LayoutProps>(e);
						if (cr2 && lp2) {
							float viewH = cr2->screen.h - lp2->padding.top - lp2->padding.bottom;
							consumed = _ScrollOnAxis(lp2->scrollY, viewH,
							                         (float)lb->items.size() * lb->itemHeight,
							                         dy, lb->itemHeight * 2.f);
						}
					}
					if (consumed || !w->dispatchEvent) return;
					auto *par = m_ctx.Get<Parent>(e);
					e = (par && par->id != ECS::NullEntity) ? par->id : ECS::NullEntity;
					continue;
				}
				// Tree has its own internal scroll.
				if (w && w->type == WidgetType::Tree) {
					bool consumed = false;
					if (auto *d2 = m_ctx.Get<TreeData>(e)) {
						auto *cr2 = m_ctx.Get<ComputedRect>(e);
						if (cr2) {
							float viewH  = cr2->screen.h - (lp ? lp->padding.top + lp->padding.bottom : 0.f);
							float totalH = (float)_GetVisibleTreeNodes(d2->nodes).size() * d2->itemHeight;
							consumed = _ScrollOnAxis(d2->scrollY, viewH, totalH, dy, d2->itemHeight * 2.f);
						}
					}
					if (consumed || !w->dispatchEvent) return;
					auto *par = m_ctx.Get<Parent>(e);
					e = (par && par->id != ECS::NullEntity) ? par->id : ECS::NullEntity;
					continue;
				}
				// TextArea has its own internal scroll.
				if (w && w->type == WidgetType::TextArea) {
					bool consumed = false;
					if (auto *ta = m_ctx.Get<TextAreaData>(e)) {
						auto *cr = m_ctx.Get<ComputedRect>(e);
						if (cr) {
							float lineH  = _TH(e) + 2.f;
							float viewH  = cr->screen.h - (lp ? lp->padding.top + lp->padding.bottom : 0.f);
							consumed = _ScrollOnAxis(ta->scrollY, viewH,
							                         (float)ta->LineCount() * lineH, dy, lineH * 3.f);
						}
					}
					if (consumed || !w->dispatchEvent) return;
					auto *par = m_ctx.Get<Parent>(e);
					e = (par && par->id != ECS::NullEntity) ? par->id : ECS::NullEntity;
					continue;
				}
				if (!w || !lp) break;

				// ScrollableX/Y = scroll permanent ; AutoScrollableX/Y = scroll si débordement.
				bool scrollableV = Has(w->behavior, BehaviorFlag::ScrollableY | BehaviorFlag::AutoScrollableY);
				bool scrollableH = Has(w->behavior, BehaviorFlag::ScrollableX | BehaviorFlag::AutoScrollableX);

				if (scrollableV || scrollableH) {
					auto *cr = m_ctx.Get<ComputedRect>(e);
					auto sv = cr ? _ComputeScrollView(cr->screen, *lp, *w) : ScrollViewInfo{};

					bool consumed = false;
					consumed |= scrollableH && _ScrollOnAxis(lp->scrollX, sv.viewW, lp->contentW, dx, 20.f);
					consumed |= scrollableV && _ScrollOnAxis(lp->scrollY, sv.viewH, lp->contentH, dy, 20.f);

					if (consumed) {
						auto *s = m_ctx.Get<Style>(e);
						if (s && !s->scrollSound.empty())
							if (auto sh = _EnsureAudio(s->scrollSound))
								_PlayAudio(sh);
					}
					if (consumed || !w->dispatchEvent) return;
				}
				auto *par = m_ctx.Get<Parent>(e);
				e = (par && par->id != ECS::NullEntity) ? par->id : ECS::NullEntity;
			}
		}

		void _CanvasEvent(const SDL::Event& evt) {
			// Forward SDL events to every visible, enabled Canvas widget whose
			// clip rect contains the mouse cursor OR that currently holds focus.
			// This allows the game canvas to receive keyboard events even when
			// the mouse is elsewhere (e.g. paused panels on top).
			m_ctx.Each<CanvasData, Widget, ComputedRect>(
				[&](ECS::EntityId e, CanvasData& cd, Widget& w, ComputedRect& cr) {
					if (!Has(w.behavior, BehaviorFlag::Visible | BehaviorFlag::Enable)) return;
					if (!cd.eventCb) return;
					const bool mouseInside = cr.clip.Contains(m_mousePos);
					const bool hasFocus    = (m_focused == e);
					if (mouseInside || hasFocus)
						cd.eventCb(const_cast<SDL::Event&>(evt));
				});
		}

		bool IsEffectivelyVisible(ECS::EntityId e, ECS::Context& ecs_context) {
			while (e != ECS::NullEntity) { 
				auto* w = ecs_context.Get<Widget>(e);
				if (!w || !Has(w->behavior, BehaviorFlag::Visible)) return false;
				
				auto* p = ecs_context.Get<Parent>(e);
				e = p ? p->id : ECS::NullEntity;
			}
			return true;
		}

		void _CollectFocusables(ECS::EntityId e, std::vector<ECS::EntityId> &out) const {
			auto *w = m_ctx.Get<Widget>(e);
			if (!w || !Has(w->behavior, BehaviorFlag::Visible | BehaviorFlag::Enable)) return;

			if (Has(w->behavior, BehaviorFlag::Focusable)) {
				// Only add if the widget is actually on-screen (clip rect has positive area).
				auto *cr = m_ctx.Get<ComputedRect>(e);
				if (cr && cr->clip.w > 0.f && cr->clip.h > 0.f)
					out.push_back(e);
			}

			if (auto *ch = m_ctx.Get<Children>(e)) {
				for (ECS::EntityId c : ch->ids)
					_CollectFocusables(c, out);
			}
		}

		void _CycleFocus(bool reverse = false) {
			std::vector<ECS::EntityId> foc; 
			_CollectFocusables(m_root, foc);
			if (foc.empty()) return;

			auto it = std::ranges::find(foc, m_focused);
			ECS::EntityId nextFocus;

			if (it == foc.end()) {
				nextFocus = reverse ? foc.back() : foc.front();
			} else {
				if (reverse) {
					nextFocus = (it == foc.begin()) ? foc.back() : *std::prev(it);
				} else {
					nextFocus = (std::next(it) == foc.end()) ? foc.front() : *std::next(it);
				}
			}

			_SetFocus(nextFocus);
		}

		// ── Canvas update ─────────────────────────────────────────────────

		void _ProcessCanvasUpdate(float dt) {
			// Call updateCb for every visible, enabled Canvas widget.
			// Layout hasn't run yet this frame, so avoid depending on ComputedRect here.
			m_ctx.Each<CanvasData, Widget>([dt](ECS::EntityId, CanvasData& cd, Widget& w) {
				if (!Has(w.behavior, BehaviorFlag::Visible | BehaviorFlag::Enable)) return;
				if (cd.updateCb) cd.updateCb(dt);
			});
		}

		// ── Animate ────────────────────────────────────────────────────────

		void _ProcessAnimate(float dt) {
			if (dt <= 0.f)
				return;

			m_ctx.Each<ToggleData>([dt](ECS::EntityId, ToggleData &t) {
				float target = t.checked ? 1.f : 0.f;
				t.animT += (target - t.animT) * SDL::Min(1.f, dt * 12.f);
				t.animT = SDL::Clamp(t.animT, 0.f, 1.f);
			});
			m_ctx.Each<SpinnerData>([dt](ECS::EntityId, SpinnerData &sp) {
				sp.angle += sp.speed * dt;
				if (sp.angle > 2.f * std::numbers::pi_v<float>)
					sp.angle -= 2.f * std::numbers::pi_v<float>;
			});
			m_ctx.Each<ExpanderData>([dt](ECS::EntityId, ExpanderData &exp) {
				float target = exp.expanded ? 1.f : 0.f;
				exp.animT += (target - exp.animT) * SDL::Min(1.f, dt * 10.f);
				exp.animT = SDL::Clamp(exp.animT, 0.f, 1.f);
			});
			
			if (m_focused != ECS::NullEntity && m_ctx.IsAlive(m_focused)) {
				if (auto *c = m_ctx.Get<EditableContent>(m_focused)) {
					c->blinkTimer += dt;
					if (c->blinkTimer > 1.f) c->blinkTimer -= 1.f;
				} 
				if (auto *ta = m_ctx.Get<TextAreaData>(m_focused)) {
					ta->blinkTimer += dt;
					if (ta->blinkTimer > 1.f) ta->blinkTimer -= 1.f;
					// Keep cursor visible after scroll
					auto *cr2 = m_ctx.Get<ComputedRect>(m_focused);
					auto *lp2 = m_ctx.Get<LayoutProps>(m_focused);
					_TextAreaScrollToCursor(ta, cr2, lp2, m_focused);
				}
			}
		}

		// ── Tooltip ────────────────────────────────────────────────────────

		void _ProcessTooltip(float dt) {
			// Hovered entity changed → reset timer
			if (m_hovered != m_tooltipTarget) {
				m_tooltipTarget  = m_hovered;
				m_tooltipTimer   = 0.f;
				m_tooltipVisible = false;
			}
			if (m_tooltipTarget == ECS::NullEntity || !m_ctx.IsAlive(m_tooltipTarget)) {
				m_tooltipVisible = false;
				return;
			}
			auto *td = m_ctx.Get<TooltipData>(m_tooltipTarget);
			if (!td || td->text.empty()) {
				m_tooltipVisible = false;
				return;
			}
			m_tooltipTimer  += dt;
			m_tooltipVisible = (m_tooltipTimer >= td->delay);
		}

		void _DrawTooltip() {
			if (!m_tooltipVisible || m_tooltipTarget == ECS::NullEntity) return;
			auto *td = m_ctx.Get<TooltipData>(m_tooltipTarget);
			if (!td || td->text.empty()) return;

			// Couleurs et police depuis le widget survolé
			auto *hs = m_ctx.Get<Style>(m_tooltipTarget);
			static const Style kDef{};
			const Style &s = hs ? *hs : kDef;

			// Entité dédiée au cache de texte TTF (ne participe pas au layout)
			if (m_tooltipEntity == ECS::NullEntity || !m_ctx.IsAlive(m_tooltipEntity)) {
				m_tooltipEntity = m_ctx.CreateEntity();
				m_ctx.Add<Style>(m_tooltipEntity);
			}
			// Resolve the font from the hovered widget and pin it as Self on the
			// tooltip entity so _TH/_TW don't walk an invalid parent chain.
			auto &ts = *m_ctx.Get<Style>(m_tooltipEntity);
			{
				auto rf = _ResolveFont(m_tooltipTarget);
				ts.fontKey  = rf.key;
				ts.fontSize = rf.size;
				ts.usedFont = rf.isDebug ? FontType::Debug
							: (rf.key.empty() ? FontType::Default : FontType::Self);
			}

			constexpr float kPadH = 8.f, kPadV = 5.f;
			const float tw = _TW(td->text, m_tooltipEntity);
			const float th = _TH(m_tooltipEntity);
			const float bw = tw + kPadH * 2.f + 2.f;
			const float bh = th + kPadV * 2.f;

			// Position : sous le curseur, basculée si proche du bord
			float x = m_mousePos.x + 14.f;
			float y = m_mousePos.y + 22.f;
			if (x + bw > m_viewport.w - 4.f) x = m_mousePos.x - bw - 6.f;
			if (y + bh > m_viewport.h - 4.f) y = m_mousePos.y - bh - 6.f;
			x = SDL::Clamp(x, 4.f, SDL::Max(4.f, m_viewport.w - bw - 4.f));
			y = SDL::Clamp(y, 4.f, SDL::Max(4.f, m_viewport.h - bh - 4.f));

			const FRect r = {x, y, bw, bh};
			m_renderer.ResetClipRect();
			_FillRR(r,   s.tooltipBgColor, SDL::FCorners(4.f), 1.f);
			_StrokeRR(r, s.tooltipBdColor, {1.f, 1.f, 1.f, 1.f}, SDL::FCorners(4.f), 1.f);
			_Text(m_tooltipEntity, td->text, x + kPadH, y + kPadV, s.tooltipTextColor, 1.f, ts);
		}

		// ── Render ─────────────────────────────────────────────────────────

		void _ProcessRender() {
			m_renderer.SetDrawBlendMode(SDL::BLENDMODE_BLEND);
			_RenderNode(m_root);
			m_renderer.ResetClipRect();
			_DrawComboOverlay();
			_DrawMenuBarOverlay();
			_DrawTooltip();
		}

		void _RenderNode(ECS::EntityId e) {
		   if (!m_ctx.IsAlive(e)) return; 
			auto *w  = m_ctx.Get<Widget>(e);
			auto *cr = m_ctx.Get<ComputedRect>(e);
			if (!w || !cr) return;

			if (!Has(w->behavior, BehaviorFlag::Visible)) return;
			
			if (cr->outer_clip.w <= 0.f || cr->outer_clip.h <= 0.f) return;

			m_renderer.SetClipRect(FRectToRect(cr->outer_clip));
			_DrawWidget(e);
			
			auto *ch = m_ctx.Get<Children>(e);
			if (ch) {
				for (ECS::EntityId c : ch->ids) {
					_RenderNode(c);
				}
			}
		}

		// ── Clip helpers ─────────────────────────────────────────────────────────

		/// Returns the intersection of two SDL::Rect clip regions.
		/// If either rect has zero area the result is also empty.
		static SDL::Rect _ClipIntersect(SDL::Rect a, SDL::Rect b) noexcept {
			int x1 = SDL::Max(a.x, b.x);
			int y1 = SDL::Max(a.y, b.y);
			int x2 = SDL::Min(a.x + a.w, b.x + b.w);
			int y2 = SDL::Min(a.y + a.h, b.y + b.h);
			return {x1, y1, SDL::Max(0, x2 - x1), SDL::Max(0, y2 - y1)};
		}

		// ── Scrollable-widget shared helpers ─────────────────────────────────────

		/// Viewport dimensions and scrollbar visibility for a scrollable widget.
		struct ScrollViewInfo {
			float innerW = 0.f, innerH = 0.f;
			float viewW  = 0.f, viewH  = 0.f;
			bool  showX  = false, showY = false;
		};

		/// Compute viewport dimensions and scrollbar visibility from a widget's rect.
		/// Central source of truth for showX/showY/viewW/viewH — replaces repeated
		/// innerW/innerH/_ContainerScrollbars/viewW/viewH blocks in draw functions.
		ScrollViewInfo _ComputeScrollView(const FRect &r, const LayoutProps &lp, const Widget &w) const noexcept {
			ScrollViewInfo v;
			v.innerW = r.w - lp.padding.left - lp.padding.right;
			v.innerH = r.h - lp.padding.top  - lp.padding.bottom;
			_ContainerScrollbars(w, lp, v.innerW, v.innerH, v.showX, v.showY);
			v.viewW = v.showY ? SDL::Max(0.f, v.innerW - lp.scrollbarThickness) : v.innerW;
			v.viewH = v.showX ? SDL::Max(0.f, v.innerH - lp.scrollbarThickness) : v.innerH;
			return v;
		}

		/// Push a new clip rect (intersection of current clip and @p area); returns
		/// the previous clip so it can be restored with _PopClip.
		SDL::Rect _PushClip(const FRect &area) {
			SDL::Rect prev = m_renderer.GetClipRect();
			m_renderer.SetClipRect(_ClipIntersect(prev,
				{(int)area.x, (int)area.y, (int)area.w, (int)area.h}));
			return prev;
		}

		/// Restore the clip rect saved by _PushClip.
		void _PopClip(const SDL::Rect &prev) {
			if (prev.w > 0 && prev.h > 0) m_renderer.SetClipRect(prev);
			else                           m_renderer.SetClipRect({});
		}

		/// Scroll one axis by @p delta * @p step, clamped to [0, contentSize-viewSize].
		/// Returns true if the scroll offset actually changed.
		static bool _ScrollOnAxis(float &scroll, float viewSize, float contentSize,
		                          float delta, float step) noexcept {
			if (delta == 0.f || contentSize <= viewSize) return false;
			float prev   = scroll;
			scroll = SDL::Clamp(scroll - delta * step, 0.f, contentSize - viewSize);
			return scroll != prev;
		}

		/// Compute a new scroll offset from a thumb-drag gesture on one axis.
		/// viewSize/contentSize define the ratio; dragStartMouse/dragStartOff are the
		/// values captured at press time; currentMouse is the live mouse coordinate.
		static float _ApplyScrollDrag(float viewSize, float contentSize, float sbThickness,
		                              float dragStartMouse, float dragStartOff,
		                              float currentMouse) noexcept {
			float ratio   = SDL::Clamp(viewSize / SDL::Max(1.f, contentSize), 0.05f, 1.f);
			float thumbSz = SDL::Max(sbThickness * 2.f, viewSize * ratio);
			float travel  = viewSize - thumbSz;
			float maxOff  = contentSize - viewSize;
			if (travel <= 0.f || maxOff <= 0.f) return 0.f;
			return SDL::Clamp(dragStartOff + (currentMouse - dragStartMouse) / travel * maxOff,
			                  0.f, maxOff);
		}

		/// Draw a vertical scrollbar track + thumb inside @p barR.
		/// Updates css->thumbY for hit-testing. @p scrollY is the current scroll offset.
		void _DrawScrollbarY(const FRect &barR, float contentH, float viewH, float scrollY,
		                     ContainerScrollState *css, const Style &s, float op) {
			float t = barR.w;
			SDL::Color trackCol = s.trackColor;
			trackCol.a = SDL::Clamp8((int)(trackCol.a * op * 0.85f));
			_FillRR(barR, trackCol, SDL::FCorners(t * 0.5f), 1.f);

			css->thumbY = {};
			if (contentH > 0.f && contentH > viewH) {
				float ratio  = SDL::Clamp(viewH / contentH, 0.05f, 1.f);
				float maxOff = contentH - viewH;
				float offN   = (maxOff > 0.f) ? scrollY / maxOff : 0.f;
				float tH     = SDL::Max(t * 2.f, barR.h * ratio);
				float tY     = barR.y + (barR.h - tH) * offN;
				FRect thumb  = {barR.x + 1.f, tY, t - 2.f, tH};
				bool  hov    = css->dragY || FRect(barR.x, tY, t, tH).Contains(m_mousePos);
				SDL::Color thumbCol = hov ? s.thumbColor : s.fillColor;
				thumbCol.a = SDL::Clamp8((int)(thumbCol.a * op));
				_FillRR(thumb, thumbCol, SDL::FCorners((t - 2.f) * 0.5f), 1.f);
				css->thumbY = thumb;
			}
		}

		// ── Primitives ────────────────────────────────────────────────────────────

		void _FillRect(const SDL::FRect& r, SDL::Color c, float op) {
			c.a = SDL::Clamp8(c.a * op);
			m_renderer.SetDrawColor(c);
			m_renderer.RenderFillRect(r);
		}
		void _FillRR(const SDL::FRect& r, SDL::Color c, const SDL::FCorners& rad, float op) {
			c.a = SDL::Clamp8(c.a * op);
			m_renderer.SetDrawColor(c);
			if (rad.bl > 0.f || rad.br > 0.f || rad.tl > 0.f || rad.tr > 0.f)
				m_renderer.RenderFillRoundedRect(r, rad);
			else
				m_renderer.RenderFillRect(r);
		}
		void _StrokeRR(const SDL::FRect& r, SDL::Color c, const SDL::FBox& b, const SDL::FCorners& rad, float op) {
			if (b.left <= 0.f || b.right <= 0.f || b.top <= 0.f || b.bottom <= 0.f) return;
			c.a = SDL::Clamp8(c.a * op);
			m_renderer.SetDrawColor(c);
			if (rad.bl > 0.f || rad.br > 0.f || rad.tl > 0.f || rad.tr > 0.f)
				m_renderer.RenderRoundedBorderedRect(r, b, rad);
			else
				m_renderer.RenderRect(r);
		}
		// ── Gradient & background helpers ─────────────────────────────────────────

		SDL::FPoint _AnchorToPoint(const SDL::FRect& r, GradientAnchor a) {
			switch (a) {
				case GradientAnchor::Top:        return {r.x + r.w * 0.5f, r.y};
				case GradientAnchor::Bottom:     return {r.x + r.w * 0.5f, r.y + r.h};
				case GradientAnchor::Left:       return {r.x, r.y + r.h * 0.5f};
				case GradientAnchor::Right:      return {r.x + r.w, r.y + r.h * 0.5f};
				case GradientAnchor::TopLeft:    return {r.x, r.y};
				case GradientAnchor::TopRight:   return {r.x + r.w, r.y};
				case GradientAnchor::BottomLeft: return {r.x, r.y + r.h};
				case GradientAnchor::BottomRight:return {r.x + r.w, r.y + r.h};
				case GradientAnchor::Center:     return {r.x + r.w * 0.5f, r.y + r.h * 0.5f};
			}
			return {r.x, r.y};
		}

		/// Fill a rectangle with a 2-colour linear gradient (no rounded corners).
		void _FillGradientRect(const FRect &r, SDL::Color c1, GradientAnchor start, SDL::Color c2, GradientAnchor end, float op) {
			SDL::FColor col1 = c1.ToFloat(op);
			SDL::FColor col2 = c2.ToFloat(op);
			SDL::FPoint p1   = _AnchorToPoint(r, start);
			SDL::FPoint p2   = _AnchorToPoint(r, end);
			float dx = p2.x - p1.x;
			float dy = p2.y - p1.y;
			float len2 = dx*dx + dy*dy;

			// Fonction projection → t
			auto computeT = [&](float x, float y) {
				if (len2 == 0.f) return 0.f;
				float t = ((x - p1.x) * dx + (y - p1.y) * dy) / len2;
				return SDL::Clamp(t, 0.f, 1.f);
			};

			// Interpolation couleur
			auto lerpColor = [&](float t) {
				return SDL::FColor{
					col1.r + (col2.r - col1.r) * t,
					col1.g + (col2.g - col1.g) * t,
					col1.b + (col2.b - col1.b) * t,
					col1.a + (col2.a - col1.a) * t
				};
			};

			// Sommets du rectangle
			SDL::FPoint pts[4] = {
				{r.x,       r.y},
				{r.x+r.w,   r.y},
				{r.x+r.w,   r.y+r.h},
				{r.x,       r.y+r.h}
			};

			SDL::Vertex vs[6];

			int indices[6] = {0,1,2, 0,2,3};

			for (int i = 0; i < 6; ++i) {
				auto& p = pts[indices[i]];
				float t = computeT(p.x, p.y);

				vs[i] = {
					{p.x, p.y},
					lerpColor(t),
					{}
				};
			}
			m_renderer.RenderGeometry(nullptr, vs);
		}

		/// Draw the background of a widget: gradient if BgGradient component is attached,
		/// otherwise solid colour via _FillRR.  Border is NOT drawn by this function.
		void _DrawBg(ECS::EntityId e, const FRect &r, const Style &s, const WidgetState &st, const Widget &w) {
			const bool enabled = Has(w.behavior, BehaviorFlag::Enable);
			SDL::Color bg1 = !enabled      ? s.bgDisabledColor
			               : st.pressed    ? s.bgPressedColor
			               : (m_focused==e)? s.bgFocusedColor
			               : st.hovered    ? s.bgHoveredColor
			               : s.bgColor;
			if (auto *bg = m_ctx.Get<BgGradient>(e)) {
				SDL::Color bg2 = !enabled       ? bg->color2Disabled
				               : st.pressed     ? bg->color2Pressed
				               : (m_focused==e) ? bg->color2Focused
				               : st.hovered     ? bg->color2Hovered
				               : bg->color2;
				_FillGradientRect(r, bg1, bg->start, bg2, bg->end, s.opacity);
			} else {
				_FillRR(r, bg1, s.radius, s.opacity);
			}
		}

		/// Convert RGB (Uint8) to HSV floats [0..1].
		static void _RgbToHsv(SDL::Color c, float &h, float &s, float &v) noexcept {
			float rf = c.r/255.f, gf = c.g/255.f, bf = c.b/255.f;
			float cmax = SDL::Max(rf, SDL::Max(gf, bf));
			float cmin = SDL::Min(rf, SDL::Min(gf, bf));
			float delta = cmax - cmin;
			v = cmax;
			s = (cmax > 0.f) ? delta/cmax : 0.f;
			if (delta < 1e-6f) { h = 0.f; return; }
			if (cmax == rf)      h = (gf - bf) / delta / 6.f;
			else if (cmax == gf) h = (2.f + (bf - rf) / delta) / 6.f;
			else                 h = (4.f + (rf - gf) / delta) / 6.f;
			if (h < 0.f) h += 1.f;
		}

		/// Convert HSV floats [0..1] to SDL::Color.
		static SDL::Color _HsvToColor(float h, float s, float v, Uint8 a = 255) noexcept {
			if (s < 1e-6f) {
				Uint8 g = (Uint8)(v * 255.f);
				return {g, g, g, a};
			}
			float hh = h * 6.f;
			int   i  = (int)hh % 6;
			float f  = hh - (float)i;
			float p  = v * (1.f - s);
			float q  = v * (1.f - f * s);
			float t  = v * (1.f - (1.f - f) * s);
			float r, g, b;
			switch (i) {
				case 0:  r=v; g=t; b=p; break;
				case 1:  r=q; g=v; b=p; break;
				case 2:  r=p; g=v; b=t; break;
				case 3:  r=p; g=q; b=v; break;
				case 4:  r=t; g=p; b=v; break;
				default: r=v; g=p; b=q; break;
			}
			return {(Uint8)(r*255.f), (Uint8)(g*255.f), (Uint8)(b*255.f), a};
		}

		/// Draw a rainbow hue bar (6 gradient segments) inside @p r.
		void _DrawHueBarGradient(const FRect &r, float op) {
			static const SDL::Color kHues[7] = {
				{255,  0,  0, 255}, {255,255,  0, 255}, {  0,255,  0, 255},
				{  0,255,255, 255}, {  0,  0,255, 255}, {255,  0,255, 255},
				{255,  0,  0, 255},
			};
			float sw = r.w / 6.f;
			for (int i = 0; i < 6; ++i)
				_FillGradientRect({r.x + i*sw, r.y, sw, r.h}, kHues[i], GradientAnchor::Left, kHues[i+1], GradientAnchor::Right, op);
		}

		/// Build the list of visible tree node indices (respecting expand/collapse).
		static std::vector<int> _GetVisibleTreeNodes(const std::vector<TreeNodeData> &nodes) {
			std::vector<int> visible;
			std::vector<bool> depthCollapsed(64, false);
			for (int i = 0; i < (int)nodes.size(); ++i) {
				const auto &n = nodes[i];
				int lvl = (n.level < 64) ? n.level : 63;
				// Rising to this level resets deeper collapse flags
				for (int l = lvl; l < 64; ++l) depthCollapsed[l] = false;
				// Hidden if any ancestor level is marked collapsed
				bool hidden = false;
				for (int l = 0; l < lvl; ++l)
					if (depthCollapsed[l]) { hidden = true; break; }
				if (hidden) continue;
				visible.push_back(i);
				if (n.hasChildren && !n.expanded)
					depthCollapsed[lvl] = true;
			}
			return visible;
		}

		/// Update ColorPicker internal state from mouse position.
		void _UpdateColorPicker(ECS::EntityId e, const FRect &r, ColorPickerData *cp, FPoint mouse) {
			constexpr float P      = 4.f;   // internal padding
			constexpr float hueH   = 14.f;
			constexpr float bGap   = 4.f;
			FRect inner = {r.x+P, r.y+P, r.w-2*P, r.h-2*P};

			if (cp->palette == ColorPickerPalette::Grayscale) {
				float t = SDL::Clamp((mouse.x - inner.x) / SDL::Max(1.f, inner.w), 0.f, 1.f);
				cp->gradT = t;
				Uint8 v = (Uint8)(t * 255.f);
				cp->currentColor = {v, v, v, cp->currentColor.a};
			} else if (cp->palette == ColorPickerPalette::GradientAB) {
				float t = SDL::Clamp((mouse.x - inner.x) / SDL::Max(1.f, inner.w), 0.f, 1.f);
				cp->gradT = t;
				auto lerp8 = [](Uint8 a, Uint8 b, float t) { return (Uint8)(a + t * (float)(b - a)); };
				cp->currentColor = {lerp8(cp->colorA.r, cp->colorB.r, t),
				                    lerp8(cp->colorA.g, cp->colorB.g, t),
				                    lerp8(cp->colorA.b, cp->colorB.b, t),
				                    cp->currentColor.a};
			} else {
				float alphaH = cp->showAlpha ? hueH : 0.f;
				float svH    = inner.h - hueH - bGap - (cp->showAlpha ? alphaH + bGap : 0.f);
				float realSvH = SDL::Max(10.f, svH);
				FRect svR  = {inner.x, inner.y, inner.w, realSvH};
				FRect hueR = {inner.x, inner.y + realSvH + bGap, inner.w, hueH};

				bool inHue = hueR.Contains(mouse);
				bool inSV  = svR.Contains(mouse);

				if (inHue || cp->draggingHue) {
					cp->hue = SDL::Clamp((mouse.x - hueR.x) / SDL::Max(1.f, hueR.w), 0.f, 1.f);
					cp->draggingHue = true;
				} else if (inSV) {
					float sat = SDL::Clamp((mouse.x - svR.x) / SDL::Max(1.f, svR.w), 0.f, 1.f);
					float val = 1.f - SDL::Clamp((mouse.y - svR.y) / SDL::Max(1.f, svR.h), 0.f, 1.f);
					if (cp->palette == ColorPickerPalette::RGBFloat && cp->precisionStep > 0.f) {
						sat = std::round(sat / cp->precisionStep) * cp->precisionStep;
						val = std::round(val / cp->precisionStep) * cp->precisionStep;
					}
					cp->sat = sat;
					cp->val = val;
					cp->draggingHue = false;
				}

				if (cp->showAlpha) {
					FRect alphaR = {inner.x, hueR.y + hueH + bGap, inner.w, alphaH};
					if (alphaR.Contains(mouse)) {
						float t = SDL::Clamp((mouse.x - alphaR.x) / SDL::Max(1.f, alphaR.w), 0.f, 1.f);
						cp->currentColor.a = (Uint8)(t * 255.f);
					}
				}
				SDL::Color nc = _HsvToColor(cp->hue, cp->sat, cp->val, cp->currentColor.a);
				cp->currentColor = nc;
			}

			if (auto *cb = m_ctx.Get<Callbacks>(e)) {
				if (cb->onChange) cb->onChange(0.f);
			}
		}

		// ── Text rendering (TTF with debug fallback) ──────────────────────────────
		//
		// All draw helpers pass `const Style& s` so font metadata is always
		// available.  When SDL3PP_ENABLE_TTF is defined and fontKey/fontSize are
		// set on the style, SDL3_ttf renders the text via the pool-cached Font
		// and SDL::RendererTextEngine otherwise the built-in 8×8 debug font is used.
		
		void _Text(ECS::EntityId e, const std::string &text, float x, float y, SDL::Color c, float op, const Style &s) {
			if (text.empty()) return;
			c.a = SDL::Clamp8(c.a * op);
#if UI_HAS_TTF
			auto rf = _ResolveFont(e);
			if (!rf.isDebug && !rf.key.empty() && rf.size > 0.f) {
				if (auto font = _EnsureFont(rf.key, rf.size)) {
					if (auto txt = _EnsureText(e, font, text)) {
						txt.SetColor(c);
						txt.DrawRenderer({x, y});
						return;
					}
				}
			}
#endif
			m_renderer.SetDrawColor(c);
			float scale = (s.fontSize > 0.f) ? s.fontSize / ((float)SDL::DEBUG_TEXT_FONT_CHARACTER_SIZE) : 1.f;
			m_renderer.SetScale({scale, scale});
			m_renderer.RenderDebugText({x / scale, y / scale}, text);
			m_renderer.SetScale({1.f, 1.f});
		}

		/// Height of one text line in pixels, resolved through the FontType hierarchy.
		[[nodiscard]] float _TH(ECS::EntityId e) noexcept {
#if UI_HAS_TTF
			auto rf = _ResolveFont(e);
			if (!rf.isDebug && !rf.key.empty() && rf.size > 0.f)
				if (auto f = _EnsureFont(rf.key, rf.size))
					return (float)f.GetHeight();
			return (rf.size > 0.f) ? rf.size : (float)SDL::DEBUG_TEXT_FONT_CHARACTER_SIZE;
#else
			auto* s = m_ctx.Get<Style>(e);
			return (s && s->fontSize > 0.f) ? s->fontSize : (float)SDL::DEBUG_TEXT_FONT_CHARACTER_SIZE;
#endif
		}

		/// Width of a string in pixels, resolved through the FontType hierarchy.
		[[nodiscard]] float _TW(const std::string &t, ECS::EntityId e) {
			if (t.empty()) return 0.f;
#if UI_HAS_TTF
			auto rf = _ResolveFont(e);
			if (!rf.isDebug && !rf.key.empty() && rf.size > 0.f) {
				if (auto font = _EnsureFont(rf.key, rf.size)) {
					int w = 0, h = 0;
					font.GetStringSize(t, &w, &h);
					return (float)w;
				}
			}
			return (float)t.size() * (rf.size > 0.f ? rf.size : (float)SDL::DEBUG_TEXT_FONT_CHARACTER_SIZE);
#else
			auto* s = m_ctx.Get<Style>(e);
			float fs = (s && s->fontSize > 0.f) ? s->fontSize : (float)SDL::DEBUG_TEXT_FONT_CHARACTER_SIZE;
			return (float)t.size() * fs;
#endif
		}

		// ── Per-type draw ─────────────────────────────────────────────────────────

		void _DrawWidget(ECS::EntityId e) {
			auto *w = m_ctx.Get<Widget>(e);
			auto *s = m_ctx.Get<Style>(e);
			auto *st = m_ctx.Get<WidgetState>(e);
			auto *cr = m_ctx.Get<ComputedRect>(e);
			if (!w || !s || !st || !cr) return;
			const FRect &r = cr->screen;

			// ── Tileset skin (9-slice) — replaces solid-colour background ────────────
			// When present, the tileset is drawn first.  For Canvas widgets the game
			// content is rendered on top afterwards; for all other widget types the
			// tileset is the only background and we skip the normal draw entirely.
			auto *ts = m_ctx.Get<TilesetStyle>(e);
			if (ts && !ts->textureKey.empty()) {
				auto tex = _EnsureTexture(ts->textureKey); 
				if (tex) {
					_Draw9Slice(r, *ts, tex);
					// Canvas: still draw game content on top of the skin.
					if (w->type == WidgetType::Canvas)
						_DrawCanvas(e, r);
					// Labels inside a skinned container still need their text drawn.
					else if (w->type == WidgetType::Label)
						_DrawLabel(e, r, *s, *st, *w);
					else if (w->type == WidgetType::Button)
						_DrawButton(e, r, *s, *st, *w);  // draws text + hover overlay
					return;
				}
			}

			// ── Default solid-colour rendering ────────────────────────────────────────
			switch (w->type) { 
				case WidgetType::Container:
					_DrawContainer(e, r, *s, *st, *w);
					break;
				case WidgetType::Label:
					_DrawLabel(e, r, *s, *st, *w);
					break;
				case WidgetType::Button:
					_DrawButton(e, r, *s, *st, *w);
					break;
				case WidgetType::Toggle:
					_DrawToggle(e, r, *s, *st);
					break;
				case WidgetType::RadioButton:
					_DrawRadio(e, r, *s, *st);
					break;
				case WidgetType::Slider:
					_DrawSlider(e, r, *s, *st, *w);
					break;
				case WidgetType::ScrollBar:
					_DrawScrollBar(e, r, *s, *st, *w);
					break;
				case WidgetType::Progress:
					_DrawProgress(e, r, *s, *st);
					break;
				case WidgetType::Separator:
					_DrawSeparator(r, *s);
					break;
				case WidgetType::Input:
					_DrawInput(e, r, *s, *st, *w);
					break;
				case WidgetType::TextArea:
					_DrawTextArea(e, r, *s, *st, *w);
					break;
				case WidgetType::Knob:
					_DrawKnob(e, r, *s, *st, *w);
					break;
				case WidgetType::Image:
					_DrawImage(e, r, *s, *st);
					break;
				case WidgetType::Canvas:
					_DrawCanvas(e, r);
					break;
				case WidgetType::ListBox:
					_DrawListBox(e, r, *s, *st, *w);
					break;
				case WidgetType::Graph:
					_DrawGraph(e, r, *s, *st);
					break;
				case WidgetType::ComboBox:
					_DrawComboBox(e, r, *s, *st, *w);
					break;
				case WidgetType::TabView:
					_DrawTabView(e, r, *s, *st);
					break;
				case WidgetType::Expander:
					_DrawExpander(e, r, *s, *st);
					break;
				case WidgetType::Splitter:
					_DrawSplitter(e, r, *s, *st);
					break;
				case WidgetType::Spinner:
					_DrawSpinner(e, r, *s, *st);
					break;
				case WidgetType::Badge:
					_DrawBadge(e, r, *s, *st, *w);
					break;
				case WidgetType::ColorPicker:
					_DrawColorPicker(e, r, *s, *st, *w);
					break;
				case WidgetType::Popup:
					_DrawPopup(e, r, *s, *st, *w);
					break;
				case WidgetType::Tree:
					_DrawTree(e, r, *s, *st, *w);
					break;
				case WidgetType::MenuBar:
					_DrawMenuBar(e, r, *s, *st, *w);
					break;
			}
		}



		// ── 9-slice tileset draw ───────────────────────────────────────

		/**
		 * Draw a 9-slice widget using a tileset texture.
		 *
		 * The nine tiles are indexed relative to `ts.firstTileIdx`:
		 *   0=TL, 1=TC, 2=TR, 3=ML, 4=MC, 5=MR, 6=BL, 7=BC, 8=BR
		 *
		 * @param r   Screen rect of the widget.
		 * @param ts  Tileset style configuration.
		 * @param tex Resolved tileset texture.
		 */
		void _Draw9Slice(const FRect& r, const TilesetStyle& ts, TextureRef tex) { 
			if (!tex) return;

			const float bw  = SDL::Min(ts.BorderW(), r.w * 0.5f);
			const float bh  = SDL::Min(ts.BorderH(), r.h * 0.5f);
			const float cx  = r.x + bw;
			const float cy  = r.y + bh;
			const float cw  = r.w - 2.f * bw;
			const float ch  = r.h - 2.f * bh;

			tex.SetAlphaMod(SDL::Clamp8(static_cast<int>(255 * ts.opacity)));

			auto draw = [&](int rel, FRect dst) {
				FRect src = ts.TileRect(rel);
				m_renderer.RenderTexture(tex, src, dst);
			};

			// Corners (always drawn)
			draw(0, {r.x,            r.y,            bw, bh});
			draw(2, {r.x + r.w - bw, r.y,            bw, bh});
			draw(6, {r.x,            r.y + r.h - bh, bw, bh});
			draw(8, {r.x + r.w - bw, r.y + r.h - bh, bw, bh});

			// Top / bottom edges
			if (cw > 0.f) {
				draw(1, {cx, r.y,            cw, bh});
				draw(7, {cx, r.y + r.h - bh, cw, bh});
			}

			// Left / right edges
			if (ch > 0.f) {
				draw(3, {r.x,            cy, bw, ch});
				draw(5, {r.x + r.w - bw, cy, bw, ch});
			}

			// Center fill
			if (cw > 0.f && ch > 0.f)
				draw(4, {cx, cy, cw, ch});

			tex.SetAlphaMod(255);
		}

		void _DrawInlineScrollbars(ECS::EntityId e, const FRect &r, const Style &s, const Widget &w) {
			auto *lp  = m_ctx.Get<LayoutProps>(e);
			auto *css = m_ctx.Get<ContainerScrollState>(e);
			if (!lp || !css) return;

			auto sv = _ComputeScrollView(r, *lp, w);
			const float t = lp->scrollbarThickness;

			// Clamp scroll so content never stays scrolled into empty space
			lp->scrollX = SDL::Clamp(lp->scrollX, 0.f, SDL::Max(0.f, lp->contentW - sv.viewW));
			lp->scrollY = SDL::Clamp(lp->scrollY, 0.f, SDL::Max(0.f, lp->contentH - sv.viewH));

			// ── Scrollbar Verticale ──
			if (sv.showY) {
				FRect barY = {r.x + r.w - t, r.y + lp->padding.top, t, sv.viewH};
				_DrawScrollbarY(barY, lp->contentH, sv.viewH, lp->scrollY, css, s, s.opacity);
			} else {
				css->thumbY = {};
			}

			// ── Scrollbar Horizontale ──
			css->thumbX = {};
			if (sv.showX) {
				float barW = sv.showY ? sv.viewW : sv.innerW;
				FRect barX = {r.x + lp->padding.left, r.y + r.h - t, barW, t};

				SDL::Color trackCol = s.trackColor;
				trackCol.a = SDL::Clamp8((int)(trackCol.a * s.opacity * 0.85f));
				_FillRR(barX, trackCol, SDL::FCorners(t * 0.5f), 1.f);

				if (lp->contentW > 0.f && lp->contentW > sv.viewW) {
					float ratio  = SDL::Clamp(sv.viewW / lp->contentW, 0.05f, 1.f);
					float maxOff = lp->contentW - sv.viewW;
					float offN   = (maxOff > 0.f) ? lp->scrollX / maxOff : 0.f;
					float tW     = SDL::Max(t * 2.f, barX.w * ratio);
					float tX     = barX.x + (barX.w - tW) * offN;
					bool  hov    = css->dragX || SDL::FRect(tX, barX.y, tW, t).Contains(m_mousePos);
					SDL::Color thumbCol = hov ? s.thumbColor : s.fillColor;
					thumbCol.a = SDL::Clamp8((int)(thumbCol.a * s.opacity));
					css->thumbX = {tX, barX.y + 1.f, tW, t - 2.f};
					_FillRR(css->thumbX, thumbCol, SDL::FCorners((t - 2.f) * 0.5f), 1.f);
				}
			}

			// ── Coin (quand les 2 barres sont affichées) ──
			if (sv.showX && sv.showY) {
				SDL::Color trackCol = s.trackColor;
				trackCol.a = SDL::Clamp8((int)(trackCol.a * s.opacity * 0.85f));
				_FillRR({r.x + r.w - t, r.y + r.h - t, t, t}, trackCol, SDL::FCorners(0.f), 1.f);
			}
		}
		
		void _DrawContainer(ECS::EntityId e, const FRect &r, const Style &s,
							const WidgetState &st, const Widget &w) {
			// ── Background & border ───────────────────────────────────────────────
			_DrawBg(e, r, s, st, w);
			_StrokeRR(r, st.hovered ? s.bdHoveredColor : s.bdColor, s.borders, s.radius, s.opacity);

			// ── InGrid separator lines ────────────────────────────────────────────
			auto *lp = m_ctx.Get<LayoutProps>(e);
			auto *gp = m_ctx.Get<LayoutGridProps>(e);
			if (lp && gp && lp->layout == Layout::InGrid && gp->lines != GridLines::None
				&& gp->lineThickness > 0.f && !gp->colWidths.empty() && !gp->rowHeights.empty()) {

				SDL::Color lc = gp->lineColor;
				lc.a = SDL::Clamp8((int)((float)lc.a * s.opacity));
				m_renderer.SetDrawColor(lc);

				float ox = r.x + lp->padding.left  - lp->scrollX;
				float oy = r.y + lp->padding.top   - lp->scrollY;
				float gap = lp->gap;
				float lt  = gp->lineThickness;
				float contentW = 0.f;
				for (float cw : gp->colWidths) contentW += cw;
				contentW += gap * (float)((int)gp->colWidths.size() - 1);
				float contentH = 0.f;
				for (float h : gp->rowHeights) contentH += h;
				contentH += gap * (float)((int)gp->rowHeights.size() - 1);

				// Vertical lines (between columns)
				if (gp->lines == GridLines::Columns || gp->lines == GridLines::Both) {
					float x = ox;
					for (int ci = 0; ci + 1 < (int)gp->colWidths.size(); ++ci) {
						x += gp->colWidths[ci];
						float lx = x + (gap - lt) * 0.5f;
						m_renderer.RenderFillRect(FRect{lx, oy, lt, contentH});
						x += gap;
					}
				}

				// Horizontal lines (between rows)
				if (gp->lines == GridLines::Rows || gp->lines == GridLines::Both) {
					float y = oy;
					for (int ri = 0; ri + 1 < (int)gp->rowHeights.size(); ++ri) {
						y += gp->rowHeights[ri];
						float ly = y + (gap - lt) * 0.5f;
						m_renderer.RenderFillRect(FRect{ox, ly, contentW, lt});
						y += gap;
					}
				}
			}

			// ── Inline scrollbars ─────────────────────────────────────────────────
			_DrawInlineScrollbars(e, r, s, w);
		}
		
		void _DrawLabel(ECS::EntityId e, const FRect &r, const Style &s, const WidgetState &st, const Widget &w) { 
			auto *c = m_ctx.Get<EditableContent>(e);
			auto *lp = m_ctx.Get<LayoutProps>(e);
			if (!c)
				return;
			SDL::Color tc = !Has(w.behavior, BehaviorFlag::Enable) ? s.textDisabledColor : st.hovered ? s.textHoveredColor
																	 : s.textColor;
			_Text(e, c->text, r.x + (lp ? lp->padding.left : 4.f), r.y + (r.h - _TH(e)) * 0.5f, tc, s.opacity, s);
		}

		void _DrawButton(ECS::EntityId e, const FRect &r, const Style &s, const WidgetState &st, const Widget &w) {
			const bool enabled = Has(w.behavior, BehaviorFlag::Enable);
			SDL::Color bdColor = (m_focused == e) ? s.bdFocusedColor
				: (st.hovered ? s.bdHoveredColor : s.bdColor);
			SDL::Color tc = !enabled ? s.textDisabledColor
				: (st.hovered ? s.textHoveredColor : s.textColor);
			_DrawBg(e, r, s, st, w);
			_StrokeRR(r, bdColor, s.borders, s.radius, s.opacity);

			auto *c  = m_ctx.Get<EditableContent>(e);
			auto *ic = m_ctx.Get<IconData>(e);

			// ── Icon ─────────────────────────────────────────────────────────
			float textX = 0.f;  // set when icon is drawn and text follows it
			if (ic && !ic->key.empty()) {
				auto tex = _EnsureTexture(ic->key);
				if (tex) {
					float sz    = SDL::Min(r.w, r.h) - ic->pad * 2.f;
					float textW = (c && !c->text.empty()) ? _TW(c->text, e) : 0.f;
					float iconX;
					if (textW > 0.f) {
						constexpr float kGap = 4.f;
						iconX = r.x + (r.w - (sz + kGap + textW)) * 0.5f;
						textX = iconX + sz + kGap;
					} else {
						iconX = r.x + (r.w - sz) * 0.5f;
					}
					float iconY = r.y + (r.h - sz) * 0.5f;

					float opacity = (!enabled ? ic->opacityDisabled
						: st.pressed ? ic->opacityPressed
						: st.hovered ? ic->opacityHovered
						: ic->opacityNormal) * s.opacity;
					const SDL::Color &tint = !enabled ? ic->tintDisabledColor
						: st.pressed ? ic->tintPressedColor
						: st.hovered ? ic->tintHoveredColor
						: ic->tintNormalColor;

					tex.SetAlphaMod(SDL::Clamp8((int)(opacity * 255.f)));
					tex.SetColorMod(tint.r, tint.g, tint.b);
					m_renderer.RenderTexture(tex, std::nullopt, FRect{iconX, iconY, sz, sz});
					tex.SetAlphaMod(255);
					tex.SetColorMod(255, 255, 255);
				}
			}

			// ── Text ─────────────────────────────────────────────────────────
			if (c && !c->text.empty()) {
				float tw = _TW(c->text, e), th = _TH(e);
				float tx_ = (textX > 0.f) ? textX : r.x + (r.w - tw) * 0.5f;
				_Text(e, c->text, tx_, r.y + (r.h - th) * 0.5f, tc, s.opacity, s);
			}
		}

		void _DrawToggle(ECS::EntityId e, const FRect &r, const Style &s, const WidgetState &st) { 
			auto *t = m_ctx.Get<ToggleData>(e);
			auto *c = m_ctx.Get<EditableContent>(e);
			auto *w = m_ctx.Get<Widget>(e);
			if (!t)
				return;
			constexpr float TW = 44.f, TH = 22.f;
			float ty = r.y + (r.h - TH) * 0.5f;
			FRect tr_ = {r.x + 8.f, ty, TW, TH};
			SDL::Color tc2 = {(Uint8)(s.trackColor.r + (s.fillColor.r - s.trackColor.r) * t->animT), (Uint8)(s.trackColor.g + (s.fillColor.g - s.trackColor.g) * t->animT), (Uint8)(s.trackColor.b + (s.fillColor.b - s.trackColor.b) * t->animT), s.trackColor.a};
			_FillRR(tr_, tc2, SDL::FCorners(TH) * 0.5f, s.opacity);
			_StrokeRR(tr_, (m_focused == e) ? s.bdFocusedColor : s.bdColor, s.borders, SDL::FCorners(TH) * 0.5f, s.opacity);
			float thumbR = (TH - 4.f) * 0.5f, thumbX = tr_.x + 2.f + thumbR + t->animT * (TW - 4.f - TH);
			_FillRR({thumbX - thumbR, ty + (TH - thumbR * 2.f) * 0.5f, thumbR * 2.f, thumbR * 2.f}, st.hovered ? s.thumbColor : SDL::Color{200, 202, 210, 255}, SDL::FCorners(thumbR), s.opacity);
			if (c && !c->text.empty()) {
				SDL::Color col = w && !Has(w->behavior, BehaviorFlag::Enable) ? s.textDisabledColor : s.textColor;
				_Text(e, c->text, tr_.x + TW + 10.f, r.y + (r.h - _TH(e)) * 0.5f, col, s.opacity, s);
			}
		}

		void _DrawRadio(ECS::EntityId e, const FRect &r, const Style &s, const WidgetState &st) { 
			auto *rd = m_ctx.Get<RadioData>(e);
			auto *c = m_ctx.Get<EditableContent>(e);
			auto *w = m_ctx.Get<Widget>(e);
			if (!rd)
				return;
			const float OR = 9.f;
			float cx_ = r.x + 14.f, cy_ = r.y + r.h * 0.5f;
			SDL::Color bgColor = !w || !Has(w->behavior, BehaviorFlag::Enable) ? s.bgDisabledColor : st.pressed ? s.bgPressedColor
														   : st.hovered   ? s.bgHoveredColor
																		  : s.bgColor;
			bgColor.a = (Uint8)(bgColor.a * s.opacity);
			m_renderer.SetDrawColor(bgColor);
			m_renderer.RenderCircle({cx_, cy_}, OR);
			SDL::Color bdColor = (m_focused == e) ? s.bdFocusedColor : st.hovered ? s.bdHoveredColor
																			: s.bdColor;
			bdColor.a = (Uint8)(bdColor.a * s.opacity);
			m_renderer.SetDrawColor(bdColor);
			m_renderer.RenderCircle({cx_, cy_}, OR);
			if (rd->checked) {
				SDL::Color fc = s.fillColor;
				fc.a = (Uint8)(fc.a * s.opacity);
				m_renderer.SetDrawColor(fc);
				m_renderer.RenderFillCircle({cx_, cy_}, OR * 0.5f);
			}
			if (c && !c->text.empty()) {
				SDL::Color tc = !w || !Has(w->behavior, BehaviorFlag::Enable) ? s.textDisabledColor : st.hovered ? s.textHoveredColor
																				: s.textColor;
				_Text(e, c->text, r.x + 30.f, r.y + (r.h - _TH(e)) * 0.5f, tc, s.opacity, s);
			}
		}

		void _DrawSlider(ECS::EntityId e, const FRect &r, const Style &s, const WidgetState &st, const Widget &w) {
			auto *sd = m_ctx.Get<SliderData>(e);
			auto *lp = m_ctx.Get<LayoutProps>(e);
			if (!sd || !lp) return;
			const float TH = 4.f, TR = 8.f;
			float norm = (sd->max > sd->min) ? (sd->val - sd->min) / (sd->max - sd->min) : 0.f;
			if (sd->orientation == Orientation::Horizontal) {
				float tx = r.x + lp->padding.left, bx_ = r.x + r.w - lp->padding.right, tw = bx_ - tx, mid = r.y + r.h * 0.5f;
				_FillRR({tx, mid - TH * 0.5f, tw, TH}, s.trackColor, SDL::FCorners(TH * 0.5f), s.opacity);
				if (norm > 0.f)
					_FillRR({tx, mid - TH * 0.5f, tw * norm, TH}, Has(w.behavior, BehaviorFlag::Enable) ? s.fillColor : s.trackColor, SDL::FCorners(TH * 0.5f), s.opacity);
				// Chapter markers
				for (float mk : sd->markers) {
					float mx = tx + tw * SDL::Clamp(mk, 0.f, 1.f);
					_FillRect({mx - 1.f, mid - TH * 2.f, 2.f, TH * 4.f}, {220, 185, 80, 210}, s.opacity);
				}
				float tcx = tx + tw * norm;
				SDL::Color tc = !Has(w.behavior, BehaviorFlag::Enable) ? s.textDisabledColor : (m_focused == e || sd->drag || st.hovered) ? s.thumbColor
																										 : SDL::Color{160, 170, 190, 255};
				_FillRR({tcx - TR, mid - TR, TR * 2.f, TR * 2.f}, tc, SDL::FCorners(TR), s.opacity);
				if (m_focused == e)
					_StrokeRR({tcx - TR, mid - TR, TR * 2.f, TR * 2.f}, s.bdFocusedColor, s.borders, SDL::FCorners(TR), s.opacity);
			}
			else {
				float ty_ = r.y + lp->padding.top, by_ = r.y + r.h - lp->padding.bottom, th_ = by_ - ty_, mid = r.x + r.w * 0.5f;
				_FillRR({mid - TH * 0.5f, ty_, TH, th_}, s.trackColor, SDL::FCorners(TH * 0.5f), s.opacity);
				if (norm > 0.f) {
					float fH = th_ * norm;
					_FillRR({mid - TH * 0.5f, by_ - fH, TH, fH}, Has(w.behavior, BehaviorFlag::Enable) ? s.fillColor : s.trackColor, SDL::FCorners(TH * 0.5f), s.opacity);
				}
				// Chapter markers (vertical)
				for (float mk : sd->markers) {
					float my = ty_ + th_ * SDL::Clamp(mk, 0.f, 1.f);
					_FillRect({mid - TH * 2.f, my - 1.f, TH * 4.f, 2.f}, {220, 185, 80, 210}, s.opacity);
				}
				float tcy = ty_ + th_ * (1.f - norm);
				SDL::Color tc = !Has(w.behavior, BehaviorFlag::Enable) ? s.textDisabledColor : (m_focused == e || sd->drag || st.hovered) ? s.thumbColor
																										 : SDL::Color{160, 170, 190, 255};
				_FillRR({mid - TR, tcy - TR, TR * 2.f, TR * 2.f}, tc, SDL::FCorners(TR), s.opacity);
			}
		}

		void _DrawScrollBar(ECS::EntityId e, const FRect &r, const Style &s, const WidgetState &st, const Widget &) { 
			auto *sb = m_ctx.Get<ScrollBarData>(e);
			if (!sb)
				return;
			_FillRR(r, s.trackColor, s.radius, s.opacity);
			if (sb->contentSize <= 0.f || sb->viewSize >= sb->contentSize)
				return;
			float ratio = sb->viewSize / sb->contentSize, maxO = sb->contentSize - sb->viewSize;
			float offN = (maxO > 0.f) ? sb->offset / maxO : 0.f;
			if (sb->orientation == Orientation::Vertical) {
				float tH = SDL::Max(20.f, r.h * ratio), tY = r.y + (r.h - tH) * offN;
				_FillRR({r.x + 1.f, tY, r.w - 2.f, tH}, (st.hovered || sb->drag) ? s.thumbColor : s.fillColor, s.radius, s.opacity);
			}
			else {
				float tW = SDL::Max(20.f, r.w * ratio), tX = r.x + (r.w - tW) * offN;
				_FillRR({tX, r.y + 1.f, tW, r.h - 2.f}, (st.hovered || sb->drag) ? s.thumbColor : s.fillColor, s.radius, s.opacity);
			}
		}

		void _DrawProgress(ECS::EntityId e, const FRect &r, const Style &s, const WidgetState &) { 
			auto *sd = m_ctx.Get<SliderData>(e);
			auto *lp = m_ctx.Get<LayoutProps>(e);
			if (!sd || !lp) return;
			float tx = r.x + lp->padding.left, tw = r.x + r.w - lp->padding.right - tx, norm = (sd->max > sd->min) ? (sd->val - sd->min) / (sd->max - sd->min) : 0.f;
			FRect tr_ = {tx, r.y + (r.h - 8.f) * 0.5f, tw, 8.f};
			_FillRR(tr_, s.trackColor, FCorners(4.f), s.opacity);
			if (norm > 0.f)
				_FillRR({tx, tr_.y, tw * norm, tr_.h}, s.fillColor, FCorners(4.f), s.opacity);
			_StrokeRR(tr_, s.bdColor, s.borders, FCorners(4.f), s.opacity);
		}
		
		void _DrawSeparator(const FRect &r, const Style &s) {
			_FillRect({r.x, r.y + r.h * 0.5f, r.w, 1.f}, s.separatorColor, s.opacity);
		}
		
		void _DrawInput(ECS::EntityId e, const FRect &r, const Style &s, const WidgetState &st, const Widget &w) {
			auto *c  = m_ctx.Get<EditableContent>(e);
			auto *lp = m_ctx.Get<LayoutProps>(e);
			if (!c || !lp) return;

			const bool foc     = (m_focused == e);
			const bool enabled = Has(w.behavior, BehaviorFlag::Enable);
			auto *d            = m_ctx.Get<InputData>(e);     // text filter mode
			auto *nv           = m_ctx.Get<NumericValue>(e);  // numeric value
			const bool hasArrows = (d != nullptr && nv != nullptr);

			// Fond et bordures
			SDL::Color bdColor = !enabled ? s.bdDisabledColor : foc ? s.bdFocusedColor
												: st.hovered ? s.bdHoveredColor
															 : s.bdColor;
			_DrawBg(e, r, s, st, w);
			_StrokeRR(r, bdColor, SDL::Max(s.borders, 1.f), s.radius, s.opacity);

			// ── Arrow buttons (numeric mode only) ───────────────────────────
			// Sit fully inside the Input's borders and inherit its right-edge
			// corner radii so they don't poke out of a rounded Input.
			float bw = 0.f;
			if (hasArrows) {
				bw = 20.f;
				const float br = SDL::Max(0.f, s.borders.right);
				const float bt = SDL::Max(0.f, s.borders.top);
				const float bb = SDL::Max(0.f, s.borders.bottom);
				const float innerH = SDL::Max(0.f, r.h - bt - bb);
				const float upH    = std::floor(innerH * 0.5f);
				const float downH  = innerH - upH;
				FRect up   = {r.x + r.w - bw - br, r.y + bt,        bw, upH};
				FRect down = {up.x,                up.y + up.h,     bw, downH};
				const bool inUp   = up.Contains(m_mousePos);
				const bool inDown = down.Contains(m_mousePos);
				SDL::Color upBg   = (d->pressUp   ? SDL::Color{40,45,60,220}
				                    : inUp       ? SDL::Color{75,80,100,210}
				                                 : SDL::Color{55,60,75,200});
				SDL::Color downBg = (d->pressDown ? SDL::Color{40,45,60,220}
				                    : inDown     ? SDL::Color{75,80,100,210}
				                                 : SDL::Color{55,60,75,200});
				// Mirror the Input's right-edge corner radii (deflated by
				// border thickness) so the buttons fit cleanly inside the
				// rounded interior.
				const float trR = SDL::Max(0.f, s.radius.tr - SDL::Max(bt, br));
				const float brR = SDL::Max(0.f, s.radius.br - SDL::Max(bb, br));
				const SDL::FCorners upRad   = {0.f, trR, 0.f, 0.f};
				const SDL::FCorners downRad = {0.f, 0.f, 0.f, brR};
				_FillRR(up,   upBg,   upRad,   s.opacity);
				_FillRR(down, downBg, downRad, s.opacity);
				m_renderer.SetDrawColor(s.textColor);
				float mx = up.x + bw * 0.5f, my = up.y + up.h * 0.5f;
				m_renderer.RenderLine({mx - 4.f, my + 2.f}, {mx, my - 2.f});
				m_renderer.RenderLine({mx + 4.f, my + 2.f}, {mx, my - 2.f});
				mx = down.x + bw * 0.5f; my = down.y + down.h * 0.5f;
				m_renderer.RenderLine({mx - 4.f, my - 2.f}, {mx, my + 2.f});
				m_renderer.RenderLine({mx + 4.f, my - 2.f}, {mx, my + 2.f});
				// Account for the right-border inset so the text clip below
				// stops short of the arrow zone (no text-bleed).
				bw += br;
			}

			// Text rendering region (clamped to leave room for arrows)
			float tx_ = r.x + lp->padding.left;
			float ty_ = r.y + (r.h - _TH(e)) * 0.5f;
			SDL::Rect prevClip = _PushClip(FRect{
				r.x, r.y,
				SDL::Max(0.f, r.w - bw - 1.f), r.h
			});

			// ── Selection background ──
			if (c->HasSelection() && !c->text.empty()) {
				float selStartX = tx_ + _TW(c->text.substr(0, c->SelMin()), e);
				float selWidth  = _TW(c->GetSelectedText(), e);
				SDL::Color hlC = c->highlightColor;
				hlC.a = SDL::Clamp8((int)((float)hlC.a * s.opacity));
				m_renderer.SetDrawColor(hlC);
				m_renderer.SetDrawBlendMode(SDL::BLENDMODE_BLEND);
				m_renderer.RenderFillRect(SDL::FRect{selStartX, ty_, selWidth, _TH(e)});
			}

			// ── Text + cursor ──
			// In numeric mode, when not focused show the formatted value rather
			// than the raw text (the user may have an in-progress unparsed edit).
			std::string display;
			if (hasArrows && !foc) display = _FormatNumeric(*nv);
			else                   display = c->text;

			const bool showPH = display.empty() && !c->placeholder.empty() && !foc;
			if (showPH) {
				_Text(e, c->placeholder, tx_, ty_, s.textPlaceholderColor, s.opacity, s);
			} else {
				_Text(e, display, tx_, ty_,
				      enabled ? s.textColor : s.textDisabledColor, s.opacity, s);
				if (foc && c->blinkTimer < 0.5f) {
					float cx_ = tx_ + _TW(c->text.substr(0, (size_t)SDL::Max(0, c->cursor)), e);
					_FillRect({cx_, ty_, 1.5f, _TH(e)}, s.textColor, s.opacity);
				}
			}

			_PopClip(prevClip);
		}

		// ── TextArea ────────────────────────────────────────────────────────
		void _DrawTextArea(ECS::EntityId e, const FRect &r, const Style &s, const WidgetState &st, const Widget &w) { 
			auto *ta  = m_ctx.Get<TextAreaData>(e);
			auto *lp  = m_ctx.Get<LayoutProps>(e);
			auto *cnt = m_ctx.Get<EditableContent>(e);
			if (!ta || !lp) return;

			const bool foc     = (m_focused == e);
			const bool enabled = Has(w.behavior, BehaviorFlag::Enable);

			// ── 1. Fond & Bordures ────────────────────────────────────────────────
			SDL::Color bgC = !enabled ? s.bgDisabledColor
						   : foc      ? s.bgFocusedColor
						   : st.hovered ? SDL::Color{30, 32, 44, 255}
										: s.bgColor;
			SDL::Color bdC = !enabled ? s.bdDisabledColor
						   : foc      ? s.bdFocusedColor
						   : st.hovered ? s.bdHoveredColor : s.bdColor;
			
			_FillRR(r, bgC, s.radius, s.opacity);
			_StrokeRR(r, bdC, SDL::Max(s.borders, 1.f), s.radius, s.opacity);

			// ── 2. Calcul des zones et scrollbars ─────────────────────────────────
			auto sv = _ComputeScrollView(r, *lp, w);
			const float viewW = sv.viewW;
			const float viewH = sv.viewH;

			// ── 3. Application du Clipping strict pour le texte ───────────────────
			SDL::Rect prevClip = _PushClip({
				r.x + lp->padding.left, r.y + lp->padding.top, viewW, viewH
			});

			// ── 4. Rendu du Placeholder (si vide) ─────────────────────────────────
			if (ta->text.empty() && cnt && !cnt->placeholder.empty() && !foc) {
				_Text(e, cnt->placeholder, r.x + lp->padding.left, r.y + lp->padding.top,
					  s.textPlaceholderColor, s.opacity, s);
			} 
			else {
				// ── 5. Préparation des coordonnées de rendu scrollées ─────────────
				const float lineH  = _TH(e) + 2.f;
				// On applique le décalage du scroll (X et Y) sur le point de départ
				const float startX = r.x + lp->padding.left - lp->scrollX;
				const float startY = r.y + lp->padding.top  - lp->scrollY;

				// Optimisation : on ne dessine que les lignes visibles
				int firstLine = SDL::Max(0, (int)(lp->scrollY / lineH) - 1);
				int lastLine  = SDL::Min(ta->LineCount() - 1, (int)((lp->scrollY + viewH) / lineH) + 1);

				// ── 6. Rendu des sélections ───────────────────────────────────────
				if (ta->HasSelection()) {
					int selMin = ta->SelMin(), selMax = ta->SelMax();
					SDL::Color hlC = ta->highlightColor;
					hlC.a = SDL::Clamp8((int)((float)hlC.a * s.opacity));
					m_renderer.SetDrawColor(hlC);
					m_renderer.SetDrawBlendMode(SDL::BLENDMODE_BLEND);

					for (int ln = firstLine; ln <= lastLine; ++ln) {
						int ls = ta->LineStart(ln);
						int le = ta->LineEnd(ln);
						if (le < selMin || ls > selMax) continue;
						
						int hiStart = SDL::Max(ls, selMin);
						int hiEnd   = SDL::Min(le, selMax);
						
						float x0 = startX + _TextAreaLineX(ta, ln, hiStart - ls, e);
						float x1 = startX + _TextAreaLineX(ta, ln, hiEnd   - ls, e);
						
						// Extension visuelle de la sélection jusqu'en fin de ligne si la sélection continue
						if (hiEnd == le && selMax > le) x1 = startX + lp->contentW + 8.f; 
						
						float ly = startY + ln * lineH;
						m_renderer.RenderFillRect(SDL::FRect{x0, ly, SDL::Max(2.f, x1 - x0), lineH});
					}
				}

				// ── 7. Rendu du texte (avec support du Rich Text / Spans) ─────────
				for (int ln = firstLine; ln <= lastLine; ++ln) {
					int ls = ta->LineStart(ln);
					int le = ta->LineEnd(ln);
					float ly = startY + ln * lineH;

					float xOff = startX; // Le curseur de rendu horizontal pour cette ligne
					
					for (int ci = ls; ci < le; ) {
						int spanEnd = le;
						const TextSpanStyle *spanStyle = ta->SpanStyleAt(ci);
						
						int ni = ci + 1;
						while (ni < le && ta->SpanStyleAt(ni) == spanStyle) ++ni;
						spanEnd = ni;

						std::string run;
						auto _rf = _ResolveFont(e);
						float charW = (_rf.size > 0.f) ? _rf.size : (float)SDL::DEBUG_TEXT_FONT_CHARACTER_SIZE;
#if UI_HAS_TTF
						if (!_rf.isDebug && !_rf.key.empty() && _rf.size > 0.f) {
							if (auto font = _EnsureFont(_rf.key, _rf.size)) {
								int sw = 0, sh = 0;
								font.GetStringSize(" ", &sw, &sh);
								charW = (float)sw;
							}
						}
#endif
						int visualCol = (ci > ls) ? (int)((xOff - startX) / charW) : 0; 
						
						for (int k = ci; k < spanEnd; ++k) {
							char ch = ta->text[k];
							if (ch == '\t') {
								if (!run.empty()) {
									SDL::Color tc = enabled ? s.textColor : s.textDisabledColor;
									if (spanStyle && spanStyle->color.a > 0) tc = spanStyle->color;
									_Text(e, run, xOff, ly, tc, s.opacity, s);
									xOff += _TW(run, e);
									run.clear();
								}
								int spaces = ta->tabSize - (visualCol % ta->tabSize);
								if (spaces == 0) spaces = ta->tabSize;
								xOff += spaces * charW;
								visualCol += spaces;
							} else {
								run += ch;
								++visualCol;
							}
						}
						if (!run.empty()) {
							float runX = xOff;
							float runW = _TW(run, e);
							SDL::Color tc = enabled ? s.textColor : s.textDisabledColor;
							if (spanStyle && spanStyle->color.a > 0) tc = spanStyle->color;
							// Highlight background
							if (spanStyle && spanStyle->highlight) {
								SDL::Color hc = spanStyle->highlightColor;
								hc.a = SDL::Clamp8((int)((float)hc.a * s.opacity));
								m_renderer.SetDrawColor(hc);
								m_renderer.RenderFillRect(SDL::FRect{runX, ly, runW, _TH(e)});
							}
							_Text(e, run, xOff, ly, tc, s.opacity, s);
							// Underline
							if (spanStyle && spanStyle->underline) {
								SDL::Color uc = tc; uc.a = SDL::Clamp8(uc.a * s.opacity);
								m_renderer.SetDrawColor(uc);
								float ulY = ly + _TH(e) + 1.f;
								m_renderer.RenderLine({runX, ulY}, {runX + runW, ulY});
							}
							// Strikethrough
							if (spanStyle && spanStyle->strikethrough) {
								SDL::Color sc2 = tc; sc2.a = SDL::Clamp8(sc2.a * s.opacity);
								m_renderer.SetDrawColor(sc2);
								float stY = ly + _TH(e) * 0.5f;
								m_renderer.RenderLine({runX, stY}, {runX + runW, stY});
							}
							xOff += runW;
						}
						ci = spanEnd;
					}
				}

				// ── 8. Rendu du Curseur clignotant ────────────────────────────────
				if (foc && !ta->readOnly && ta->blinkTimer < 0.5f) {
					int curLine = ta->LineOf(ta->cursorPos);
					int curCol  = ta->ColOf(ta->cursorPos);
					
					float cx_ = startX + _TextAreaLineX(ta, curLine, curCol, e);
					float cy_ = startY + curLine * lineH;
					
					_FillRect({cx_, cy_, 1.5f, lineH}, s.textColor, s.opacity);
				}
			} // Fin du bloc (Texte non-vide)

			// ── 9. Restauration du Clip parent ────────────────────────────────────
			_PopClip(prevClip);

			// ── 10. Rendu des Scrollbars unifiées ─────────────────────────────────
			// Dessinées en dernier, PARDESSUS le texte, hors du clip restrictif.
			_DrawInlineScrollbars(e, r, s, w);
		}

		void _DrawKnob(ECS::EntityId e, const FRect &r, const Style &s, const WidgetState &st, const Widget &w) {
			auto *kd = m_ctx.Get<KnobData>(e);
			if (!kd) return;

			// Sécurité : Si le widget est trop petit, on ne dessine rien pour éviter le crash
			float minDim = SDL::Min(r.w, r.h);
			if (minDim < 10.f) return; 

			float cx_ = r.x + r.w * 0.5f;
			float cy_ = r.y + r.h * 0.5f;
			float oR = minDim * 0.5f - 2.f;
			float iR = oR * 0.55f;

			if (oR <= 0.f) return; // Ultime sécurité

			// Couleur de fond
			SDL::Color bgColor = !Has(w.behavior, BehaviorFlag::Enable) ? s.bgDisabledColor 
							: st.pressed ? s.bgPressedColor
							: st.hovered ? s.bgHoveredColor : s.bgColor;
			bgColor.a = (Uint8)(bgColor.a * s.opacity);
			m_renderer.SetDrawColor(bgColor);
			m_renderer.RenderFillCircle({cx_, cy_}, oR);

			// Bordure
			SDL::Color bdColor = (m_focused == e) ? s.bdFocusedColor 
							: st.hovered ? s.bdHoveredColor : s.bdColor;
			bdColor.a = (Uint8)(bdColor.a * s.opacity);
			m_renderer.SetDrawColor(bdColor);
			m_renderer.RenderCircle({cx_, cy_}, oR);

			float norm = (kd->max > kd->min) ? (kd->val - kd->min) / (kd->max - kd->min) : 0.5f;
			norm = SDL::Clamp(norm, 0.f, 1.f);

			SDL::Color fillC = Has(w.behavior, BehaviorFlag::Enable) ? s.fillColor : s.textDisabledColor;
			fillC.a = (Uint8)(fillC.a * s.opacity);

			if (kd->shape == KnobShape::Potentiometer) {
				// ── Potentiometer style ─────────────────────────────────────────
				// Inner dial ring (thick border effect)
				SDL::Color ringC = s.trackColor;
				ringC.a = (Uint8)(ringC.a * s.opacity);
				m_renderer.SetDrawColor(ringC);
				for (float dr = 0.f; dr < 3.f; dr += 1.f)
					m_renderer.RenderCircle({cx_, cy_}, iR - dr);

				// Pointer line from center to rim
				float aRad = (135.f + norm * 270.f) * (SDL::PI_F / 180.f);
				float px1 = cx_ + SDL::Cos(aRad) * (iR * 0.25f);
				float py1 = cy_ + SDL::Sin(aRad) * (iR * 0.25f);
				float px2 = cx_ + SDL::Cos(aRad) * (oR - 2.f);
				float py2 = cy_ + SDL::Sin(aRad) * (oR - 2.f);
				m_renderer.SetDrawColor(fillC);
				for (float dt = -1.f; dt <= 1.f; dt += 1.f) {
					float perpX = -SDL::Sin(aRad) * dt;
					float perpY =  SDL::Cos(aRad) * dt;
					m_renderer.RenderLine({px1 + perpX, py1 + perpY}, {px2 + perpX, py2 + perpY});
				}
			} else {
				// ── Arc style (default) ─────────────────────────────────────────
				// Track (fond de l'arc) : 135° à 45° (sens horaire)
				SDL::Color trackC = s.trackColor;
				trackC.a = (Uint8)(trackC.a * s.opacity);
				m_renderer.SetDrawColor(trackC);
				m_renderer.RenderArc({cx_, cy_}, iR, 135.f, 360.f);
				m_renderer.RenderArc({cx_, cy_}, iR, 0.f, 45.f);

				// Remplissage de la valeur
				m_renderer.SetDrawColor(fillC);
				if (norm > 0.001f) {
					float endAngle = 135.f + (norm * 270.f);
					if (endAngle > 360.f) {
						m_renderer.RenderArc({cx_, cy_}, iR, 135.f, 360.f);
						m_renderer.RenderArc({cx_, cy_}, iR, 0.f, endAngle - 360.f);
					} else {
						m_renderer.RenderArc({cx_, cy_}, iR, 135.f, endAngle);
					}
				}

				// Point indicateur (Thumb)
				float aRad = (135.f + norm * 270.f) * (SDL::PI_F / 180.f);
				float lx = cx_ + SDL::Cos(aRad) * iR;
				float ly = cy_ + SDL::Sin(aRad) * iR;
				_FillRR({lx - 4.f, ly - 4.f, 8.f, 8.f}, fillC, SDL::FCorners(4.f), s.opacity);
			}
		}

		void _DrawImage(ECS::EntityId e, const FRect &r, const Style &s, const WidgetState &) { 
			auto *d = m_ctx.Get<ImageData>(e);
			if (!d || d->key.empty()) {
				_FillRR(r, s.bgColor, s.radius, s.opacity);
				_StrokeRR(r, s.bdColor, s.borders, s.radius, s.opacity);
				return;
			}
			
			auto texture = _EnsureTexture(d->key);
			if (!texture) {
				_FillRR(r, {60, 20, 20, 200}, s.radius, s.opacity);
				return;
			}

			SDL::Point tsz = texture.GetSize();
			float tw = (float)tsz.x, th = (float)tsz.y;
			FRect dst = r; 
			switch (d->fit) {
				case ImageFit::Fill:
					dst = r;
					break;
				case ImageFit::Contain: {
					float sc = SDL::Min(r.w / tw, r.h / th);
					dst = {r.x + (r.w - tw * sc) * 0.5f, r.y + (r.h - th * sc) * 0.5f, tw * sc, th * sc};
					break;
				}
				case ImageFit::Cover: {
					float sc = SDL::Max(r.w / tw, r.h / th);
					dst = {r.x + (r.w - tw * sc) * 0.5f, r.y + (r.h - th * sc) * 0.5f, tw * sc, th * sc};
					break;
				}
				case ImageFit::None:
					dst = {r.x, r.y, tw, th};
					break;
				case ImageFit::Tile:
					for (float iy = r.y; iy < r.y + r.h; iy += th) {
						for (float ix = r.x; ix < r.x + r.w; ix += tw) {
							FRect td{ix, iy, SDL::Min(tw, r.x + r.w - ix), SDL::Min(th, r.y + r.h - iy)};
							texture.SetAlphaMod(SDL::Clamp8(255 * s.opacity));
							m_renderer.RenderTexture(texture, std::nullopt, td);
						}
					}
					texture.SetAlphaMod(255);
					return;
			}
			texture.SetAlphaMod(SDL::Clamp8(255 * s.opacity));
			m_renderer.RenderTexture(texture, std::nullopt, dst);
			texture.SetAlphaMod(255);
		}
		
		void _DrawCanvas(ECS::EntityId e, const FRect &r) { 
			auto *cd = m_ctx.Get<CanvasData>(e);
			if (!cd || !cd->renderCb) return;

			// The renderCb typically sets its own SDL viewport/scissor.
			// We save/restore the outer clip rect so the UI compositing
			// remains correct after the canvas has drawn into its rect.
			SDL::Rect prevClip = m_renderer.GetClipRect();

			cd->renderCb(m_renderer, r);

			// Restore renderer state for the parent UI layer.
			if (prevClip.w > 0 && prevClip.h > 0)
				m_renderer.SetClipRect(prevClip);
			else
				m_renderer.SetClipRect({});
			m_renderer.SetViewport({});
		}

		// ── ListBox draw ──────────────────────────────────────────────
		void _DrawListBox(ECS::EntityId e, const FRect &r, const Style &s,
						  const WidgetState &st, const Widget &w) {
			auto *lb  = m_ctx.Get<ListBoxData>(e);
			auto *lp  = m_ctx.Get<LayoutProps>(e);
			if (!lb || !lp) return;

			// Background + focused/hovered border
			SDL::Color bg = st.focused ? s.bgFocusedColor
						  : st.hovered ? s.bgHoveredColor
						  : s.bgColor;
			_FillRR(r, bg, s.radius, s.opacity);
			_StrokeRR(r, st.focused ? s.bdFocusedColor : st.hovered ? s.bdHoveredColor : s.bdColor,
					  s.borders, s.radius, s.opacity);

			const float ih    = lb->itemHeight;
			const float iy    = r.y + lp->padding.top;
			const float charH = _TH(e);

			auto sv = _ComputeScrollView(r, *lp, w);
			const float viewH = sv.viewH;
			const bool  showY = sv.showY;
			const float t     = lp->scrollbarThickness;
			const float itemW = r.w - s.borders.left - s.borders.right - (showY ? t : 0.f);
			const float px    = r.x + lp->padding.left - lp->scrollX;

			SDL::Rect prevClip = _PushClip({
				r.x + 1.f, iy, r.w - 2.f - (showY ? t : 0.f), viewH
			});

			int firstIdx = SDL::Max(0, (int)(lp->scrollY / ih));
			int lastIdx  = SDL::Min((int)lb->items.size(),
									firstIdx + (int)(viewH / ih) + 2);

			for (int i = firstIdx; i < lastIdx; ++i) {
				if (lb->dragActive && i == lb->dragSrcIdx) continue; // drawn as ghost
				float ry    = iy + (float)i * ih - lp->scrollY;
				FRect itemR = {r.x + s.borders.left, ry, itemW, ih};
				bool  isSel = (i == lb->selectedIndex);
				bool  isHov = (!lb->dragActive
							   && m_mousePos.y >= ry && m_mousePos.y < ry + ih
							   && m_mousePos.x >= r.x && m_mousePos.x < r.x + r.w - (showY ? t : 0.f));

				if (isSel) {
					SDL::Color c = s.bgCheckedColor;
					c.a = SDL::Clamp8((int)((float)c.a * s.opacity));
					m_renderer.SetDrawColor(c);
					m_renderer.RenderFillRect(itemR);
				} else if (isHov) {
					SDL::Color c = s.bgHoveredColor;
					c.a = SDL::Clamp8((int)((float)c.a * s.opacity * 0.55f));
					m_renderer.SetDrawColor(c);
					m_renderer.RenderFillRect(itemR);
				}

				SDL::Color tc = isSel ? s.textCheckedColor : s.textColor;
				_Text(e, lb->items[(size_t)i],
					  px, ry + (ih - charH) * 0.5f,
					  tc, s.opacity, s);
			}

			// Drop indicator line at dragDstIdx
			if (lb->dragActive && lb->dragDstIdx >= 0) {
				float lineY = iy + (float)lb->dragDstIdx * ih - lp->scrollY + ih - 1.f;
				lineY = SDL::Clamp(lineY, iy, iy + viewH - 1.f);
				m_renderer.SetDrawBlendMode(SDL::BLENDMODE_BLEND);
				m_renderer.SetDrawColor({100, 160, 235, 220});
				FRect lineR = {r.x + s.borders.left + 2.f, lineY, itemW - 4.f, 2.f};
				m_renderer.RenderFillRect(lineR);
			}

			_PopClip(prevClip);

			// Drag ghost: draw the dragged item floating under the cursor (outside clip)
			if (lb->dragActive && lb->dragSrcIdx >= 0
			    && lb->dragSrcIdx < (int)lb->items.size()) {
				float ghostY = lb->dragY - ih * 0.5f;
				FRect ghostR = {r.x + s.borders.left, ghostY, itemW, ih};
				m_renderer.SetDrawBlendMode(SDL::BLENDMODE_BLEND);
				SDL::Color gc = s.bgCheckedColor;
				gc.a = SDL::Clamp8((int)((float)gc.a * 0.85f));
				m_renderer.SetDrawColor(gc);
				m_renderer.RenderFillRect(ghostR);
				// ghost border
				SDL::Color bc = {100, 160, 235, 200};
				m_renderer.SetDrawColor(bc);
				m_renderer.RenderRect(ghostR);
				// ghost text
				_Text(e, lb->items[(size_t)lb->dragSrcIdx],
				      px, ghostY + (ih - charH) * 0.5f,
				      s.textCheckedColor, 0.9f, s);
			}

			// Dessin des scrollbars (désormais pardessus et hors du clip des items)
			_DrawInlineScrollbars(e, r, s, w);
		}

		// ── Graph draw ────────────────────────────────────────────────
		void _DrawGraph(ECS::EntityId e, const FRect &r, const Style &s,
						const WidgetState &st) {
			auto *gd = m_ctx.Get<GraphData>(e);
			if (!gd) return;

			// Save the outer clip (set by _RenderNode) so we can restore it after
			// clipping to the plot area, and so all sub-clips are properly intersected.
			const SDL::Rect outerClip = m_renderer.GetClipRect();

			_FillRR(r, s.bgColor, s.radius, s.opacity);
			_StrokeRR(r, st.hovered ? s.bdHoveredColor : s.bdColor, s.borders, s.radius, s.opacity);

			const float op    = s.opacity;
			const float charH = _TH(e);

			// Margins: leave space for Y tick labels (left) and X tick labels (bottom).
			float ml = 38.f, mr = 6.f, mt = 6.f, mb = charH + 10.f;
			if (!gd->title.empty())  mt += charH + 4.f;
			if (!gd->yLabel.empty()) ml += charH + 2.f;
			if (!gd->xLabel.empty()) mb += charH + 2.f;

			FRect plot = { r.x + ml, r.y + mt, r.w - ml - mr, r.h - mt - mb };
			if (plot.w < 4.f || plot.h < 4.f) return;

			// Title
			if (!gd->title.empty()) {
				SDL::Color tc = s.textColor;
				float tw = _TW(gd->title, e);
				_Text(e, gd->title, plot.x + (plot.w - tw) * 0.5f, r.y + 2.f, tc, op, s);
			}
			// X label
			if (!gd->xLabel.empty()) {
				SDL::Color tc = s.textColor;
				float tw = _TW(gd->xLabel, e);
				_Text(e, gd->xLabel, plot.x + (plot.w - tw) * 0.5f,
					  r.y + r.h - charH - 2.f, tc, op, s);
			}
			// Y label — one character per line (vertical text approximation)
			if (!gd->yLabel.empty()) {
				SDL::Color ac = gd->axisColor; ac.a = SDL::Clamp8((int)((float)ac.a * op));
				float totalH  = charH * (float)gd->yLabel.size();
				float startY  = plot.y + (plot.h - totalH) * 0.5f;
				for (int ci = 0; ci < (int)gd->yLabel.size(); ++ci) {
					std::string ch(1, gd->yLabel[(size_t)ci]);
					_Text(e, ch, r.x + 2.f, startY + (float)ci * charH, ac, op, s);
				}
			}

			// Plot background
			m_renderer.SetDrawColor({12, 13, 20, (uint8_t)(220 * op)});
			m_renderer.RenderFillRect(plot);

			float range = gd->maxVal - gd->minVal;
			if (range == 0.f) range = 1.f;

			// ── Grid & Y-axis tick labels ────────────────────────────────────────
			for (int i = 0; i <= gd->yDivisions; ++i) {
				float t   = (float)i / (float)gd->yDivisions;
				float gy  = plot.y + plot.h * t;
				float val = gd->maxVal - t * range;

				SDL::Color gc = gd->gridColor; gc.a = SDL::Clamp8((int)((float)gc.a * op));
				m_renderer.SetDrawColor(gc);
				m_renderer.RenderLine(SDL::FPoint{plot.x, gy}, SDL::FPoint{plot.x + plot.w, gy});

				std::string lbl;
				float absVal = std::abs(val);
				if (absVal >= 10000.f)      lbl = std::format("{:.0f}k", val / 1000.f);
				else if (absVal >= 1000.f)  lbl = std::format("{:.1f}k", val / 1000.f);
				else if (absVal >= 10.f)    lbl = std::format("{:.0f}", val);
				else if (absVal >= 0.01f)   lbl = std::format("{:.2f}", val);
				else                        lbl = std::format("{:.0f}", val);

				SDL::Color ac = gd->axisColor; ac.a = SDL::Clamp8((int)((float)ac.a * op));
				float tw = _TW(lbl, e);
				_Text(e, lbl, plot.x - tw - 3.f, gy - charH * 0.5f, ac, op, s);
			}

			// ── Grid & X-axis tick labels ────────────────────────────────────────
			for (int i = 0; i <= gd->xDivisions; ++i) {
				float t    = (float)i / (float)gd->xDivisions;
				float gx   = plot.x + plot.w * t;
				float xval = gd->xMin + t * (gd->xMax - gd->xMin);

				if (gd->logFreq && gd->xMin > 0.f && gd->xMax > gd->xMin) {
					float logMin = std::log10(gd->xMin);
					float logMax = std::log10(gd->xMax);
					xval = std::pow(10.f, logMin + t * (logMax - logMin));
					gx   = plot.x + t * plot.w;
				}

				SDL::Color gc = gd->gridColor; gc.a = SDL::Clamp8((int)((float)gc.a * op));
				m_renderer.SetDrawColor(gc);
				m_renderer.RenderLine(SDL::FPoint{gx, plot.y}, SDL::FPoint{gx, plot.y + plot.h});

				std::string lbl;
				float absX = std::abs(xval);
				if (absX >= 10000.f)       lbl = std::format("{:.0f}k", xval / 1000.f);
				else if (absX >= 1000.f)   lbl = std::format("{:.1f}k", xval / 1000.f);
				else if (absX >= 1.f)      lbl = std::format("{:.0f}", xval);
				else if (absX >= 0.01f)    lbl = std::format("{:.2f}", xval);
				else                       lbl = "0";

				SDL::Color ac = gd->axisColor; ac.a = SDL::Clamp8((int)((float)ac.a * op));
				float tw = _TW(lbl, e);
				_Text(e, lbl, gx - tw * 0.5f, plot.y + plot.h + 3.f, ac, op, s);
			}

			if (gd->data.empty()) return;

			// Clip data rendering to the plot area, intersected with the outer widget
			// clip so graph content never overflows a scrolled ancestor container.
			m_renderer.SetClipRect(_ClipIntersect(outerClip, FRectToRect(plot)));

			int n = (int)gd->data.size();

			auto yScr = [&](float v) -> float {
				return plot.y + plot.h * (1.f - (SDL::Clamp(v, gd->minVal, gd->maxVal) - gd->minVal) / range);
			};

			// Map data index → screen X, respecting log scale
			auto xScr = [&](int i) -> float {
				float t = (n > 1) ? (float)i / (float)(n - 1) : 0.f;
				if (gd->logFreq && gd->xMin > 0.f && gd->xMax > gd->xMin) {
					// Map freq linearly through log space
					float freqAtI = gd->xMin + (gd->xMax - gd->xMin) * t;
					float logMin  = std::log10(gd->xMin);
					float logMax  = std::log10(gd->xMax);
					float logFreq = (freqAtI > 0.f) ? std::log10(freqAtI) : logMin;
					t = (logFreq - logMin) / (logMax - logMin);
				}
				return plot.x + t * plot.w;
			};

			if (gd->barMode) {
				// ── Bar mode (spectrum) ───────────────────────────────────────────
				SDL::Color fc = gd->fillColor;  fc.a = SDL::Clamp8((int)((float)fc.a * op));
				SDL::Color lc = gd->lineColor;  lc.a = SDL::Clamp8((int)((float)lc.a * op));
				float barW = SDL::Max(1.f, plot.w / (float)n);
				for (int i = 0; i < n; ++i) {
					float x1 = plot.x + barW * (float)i;
					float y1 = yScr(gd->data[(size_t)i]);
					float y2 = plot.y + plot.h;
					m_renderer.SetDrawColor(fc);
					m_renderer.RenderFillRect(SDL::FRect{x1, y1, SDL::Max(1.f, barW - 1.f), y2 - y1});
					if (barW > 2.f) {
						m_renderer.SetDrawColor(lc);
						m_renderer.RenderFillRect(SDL::FRect{x1, y1, SDL::Max(1.f, barW - 1.f), 1.f});
					}
				}
			} else {
				// ── Line / fill mode (waveform) ───────────────────────────────────
				float baseline = yScr(SDL::Clamp(0.f, gd->minVal, gd->maxVal));

				if (gd->showFill) {
					SDL::Color fc = gd->fillColor; fc.a = SDL::Clamp8((int)((float)fc.a * op));
					m_renderer.SetDrawColor(fc);
					for (int i = 0; i < n - 1; ++i) {
						float x1 = xScr(i),     y1 = yScr(gd->data[(size_t)i]);
						float x2 = xScr(i + 1), y2 = yScr(gd->data[(size_t)(i + 1)]);
						// Vertical fill strip between curve and baseline
						float top = SDL::Min(SDL::Min(y1, y2), baseline);
						float bot = SDL::Max(SDL::Max(y1, y2), baseline);
						m_renderer.RenderFillRect(SDL::FRect{x1, top, SDL::Max(1.f, x2 - x1), bot - top});
					}
				}
				// Line
				SDL::Color lc = gd->lineColor; lc.a = SDL::Clamp8((int)((float)lc.a * op));
				m_renderer.SetDrawColor(lc);
				for (int i = 0; i < n - 1; ++i) {
					float x1 = xScr(i),     y1 = yScr(gd->data[(size_t)i]);
					float x2 = xScr(i + 1), y2 = yScr(gd->data[(size_t)(i + 1)]);
					m_renderer.RenderLine(SDL::FPoint{x1, y1}, SDL::FPoint{x2, y2});
				}
			}

			// Restore the outer widget clip.
			m_renderer.SetClipRect(outerClip);
		}

		// ── ListBox keyboard handler ──────────────────────────────────
		void _HandleKeyDownListBox(SDL::Keycode k) {
			auto *lb  = m_ctx.Get<ListBoxData>(m_focused); 
			auto *cb  = m_ctx.Get<Callbacks>(m_focused);
			auto *cr  = m_ctx.Get<ComputedRect>(m_focused);
			auto *lp  = m_ctx.Get<LayoutProps>(m_focused);
			if (!lb || lb->items.empty()) return;

			int prev = lb->selectedIndex;
			int n    = (int)lb->items.size();
			switch (k) { 
				case SDL::KEYCODE_UP:
					lb->selectedIndex = (lb->selectedIndex > 0) ? lb->selectedIndex - 1
									  : (prev < 0)              ? 0 : 0;
					break;
				case SDL::KEYCODE_DOWN:
					lb->selectedIndex = (lb->selectedIndex < 0)       ? 0
									  : (lb->selectedIndex < n - 1)   ? lb->selectedIndex + 1
									  : n - 1;
					break;
				case SDL::KEYCODE_HOME: lb->selectedIndex = 0;     break;
				case SDL::KEYCODE_END:  lb->selectedIndex = n - 1; break;
				case SDL::KEYCODE_RETURN:
				case SDL::KEYCODE_RETURN2:
				case SDL::KEYCODE_KP_ENTER:
					if (lb->selectedIndex >= 0 && cb && cb->onClick) cb->onClick();
					return;
				case SDL::KEYCODE_ESCAPE:
					_SetFocus(ECS::NullEntity);
					return;
				default: return;
			}
			// Auto-scroll to keep selection visible
			if (cr && lp && lb->selectedIndex >= 0) {
				float viewH = cr->screen.h - lp->padding.top - lp->padding.bottom;
				float itemY = (float)lb->selectedIndex * lb->itemHeight;
				if (itemY < lp->scrollY)
					lp->scrollY = itemY;
				else if (itemY + lb->itemHeight > lp->scrollY + viewH)
					lp->scrollY = SDL::Max(0.f, itemY + lb->itemHeight - viewH);
			}
			if (lb->selectedIndex != prev && cb && cb->onChange)
				cb->onChange((float)lb->selectedIndex);
		}

		void _OnClickListBox(ECS::EntityId e) {
			if (auto *lb = m_ctx.Get<ListBoxData>(e)) {
				auto *cr2 = m_ctx.Get<ComputedRect>(e);
				auto *lp2 = m_ctx.Get<LayoutProps>(e);
				if (cr2 && lp2) {
					float iy  = cr2->screen.y + lp2->padding.top;
					int   idx = (int)((m_mousePos.y - iy + lp2->scrollY) / lb->itemHeight);
					if (idx >= 0 && idx < (int)lb->items.size()) {
						lb->selectedIndex = idx;
						auto *cb  = m_ctx.Get<Callbacks>(m_focused);
						if (cb && cb->onChange) cb->onChange((float)idx);
						if (cb && cb->onClick)  cb->onClick();
					}
				}
			}
		
		}

		// ── ComboBox helpers ───────────────────────────────────────────────────────

		void _OnClickComboBox(ECS::EntityId e) {
			auto *d  = m_ctx.Get<ComboBoxData>(e);
			auto *cb = m_ctx.Get<Callbacks>(e);
			if (!d) return;
			d->open = !d->open;
			m_comboOpen = d->open ? e : ECS::NullEntity;
			if (!d->open && cb && cb->onClick) cb->onClick();
		}

		/// Try to handle a click on a currently-open ComboBox dropdown item.
		/// Returns true if the click was consumed.
		bool _TryComboBoxClick() {
			if (m_comboOpen == ECS::NullEntity || !m_ctx.IsAlive(m_comboOpen)) {
				m_comboOpen = ECS::NullEntity;
				return false;
			}
			auto *d  = m_ctx.Get<ComboBoxData>(m_comboOpen);
			auto *cr = m_ctx.Get<ComputedRect>(m_comboOpen);
			auto *cb = m_ctx.Get<Callbacks>(m_comboOpen);
			if (!d || !cr) { m_comboOpen = ECS::NullEntity; return false; }

			const FRect &drop = d->dropRect;
			if (!drop.HasIntersection(FRect{m_mousePos.x, m_mousePos.y, 1.f, 1.f})) {
				d->open = false;
				m_comboOpen = ECS::NullEntity;
				return true;
			}
			float ry = m_mousePos.y - drop.y;
			int idx = (int)(ry / SDL::Max(1.f, (float)d->itemHeight));
			idx = SDL::Clamp(idx, 0, (int)d->items.size() - 1);
			if (idx != d->selectedIndex) {
				d->selectedIndex = idx;
				if (cb && cb->onChange) cb->onChange((float)idx);
			}
			d->open = false;
			m_comboOpen = ECS::NullEntity;
			if (cb && cb->onClick) cb->onClick();
			return true;
		}

		// ── TabView helper ─────────────────────────────────────────────────────────

		void _OnClickTabView(ECS::EntityId e) {
			auto *d  = m_ctx.Get<TabViewData>(e);
			auto *cr = m_ctx.Get<ComputedRect>(e);
			auto *cb = m_ctx.Get<Callbacks>(e);
			if (!d || !cr) return;
			float bx = cr->screen.x;
			float x  = m_mousePos.x - bx;
			float cumX = 0.f;
			for (int i = 0; i < (int)d->tabs.size(); ++i) {
				float tw = (i < (int)d->tabWidths.size()) ? d->tabWidths[i] : 80.f;
				if (x >= cumX && x < cumX + tw) {
					if (d->activeTab != i) {
						d->activeTab = i;
						// Show/hide children according to activeTab.
						auto *ch2 = m_ctx.Get<Children>(e);
						if (ch2) {
							for (int j = 0; j < (int)ch2->ids.size(); ++j) {
								auto *cw = m_ctx.Get<Widget>(ch2->ids[j]);
								if (cw) {
									if (j == d->activeTab)
										cw->behavior |= BehaviorFlag::Visible;
									else
										cw->behavior &= ~BehaviorFlag::Visible;
								}
							}
						}
						m_layoutDirty = true;
						if (cb && cb->onChange) cb->onChange((float)i);
					}
					break;
				}
				cumX += tw;
			}
			if (cb && cb->onClick) cb->onClick();
		}

		// ── Expander helper ────────────────────────────────────────────────────────

		void _OnClickTree(ECS::EntityId e) {
			auto *d  = m_ctx.Get<TreeData>(e);
			auto *lp = m_ctx.Get<LayoutProps>(e);
			auto *cr = m_ctx.Get<ComputedRect>(e);
			auto *cb = m_ctx.Get<Callbacks>(e);
			if (!d || !lp || !cr) return;

			std::vector<int> visible = _GetVisibleTreeNodes(d->nodes);
			float y0 = cr->screen.y + lp->padding.top - d->scrollY;

			for (int vi = 0; vi < (int)visible.size(); ++vi) {
				int ni = visible[vi];
				float iy = y0 + vi * d->itemHeight;
				FRect itemR = {cr->screen.x, iy, cr->screen.w, d->itemHeight};
				if (!itemR.Contains(m_mousePos)) continue;

				auto &node = d->nodes[ni];
				// Arrow area: click to expand/collapse
				if (node.hasChildren) {
					float ax = cr->screen.x + lp->padding.left + node.level * d->indentSize;
					FRect arrowR = {ax, iy, 14.f, d->itemHeight};
					if (arrowR.Contains(m_mousePos)) {
						node.expanded = !node.expanded;
						if (cb && cb->onToggle) cb->onToggle(node.expanded);
					}
				}
				d->selectedIndex = ni;
				if (cb && cb->onChange)    cb->onChange((float)ni);
				if (cb && cb->onClick)     cb->onClick();
				if (cb && cb->onTreeSelect) cb->onTreeSelect(ni, node.hasChildren);
				break;
			}
		}

		void _OnClickExpander(ECS::EntityId e) {
			auto *d  = m_ctx.Get<ExpanderData>(e);
			auto *cb = m_ctx.Get<Callbacks>(e);
			if (!d) return;
			d->expanded = !d->expanded;
			auto *ch2 = m_ctx.Get<Children>(e);
			if (ch2) {
				for (ECS::EntityId cid : ch2->ids) {
					if (!m_ctx.IsAlive(cid)) continue;
					auto *cw = m_ctx.Get<Widget>(cid);
					if (cw) {
						if (d->expanded)
							cw->behavior |= BehaviorFlag::Visible;
						else
							cw->behavior &= ~BehaviorFlag::Visible;
					}
				}
			}
			m_layoutDirty = true;
			if (cb && cb->onToggle) cb->onToggle(d->expanded);
			if (cb && cb->onClick)  cb->onClick();
		}

		// ── Draw helpers ───────────────────────────────────────────────────────────

		void _DrawComboBox(ECS::EntityId e, const FRect &r, const Style &s, const WidgetState &st, const Widget &) {
			auto *d  = m_ctx.Get<ComboBoxData>(e);
			if (!d) return;
			const bool hover  = st.hovered;
			const bool focus  = (m_focused == e);
			SDL::Color bg  = hover ? s.bgHoveredColor  : s.bgColor;
			SDL::Color bd  = focus ? s.fillColor : s.bdColor;
			_FillRR(r, bg, s.radius, s.opacity);
			_StrokeRR(r, bd, {1.f,1.f,1.f,1.f}, s.radius, s.opacity);

			// Arrow
			float ax = r.x + r.w - 18.f, ay = r.y + r.h * 0.5f;
			m_renderer.SetDrawColor(s.textColor);
			if (d->open) {
				m_renderer.RenderLine({ax,     ay - 4.f}, {ax + 8.f, ay - 4.f});
				m_renderer.RenderLine({ax,     ay - 4.f}, {ax + 4.f, ay + 3.f});
				m_renderer.RenderLine({ax + 8.f, ay - 4.f}, {ax + 4.f, ay + 3.f});
			} else {
				m_renderer.RenderLine({ax,     ay + 4.f}, {ax + 8.f, ay + 4.f});
				m_renderer.RenderLine({ax,     ay + 4.f}, {ax + 4.f, ay - 3.f});
				m_renderer.RenderLine({ax + 8.f, ay + 4.f}, {ax + 4.f, ay - 3.f});
			}

			// Selected text
			const std::string &label = (d->selectedIndex >= 0 && d->selectedIndex < (int)d->items.size())
				? d->items[d->selectedIndex] : "";
			if (!label.empty())
				_Text(e, label, r.x + 8.f, r.y + (r.h - _TH(e)) * 0.5f, s.textColor, s.opacity, s);
			
			// Store drop rect for overlay rendering
			float dropH = SDL::Min((float)d->maxVisible, (float)d->items.size()) * d->itemHeight;
			d->dropRect = {r.x, r.y + r.h, r.w, dropH};
		}

		void _DrawComboOverlay() {
			if (m_comboOpen == ECS::NullEntity || !m_ctx.IsAlive(m_comboOpen)) return;
			auto *d  = m_ctx.Get<ComboBoxData>(m_comboOpen);
			auto *s  = m_ctx.Get<Style>(m_comboOpen);
			if (!d || !s || d->items.empty()) return;
			const FRect &drop = d->dropRect;
			m_renderer.ResetClipRect();
			_FillRR(drop, SDL::Color{30,30,35,245}, SDL::FCorners(4.f), 1.f);
			_StrokeRR(drop, s->bdColor, {1.f,1.f,1.f,1.f}, SDL::FCorners(4.f), 1.f);
			float iy = drop.y;
			int count = SDL::Min((int)d->items.size(), d->maxVisible);
			for (int i = d->scrollOffset; i < d->scrollOffset + count && i < (int)d->items.size(); ++i) {
				FRect row = {drop.x, iy, drop.w, (float)d->itemHeight};
				if (i == d->selectedIndex)
					_FillRR(row, s->fillColor, {}, 0.35f);
				if (i == d->hoverIndex)
					_FillRR(row, {255,255,255,50}, {}, 1.f);
				_Text(m_comboOpen, d->items[i], drop.x + 8.f, iy + (d->itemHeight - _TH(m_comboOpen)) * 0.5f,
					  s->textColor, 1.f, *s);
				iy += d->itemHeight;
			}
		}

		void _DrawTabView(ECS::EntityId e, const FRect &r, const Style &s, const WidgetState &) {
			auto *d  = m_ctx.Get<TabViewData>(e);
			if (!d) return;
			float tabH = d->tabHeight;
			float ty   = d->tabsBottom ? (r.y + r.h - tabH) : r.y;
			float tx   = r.x;
			d->tabWidths.resize(d->tabs.size());
			for (int i = 0; i < (int)d->tabs.size(); ++i) {
				float tw = _TW(d->tabs[i].label, e) + 20.f;
				d->tabWidths[i] = tw;
				FRect tab = {tx, ty, tw, tabH};
				SDL::Color bg = (i == d->activeTab) ? s.fillColor : SDL::Color{45,50,65,220};
				_FillRR(tab, bg, SDL::FCorners(4.f, 4.f, 0.f, 0.f), s.opacity);
				_Text(e, d->tabs[i].label, tx + 10.f, ty + (tabH - _TH(e)) * 0.5f, s.textColor, s.opacity, s);
				tx += tw + 2.f;
			}
			// Content area border
			FRect content = d->tabsBottom
				? FRect{r.x, r.y, r.w, r.h - tabH}
				: FRect{r.x, r.y + tabH, r.w, r.h - tabH};
			_StrokeRR(content, s.bdColor, {1.f,1.f,1.f,1.f}, {}, s.opacity);
		}

		void _DrawExpander(ECS::EntityId e, const FRect &r, const Style &s, const WidgetState &st) {
			auto *d = m_ctx.Get<ExpanderData>(e);
			if (!d) return;
			float headerH = d->headerH;
			FRect header = {r.x, r.y, r.w, headerH};
			SDL::Color bg = st.hovered ? s.bgHoveredColor : s.bgColor;
			_FillRR(header, bg, s.radius, s.opacity);
			// Arrow  (rotates with animT: 0=right, 1=down)
			float ax = r.x + 14.f, ay = r.y + headerH * 0.5f;
			float t  = d->animT;
			m_renderer.SetDrawColor(s.textColor);
			if (t < 0.5f) {
				// right-pointing triangle
				m_renderer.RenderLine({ax - 4.f, ay - 5.f}, {ax + 4.f, ay});
				m_renderer.RenderLine({ax + 4.f, ay},        {ax - 4.f, ay + 5.f});
			} else {
				// down-pointing triangle
				m_renderer.RenderLine({ax - 5.f, ay - 3.f}, {ax, ay + 4.f});
				m_renderer.RenderLine({ax + 5.f, ay - 3.f}, {ax, ay + 4.f});
			}
			// Label
			_Text(e, d->label, r.x + 26.f, r.y + (headerH - _TH(e)) * 0.5f, s.textColor, s.opacity, s);
		}

		void _DrawSplitter(ECS::EntityId e, const FRect &r, const Style &s, const WidgetState &st) {
			auto *d = m_ctx.Get<SplitterData>(e);
			if (!d) return;
			bool horiz = (d->orientation == Orientation::Horizontal);
			float pos  = horiz ? r.x + r.w * d->ratio : r.y + r.h * d->ratio;
			float hs   = d->handleSize;
			FRect handle = horiz
				? FRect{pos, r.y, hs, r.h}
				: FRect{r.x, pos, r.w, hs};
			SDL::Color hc = st.hovered ? s.fillColor : s.bdColor;
			_FillRR(handle, hc, {}, s.opacity);
		}

		void _DrawSpinner(ECS::EntityId e, const FRect &r, const Style &s, const WidgetState &) {
			auto *d = m_ctx.Get<SpinnerData>(e);
			if (!d) return;
			float cx  = r.x + r.w * 0.5f;
			float cy  = r.y + r.h * 0.5f;
			float rad = SDL::Min(r.w, r.h) * 0.5f - d->thickness;
			if (rad <= 0.f) return;
			// Draw arc via line segments (approximation)
			int segments = 24;
			float arcEnd = d->angle + d->arcSpan;
			SDL::Color c = s.fillColor;
			m_renderer.SetDrawColor({c.r, c.g, c.b, (Uint8)(c.a * s.opacity)});
			for (int i = 0; i < segments; ++i) {
				float a0 = d->angle + d->arcSpan * i       / segments;
				float a1 = d->angle + d->arcSpan * (i + 1) / segments;
				float x0 = cx + std::cos(a0) * rad;
				float y0 = cy + std::sin(a0) * rad;
				float x1 = cx + std::cos(a1) * rad;
				float y1 = cy + std::sin(a1) * rad;
				m_renderer.RenderLine({x0, y0}, {x1, y1});
			}
			(void)arcEnd;
		}

		void _DrawBadge(ECS::EntityId e, const FRect &r, const Style &s, const WidgetState &, const Widget &) {
			auto *d = m_ctx.Get<BadgeData>(e);
			if (!d) return;
			SDL::Color bg = d->bgColor.a ? d->bgColor : s.fillColor;
			_FillRR(r, bg, SDL::FCorners(r.h * 0.5f), s.opacity);
			float tw = _TW(d->text, e);
			float th = _TH(e);
			_Text(e, d->text, r.x + (r.w - tw) * 0.5f, r.y + (r.h - th) * 0.5f,
				  d->textColor.a ? d->textColor : SDL::Color{255,255,255,255}, s.opacity, s);
		}

		// ── ColorPicker ────────────────────────────────────────────────────────────

		void _DrawColorPicker(ECS::EntityId e, const FRect &r, const Style &s, const WidgetState &st, const Widget &w) {
			auto *d = m_ctx.Get<ColorPickerData>(e);
			if (!d) return;

			_DrawBg(e, r, s, st, w);
			_StrokeRR(r, (m_focused==e) ? s.bdFocusedColor : s.bdColor, s.borders, s.radius, s.opacity);

			constexpr float P    = 4.f;
			constexpr float hueH = 14.f;
			constexpr float bGap = 4.f;
			float op = s.opacity;
			FRect inner = {r.x+P, r.y+P, r.w-2*P, r.h-2*P};

			if (d->palette == ColorPickerPalette::Grayscale) {
				_FillGradientRect(inner,
					{0,0,0,255}, GradientAnchor::Left,
					{255,255,255,255}, GradientAnchor::Right, op);
				_StrokeRR(inner, s.bdColor, {1,1,1,1}, FCorners(2.f), op);
				float cx = inner.x + d->gradT * inner.w;
				SDL::Color cc = {255,255,255,200}; cc.a = SDL::Clamp8(cc.a * op);
				m_renderer.SetDrawColor(cc);
				m_renderer.RenderFillRect(FRect{cx - 2.f, inner.y, 4.f, inner.h});

			} else if (d->palette == ColorPickerPalette::GradientAB) {
				_FillGradientRect(inner,
					d->colorA, GradientAnchor::Left,
					d->colorB, GradientAnchor::Right, op);
				_StrokeRR(inner, s.bdColor, {1,1,1,1}, FCorners(2.f), op);
				float cx = inner.x + d->gradT * inner.w;
				SDL::Color cc = {255,255,255,200}; cc.a = SDL::Clamp8(cc.a * op);
				m_renderer.SetDrawColor(cc);
				m_renderer.RenderFillRect(FRect{cx - 2.f, inner.y, 4.f, inner.h});

			} else {
				// RGB8 / RGBFloat — SV square + hue bar (+ optional alpha bar)
				float alphaH = d->showAlpha ? hueH : 0.f;
				float svH    = inner.h - hueH - bGap - (d->showAlpha ? alphaH + bGap : 0.f);
				float realSvH = SDL::Max(10.f, svH);
				FRect svR  = {inner.x, inner.y, inner.w, realSvH};
				FRect hueR = {inner.x, inner.y + realSvH + bGap, inner.w, hueH};

				// SV square: horizontal white→hue, then vertical transparent→black overlay
				SDL::Color hueCol = _HsvToColor(d->hue, 1.f, 1.f);
				_FillGradientRect(svR,
					{255,255,255,255}, GradientAnchor::Left,
					hueCol, GradientAnchor::Right, op);
				_FillGradientRect(svR,
					{0,0,0,0}, GradientAnchor::Left,
					{0,0,0,255}, GradientAnchor::Right, op);
				_StrokeRR(svR, s.bdColor, {1,1,1,1}, FCorners(2.f), op);

				// Hue bar
				_DrawHueBarGradient(hueR, op);
				_StrokeRR(hueR, s.bdColor, {1,1,1,1}, FCorners(2.f), op);

				// Alpha bar (optional)
				if (d->showAlpha) {
					FRect alphaR = {inner.x, hueR.y + hueH + bGap, inner.w, alphaH};
					_FillGradientRect(alphaR,
					    {d->currentColor.r, d->currentColor.g, d->currentColor.b, 0}, GradientAnchor::Left,
					    {d->currentColor.r, d->currentColor.g, d->currentColor.b, 255}, GradientAnchor::Right, op);
					_StrokeRR(alphaR, s.bdColor, {1,1,1,1}, FCorners(2.f), op);
					// Alpha cursor
					float acx = alphaR.x + (d->currentColor.a / 255.f) * alphaR.w;
					SDL::Color acc = {255,255,255,200}; acc.a = SDL::Clamp8(acc.a * op);
					m_renderer.SetDrawColor(acc);
					m_renderer.RenderFillRect(FRect{acx - 2.f, alphaR.y, 4.f, alphaR.h});
				}

				// SV cursor (small square)
				float cvx = svR.x + d->sat * svR.w;
				float cvy = svR.y + (1.f - d->val) * svR.h;
				float cr2 = 5.f;
				SDL::Color inner_c = (d->val > 0.5f) ? SDL::Color{0,0,0,200} : SDL::Color{255,255,255,200};
				inner_c.a = SDL::Clamp8(inner_c.a * op);
				SDL::Color outer_c = {255,255,255,220}; outer_c.a = SDL::Clamp8(outer_c.a * op);
				m_renderer.SetDrawColor(outer_c);
				m_renderer.RenderFillRect(FRect{cvx - cr2, cvy - cr2, cr2*2.f, cr2*2.f});
				m_renderer.SetDrawColor(inner_c);
				m_renderer.RenderFillRect(FRect{cvx - cr2+1.5f, cvy - cr2+1.5f, cr2*2.f-3.f, cr2*2.f-3.f});

				// Hue cursor
				float hcx = hueR.x + d->hue * hueR.w;
				SDL::Color hcc = {255,255,255,220}; hcc.a = SDL::Clamp8(hcc.a * op);
				m_renderer.SetDrawColor(hcc);
				m_renderer.RenderFillRect(FRect{hcx - 2.f, hueR.y, 4.f, hueR.h});
			}

			// Current colour swatch at the bottom
			FRect swatchR = {inner.x, inner.y + inner.h - 8.f, inner.w, 8.f};
			SDL::Color sc = d->currentColor; sc.a = SDL::Clamp8(sc.a * op);
			m_renderer.SetDrawColor(sc);
			m_renderer.RenderFillRect(swatchR);
			_StrokeRR(swatchR, s.bdColor, {1,1,1,1}, FCorners(2.f), op);
		}

		// ── Popup ──────────────────────────────────────────────────────────────────

		void _DrawPopup(ECS::EntityId e, const FRect &r, const Style &s, const WidgetState &st, const Widget &w) {
			auto *d = m_ctx.Get<PopupData>(e);
			if (!d || !d->open) return;
			float op = s.opacity;

			// Window background & outer border
			_DrawBg(e, r, s, st, w);
			_StrokeRR(r, s.bdColor, s.borders, s.radius, op);

			// Header bar
			FRect hdrR = {r.x, r.y, r.w, d->headerH};
			SDL::Color hdrBg = {s.bgPressedColor.r, s.bgPressedColor.g, s.bgPressedColor.b, s.bgPressedColor.a};
			hdrBg.a = SDL::Clamp8(hdrBg.a * op);
			m_renderer.SetDrawColor(hdrBg);
			m_renderer.RenderFillRect(hdrR);

			// Header border-bottom (separator line)
			SDL::Color sep = s.bdColor; sep.a = SDL::Clamp8(sep.a * op);
			m_renderer.SetDrawColor(sep);
			m_renderer.RenderLine({r.x, r.y + d->headerH}, {r.x + r.w, r.y + d->headerH});

			// Title text
			float ty = r.y + (d->headerH - _TH(e)) * 0.5f;
			_Text(e, d->title, r.x + 8.f, ty, s.textColor, op, s);

			// Close button [×]
			float btnSize = d->headerH;
			float btnX    = r.x + r.w;
			if (d->closable) {
				btnX -= btnSize;
				FRect closeR = {btnX, r.y, btnSize, btnSize};
				bool hov = closeR.Contains(m_mousePos);
				if (hov) {
					SDL::Color hc = {200, 60, 50, 200}; hc.a = SDL::Clamp8(hc.a * op);
					m_renderer.SetDrawColor(hc);
					m_renderer.RenderFillRect(closeR);
				}
				float cx = closeR.x + btnSize * 0.5f, cy = r.y + btnSize * 0.5f, cs = 5.f;
				SDL::Color xc = hov ? SDL::Color{255,255,255,230} : SDL::Color{200,200,200,200};
				xc.a = SDL::Clamp8(xc.a * op);
				m_renderer.SetDrawColor(xc);
				m_renderer.RenderLine({cx-cs, cy-cs}, {cx+cs, cy+cs});
				m_renderer.RenderLine({cx+cs, cy-cs}, {cx-cs, cy+cs});
			}

			// Custom header buttons (right to left, before close button)
			for (int i = (int)d->headerButtons.size() - 1; i >= 0; --i) {
				btnX -= btnSize;
				FRect btnR = {btnX, r.y, btnSize, btnSize};
				bool hov = btnR.Contains(m_mousePos);
				if (hov) {
					SDL::Color hc = s.bgHoveredColor; hc.a = SDL::Clamp8(hc.a * op);
					m_renderer.SetDrawColor(hc);
					m_renderer.RenderFillRect(btnR);
				}
				if (!d->headerButtons[i].iconKey.empty()) {
					if (auto tex = _EnsureTexture(d->headerButtons[i].iconKey)) {
						float ip = btnSize * 0.25f;
						FRect ir = {btnR.x+ip, btnR.y+ip, btnR.w-2*ip, btnR.h-2*ip};
						tex.SetAlphaMod(SDL::Clamp8((int)(255*op)));
						m_renderer.RenderTexture(tex, {}, ir);
						tex.SetAlphaMod(255);
					}
				}
			}

			// Resize grip (bottom-right corner)
			if (d->resizable) {
				SDL::Color rc = d->resizing ? s.thumbColor : s.trackColor;
				rc.a = SDL::Clamp8(rc.a * op);
				m_renderer.SetDrawColor(rc);
				for (int i = 0; i < 3; ++i) {
					float off = 4.f + i * 4.f;
					m_renderer.RenderLine({r.x + r.w - off, r.y + r.h - 1},
					                      {r.x + r.w - 1,   r.y + r.h - off});
				}
			}
		}

		// ── Tree ───────────────────────────────────────────────────────────────────

		void _DrawTree(ECS::EntityId e, const FRect &r, const Style &s, const WidgetState &st, const Widget &w) {
			auto *d  = m_ctx.Get<TreeData>(e);
			auto *lp = m_ctx.Get<LayoutProps>(e);
			if (!d || !lp) return;

			_DrawBg(e, r, s, st, w);
			SDL::Color bd = (m_focused==e) ? s.bdFocusedColor : st.hovered ? s.bdHoveredColor : s.bdColor;
			_StrokeRR(r, bd, s.borders, s.radius, s.opacity);

			float op   = s.opacity;
			float iH   = d->itemHeight;
			float padL = lp->padding.left;
			float padT = lp->padding.top;
			float padB = lp->padding.bottom;
			float th   = _TH(e);

			std::vector<int> visible = _GetVisibleTreeNodes(d->nodes);
			float totalH = (float)visible.size() * iH;
			float viewH  = r.h - padT - padB;

			d->scrollY = SDL::Clamp(d->scrollY, 0.f, SDL::Max(0.f, totalH - viewH));

			float sbW = 0.f;
			if (auto *css = m_ctx.Get<ContainerScrollState>(e); css && totalH > viewH) {
				float t = lp->scrollbarThickness;
				sbW = t;
				_DrawScrollbarY({r.x + r.w - t, r.y + padT, t, viewH},
				                totalH, viewH, d->scrollY, css, s, op);
			}

			float contentW = r.w - padL - lp->padding.right - sbW;
			float y0 = r.y + padT - d->scrollY;

			for (int vi = 0; vi < (int)visible.size(); ++vi) {
				int ni = visible[vi];
				const auto &node = d->nodes[ni];
				float iy = y0 + vi * iH;

				// Clip check
				if (iy + iH < r.y + padT || iy > r.y + r.h - padB) continue;

				FRect itemR = {r.x + padL, iy, contentW, iH};
				float indent = node.level * d->indentSize;

				// Row background (selected / hovered)
				if (ni == d->selectedIndex)
					_FillRR(itemR, s.bgCheckedColor, FCorners(3.f), op);
				else if (itemR.Contains(m_mousePos))
					_FillRR(itemR, s.bgHoveredColor, FCorners(3.f), op);

				float tx = itemR.x + indent;

				// Expand/collapse arrow (8×8 triangle)
				float ax = tx + 3.f, ay = iy + (iH - 8.f) * 0.5f;
				if (node.hasChildren) {
					SDL::Color ac = s.textColor; ac.a = SDL::Clamp8(ac.a * op);
					m_renderer.SetDrawColor(ac);
					if (node.expanded) {
						// ▼ down arrow (filled with lines)
						for (int li = 0; li < 4; ++li)
							m_renderer.RenderLine({ax + li, ay + li}, {ax + 7 - li, ay + li});
					} else {
						// ▶ right arrow
						for (int li = 0; li < 4; ++li)
							m_renderer.RenderLine({ax + li, ay + li}, {ax + li, ay + 7 - li});
					}
				}
				tx += 14.f; // arrow slot (present even for leaf nodes)

				// Optional icon
				if (!node.iconKey.empty()) {
					if (auto tex = _EnsureTexture(node.iconKey)) {
						float ip = (iH - d->iconSize) * 0.5f;
						FRect ir = {tx, iy + ip, d->iconSize, d->iconSize};
						tex.SetAlphaMod(SDL::Clamp8((int)(255*op)));
						m_renderer.RenderTexture(tex, {}, ir);
						tex.SetAlphaMod(255);
					}
					tx += d->iconSize + 4.f;
				}

				// Label
				SDL::Color tc = (ni == d->selectedIndex) ? s.textCheckedColor : s.textColor;
				_Text(e, node.label, tx, iy + (iH - th) * 0.5f, tc, op, s);
			}
		}

		void _DrawColorButton(ECS::EntityId e, const FRect &r, const Style &s, const WidgetState &st, const Widget &) {
			SDL::Color bd = (m_focused == e) ? s.fillColor : s.bdColor;
			_FillRR(r, s.bgColor, s.radius, s.opacity);
			_StrokeRR(r, bd, {1.f,1.f,1.f,1.f}, s.radius, s.opacity);
			if (st.hovered) {
				_FillRR(r, {255,255,255,30}, s.radius, 1.f);
			}
		}

		// ── ComboBox press intercept ──────────────────────────────────────────────
		// Called from _ProcessInput press section to check combo overlay first.
		// Returns true if consumed.
		bool _CheckComboOverlayPress() {
			return _TryComboBoxClick();
		}

		// ── MenuBar draw ──────────────────────────────────────────────────────────

		void _DrawMenuBar(ECS::EntityId e, const FRect &r, const Style &s, const WidgetState &, const Widget &) {
			auto *d = m_ctx.Get<MenuBarData>(e);
			if (!d) return;

			// Background bar
			_FillRR(r, s.bgColor, {}, s.opacity);
			// Bottom border
			SDL::Color bdC = s.bdColor; bdC.a = SDL::Clamp8(bdC.a * s.opacity);
			m_renderer.SetDrawColor(bdC);
			m_renderer.RenderLine({r.x, r.y + r.h - 1.f}, {r.x + r.w, r.y + r.h - 1.f});

			float th  = _TH(e);
			float x   = r.x + 4.f;
			bool  onBar = (m_mousePos.y >= r.y && m_mousePos.y < r.y + r.h - 1.f);

			d->menuBtnRects.resize(d->menus.size());
			d->hovMenu = -1;

			for (int i = 0; i < (int)d->menus.size(); ++i) {
				const auto &menu = d->menus[i];
				float tw  = _TW(menu.label, e) + 18.f;
				d->menuBtnRects[i] = {x - r.x, tw};

				bool isOpen = (d->openMenu == i);
				bool isHov  = onBar && m_mousePos.x >= x && m_mousePos.x < x + tw;
				if (isHov) d->hovMenu = i;

				if (isOpen) {
					_FillRR({x - 2.f, r.y, tw + 4.f, r.h}, s.bgCheckedColor,
					        SDL::FCorners{2.f, 2.f, 0.f, 0.f}, s.opacity * 0.6f);
				} else if (isHov) {
					_FillRR({x - 2.f, r.y, tw + 4.f, r.h}, s.bgHoveredColor,
					        SDL::FCorners{2.f, 2.f, 0.f, 0.f}, s.opacity * 0.5f);
				}

				SDL::Color tc = (menu.enabled) ? s.textColor : s.textDisabledColor;
				_Text(e, menu.label, x + 4.f, r.y + (r.h - th) * 0.5f, tc, s.opacity, s);
				x += tw + 2.f;
			}

			// Live menu-switch: when a menu IS open and the user hovers a different button
			if (d->openMenu >= 0 && d->hovMenu >= 0 && d->hovMenu != d->openMenu) {
				if (d->menus[d->hovMenu].enabled) {
					d->openMenu = d->hovMenu;
					m_menuBarOpen = e;
				}
			}
		}

		void _DrawMenuBarOverlay() {
			if (m_menuBarOpen == ECS::NullEntity || !m_ctx.IsAlive(m_menuBarOpen)) return;
			auto *d  = m_ctx.Get<MenuBarData>(m_menuBarOpen);
			auto *s  = m_ctx.Get<Style>(m_menuBarOpen);
			auto *cr = m_ctx.Get<ComputedRect>(m_menuBarOpen);
			if (!d || !s || !cr || d->openMenu < 0 || d->openMenu >= (int)d->menus.size()) {
				m_menuBarOpen = ECS::NullEntity;
				return;
			}
			const FRect &barR = cr->screen;
			const auto &menu  = d->menus[d->openMenu];

			// Dropdown position
			auto &btn   = d->menuBtnRects[d->openMenu];
			float dropX = barR.x + btn.x - 2.f;
			float dropY = barR.y + barR.h;

			float itemH = _TH(m_menuBarOpen) + 8.f;
			float dropW = 200.f;

			// Compute width from label + shortcut widths
			for (const auto &item : menu.items) {
				if (item.separator) continue;
				float iw = _TW(item.label, m_menuBarOpen) + 40.f;
				if (!item.shortcutText.empty()) iw += _TW(item.shortcutText, m_menuBarOpen) + 20.f;
				if (!item.sub.empty()) iw += 12.f;
				dropW = SDL::Max(dropW, iw);
			}

			float totalH = 0.f;
			for (const auto &item : menu.items) totalH += item.separator ? 9.f : itemH;

			d->dropRect = {dropX, dropY, dropW, totalH};
			d->itemRects.resize(menu.items.size());

			m_renderer.ResetClipRect();

			// Shadow
			_FillRR({dropX + 3.f, dropY + 3.f, dropW, totalH},
			        SDL::Color{0, 0, 0, 50}, SDL::FCorners(4.f), 1.f);

			// Panel
			SDL::Color panelBg{28, 30, 42, 252};
			_FillRR(d->dropRect, panelBg, SDL::FCorners(4.f), 1.f);
			_StrokeRR(d->dropRect, s->bdColor, {1.f, 1.f, 1.f, 1.f}, SDL::FCorners(4.f), 1.f);

			float iy = dropY;
			d->hovItem = -1;

			for (int i = 0; i < (int)menu.items.size(); ++i) {
				const auto &item = menu.items[i];
				if (item.separator) {
					d->itemRects[i] = {};
					float sy = iy + 4.f;
					SDL::Color sepC{60, 65, 85, 200};
					m_renderer.SetDrawColor(sepC);
					m_renderer.RenderLine({dropX + 8.f, sy}, {dropX + dropW - 8.f, sy});
					iy += 9.f;
					continue;
				}

				FRect ir = {dropX, iy, dropW, itemH};
				d->itemRects[i] = ir;

				bool isHov = item.enabled && ir.Contains(m_mousePos);
				if (isHov) d->hovItem = i;

				if (isHov)
					_FillRR(ir, s->fillColor, SDL::FCorners(2.f), 0.35f);

				float textY = iy + (itemH - _TH(m_menuBarOpen)) * 0.5f;

				// Checkmark
				if (item.checkable && item.checked) {
					float cx = dropX + 9.f, cy = iy + itemH * 0.5f;
					SDL::Color cc = item.enabled ? s->textColor : s->textDisabledColor;
					cc.a = SDL::Clamp8(cc.a * s->opacity);
					m_renderer.SetDrawColor(cc);
					m_renderer.RenderLine({cx,       cy + 1.f}, {cx + 3.f, cy + 4.f});
					m_renderer.RenderLine({cx + 3.f, cy + 4.f}, {cx + 9.f, cy - 3.f});
				}

				// Icon (16×16 slot at x+2)
				if (!item.iconKey.empty()) {
					if (auto tex = _EnsureTexture(item.iconKey)) {
						float isz = itemH - 4.f;
						tex.SetAlphaMod(item.enabled ? 220 : 100);
						m_renderer.RenderTexture(tex, {}, SDL::FRect(dropX + 2.f, iy + 2.f, isz, isz));
					}
				}

				// Label
				SDL::Color tc = item.enabled ? s->textColor : s->textDisabledColor;
				_Text(m_menuBarOpen, item.label, dropX + 28.f, textY, tc, s->opacity, *s);

				// Shortcut
				if (!item.shortcutText.empty()) {
					float sw = _TW(item.shortcutText, m_menuBarOpen);
					SDL::Color sc{150, 155, 170, 200};
					if (!item.enabled) sc = s->textDisabledColor;
					_Text(m_menuBarOpen, item.shortcutText,
					      dropX + dropW - sw - 10.f, textY, sc, s->opacity * 0.8f, *s);
				}

				// Sub-menu arrow
				if (!item.sub.empty()) {
					float ax = dropX + dropW - 10.f, ay = iy + itemH * 0.5f;
					SDL::Color ac = item.enabled ? s->textColor : s->textDisabledColor;
					ac.a = SDL::Clamp8(ac.a * s->opacity);
					m_renderer.SetDrawColor(ac);
					m_renderer.RenderLine({ax, ay - 3.f}, {ax + 5.f, ay});
					m_renderer.RenderLine({ax, ay + 3.f}, {ax + 5.f, ay});
				}

				iy += itemH;
			}
		}

		// ── MenuBar helpers ───────────────────────────────────────────────────────

		void _OnClickMenuBar(ECS::EntityId e) {
			auto *d  = m_ctx.Get<MenuBarData>(e);
			auto *cr = m_ctx.Get<ComputedRect>(e);
			if (!d || !cr) return;
			const FRect &barR = cr->screen;
			float mx = m_mousePos.x - barR.x;

			for (int i = 0; i < (int)d->menuBtnRects.size(); ++i) {
				const auto &btn = d->menuBtnRects[i];
				if (mx >= btn.x - 4.f && mx < btn.x + btn.w + 4.f) {
					if (!d->menus[i].enabled) return;
					if (d->openMenu == i) {
						d->openMenu = -1;
						m_menuBarOpen = ECS::NullEntity;
					} else {
						d->openMenu = i;
						d->hovItem  = -1;
						m_menuBarOpen = e;
					}
					return;
				}
			}
			d->openMenu   = -1;
			m_menuBarOpen = ECS::NullEntity;
		}

		/// Intercept a mouse press when a MenuBar dropdown is open.
		/// Returns true if the event was consumed (caller must goto end_press).
		bool _CheckMenuBarOverlayPress() {
			if (m_menuBarOpen == ECS::NullEntity || !m_ctx.IsAlive(m_menuBarOpen)) return false;
			auto *d  = m_ctx.Get<MenuBarData>(m_menuBarOpen);
			auto *cr = m_ctx.Get<ComputedRect>(m_menuBarOpen);
			if (!d || !cr || d->openMenu < 0) {
				m_menuBarOpen = ECS::NullEntity;
				return false;
			}
			const FRect &barR = cr->screen;

			// Click inside the dropdown?
			if (d->dropRect.Contains(m_mousePos) && d->hovItem >= 0) {
				auto &menu = d->menus[d->openMenu];
				if (d->hovItem < (int)menu.items.size()) {
					auto &item = menu.items[d->hovItem];
					if (item.enabled && !item.separator) {
						if (item.checkable) item.checked = !item.checked;
						if (item.action) item.action();
					}
				}
				d->openMenu   = -1;
				d->hovItem    = -1;
				m_menuBarOpen = ECS::NullEntity;
				return true;
			}

			// Click in the bar area — check for menu-switch or close
			if (barR.Contains(m_mousePos)) {
				float mx = m_mousePos.x - barR.x;
				for (int i = 0; i < (int)d->menuBtnRects.size(); ++i) {
					const auto &btn = d->menuBtnRects[i];
					if (mx >= btn.x - 4.f && mx < btn.x + btn.w + 4.f) {
						if (!d->menus[i].enabled) {
							d->openMenu = -1;
							m_menuBarOpen = ECS::NullEntity;
							return true;
						}
						if (i == d->openMenu) {
							d->openMenu   = -1;
							m_menuBarOpen = ECS::NullEntity;
						} else {
							d->openMenu = i;
							d->hovItem  = -1;
						}
						return true;
					}
				}
				d->openMenu   = -1;
				m_menuBarOpen = ECS::NullEntity;
				return true;
			}

			// Click outside everything — close menu but do not consume
			d->openMenu   = -1;
			d->hovItem    = -1;
			m_menuBarOpen = ECS::NullEntity;
			return false;
		}
	};

} // namespace UI
} // namespace SDL
