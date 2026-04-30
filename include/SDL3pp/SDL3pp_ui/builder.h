#pragma once
#include "system.h"

namespace SDL {
namespace UI {

	// =============================================================================
	// Builder
	// =============================================================================

	/**
	 * @brief Fluent builder DSL wrapping a widget entity.
	 *
	 * Every setter returns `*this` so calls can be chained:
	 * ```cpp
	 * ui.Button("ok", "OK")
	 *   .W(100).H(36)
	 *   .BgColor({70,130,210,255})
	 *   .OnClick([]{ ... })
	 *   .AttachTo(root);
	 * ```
	 */
	struct Builder {
		System &sys;
		ECS::EntityId id;
		Builder(System &s, ECS::EntityId e) : sys(s), id(e) {}
		/** @brief Implicit conversion to the underlying entity ID. */
		operator ECS::EntityId() const noexcept { return id; }
		/** @brief Return the underlying entity ID. */
		[[nodiscard]] ECS::EntityId Id() const noexcept { return id; }

		// Style
		/** @brief Replace the entire Style component with @p s.
		 *  @return Reference to this builder for chaining. */
		Builder &Style(const Style &s) {
			sys.GetStyle(id) = s;
			return *this;
		}
		/** @brief Apply an inline lambda to the Style component. */
		Builder &WithStyle(std::function<void(UI::Style &)> fn) {
			fn(sys.GetStyle(id));
			return *this;
		}
		/** @brief Set the background color (normal state). */
		Builder &BgColor(SDL::Color c) {
			sys.GetStyle(id).bgColor = c;
			return *this;
		}
		/** @brief Set the background color when the widget is hovered. */
		Builder &BgHover(SDL::Color c) {
			sys.GetStyle(id).bgHoveredColor = c;
			return *this;
		}
		/** @brief Set the background color when the widget is pressed. */
		Builder &BgPress(SDL::Color c) {
			sys.GetStyle(id).bgPressedColor = c;
			return *this;
		}
		/** @brief Set the background color when the widget is checked (Toggle, selected ListBox item). */
		Builder &BgCheck(SDL::Color c) {
			sys.GetStyle(id).bgCheckedColor = c;
			return *this;
		}
		/** @brief Set the border color (normal state). */
		Builder &BorderColor(SDL::Color c) {
			sys.GetStyle(id).bdColor = c;
			return *this;
		}
		/** @brief Set the left border thickness in pixels. */
		Builder &BorderLeft(float w) {
			sys.GetStyle(id).borders.left = w;
			return *this;
		}
		/** @brief Set the right border thickness in pixels. */
		Builder &BorderRight(float w) {
			sys.GetStyle(id).borders.right = w;
			return *this;
		}
		/** @brief Set the top border thickness in pixels. */
		Builder &BorderTop(float w) {
			sys.GetStyle(id).borders.top = w;
			return *this;
		}
		/** @brief Set the bottom border thickness in pixels. */
		Builder &BorderBottom(float w) {
			sys.GetStyle(id).borders.bottom = w;
			return *this;
		}
		/** @brief Set all four border thicknesses at once. */
		Builder &Borders(SDL::FBox bd) {
			sys.GetStyle(id).borders = bd;
			return *this;
		}

		/** @brief Set the top-left corner radius. */
		Builder &RadiusTopLeft(float r) {
			sys.GetStyle(id).radius.tl = r;
			return *this;
		}
		/** @brief Set the top-right corner radius. */
		Builder &RadiusTopRight(float r) {
			sys.GetStyle(id).radius.tr = r;
			return *this;
		}
		/** @brief Set the bottom-left corner radius. */
		Builder &RadiusBottomLeft(float r) {
			sys.GetStyle(id).radius.bl = r;
			return *this;
		}
		/** @brief Set the bottom-right corner radius. */
		Builder &RadiusBottomRight(float r) {
			sys.GetStyle(id).radius.br = r;
			return *this;
		}
		/** @brief Set all four corner radii at once. */
		Builder &Radius(SDL::FCorners rad) {
			sys.GetStyle(id).radius = rad;
			return *this;
		}

		/** @brief Set the text color (normal state). */
		Builder &TextColor(SDL::Color c) {
			sys.GetStyle(id).textColor = c;
			return *this;
		}
		/** @brief Set the fill color (Slider track fill, Toggle active state, etc.). */
		Builder &FillColor(SDL::Color c) {
			sys.GetStyle(id).fillColor = c;
			return *this;
		}
		/** @brief Set the track color (Slider / ScrollBar background rail). */
		Builder &TrackColor(SDL::Color c) {
			sys.GetStyle(id).trackColor = c;
			return *this;
		}
		/** @brief Set the thumb color (Slider handle, ScrollBar thumb when hovered). */
		Builder &ThumbColor(SDL::Color c) {
			sys.GetStyle(id).thumbColor = c;
			return *this;
		}
		/** @brief Set the overall opacity multiplier [0..1] applied to all rendering. */
		Builder &Opacity(float o) {
			sys.GetStyle(id).opacity = o;
			return *this;
		}

		/** @brief Set the texture key and fit mode for an Image widget. */
		Builder &ImageKey(const std::string &key, ImageFit f = ImageFit::Contain) {
			sys.SetImageKey(id, key, f);
			return *this;
		}

		/// Attach an icon texture to a Button (or any widget rendered by _DrawButton).
		/// The icon is drawn to the left of any text label, or centred when no text is set.
		/// @param key  Resource-pool key of the texture.
		/// @param pad  Inset from widget edges in pixels (default 4).
		Builder &Icon(const std::string &key, float pad = 4.f) {
			auto &ic = sys.GetOrAddIconData(id);
			ic.key = key;
			ic.pad = pad;
			return *this;
		}

		/// Set the icon opacity for each interactive state (values in [0, 1]).
		Builder &IconOpacity(float normal, float hovered = 1.f,
							 float pressed = 0.85f, float disabled = 0.35f) {
			auto &ic = sys.GetOrAddIconData(id);
			ic.opacityNormal   = normal;
			ic.opacityHovered  = hovered;
			ic.opacityPressed  = pressed;
			ic.opacityDisabled = disabled;
			return *this;
		}

		/// Set the icon color modulation (tint) for each interactive state.
		Builder &IconTint(SDL::Color normal,
						  SDL::Color hovered  = {255, 255, 255, 255},
						  SDL::Color pressed  = {220, 220, 220, 255},
						  SDL::Color disabled = {180, 180, 180, 255}) {
			auto &ic = sys.GetOrAddIconData(id);
			ic.tintNormalColor   = normal;
			ic.tintHoveredColor  = hovered;
			ic.tintPressedColor  = pressed;
			ic.tintDisabledColor = disabled;
			return *this;
		}

		/** @brief Set own font key and point size; sets FontType::Self. Pass empty key to force debug font. */
		Builder &Font(const std::string &key, float sz = 0.f) {
			auto &s = sys.GetStyle(id);
			s.fontKey  = key;
			s.usedFont = key.empty() ? FontType::Debug : FontType::Self;
			if (sz > 0.f) s.fontSize = sz;
			return *this;
		}
		/** @brief Set the font point size without changing the font key or FontType. */
		Builder &FontSize(float sz) {
			sys.GetStyle(id).fontSize = sz;
			return *this;
		}
		/** @brief Control how the font is resolved for this widget. */
		Builder &FontUsage(FontType ft) {
			sys.GetStyle(id).usedFont = ft;
			return *this;
		}
		/** @brief Inherit the font from the nearest ancestor configured with FontType::Self. */
		Builder &UseInheritedFont() {
			sys.GetStyle(id).usedFont = FontType::Inherited;
			return *this;
		}
		/** @brief Use the system-level default font (System::SetDefaultFont). */
		Builder &UseDefaultFont() {
			sys.GetStyle(id).usedFont = FontType::Default;
			return *this;
		}
		/** @brief Always use the SDL3 built-in debug font regardless of parent fonts. */
		Builder &UseDebugFont() {
			sys.GetStyle(id).usedFont = FontType::Debug;
			return *this;
		}
		/** @brief Use the root widget's configured font. */
		Builder &UseRootFont() {
			sys.GetStyle(id).usedFont = FontType::Root;
			return *this;
		}

		/** @brief Set the audio clip played on click. */
		Builder &ClickSound(const std::string &key) {
			sys.GetStyle(id).clickSound = key;
			return *this;
		}
		/** @brief Set the audio clip played on hover-enter. */
		Builder &HoverSound(const std::string &key) {
			sys.GetStyle(id).hoverSound = key;
			return *this;
		}
		/** @brief Set the audio clip played when the widget is scrolled. */
		Builder &ScrollSound(const std::string &key) {
			sys.GetStyle(id).scrollSound = key;
			return *this;
		}
		/** @brief Set the audio clip played when the widget becomes visible. */
		Builder &ShowSound(const std::string &key) {
			sys.GetStyle(id).showSound = key;
			return *this;
		}
		/** @brief Set the audio clip played when the widget is hidden. */
		Builder &HideSound(const std::string &key) {
			sys.GetStyle(id).hideSound = key;
			return *this;
		}

		// Layout
		/** @brief Apply an inline lambda to the LayoutProps component. */
		Builder &WithLayout(std::function<void(LayoutProps &)> fn) {
			fn(sys.GetLayout(id));
			return *this;
		}
		/** @brief Set the absolute X position (pixels) — only meaningful with Absolute/Fixed attach. */
		Builder &X(float px) {
			sys.GetLayout(id).absX = Value::Px(px);
			return *this;
		}
		/** @brief Set the absolute X position via a Value — only meaningful with Absolute/Fixed attach. */
		Builder &X(Value v) {
			sys.GetLayout(id).absX = v;
			return *this;
		}
		/** @brief Set the absolute Y position (pixels) — only meaningful with Absolute/Fixed attach. */
		Builder &Y(float px) {
			sys.GetLayout(id).absY = Value::Px(px);
			return *this;
		}
		/** @brief Set the absolute Y position via a Value — only meaningful with Absolute/Fixed attach. */
		Builder &Y(Value v) {
			sys.GetLayout(id).absY = v;
			return *this;
		}
		/** @brief Set the widget width (pixels). */
		Builder &W(float px) {
			sys.GetLayout(id).width = Value::Px(px);
			return *this;
		}
		/** @brief Set the widget width via a Value (Px, Pw, Grow, etc.). */
		Builder &W(Value v) {
			sys.GetLayout(id).width = v;
			return *this;
		}
		/** @brief Set the widget height (pixels). */
		Builder &H(float px) {
			sys.GetLayout(id).height = Value::Px(px);
			return *this;
		}
		/** @brief Set the widget height via a Value (Px, Ph, Grow, etc.). */
		Builder &H(Value v) {
			sys.GetLayout(id).height = v;
			return *this;
		}
		/** @brief Set the minimum width (pixels). */
		Builder &MinW(float px) { sys.GetLayout(id).minWidth  = Value::Px(px); return *this; }
		/** @brief Set the minimum width via a Value. */
		Builder &MinW(Value  v) { sys.GetLayout(id).minWidth  = v;             return *this; }
		/** @brief Set the minimum height (pixels). */
		Builder &MinH(float px) { sys.GetLayout(id).minHeight = Value::Px(px); return *this; }
		/** @brief Set the minimum height via a Value. */
		Builder &MinH(Value  v) { sys.GetLayout(id).minHeight = v;             return *this; }
		/** @brief Set the maximum width (pixels). */
		Builder &MaxW(float px) { sys.GetLayout(id).maxWidth  = Value::Px(px); return *this; }
		/** @brief Set the maximum width via a Value. */
		Builder &MaxW(Value  v) { sys.GetLayout(id).maxWidth  = v;             return *this; }
		/** @brief Set the maximum height (pixels). */
		Builder &MaxH(float px) { sys.GetLayout(id).maxHeight = Value::Px(px); return *this; }
		/** @brief Set the maximum height via a Value. */
		Builder &MaxH(Value  v) { sys.GetLayout(id).maxHeight = v;             return *this; }
		/** @brief Grow the widget's width to fill remaining horizontal space (InLine parents only). */
		Builder &GrowW(float g) { W(Value::Grow(g)); return *this; }
		/** @brief Grow the widget's height to fill remaining vertical space (InColumn parents only). */
		Builder &GrowH(float g) { H(Value::Grow(g)); return *this; }
		/** @brief Grow both width and height to fill remaining space. */
		Builder &Grow(float g) { W(Value::Grow(g)); H(Value::Grow(g)); return *this; }
		/** @brief Set the gap between children in pixel (InColumn / InLine / Stack layouts). */
		Builder &Gap(float g) {
			sys.GetLayout(id).gap = g;
			return *this;
		}
		/** @brief Set the layout mode for a container widget. */
		Builder &Layout(Layout l) {
			sys.GetLayout(id).layout = l;
			return *this;
		}
		/** @brief Set the default horizontal alignment for direct children (InColumn containers). */
		Builder &AlignChildrenH(UI::Align a) {
			sys.GetLayout(id).alignChildrenH = a;
			return *this;
		}
		/** @brief Set the default vertical alignment for direct children (InLine / Stack containers). */
		Builder &AlignChildrenV(UI::Align a) {
			sys.GetLayout(id).alignChildrenV = a;
			return *this;
		}
		/** @brief Set both horizontal and vertical default child alignment at once. */
		Builder &AlignChildren(UI::Align h, UI::Align v) {
			auto &lp = sys.GetLayout(id);
			lp.alignChildrenH = h;
			lp.alignChildrenV = v;
			return *this;
		}
		/** @brief Set this widget's own horizontal alignment within its parent. */
		Builder &AlignH(UI::Align a) {
			sys.GetLayout(id).alignSelfH = a;
			return *this;
		}
		/** @brief Set this widget's own vertical alignment within its parent. */
		Builder &AlignV(UI::Align a) {
			sys.GetLayout(id).alignSelfV = a;
			return *this;
		}
		/** @brief Set both horizontal and vertical self-alignment at once. */
		Builder &Align(UI::Align h, UI::Align v) {
			auto &lp = sys.GetLayout(id);
			lp.alignSelfH = h;
			lp.alignSelfV = v;
			return *this;
		}
		/** @brief Set the CSS-style box-sizing mode (what W/H dimensions include). */
		Builder &BoxSizingMode(BoxSizing b) {
			sys.GetLayout(id).boxSizing = b;
			return *this;
		}
		/** @brief Set the attach mode (Relative, Absolute, or Fixed). */
		Builder &Attach(AttachLayout a) {
			sys.GetLayout(id).attach = a;
			return *this;
		}
		/** @brief Position the widget absolutely within its parent using Values. */
		Builder &Absolute(Value x, Value y) {
			auto &l = sys.GetLayout(id);
			l.attach = AttachLayout::Absolute;
			l.absX = x;
			l.absY = y;
			return *this;
		}
		/** @brief Position the widget absolutely within its parent using pixel coordinates. */
		Builder &Absolute(float x, float y) { return Absolute(Value::Px(x), Value::Px(y)); }
		/** @brief Position the widget at a fixed position relative to the root viewport using Values. */
		Builder &Fixed(Value x, Value y) {
			auto &l = sys.GetLayout(id);
			l.attach = AttachLayout::Fixed;
			l.absX = x;
			l.absY = y;
			return *this;
		}

		/** @brief Set uniform padding on all four sides (pixels). */
		Builder &Padding(float a) {
			auto &l = sys.GetLayout(id);
			l.padding.left = l.padding.top = l.padding.right = l.padding.bottom = a;
			return *this;
		}
		/** @brief Set horizontal and vertical padding independently (pixels). */
		Builder &Padding(float h, float v) {
			auto &l = sys.GetLayout(id);
			l.padding.left = l.padding.right = h;
			l.padding.top = l.padding.bottom = v;
			return *this;
		}
		/** @brief Set padding on all four sides individually. */
		Builder &Padding(SDL::FBox p) {
			auto &l = sys.GetLayout(id);
			l.padding = p;
			return *this;
		}
		/** @brief Set left and right padding to the same value (pixels). */
		Builder &PaddingH(float h) {
			auto &l = sys.GetLayout(id);
			l.padding.left = l.padding.right = h;
			return *this;
		}
		/** @brief Set top and bottom padding to the same value (pixels). */
		Builder &PaddingV(float v) {
			auto &l = sys.GetLayout(id);
			l.padding.top = l.padding.bottom = v;
			return *this;
		}
		/** @brief Set the bottom padding (pixels). */
		Builder &PaddingBottom(float v) {
			sys.GetLayout(id).padding.bottom = v;
			return *this;
		}
		/** @brief Set the top padding (pixels). */
		Builder &PaddingTop(float v) {
			sys.GetLayout(id).padding.top = v;
			return *this;
		}
		/** @brief Set the left padding (pixels). */
		Builder &PaddingLeft(float v) {
			sys.GetLayout(id).padding.left = v;
			return *this;
		}
		/** @brief Set the right padding (pixels). */
		Builder &PaddingRight(float v) {
			sys.GetLayout(id).padding.right = v;
			return *this;
		}

		/** @brief Set uniform margin on all four sides (pixels). */
		Builder &Margin(float a) {
			auto &l = sys.GetLayout(id);
			l.margin.left = l.margin.top = l.margin.right = l.margin.bottom = a;
			return *this;
		}
		/** @brief Set horizontal and vertical margins independently (pixels). */
		Builder &Margin(float h, float v) {
			auto &l = sys.GetLayout(id);
			l.margin.left = l.margin.right = h;
			l.margin.top = l.margin.bottom = v;
			return *this;
		}
		/** @brief Set margin on all four sides individually. */
		Builder &Margin(SDL::FBox m) {
			auto &l = sys.GetLayout(id);
			l.margin = m;
			return *this;
		}
		/** @brief Set left and right margins to the same value (pixels). */
		Builder &MarginH(float h) {
			auto &l = sys.GetLayout(id);
			l.margin.left = l.margin.right = h;
			return *this;
		}
		/** @brief Set top and bottom margins to the same value (pixels). */
		Builder &MarginV(float v) {
			auto &l = sys.GetLayout(id);
			l.margin.top = l.margin.bottom = v;
			return *this;
		}
		/** @brief Set the bottom margin (pixels). */
		Builder &MarginBottom(float v) {
			sys.GetLayout(id).margin.bottom = v;
			return *this;
		}
		/** @brief Set the top margin (pixels). */
		Builder &MarginTop(float v) {
			sys.GetLayout(id).margin.top = v;
			return *this;
		}
		/** @brief Set the left margin (pixels). */
		Builder &MarginLeft(float v) {
			sys.GetLayout(id).margin.left = v;
			return *this;
		}
		/** @brief Set the right margin (pixels). */
		Builder &MarginRight(float v) {
			sys.GetLayout(id).margin.right = v;
			return *this;
		}

		// BehaviorFlag
		/** @brief Enable or disable the widget (disabled = not interactive, grayed visually). */
		Builder &Enable(bool e = true) {
			sys.SetEnabled(id, e);
			return *this;
		}
		/** @brief Disable the widget (shorthand for Enable(false)). */
		Builder &Disable() {
			sys.SetEnabled(id, false);
			return *this;
		}
		/** @brief Show or hide the widget (hidden = takes no space, receives no input). */
		Builder &Visible(bool v = true) {
			sys.SetVisible(id, v);
			return *this;
		}
		/** @brief Hide the widget (shorthand for Visible(false)). */
		Builder &Hide() {
			sys.SetVisible(id, false);
			return *this;
		}
		/** @brief Show the widget (shorthand for Visible(true)). */
		Builder &Show() {
			sys.SetVisible(id, true);
			return *this;
		}
		/** @brief Enable or disable hover detection on the widget. */
		Builder &Hoverable(bool v = true) {
			sys.SetHoverable(id, v);
			return *this;
		}
		/** @brief Enable or disable mouse-click selection on the widget. */
		Builder &Selectable(bool v = true) {
			sys.SetSelectable(id, v);
			return *this;
		}
		/** @brief Enable or disable keyboard focus on the widget. */
		Builder &Focusable(bool v = true) {
			sys.SetFocusable(id, v);
			return *this;
		}
		/** @brief Enable or disable the permanent horizontal scrollbar. */
		Builder &ScrollableX(bool b = true) {
			sys.SetScrollableX(id, b);
			return *this;
		}
		/** @brief Enable or disable the permanent vertical scrollbar. */
		Builder &ScrollableY(bool b = true) {
			sys.SetScrollableY(id, b);
			return *this;
		}
		/** @brief Shorthand to set both permanent scrollbar axes at once. */
		Builder &Scrollable(bool bx = true, bool by = true) {
			sys.SetScrollable(id, bx, by);
			return *this;
		}
		/// Scrollbar horizontal automatique : visible seulement si le contenu déborde.
		Builder &AutoScrollableX(bool b = true) {
			sys.SetAutoScrollableX(id, b);
			return *this;
		}
		/// Scrollbar vertical automatique : visible seulement si le contenu déborde.
		Builder &AutoScrollableY(bool b = true) {
			sys.SetAutoScrollableY(id, b);
			return *this;
		}
		/// Active les deux scrollbars automatiques.
		Builder &AutoScrollable(bool bx = true, bool by = true) {
			sys.SetAutoScrollable(id, bx, by);
			return *this;
		}
		/// Épaisseur en pixels des scrollbars inline (défaut : 8 px).
		Builder &ScrollbarThickness(float t) {
			sys.SetScrollbarThickness(id, t);
			return *this;
		}

		// ── Grid layout (container) ───────────────────────────────────────────────

		/// Number of columns in a Layout::InGrid container.
		Builder &GridCols(int n) { sys.SetGridCols(id, n); return *this; }
		/// Fixed number of rows (0 = auto-computed from children).
		Builder &GridRows(int n) { sys.SetGridRows(id, n); return *this; }
		/// Column sizing mode (Fixed = equal width, Content = fit content).
		Builder &GridColSizing(GridSizing s) { sys.SetGridColSizing(id, s); return *this; }
		/// Row sizing mode (Fixed = equal height, Content = fit content).
		Builder &GridRowSizing(GridSizing s) { sys.SetGridRowSizing(id, s); return *this; }
		/// Shorthand to set both column and row sizing at once.
		Builder &GridSizingMode(GridSizing cs, GridSizing rs) {
			sys.SetGridColSizing(id, cs);
			sys.SetGridRowSizing(id, rs);
			return *this;
		}
		/// Which separator lines to draw between cells.
		Builder &GridLineStyle(GridLines l) { sys.SetGridLines(id, l); return *this; }
		/// Color of the separator lines.
		Builder &GridLineColor(SDL::Color c) { sys.SetGridLineColor(id, c); return *this; }
		/// Thickness in pixels of the separator lines.
		Builder &GridLineThickness(float t) { sys.SetGridLineThickness(id, t); return *this; }

		// ── Grid cell (child) ─────────────────────────────────────────────────────

		/// Place this widget at the given grid cell.  colSpan/rowSpan default to 1.
		Builder &Cell(int col, int row, int colSpan = 1, int rowSpan = 1) {
			sys.SetGridCell(id, col, row, colSpan, rowSpan);
			return *this;
		}

		/** @brief Set the text content of the widget at runtime. */
		Builder &SetText(const std::string &t) {
			sys.SetText(id, t);
			return *this;
		}
		/** @brief Set the numeric value of a Slider or Knob widget at runtime (clamped). */
		Builder &SetValue(float v) {
			sys.SetValue(id, v);
			return *this;
		}
		/** @brief Set the checked state of a Toggle or RadioButton widget at runtime. */
		Builder &Check(bool c = true) {
			sys.SetChecked(id, c);
			return *this;
		}

		// Callbacks
		/** @brief Register (or replace) the render callback on a Canvas widget. */
		Builder &OnRenderCanvas(std::function<void(RendererRef, FRect)> cb) {
			sys.OnRenderCanvas(id, std::move(cb));
			return *this;
		}

		// TextArea
		/** @brief Replace the document content of a TextArea widget. */
		Builder &TextAreaContent(const std::string &t) {
			sys.SetTextAreaContent(id, t);
			return *this;
		}
		/** @brief Set the selection highlight color of a TextArea widget. */
		Builder &TextAreaHighlightColor(SDL::Color c) {
			sys.SetTextAreaHighlightColor(id, c);
			return *this;
		}
		/** @brief Set the tab-stop size (in character columns) for a TextArea widget. */
		Builder &TextAreaTabSize(int n) {
			sys.SetTextAreaTabSize(id, n);
			return *this;
		}
		/** @brief Add a rich-text span to a TextArea widget (see AddTextAreaSpan). */
		Builder &TextAreaSpan(int start, int end, TextSpanStyle style) {
			sys.AddTextAreaSpan(id, start, end, style);
			return *this;
		}
		/** @brief Set or clear the read-only mode of a TextArea widget. */
		Builder &ReadOnly(bool ro = true) {
			sys.SetTextAreaReadOnly(id, ro);
			return *this;
		}

		// ── Tooltip ───────────────────────────────────────────────────────────────────

		/// Attache une info-bulle à ce widget (affichée après @p delay secondes de survol).
		Builder &Tooltip(const std::string &text, float delay = 1.f) {
			sys.SetTooltip(id, text, delay);
			return *this;
		}
		/// Couleur de fond de l'info-bulle (héritée du widget survolé).
		Builder &TooltipBg(SDL::Color c) {
			sys.GetStyle(id).tooltipBgColor = c;
			return *this;
		}
		/// Couleur de bordure de l'info-bulle.
		Builder &TooltipBd(SDL::Color c) {
			sys.GetStyle(id).tooltipBdColor = c;
			return *this;
		}
		/// Couleur du texte de l'info-bulle.
		Builder &TooltipTextColor(SDL::Color c) {
			sys.GetStyle(id).tooltipTextColor = c;
			return *this;
		}

		// ── BgGradient ────────────────────────────────────────────────────────────────

		/// Attach a gradient background to this widget.
		/// @param color2	End colour used for the normal state.
		/// @param start	Gradient start.
		/// @param end		Gradient end.
		Builder &BgGradient(SDL::Color color2, GradientAnchor start = GradientAnchor::Top, GradientAnchor end = GradientAnchor::Bottom) {
			::SDL::UI::BgGradient g;
			g.color2         = color2;
			g.color2Hovered  = {(Uint8)SDL::Min(255, color2.r + 20), (Uint8)SDL::Min(255, color2.g + 20), (Uint8)SDL::Min(255, color2.b + 30), color2.a};
			g.color2Pressed  = {(Uint8)(color2.r * 0.6f), (Uint8)(color2.g * 0.6f), (Uint8)(color2.b * 0.6f), color2.a};
			g.color2Focused  = g.color2Pressed;
			g.color2Disabled = {(Uint8)(color2.r * 0.5f), (Uint8)(color2.g * 0.5f), (Uint8)(color2.b * 0.5f), (Uint8)(color2.a * 0.6f)};
			g.start          = start;
			g.end			 = end;
			sys.SetBgGradient(id, g);
			return *this;
		}
		/// Set the end color of the gradient for the hovered state.
		Builder &BgGradientHover(SDL::Color c)    { if (auto *g = sys.GetECSContext().Get<::SDL::UI::BgGradient>(id)) g->color2Hovered   = c; return *this; }
		/// Set the end color of the gradient for the pressed state.
		Builder &BgGradientPress(SDL::Color c)    { if (auto *g = sys.GetECSContext().Get<::SDL::UI::BgGradient>(id)) g->color2Pressed   = c; return *this; }
		/// Set the end color of the gradient for the checked state.
		Builder &BgGradientCheck(SDL::Color c)    { if (auto *g = sys.GetECSContext().Get<::SDL::UI::BgGradient>(id)) g->color2Checked   = c; return *this; }
		/// Set the end color of the gradient for the focused state.
		Builder &BgGradientFocus(SDL::Color c)    { if (auto *g = sys.GetECSContext().Get<::SDL::UI::BgGradient>(id)) g->color2Focused   = c; return *this; }
		/// Set the end color of the gradient for the disabled state.
		Builder &BgGradientDisable(SDL::Color c)  { if (auto *g = sys.GetECSContext().Get<::SDL::UI::BgGradient>(id)) g->color2Disabled  = c; return *this; }
		/// Set the gradient start.
		Builder &BgGradientStart(GradientAnchor start) { if (auto *g = sys.GetECSContext().Get<::SDL::UI::BgGradient>(id)) g->start = start; return *this; }
		/// Set the gradient end.
		Builder &BgGradientEnd(GradientAnchor end)     { if (auto *g = sys.GetECSContext().Get<::SDL::UI::BgGradient>(id)) g->end = end; return *this; }
		/// Remove the gradient background (revert to solid colour).
		Builder &RemoveBgGradient() { sys.RemoveBgGradient(id); return *this; }

		/// Attach a 9-slice tileset skin to this widget.
		Builder &TilesetSkin(TilesetStyle ts) {
			sys.SetTilesetStyle(id, std::move(ts));
			return *this;
		}

		/// Remove a previously attached tileset skin.
		Builder &RemoveTilesetSkin() {
			sys.RemoveTilesetStyle(id);
			return *this;
		}

		// ── ComboBox setters ───────────────────────────────────────────────────────
		Builder &Items(const std::vector<std::string>& items) {
			if (auto *d = sys.GetECSContext().Get<ComboBoxData>(id)) d->items = items;
			return *this;
		}
		Builder &SelectedIndex(int idx) {
			if (auto *d = sys.GetECSContext().Get<ComboBoxData>(id)) d->selectedIndex = idx;
			return *this;
		}
		Builder &ItemHeight(int h) {
			if (auto *d = sys.GetECSContext().Get<ComboBoxData>(id)) d->itemHeight = h;
			return *this;
		}
		Builder &MaxVisible(int n) {
			if (auto *d = sys.GetECSContext().Get<ComboBoxData>(id)) d->maxVisible = n;
			return *this;
		}

		// ── InputValue setters ────────────────────────────────────────────────────────
		Builder &InputRange(float mn, float mx) {
			if (auto *d = sys.GetECSContext().Get<InputData>(id)) { d->min = mn; d->max = mx; }
			return *this;
		}
		Builder &InputStep(float step) {
			if (auto *d = sys.GetECSContext().Get<InputData>(id)) d->step = step;
			return *this;
		}
		Builder &InputDecimals(int dec) {
			if (auto *d = sys.GetECSContext().Get<InputData>(id)) d->decimals = dec;
			return *this;
		}

		// ── TabView setters ────────────────────────────────────────────────────────
		Builder &AddTab(const std::string &label, bool closable = false) {
			if (auto *d = sys.GetECSContext().Get<TabViewData>(id)) d->tabs.push_back({label, closable});
			return *this;
		}
		Builder &ActiveTab(int idx) {
			if (auto *d = sys.GetECSContext().Get<TabViewData>(id)) d->activeTab = idx;
			return *this;
		}
		Builder &TabHeight(float h) {
			if (auto *d = sys.GetECSContext().Get<TabViewData>(id)) d->tabHeight = h;
			return *this;
		}
		Builder &TabsBottom(bool v) {
			if (auto *d = sys.GetECSContext().Get<TabViewData>(id)) d->tabsBottom = v;
			return *this;
		}

		// ── Expander setters ───────────────────────────────────────────────────────
		Builder &ExpanderLabel(const std::string &label) {
			if (auto *d = sys.GetECSContext().Get<ExpanderData>(id)) d->label = label;
			return *this;
		}
		Builder &Expanded(bool v) {
			if (auto *d = sys.GetECSContext().Get<ExpanderData>(id)) {
				d->expanded = v; d->animT = v ? 1.f : 0.f;
			}
			return *this;
		}
		Builder &HeaderHeight(float h) {
			if (auto *d = sys.GetECSContext().Get<ExpanderData>(id)) d->headerH = h;
			if (auto *d = sys.GetECSContext().Get<TabViewData>(id))   d->tabHeight = h;
			return *this;
		}

		// ── Splitter setters ───────────────────────────────────────────────────────
		Builder &SplitRatio(float ratio) {
			if (auto *d = sys.GetECSContext().Get<SplitterData>(id)) d->ratio = SDL::Clamp(ratio, d->minRatio, d->maxRatio);
			return *this;
		}
		Builder &SplitRatioRange(float mn, float mx) {
			if (auto *d = sys.GetECSContext().Get<SplitterData>(id)) { d->minRatio = mn; d->maxRatio = mx; }
			return *this;
		}
		Builder &HandleSize(float s) {
			if (auto *d = sys.GetECSContext().Get<SplitterData>(id)) d->handleSize = s;
			return *this;
		}

		// ── Spinner setters ────────────────────────────────────────────────────────
		Builder &SpinnerSpeed(float speed) {
			if (auto *d = sys.GetECSContext().Get<SpinnerData>(id)) d->speed = speed;
			return *this;
		}
		Builder &SpinnerArc(float arc) {
			if (auto *d = sys.GetECSContext().Get<SpinnerData>(id)) d->arcSpan = arc;
			return *this;
		}
		Builder &SpinnerThickness(float t) {
			if (auto *d = sys.GetECSContext().Get<SpinnerData>(id)) d->thickness = t;
			return *this;
		}

		// ── Knob setters ──────────────────────────────────────────────────────────
		Builder &SetKnobShape(KnobShape shape) {
			if (auto *kd = sys.GetECSContext().Get<KnobData>(id)) kd->shape = shape;
			return *this;
		}

		// ── ColorPicker setters ───────────────────────────────────────────────────
		Builder &PickerPalette(ColorPickerPalette p) {
			if (auto *d = sys.GetColorPickerData(id)) d->palette = p;
			return *this;
		}
		Builder &PickedColor(SDL::Color c) {
			sys.SetPickedColor(id, c);
			return *this;
		}
		Builder &PickerColorA(SDL::Color c) {
			if (auto *d = sys.GetColorPickerData(id)) d->colorA = c;
			return *this;
		}
		Builder &PickerColorB(SDL::Color c) {
			if (auto *d = sys.GetColorPickerData(id)) d->colorB = c;
			return *this;
		}
		Builder &PickerStep(float step) {
			if (auto *d = sys.GetColorPickerData(id)) d->precisionStep = step;
			return *this;
		}
		Builder &PickerShowAlpha(bool v = true) {
			if (auto *d = sys.GetColorPickerData(id)) d->showAlpha = v;
			return *this;
		}

		// ── Popup setters ──────────────────────────────────────────────────────────
		Builder &PopupTitle(const std::string &title) {
			sys.SetPopupTitle(id, title);
			return *this;
		}
		Builder &PopupOpen(bool v = true) {
			sys.SetPopupOpen(id, v);
			return *this;
		}
		Builder &PopupHeaderH(float h) {
			if (auto *d = sys.GetPopupData(id)) d->headerH = h;
			return *this;
		}
		Builder &PopupClosable(bool v = true) {
			if (auto *d = sys.GetPopupData(id)) d->closable = v;
			return *this;
		}
		Builder &PopupDraggable(bool v = true) {
			if (auto *d = sys.GetPopupData(id)) d->draggable = v;
			return *this;
		}
		Builder &PopupResizable(bool v = true) {
			if (auto *d = sys.GetPopupData(id)) d->resizable = v;
			return *this;
		}
		Builder &PopupHeaderBtn(const std::string &iconKey, std::function<void()> cb) {
			sys.AddPopupHeaderButton(id, iconKey, std::move(cb));
			return *this;
		}

		// ── Tree setters ───────────────────────────────────────────────────────────
		Builder &TreeNode(const std::string &label, int level = 0, bool hasChildren = false,
		                  bool expanded = false, const std::string &iconKey = "") {
			sys.AddTreeNode(id, {label, iconKey, level, hasChildren, expanded});
			return *this;
		}
		Builder &ClearTree() {
			sys.ClearTreeNodes(id);
			return *this;
		}
		Builder &TreeItemHeight(float h) {
			if (auto *d = sys.GetTreeData(id)) d->itemHeight = h;
			return *this;
		}
		Builder &TreeIndent(float px) {
			if (auto *d = sys.GetTreeData(id)) d->indentSize = px;
			return *this;
		}
		Builder &TreeIconSize(float px) {
			if (auto *d = sys.GetTreeData(id)) d->iconSize = px;
			return *this;
		}

		// ── MenuBar setters ────────────────────────────────────────────────────────
		/** @brief Append a top-level menu to a MenuBar widget. */
		Builder &AddMenu(MenuBarMenu menu) {
			if (auto *d = sys.GetMenuBarData(id)) d->menus.push_back(std::move(menu));
			return *this;
		}
		/** @brief Append a top-level menu (label + items) to a MenuBar widget. */
		Builder &AddMenu(const std::string &label, std::vector<MenuBarItem> items, bool enabled = true) {
			if (auto *d = sys.GetMenuBarData(id)) d->menus.push_back({label, std::move(items), enabled});
			return *this;
		}
		/** @brief Set MenuBar height. */
		Builder &MenuBarH(float h) {
			if (auto *d = sys.GetMenuBarData(id)) (void)d;
			sys.GetECSContext().Get<LayoutProps>(id)->height = Value::Px(h);
			return *this;
		}

		// ── Badge setters ──────────────────────────────────────────────────────────
		Builder &BadgeText(const std::string &text) {
			if (auto *d = sys.GetECSContext().Get<BadgeData>(id)) d->text = text;
			return *this;
		}
		Builder &BadgeBgColor(SDL::Color c) {
			if (auto *d = sys.GetECSContext().Get<BadgeData>(id)) d->bgColor = c;
			return *this;
		}
		Builder &BadgeTextColor(SDL::Color c) {
			if (auto *d = sys.GetECSContext().Get<BadgeData>(id)) d->textColor = c;
			return *this;
		}

		/** @brief Register a callback invoked when the widget is clicked. */
		Builder &OnClick(std::function<void()> cb) {
			sys.OnClick(id, std::move(cb));
			return *this;
		}
		/** @brief Register a callback invoked on double-click. For Input/TextArea it fires after the automatic selection. */
		Builder &OnDoubleClick(std::function<void()> cb) {
			sys.OnDoubleClick(id, std::move(cb));
			return *this;
		}
		/** @brief When set to false, unhandled scroll/wheel events are not propagated to parent widgets. */
		Builder &DispatchEvent(bool b) {
			sys.SetDispatchEvent(id, b);
			return *this;
		}
		/**
		 * @brief Register a callback invoked when the widget's numeric value changes.
		 * @param cb  Receives the new value (Slider, Knob, ScrollBar, etc.).
		 */
		Builder &OnChange(std::function<void(float)> cb) {
			sys.OnChange(id, std::move(cb));
			return *this;
		}
		/**
		 * @brief Register a callback invoked when the widget's text content changes.
		 * @param cb  Receives the new text string (Input, TextArea, etc.).
		 */
		Builder &OnTextChange(std::function<void(const std::string &)> cb) {
			sys.OnTextChange(id, std::move(cb));
			return *this;
		}
		/**
		 * @brief Register a callback invoked when a Toggle or RadioButton changes state.
		 * @param cb  Receives `true` if the widget is now checked.
		 */
		Builder &OnToggle(std::function<void(bool)> cb) {
			sys.OnToggle(id, std::move(cb));
			return *this;
		}
		/**
		 * @brief Register a callback invoked when the widget is scrolled.
		 * @param cb  Receives the scroll delta.
		 */
		Builder &OnScroll(std::function<void(float)> cb) {
			sys.OnScroll(id, std::move(cb));
			return *this;
		}
		/** @brief Enable drag-to-reorder on a ListBox widget. */
		Builder &Reorderable(bool v = true) {
			sys.SetListBoxReorderable(id, v);
			return *this;
		}
		/** @brief Register a reorder callback for a ListBox (fromIdx, toIdx). */
		Builder &OnReorder(std::function<void(int,int)> cb) {
			sys.SetListBoxOnReorder(id, std::move(cb));
			return *this;
		}
		/** @brief Register a callback invoked when the mouse cursor enters the widget. */
		Builder &OnHoverEnter(std::function<void()> cb) {
			sys.OnHoverEnter(id, std::move(cb));
			return *this;
		}
		/** @brief Register a callback invoked when the mouse cursor leaves the widget. */
		Builder &OnHoverLeave(std::function<void()> cb) {
			sys.OnHoverLeave(id, std::move(cb));
			return *this;
		}
		/** @brief Register a callback invoked when the widget gains keyboard focus. */
		Builder &OnFocusGain(std::function<void()> cb) {
			sys.OnFocusGain(id, std::move(cb));
			return *this;
		}
		/** @brief Register a callback invoked when the widget loses keyboard focus. */
		Builder &OnFocusLose(std::function<void()> cb) {
			sys.OnFocusLose(id, std::move(cb));
			return *this;
		}

		/// Register a callback fired when a Tree node is selected (nodeIndex, hasChildren).
		Builder &OnTreeSelect(std::function<void(int, bool)> cb) {
			sys.OnTreeSelect(id, std::move(cb));
			return *this;
		}

		// Children
		/** @brief Append a child entity to this container. */
		Builder &Child(ECS::EntityId c) {
			sys.AppendChild(id, c);
			return *this;
		}
		/// Append a child and pin it to a grid cell (shorthand for child.Cell(...)).
		/// Only meaningful when this container uses Layout::InGrid.
		Builder &Child(ECS::EntityId c, int col, int row, int colSpan = 1, int rowSpan = 1) {
			sys.SetGridCell(c, col, row, colSpan, rowSpan);
			sys.AppendChild(id, c);
			return *this;
		}
		/// Append a child Builder and pin it to a grid cell.
		Builder &Child(Builder c, int col, int row, int colSpan = 1, int rowSpan = 1) {
			sys.SetGridCell(c.id, col, row, colSpan, rowSpan);
			sys.AppendChild(id, c.id);
			return *this;
		}
		/**
		 * @brief Append multiple child entities to this container in one call.
		 * @param cs  Any number of entity IDs (or Builder instances) to append.
		 */
		template <typename... Cs>
			requires(std::convertible_to<Cs, ECS::EntityId> && ...)
		Builder &Children(Cs &&...cs) {
			(sys.AppendChild(id, static_cast<ECS::EntityId>(std::forward<Cs>(cs))), ...);
			return *this;
		}

		/** @brief Append this widget as a child of @p parent. */
		Builder &AttachTo(ECS::EntityId parent) {
			sys.AppendChild(parent, id);
			return *this;
		}
		/**
		 * @brief Append this widget as a child of @p parent and pin it to a grid cell.
		 * @param parent   Target container entity.
		 * @param col      Zero-based column index.
		 * @param row      Zero-based row index.
		 * @param colSpan  Number of columns to span (default 1).
		 * @param rowSpan  Number of rows to span (default 1).
		 */
		Builder &AttachTo(ECS::EntityId parent, int col, int row, int colSpan = 1, int rowSpan = 1) {
			sys.SetGridCell(id, col, row, colSpan, rowSpan);
			sys.AppendChild(parent, id);
			return *this;
		}
		/**
		 * @brief Append this widget as a child of @p parent (Builder overload) and pin it to a grid cell.
		 * @param parent   Target container Builder.
		 * @param col      Zero-based column index.
		 * @param row      Zero-based row index.
		 * @param colSpan  Number of columns to span (default 1).
		 * @param rowSpan  Number of rows to span (default 1).
		 */
		Builder &AttachTo(Builder parent, int col, int row, int colSpan = 1, int rowSpan = 1) {
			sys.SetGridCell(id, col, row, colSpan, rowSpan);
			sys.AppendChild(parent.id, id);
			return *this;
		}

		/** @brief Promote this widget to be the root of the UI tree. */
		Builder &AsRoot() {
			sys.SetRoot(id);
			return *this;
		}
	};

	// =============================================================================
	// System builder factory implementations
	// =============================================================================

	inline Builder System::GetBuilder(ECS::EntityId e) { return {*this, e}; }
	
	inline Builder System::Container(const std::string &n) { return {*this, MakeContainer(n)}; }
	inline Builder System::Label(const std::string &n, const std::string &t) { return {*this, MakeLabel(n, t)}; } 
	inline Builder System::Button(const std::string &n, const std::string &t) { return {*this, MakeButton(n, t)}; }
	inline Builder System::Toggle(const std::string &n, const std::string &t) { return {*this, MakeToggle(n, t)}; }
	inline Builder System::Radio(const std::string &n, const std::string &g, const std::string &t) { return {*this, MakeRadioButton(n, g, t)}; }
	inline Builder System::Slider(const std::string &n, float mn, float mx, float v, Orientation o) { return {*this, MakeSlider(n, mn, mx, v, o)}; }
	inline Builder System::ScrollBar(const std::string &n, float cs, float vs, Orientation o) { return {*this, MakeScrollBar(n, cs, vs, o)}; }
	inline Builder System::Progress(const std::string &n, float v, float mx) { return {*this, MakeProgress(n, v, mx)}; }
	inline Builder System::Separator(const std::string &n) { return {*this, MakeSeparator(n)}; }
	inline Builder System::Input(const std::string &n, const std::string &ph) { return {*this, MakeInput(n, ph)}; }
	inline Builder System::Knob(const std::string &n, float mn, float mx, float v, KnobShape shape) { return {*this, MakeKnob(n, mn, mx, v, shape)}; }
	inline Builder System::ImageWidget(const std::string &n, const std::string &p, ImageFit f) { return {*this, MakeImage(n, p, f)}; }
	inline Builder System::CanvasWidget(const std::string &n,
		std::function<void(SDL::Event&)> cb_event, 
		std::function<void(float)> cb_update,
		std::function<void(RendererRef, FRect)> cb_render) { return {*this, MakeCanvas(n, std::move(cb_event), std::move(cb_update), std::move(cb_render))}; }
	inline Builder System::TextArea(const std::string &n, const std::string &text, const std::string &ph) { return {*this, MakeTextArea(n, text, ph)}; }

	inline Builder System::Column(const std::string &n, float gap, float pad, float marg) {
        auto b = Container(n);
        b.Layout(Layout::InColumn).Gap(gap).Padding(pad).Margin(marg);
        return b;
    }

    inline Builder System::Row(const std::string &n, float gap, float pad, float marg) {
        auto b = Container(n);
        b.Layout(Layout::InLine).Gap(gap).Padding(pad).Margin(marg)
            .AlignH(Align::Center);
        return b;
    }

    inline Builder System::Card(const std::string &n, float gap, float marg) {
        auto b = Container(n);
        b.Layout(Layout::InColumn).Gap(gap).Margin(marg);
        b.Style(Theme::Card()).PaddingH(14.f).PaddingV(12.f);
        return b;
    }

	inline Builder System::Stack(const std::string &n, float gap, float pad, float marg) {
		auto b = Container(n);
		b.Layout(Layout::Stack).Gap(gap).Padding(pad).Padding(marg)
			.W(SDL::UI::Value::Auto())
			.H(SDL::UI::Value::Auto())
			.AlignH(Align::Center);
		return b;
	}

	inline Builder System::SectionTitle(const std::string &text, SDL::Color col) {
		auto b = Label("title_" + text, text);
		b.TextColor(col).MarginBottom(4.f);
		return b;
	}

	inline Builder System::ScrollView(const std::string &n, float gap) {
		auto b = Column(n, gap, 0.f);
		// AutoScrollableY : la scrollbar verticale n'apparaît que si le contenu déborde.
		b.AutoScrollableY().Padding(0);
		return b;
	}

	inline Builder System::Grid(const std::string &n, int columns, float gap, float pad) {
		auto b = Container(n);
		b.Layout(Layout::InGrid).Gap(gap).Padding(pad);
		b.GridCols(columns);
		return b;
	}

	// =============================================================================
	// Theme implementations
	// =============================================================================

	inline void Theme::ApplyDark(System &) { accentColor = {70, 130, 210, 255}; }
	inline void Theme::ApplyLight(System &) { accentColor = {60, 120, 200, 255}; } 

	inline Builder System::ListBoxWidget(const std::string &n,
										 const std::vector<std::string>& items) {
		return Builder(*this, MakeListBox(n, items));
	}

	inline Builder System::GradedGraph(const std::string &n) {
		return Builder(*this, MakeGraph(n));
	}

	inline Builder System::ComboBox(const std::string &n, const std::vector<std::string>& items, int sel) {
		return Builder(*this, MakeComboBox(n, items, sel));
	}
	inline Builder System::TabView(const std::string &n) {
		return Builder(*this, MakeTabView(n));
	}
	inline Builder System::Expander(const std::string &n, const std::string &label, bool expanded) {
		return Builder(*this, MakeExpander(n, label, expanded));
	}
	inline Builder System::Splitter(const std::string &n, Orientation o, float ratio) {
		return Builder(*this, MakeSplitter(n, o, ratio));
	}
	inline Builder System::Spinner(const std::string &n, float speed) {
		return Builder(*this, MakeSpinner(n, speed));
	}
	inline Builder System::Badge(const std::string &n, const std::string &text) {
		return Builder(*this, MakeBadge(n, text));
	}
	inline Builder System::ColorPicker(const std::string &n, ColorPickerPalette palette, float step) {
		return Builder(*this, MakeColorPicker(n, palette, step));
	}
	inline Builder System::Popup(const std::string &n, const std::string &title,
	                             bool closable, bool draggable, bool resizable) {
		return Builder(*this, MakePopup(n, title, closable, draggable, resizable));
	}
	inline Builder System::Tree(const std::string &n) {
		return Builder(*this, MakeTree(n));
	}
	inline Builder System::MenuBar(const std::string &n) {
		return Builder(*this, MakeMenuBar(n));
	}

} // namespace UI
} // namespace SDL
