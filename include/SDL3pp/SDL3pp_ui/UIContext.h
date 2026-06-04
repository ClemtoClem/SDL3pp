#pragma once

#include "UIComponents.h"
#include "UIPipeline.h"
#include "../SDL3pp_ecs.h"
#include "../SDL3pp_error.h"
#include "../SDL3pp_mixer.h"
#include "../SDL3pp_render.h"
#include "../SDL3pp_resources.h"

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

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

namespace SDL::UI {

	template <class T>
	concept is_numeric_value = std::is_arithmetic_v<T> && !std::is_same_v<T, bool>;

	// ── Forward declarations ──────────────────────────────────────────────────────
	struct Builder;
	struct ContainerBuilder;
	struct ListBoxBuilder;
	struct SliderBuilder;
	struct SpinnerBuilder;
	struct SplitterBuilder;
	struct TextAreaBuilder;
	struct LabelBuilder;
	struct ButtonBuilder;
	struct KnobBuilder;
	struct ProgressBuilder;
	struct InputBuilder;
	struct ScrollBarBuilder;
	struct ToggleBuilder;
	struct RadioBuilder;
	struct ImageBuilder;
	struct BadgeBuilder;
	struct CanvasBuilder;
	struct SeparatorBuilder;
	struct GraphBuilder;
	struct ComboBoxBuilder;
	struct TreeBuilder;
	struct ColorPickerBuilder;
	struct PopupBuilder;
	struct TabViewBuilder;
	struct ExpanderBuilder;
	struct MenuBarBuilder;
	struct ItemBuilder;

	class Factory;
	class UIPipeline;

	// ==================================================================================
	// Context — public façade orchestrating all UI sub-systems
	// ==================================================================================

	class Context {
	public:
		struct ResolvedFont {
			std::string key;
			float size   = 0.f;
			bool isDebug = false;
		};

	private:
		ECS::Context&  m_ctx;
		RendererRef    m_renderer;
		MixerRef       m_mixer;
		ResourcePool&  m_pool;

		std::unique_ptr<Factory>    m_factory;
		std::unique_ptr<UIPipeline> m_pipeline;

		ECS::EntityId  m_root = ECS::NullEntity;

		std::string m_defaultFontPath;
		float       m_defaultFontSize = 14.f;
		std::optional<SDL::RendererTextEngine> m_engine;
		bool        m_usedDebugFontPerDefault = false;

		SDL::TextureRef _EnsureTexture(const std::string& key, const std::string& path = "");
		SDL::RendererTextEngine* _EnsureEngine();
		SDL::FontRef _EnsureFont(const std::string& key, float ptsize, const std::string& path = "");
		SDL::TextRef _EnsureText(ECS::EntityId e, SDL::FontRef font, const std::string& text);
		ResolvedFont _ResolveFont(ECS::EntityId e);
		SDL::AudioRef _EnsureAudio(const std::string& key, const std::string& path = "");
		void _PlayAudio(SDL::AudioRef audio);

		ECS::EntityId _FindByTagInTree(ECS::EntityId root, std::string_view tag) const;

	public:
		Context(ECS::Context& ctx, RendererRef r, MixerRef m, ResourcePool& pool);
		~Context();
		Context(const Context&)            = delete;
		Context& operator=(const Context&) = delete;

		// ── Pipeline ─────────────────────────────────────────────────────────
		void ProcessEvent(const SDL::Event& ev);
		void Update(float dt);
		void Render();

		// ── Engine accessors ─────────────────────────────────────────────────
		[[nodiscard]] ECS::Context& GetCtx()      noexcept { return m_ctx; }
		[[nodiscard]] ResourcePool& GetPool()     noexcept { return m_pool; }
		[[nodiscard]] RendererRef   GetRenderer() noexcept { return m_renderer; }
		[[nodiscard]] MixerRef      GetMixer()    noexcept { return m_mixer; }
		[[nodiscard]] Factory&      GetFactory()  noexcept { return *m_factory; }

		void StartTextInput();
		void StopTextInput();

		// ── Default font ─────────────────────────────────────────────────────
		void                              SetDefaultFont(std::string_view path, float ptsize);
		void                              UseDebugFont(float ptsize);
		[[nodiscard]] const std::string&  GetDefaultFontPath() const noexcept;
		[[nodiscard]] float               GetDefaultFontSize() const noexcept;

		// ── Tree management ──────────────────────────────────────────────────
		void                        SetRoot(ECS::EntityId e);
		[[nodiscard]] ECS::EntityId GetRootId() const noexcept { return m_root; }
		void AppendChild(ECS::EntityId parent, ECS::EntityId child);
		void RemoveChild(ECS::EntityId parent, ECS::EntityId child);

		// ── Tag search ───────────────────────────────────────────────────────

		/// @brief Find the first widget tagged @p tag anywhere in the widget tree.
		[[nodiscard]] ECS::EntityId FindByTag(std::string_view tag) const;

		/// @brief Find a widget by tree path — segments are tag values, e.g. "/root/panel/btn".
		///        An "@N" segment matches by ECS entity id (numeric).
		[[nodiscard]] ECS::EntityId FindByPath(std::string_view path) const;

		/// @brief Find by tag, restricted to the subtree rooted at @p root.
		[[nodiscard]] ECS::EntityId FindByTagIn(ECS::EntityId root, std::string_view tag) const;

		// ──────────────────────────────────────────────────────────────────────
		// Factory — entity creation (delegates to Factory)
		// ──────────────────────────────────────────────────────────────────────

		void SetTag(ECS::EntityId e, std::string_view tag);
		ECS::EntityId MakeContainer();
		ECS::EntityId MakeLabel    (std::string_view text = "");
		ECS::EntityId MakeButton   (std::string_view text = "");
		ECS::EntityId MakeToggle   (std::string_view text = "");
		ECS::EntityId MakeRadio    (std::string_view group, std::string_view text = "");

		template <typename T = float>
		ECS::EntityId MakeSlider(NumericValue<T> v = {},
		                         Orientation o = Orientation::Horizontal);

		ECS::EntityId MakeScrollBar(float contentSize = 0.f,
		                            float viewSize = 0.f, float thickness = 10.f,
		                            Orientation o = Orientation::Vertical);

		template <typename T = float>
		ECS::EntityId MakeProgress(NumericValue<T> v = {},
		                           Orientation o = Orientation::Horizontal);

		ECS::EntityId MakeSeparator();
		ECS::EntityId MakeInput    (std::string_view placeholder = "");
		ECS::EntityId MakeInputFiltered(InputFilterType filter, std::string_view placeholder = "");

		template <typename T = float>
		ECS::EntityId MakeInputValue(NumericValue<T> v = {});

		template <typename T = float>
		ECS::EntityId MakeKnob(NumericValue<T> v = {},
		                       KnobShape shape = KnobShape::Arc);

		ECS::EntityId MakeImage(std::string_view key = "",
		                        ImageFit fit = ImageFit::Contain);
		ECS::EntityId MakeCanvas(std::function<void(SDL::Event&)>        eventCb  = nullptr,
		                         std::function<void(float)>              updateCb = nullptr,
		                         std::function<void(RendererRef, FRect)> renderCb = nullptr);

		ECS::EntityId MakeListBox   (const std::vector<std::string>& items = {});
		ECS::EntityId MakeGraph     ();
		ECS::EntityId MakeTextArea  (std::string_view text = "", std::string_view placeholder = "");
		ECS::EntityId MakeComboBox  (const std::vector<std::string>& items = {}, int sel = 0);
		ECS::EntityId MakeTabView   ();
		ECS::EntityId MakeExpander  (std::string_view label = "", bool expanded = false);
		ECS::EntityId MakeSplitter  (Orientation o = Orientation::Horizontal, float ratio = 0.5f);
		ECS::EntityId MakeSpinner   (float speed = 3.f);
		ECS::EntityId MakeBadge     (std::string_view text = "0");
		ECS::EntityId MakeColorPicker(ColorPickerPalette palette = ColorPickerPalette::RGB8, float step = 1.f / 255.f);
		ECS::EntityId MakePopup     (std::string_view title = "", bool closable = true, bool draggable = true, bool resizable = false);
		ECS::EntityId MakeTree      ();
		ECS::EntityId MakeMenuBar   ();

		// ── Factory — Builder-returning (fluent DSL) ──────────────────────────
		ContainerBuilder Container  ();
		LabelBuilder     Label      (std::string_view text = "");
		ButtonBuilder    Button     (std::string_view text = "");
		ToggleBuilder    Toggle     (std::string_view text = "");
		RadioBuilder     Radio      (std::string_view group, std::string_view text = "");

		template <typename T = float>
		SliderBuilder    Slider (NumericValue<T> v = {},
		                             Orientation o = Orientation::Horizontal);

		ScrollBarBuilder ScrollBar (float contentSize = 0.f,
		                             float viewSize = 0.f, Orientation o = Orientation::Vertical);

		template <typename T = float>
		ProgressBuilder  Progress   (NumericValue<T> v = {});

		SeparatorBuilder Separator  ();
		InputBuilder     Input      (std::string_view placeholder = "");

		template <typename T = float>
		InputBuilder     InputValue (NumericValue<T> v = {});

		template <typename T = float>
		KnobBuilder      Knob       (NumericValue<T> v = {},
		                             KnobShape shape = KnobShape::Arc);

		ImageBuilder     Image(std::string_view key = "",
		                             ImageFit fit = ImageFit::Contain);
		CanvasBuilder    Canvas(std::function<void(SDL::Event&)>        eventCb  = nullptr,
		                              std::function<void(float)>              updateCb = nullptr,
		                              std::function<void(RendererRef, FRect)> renderCb = nullptr);

		TextAreaBuilder  TextArea   (std::string_view text = "", std::string_view placeholder = "");
		ListBoxBuilder   ListBoxWidget(const std::vector<std::string>& items = {});
		GraphBuilder     GradedGraph();
		ComboBoxBuilder  ComboBox   (const std::vector<std::string>& items = {}, int sel = 0);
		TabViewBuilder   TabView    ();
		ExpanderBuilder  Expander   (std::string_view label = "", bool expanded = false);
		SplitterBuilder  Splitter   (Orientation o = Orientation::Horizontal, float ratio = 0.5f);
		SpinnerBuilder   Spinner    (float speed = 3.f);
		BadgeBuilder     Badge      (std::string_view text = "0");
		ColorPickerBuilder ColorPicker(ColorPickerPalette palette = ColorPickerPalette::RGB8, float step = 1.f / 255.f);
		PopupBuilder     Popup      (std::string_view title = "", bool closable = true, bool draggable = true, bool resizable = false);
		TreeBuilder      Tree       ();
		MenuBarBuilder   MenuBar    ();

		// Layout shortcuts
		ContainerBuilder Column    (float gap = 4.f, float pad = 8.f, float marg = 0.f);
		ContainerBuilder Row       (float gap = 8.f, float pad = 0.f, float marg = 0.f);
		ContainerBuilder Card      (float gap = 8.f, float marg = 0.f);
		ContainerBuilder Stack     (float gap = 0.f, float pad = 0.f, float marg = 0.f);
		ContainerBuilder ScrollView(float gap = 4.f);
		ContainerBuilder Grid      (int columns = 2, float gap = 4.f, float pad = 8.f);
		Builder          SectionTitle(std::string_view text, SDL::Color color = {70, 130, 210, 255});

		Builder GetBuilder(ECS::EntityId e);

		// ──────────────────────────────────────────────────────────────────────
		// Component accessors
		// ──────────────────────────────────────────────────────────────────────
		[[nodiscard]] LayoutProps&  GetLayout  (ECS::EntityId e);
		[[nodiscard]] SpacingStyle& GetSpacing (ECS::EntityId e);
		[[nodiscard]] TextEdit&     GetContent (ECS::EntityId e);

		[[nodiscard]] MenuBarData*       GetMenuBarData    (ECS::EntityId e);
		[[nodiscard]] ColorPickerState* GetColorPickerData(ECS::EntityId e);
		[[nodiscard]] PopupConfig*      GetPopupData      (ECS::EntityId e);
		[[nodiscard]] TreeData*        GetTreeData       (ECS::EntityId e);
		[[nodiscard]] ListBoxData*     GetListBoxData    (ECS::EntityId e);
		[[nodiscard]] GraphData*       GetGraphData      (ECS::EntityId e);
		[[nodiscard]] TextAreaData*    GetTextAreaData   (ECS::EntityId e);
		[[nodiscard]] TilesetData*     GetTilesetStyle   (ECS::EntityId e);
		IconData& GetOrAddIconData(ECS::EntityId e);

		// ──────────────────────────────────────────────────────────────────────
		// Property setters
		// ──────────────────────────────────────────────────────────────────────

		void SetText     (ECS::EntityId e, std::string_view text);

		template <typename T = float> void SetValue   (ECS::EntityId e, T v);
		template <typename T = float> void SetMinValue(ECS::EntityId e, T v);
		template <typename T = float> void SetMaxValue(ECS::EntityId e, T v);

		void SetChecked      (ECS::EntityId e, bool b);
		void SetEnable       (ECS::EntityId e, bool b);
		void SetVisible      (ECS::EntityId e, bool b);
		void SetHoverable    (ECS::EntityId e, bool b);
		void SetSelectable   (ECS::EntityId e, bool b);
		void SetFocusable    (ECS::EntityId e, bool b);
		void SetDispatchEvent(ECS::EntityId e, bool b);
		void MarkLayoutDirty();

		// ── Scroll ───────────────────────────────────────────────────────────
		void SetScrollableX    (ECS::EntityId e, bool b);
		void SetScrollableY    (ECS::EntityId e, bool b);
		void SetScrollable     (ECS::EntityId e, bool bx, bool by);
		void SetAutoScrollableX(ECS::EntityId e, bool b);
		void SetAutoScrollableY(ECS::EntityId e, bool b);
		void SetAutoScrollable (ECS::EntityId e, bool bx, bool by);
		void SetScrollbarThickness(ECS::EntityId e, float t);
		void SetScrollOffset   (ECS::EntityId e, float off);

		// ── Slider / Knob ─────────────────────────────────────────────────────
		template <typename T = float>
		void SetSliderMarkers(ECS::EntityId e, std::vector<T> markers);

		template <typename T>
		void SetKnobMarkers(ECS::EntityId e, std::vector<T> markers) {
			SetSliderMarkers(e, std::move(markers));
		}

		// ── ListBox ───────────────────────────────────────────────────────────
		void SetListBoxItems      (ECS::EntityId e, std::vector<std::string> items);
		void SetListBoxSelection  (ECS::EntityId e, int idx);
		void SetListBoxReorderable(ECS::EntityId e, bool v);
		void SetListBoxOnReorder  (ECS::EntityId e, std::function<void(int, int)> cb);

		template <typename F>
		void ApplyListBoxItemStyle(ECS::EntityId e, F&& fn) {
			auto* lbd = m_ctx.Get<ListBoxData>(e);
			if (!lbd) return;
			for (auto eid : lbd->itemButtons) {
				if (m_ctx.IsAlive(eid)) fn(eid);
			}
		}

		// ── ComboBox ──────────────────────────────────────────────────────────
		void SetComboBoxItems    (ECS::EntityId e, std::vector<std::string> items);
		void SetComboBoxSelection(ECS::EntityId e, int idx);

		// ── Expander ──────────────────────────────────────────────────────────
		void SetExpanderExpanded(ECS::EntityId e, bool expanded);
		void OnExpanderToggle   (ECS::EntityId e, std::function<void(bool)> cb);

		// ── TabView ───────────────────────────────────────────────────────────
		void AddTabViewTab(ECS::EntityId e, std::string_view label, bool closable = false);
		void SetActiveTab (ECS::EntityId e, int index);

		// ── Tooltip ───────────────────────────────────────────────────────────
		void EnsureTooltip      (ECS::EntityId e, std::string_view text, float delay = 1.f);
		void SetTooltipText     (ECS::EntityId e, std::string_view t);
		void SetTooltipDelay    (ECS::EntityId e, float delay);
		void SetTooltipVisible  (ECS::EntityId e, bool v);
		void RemoveTooltip      (ECS::EntityId e);

		// ── Graph ─────────────────────────────────────────────────────────────
		void SetGraphData  (ECS::EntityId e, std::vector<float> data);
		void SetGraphRange (ECS::EntityId e, float minV, float maxV);
		void SetGraphXRange(ECS::EntityId e, float xMin, float xMax);

		// ── ColorPicker ───────────────────────────────────────────────────────
		void                     SetPickedColor(ECS::EntityId e, SDL::Color c);
		[[nodiscard]] SDL::Color GetPickedColor(ECS::EntityId e) const;

		// ── Popup ─────────────────────────────────────────────────────────────
		void SetPopupOpen        (ECS::EntityId e, bool open);
		void SetPopupTitle       (ECS::EntityId e, std::string_view title);
		void AddPopupHeaderButton(ECS::EntityId e, std::string_view iconKey, std::function<void()> cb);

		// ── Tree ──────────────────────────────────────────────────────────────
		void AddTreeNode    (ECS::EntityId e, const TreeNodeData& node);
		void ClearTreeNodes (ECS::EntityId e);
		[[nodiscard]] int GetTreeSelection(ECS::EntityId e) const;

		// ── MenuBar ───────────────────────────────────────────────────────────
		void AddMenuBarMenu (ECS::EntityId e, MenuBarMenu menu);
		void CloseMenuBar   (ECS::EntityId e);

		// ── Backgrounds & skins ───────────────────────────────────────────────
		void SetBgGradient  (ECS::EntityId e, BgGradient grad);
		void RemoveBgGradient(ECS::EntityId e);
		void SetTileset     (ECS::EntityId e, TilesetData ts);
		void RemoveTileset  (ECS::EntityId e);

		// ── Image ─────────────────────────────────────────────────────────────
		void SetImageKey(ECS::EntityId e, std::string_view key, ImageFit fit = ImageFit::Contain);

		// ── Grid layout ───────────────────────────────────────────────────────
		LayoutGridProps& EnsureGridProps    (ECS::EntityId e);
		void SetGridCols       (ECS::EntityId e, int n);
		void SetGridRows       (ECS::EntityId e, int n);
		void SetGridColSizing  (ECS::EntityId e, GridSizing s);
		void SetGridRowSizing  (ECS::EntityId e, GridSizing s);
		void SetGridLines      (ECS::EntityId e, GridLines l);
		void SetGridLineColor  (ECS::EntityId e, SDL::Color c);
		void SetGridLineThickness(ECS::EntityId e, float t);
		void SetGridCell       (ECS::EntityId e, int col, int row, int colSpan = 1, int rowSpan = 1);

		// ── TextArea ──────────────────────────────────────────────────────────
		void SetTextAreaContent      (ECS::EntityId e, std::string_view text);
		void SetTextAreaHighlightColor(ECS::EntityId e, SDL::Color c);
		void SetTextAreaTabSize      (ECS::EntityId e, int sz);
		void AddTextAreaSpan         (ECS::EntityId e, int start, int end, TextSpanStyle style);
		void ClearTextAreaSpans      (ECS::EntityId e);
		void SetTextAreaReadOnly     (ECS::EntityId e, bool ro);
		[[nodiscard]] const std::string& GetTextAreaContent(ECS::EntityId e) const;
		[[nodiscard]] bool GetTextAreaReadOnly(ECS::EntityId e) const;

		// ── Canvas ────────────────────────────────────────────────────────────
		void OnEventCanvas (ECS::EntityId e, std::function<void(SDL::Event&)> cb);
		void OnUpdateCanvas(ECS::EntityId e, std::function<void(float)> cb);
		void OnRenderCanvas(ECS::EntityId e, std::function<void(RendererRef, FRect)> cb);

		// ──────────────────────────────────────────────────────────────────────
		// Callback registration
		// ──────────────────────────────────────────────────────────────────────
		void OnPress      (ECS::EntityId e, std::function<void(SDL::MouseButton)> cb);
		void OnRelease    (ECS::EntityId e, std::function<void(SDL::MouseButton)> cb);
		void OnClick      (ECS::EntityId e, std::function<void(SDL::MouseButton)> cb);
		void OnDoubleClick(ECS::EntityId e, std::function<void(SDL::MouseButton)> cb);
		void OnMultiClick (ECS::EntityId e, std::function<void(SDL::MouseButton, int)> cb);
		void OnMouseEnter (ECS::EntityId e, std::function<void()> cb);
		void OnMouseLeave (ECS::EntityId e, std::function<void()> cb);
		void OnFocusGain  (ECS::EntityId e, std::function<void()> cb);
		void OnFocusLose  (ECS::EntityId e, std::function<void()> cb);
		void OnDrag       (ECS::EntityId e, std::function<void(SDL::FPoint)> cb);
		void OnTouchFingerEvent(ECS::EntityId e, std::function<void(const SDL::TouchFingerEvent&)> cb);

		template <typename T = float>
		void OnChange     (ECS::EntityId e, std::function<void(T)> cb);
		void OnChange     (ECS::EntityId e, std::function<void(float)> cb);

		void OnColorChange(ECS::EntityId e, std::function<void(SDL::Color)> cb);
		void OnTextChange (ECS::EntityId e, std::function<void(const std::string&)> cb);
		void OnToggle     (ECS::EntityId e, std::function<void(bool)> cb);
		void OnScroll     (ECS::EntityId e, std::function<void(float)> cb);
		void OnScrollChange(ECS::EntityId e, std::function<void(SDL::FPoint, SDL::FPoint)> cb);
		void OnTreeSelect (ECS::EntityId e, std::function<void(int, bool)> cb);

		// ──────────────────────────────────────────────────────────────────────
		// Getters
		// ──────────────────────────────────────────────────────────────────────
		[[nodiscard]] const std::string& GetText(ECS::EntityId e) const;

		template <typename T = float>
		[[nodiscard]] T GetValue(ECS::EntityId e) const;

		[[nodiscard]] int   GetListBoxSelection(ECS::EntityId e) const;
		[[nodiscard]] float GetScrollOffset    (ECS::EntityId e) const;
		[[nodiscard]] FRect GetScreenRect      (ECS::EntityId e) const;

		[[nodiscard]] bool IsChecked   (ECS::EntityId e) const;
		[[nodiscard]] bool IsEnabled   (ECS::EntityId e) const;
		[[nodiscard]] bool IsVisible   (ECS::EntityId e) const;
		[[nodiscard]] bool IsHoverable (ECS::EntityId e) const;
		[[nodiscard]] bool IsSelectable(ECS::EntityId e) const;
		[[nodiscard]] bool IsFocusable (ECS::EntityId e) const;
		[[nodiscard]] bool IsScrollableX(ECS::EntityId e) const;
		[[nodiscard]] bool IsScrollableY(ECS::EntityId e) const;
		[[nodiscard]] bool IsHovered   (ECS::EntityId e) const;
		[[nodiscard]] bool IsFocused   (ECS::EntityId e) const;
		[[nodiscard]] bool IsPressed   (ECS::EntityId e) const;

		void FocusNext();
		void FocusPrev();
		void NextFocus() { FocusNext(); }
		void PrevFocus() { FocusPrev(); }

		// ── Resource loading ──────────────────────────────────────────────────
		void LoadTexture(std::string_view key, std::string_view path);
		void LoadFont   (std::string_view key, std::string_view path);
		void LoadAudio  (std::string_view key, std::string_view path);

		// ── Public accessors for sub-systems ──────────────────────────────────
		[[nodiscard]] SDL::TextRef  EnsureText(ECS::EntityId e, SDL::FontRef font, const std::string& text);
		[[nodiscard]] SDL::FontRef  EnsureFont(const std::string& key, float ptsize, const std::string& path = "");
		[[nodiscard]] ResolvedFont  ResolveFont(ECS::EntityId e);
	};

	// ==================================================================================
	// Implementation: Context
	// ==================================================================================

	inline Context::Context(ECS::Context& ctx, RendererRef r, MixerRef m, ResourcePool& pool)
		: m_ctx(ctx), m_renderer(r), m_mixer(m), m_pool(pool) {
		m_factory  = std::make_unique<Factory>(*this, ctx);
		m_pipeline = std::make_unique<UIPipeline>(*this, ctx, r, m, pool);

		m_pipeline->onTextInputActive = [this](bool start) {
			if (start) StartTextInput();
			else       StopTextInput();
		};
	}

	inline Context::~Context() = default;

	inline void Context::ProcessEvent(const SDL::Event& ev) {
		if (m_pipeline) m_pipeline->FeedEvent(ev);
	}

	inline void Context::Update(float dt) {
		if (m_root == ECS::NullEntity || !m_ctx.IsAlive(m_root)) return;

		FRect viewport = {0, 0, 1024.f, 768.f};
		if (m_renderer) {
			try {
				auto size = SDL::GetRenderOutputSize(m_renderer);
				viewport.w = (float)size.x;
				viewport.h = (float)size.y;
			} catch (...) {}
		}
		if (m_pipeline) m_pipeline->Update(m_root, viewport, dt);
	}

	inline void Context::Render() {
		if (m_root == ECS::NullEntity || !m_ctx.IsAlive(m_root)) return;
		if (m_pipeline) m_pipeline->Render(m_root);
	}

	inline void Context::SetRoot(ECS::EntityId e) {
		m_root = e;
		if (m_pipeline) m_pipeline->MarkLayoutDirty();
	}

	inline void Context::AppendChild(ECS::EntityId parent, ECS::EntityId child) {
		if (m_factory) m_factory->AppendChild(parent, child);
	}

	inline void Context::RemoveChild(ECS::EntityId parent, ECS::EntityId child) {
		if (m_factory) m_factory->RemoveChild(parent, child);
	}

	inline void Context::SetDefaultFont(std::string_view path, float ptsize) {
		m_defaultFontPath = std::string(path);
		m_defaultFontSize = ptsize;
	}

	inline void Context::UseDebugFont(float ptsize) {
		m_defaultFontPath.clear();
		m_defaultFontSize = ptsize;
		m_usedDebugFontPerDefault = true;
	}

	inline const std::string& Context::GetDefaultFontPath() const noexcept { return m_defaultFontPath; }
	inline float              Context::GetDefaultFontSize()  const noexcept { return m_defaultFontSize; }

	// ── Tag search ───────────────────────────────────────────────────────────────

	inline ECS::EntityId Context::_FindByTagInTree(ECS::EntityId root, std::string_view tag) const {
		if (!m_ctx.IsAlive(root)) return ECS::NullEntity;
		if (auto* t = m_ctx.Get<Tag>(root))
			if (t->value == tag) return root;
		if (auto* ch = m_ctx.Get<Children>(root))
			for (auto child : ch->ids) {
				ECS::EntityId found = _FindByTagInTree(child, tag);
				if (found != ECS::NullEntity) return found;
			}
		return ECS::NullEntity;
	}

	inline ECS::EntityId Context::FindByTag(std::string_view tag) const {
		return _FindByTagInTree(m_root, tag);
	}

	inline ECS::EntityId Context::FindByTagIn(ECS::EntityId root, std::string_view tag) const {
		return _FindByTagInTree(root, tag);
	}

	inline ECS::EntityId Context::FindByPath(std::string_view path) const {
		// Path format: "/seg1/seg2/..." where each segment is a tag value.
		// "#N" matches entity id N numerically.
		ECS::EntityId cur = m_root;
		if (path.empty() || path == "/") return cur;
		// Strip leading slash
		size_t pos = (path[0] == '/') ? 1 : 0;
		while (pos < path.size()) {
			size_t slash = path.find('/', pos);
			std::string_view seg = path.substr(pos, slash == std::string_view::npos ? slash : slash - pos);
			if (seg.empty()) { pos = slash + 1; continue; }

			ECS::EntityId next = ECS::NullEntity;
			if (!seg.empty() && seg[0] == '#') {
				// Numeric entity id
				ECS::EntityId eid = static_cast<ECS::EntityId>(std::stoull(std::string(seg.substr(1))));
				if (m_ctx.IsAlive(eid)) next = eid;
			} else {
				// Search children of cur for matching tag
				if (auto* ch = m_ctx.Get<Children>(cur)) {
					for (auto child : ch->ids) {
						if (auto* t = m_ctx.Get<Tag>(child))
							if (t->value == seg) { next = child; break; }
					}
				}
			}
			if (next == ECS::NullEntity) return ECS::NullEntity;
			cur = next;
			if (slash == std::string_view::npos) break;
			pos = slash + 1;
		}
		return cur;
	}

	// ── Factory delegates ────────────────────────────────────────────────────────

	inline void Context::SetTag(ECS::EntityId e, std::string_view tag) { m_factory->SetTag(e, tag); }
	inline ECS::EntityId Context::MakeContainer()       { return m_factory->MakeContainer(); }
	inline ECS::EntityId Context::MakeLabel    (std::string_view text) { return m_factory->MakeLabel(text); }
	inline ECS::EntityId Context::MakeButton   (std::string_view text) { return m_factory->MakeButton(text); }
	inline ECS::EntityId Context::MakeToggle   (std::string_view text) { return m_factory->MakeToggle(text); }
	inline ECS::EntityId Context::MakeRadio    (std::string_view group, std::string_view text) { return m_factory->MakeRadio(group, text); }
	inline ECS::EntityId Context::MakeSeparator()       { return m_factory->MakeSeparator(); }
	inline ECS::EntityId Context::MakeInput    (std::string_view placeholder) { return m_factory->MakeInput(InputFilterType::Text, placeholder); }
	inline ECS::EntityId Context::MakeInputFiltered(InputFilterType filter, std::string_view placeholder) { return m_factory->MakeInputFiltered(filter, placeholder); }
	inline ECS::EntityId Context::MakeScrollBar(float cS, float vS, float t, Orientation o) { return m_factory->MakeScrollBar(cS, vS, t, o); }
	inline ECS::EntityId Context::MakeImage    (std::string_view key, ImageFit fit) { return m_factory->MakeImage(key, fit); }
	inline ECS::EntityId Context::MakeCanvas   (std::function<void(SDL::Event&)> e, std::function<void(float)> u, std::function<void(RendererRef, FRect)> r) { return m_factory->MakeCanvas(std::move(e), std::move(u), std::move(r)); }
	inline ECS::EntityId Context::MakeListBox  (const std::vector<std::string>& items) { return m_factory->MakeListBox(items); }
	inline ECS::EntityId Context::MakeGraph    ()       { return m_factory->MakeGraph(); }
	inline ECS::EntityId Context::MakeTextArea (std::string_view text, std::string_view ph) { return m_factory->MakeTextArea(text, ph); }
	inline ECS::EntityId Context::MakeComboBox (const std::vector<std::string>& items, int sel) { return m_factory->MakeComboBox(items, sel); }
	inline ECS::EntityId Context::MakeTabView  ()       { return m_factory->MakeTabView(); }
	inline ECS::EntityId Context::MakeExpander (std::string_view label, bool expanded) { return m_factory->MakeExpander(label, expanded); }
	inline ECS::EntityId Context::MakeSplitter (Orientation o, float ratio) { return m_factory->MakeSplitter(o, ratio); }
	inline ECS::EntityId Context::MakeSpinner  (float speed) { return m_factory->MakeSpinner(speed); }
	inline ECS::EntityId Context::MakeBadge    (std::string_view text) { return m_factory->MakeBadge(text); }
	inline ECS::EntityId Context::MakeColorPicker(ColorPickerPalette p, float step) { return m_factory->MakeColorPicker(p, step); }
	inline ECS::EntityId Context::MakePopup    (std::string_view title, bool cl, bool dr, bool rs) { return m_factory->MakePopup(title, cl, dr, rs); }
	inline ECS::EntityId Context::MakeTree     ()       { return m_factory->MakeTree(); }
	inline ECS::EntityId Context::MakeMenuBar  ()       { return m_factory->MakeMenuBar(); }

	template <typename T>
	inline ECS::EntityId Context::MakeSlider(NumericValue<T> v, Orientation o) {
		return m_factory->MakeSlider(std::move(v), 12.f, o);
	}
	template <typename T>
	inline ECS::EntityId Context::MakeProgress(NumericValue<T> v, Orientation o) {
		return m_factory->MakeProgress(std::move(v), 12.f, o);
	}
	template <typename T>
	inline ECS::EntityId Context::MakeKnob(NumericValue<T> v, KnobShape shape) {
		return m_factory->MakeKnob(std::move(v), 80.f, shape);
	}
	template <typename T>
	inline ECS::EntityId Context::MakeInputValue(NumericValue<T> v) {
		return m_factory->MakeInputValue(std::move(v));
	}

	// ── Component accessors ──────────────────────────────────────────────────────

	inline LayoutProps& Context::GetLayout(ECS::EntityId e) {
		static LayoutProps fallback;
		if (auto* lp = m_ctx.Get<LayoutProps>(e)) return *lp;
		return fallback;
	}

	inline SpacingStyle& Context::GetSpacing(ECS::EntityId e) {
		static SpacingStyle fallback;
		if (auto* sp = m_ctx.Get<SpacingStyle>(e)) return *sp;
		return fallback;
	}

	inline TextEdit& Context::GetContent(ECS::EntityId e) {
		static TextEdit fallback;
		if (auto* te = m_ctx.Get<TextEdit>(e)) return *te;
		return fallback;
	}

	inline MenuBarData*       Context::GetMenuBarData    (ECS::EntityId e) { return m_ctx.Get<MenuBarData>(e); }
	inline ColorPickerState*  Context::GetColorPickerData(ECS::EntityId e) { return m_ctx.Get<ColorPickerState>(e); }
	inline PopupConfig*       Context::GetPopupData      (ECS::EntityId e) { return m_ctx.Get<PopupConfig>(e); }
	inline TreeData*        Context::GetTreeData       (ECS::EntityId e) { return m_ctx.Get<TreeData>(e); }
	inline ListBoxData*     Context::GetListBoxData    (ECS::EntityId e) { return m_ctx.Get<ListBoxData>(e); }
	inline GraphData*       Context::GetGraphData      (ECS::EntityId e) { return m_ctx.Get<GraphData>(e); }
	inline TextAreaData*    Context::GetTextAreaData   (ECS::EntityId e) { return m_ctx.Get<TextAreaData>(e); }
	inline TilesetData*     Context::GetTilesetStyle   (ECS::EntityId e) { return m_ctx.Get<TilesetData>(e); }

	inline IconData& Context::GetOrAddIconData(ECS::EntityId e) {
		if (auto* d = m_ctx.Get<IconData>(e)) return *d;
		return m_ctx.Add<IconData>(e, IconData{});
	}

	// ── Property setters ─────────────────────────────────────────────────────────

	inline void Context::SetText(ECS::EntityId e, std::string_view text) {
		if (auto* te = m_ctx.Get<TextEdit>(e)) te->text = std::string(text);
	}

	template <typename T>
	inline void Context::SetValue(ECS::EntityId e, T v) {
		if (auto* nv = m_ctx.Get<NumericValue<T>>(e)) nv->Set(v);
	}
	template <typename T>
	inline void Context::SetMinValue(ECS::EntityId e, T v) {
		if (auto* nv = m_ctx.Get<NumericValue<T>>(e)) nv->min = v;
	}
	template <typename T>
	inline void Context::SetMaxValue(ECS::EntityId e, T v) {
		if (auto* nv = m_ctx.Get<NumericValue<T>>(e)) nv->max = v;
	}

	inline void Context::SetChecked(ECS::EntityId e, bool b) {
		if (auto* tog = m_ctx.Get<ToggleState>(e)) tog->checked = b;
		if (auto* rad = m_ctx.Get<RadioState>(e))  rad->checked = b;
	}

	inline void Context::SetEnable(ECS::EntityId e, bool b) {
		if (auto* w = m_ctx.Get<Widget>(e)) {
			if (b) w->behavior |= WidgetBehaviorFlag::Enable;
			else   w->behavior &= ~WidgetBehaviorFlag::Enable;
		}
	}

	inline void Context::SetVisible(ECS::EntityId e, bool b) {
		auto* w = m_ctx.Get<Widget>(e);
		if (!w) return;

		// A widget that animates opacity fades instead of snapping: on show it starts
		// transparent and the AnimationSystem fades it in; on hide it keeps drawing
		// while fading out and is hidden (and dropped from the index) at opacity≈0.
		auto* an = m_ctx.Get<AnimationState>(e);
		bool fade = false;
		if (an) {
			auto* as = m_ctx.Get<AnimationStyle>(e);
			fade = !as || Has(as->channels, AnimChannel::Opacity);
		}

		if (b) {
			bool wasHidden = !Has(w->behavior, WidgetBehaviorFlag::Visible);
			w->behavior |= WidgetBehaviorFlag::Visible;
			if (an) { an->closing = false; if (fade && wasHidden) an->opacity = 0.f; }
			if (m_pipeline) m_pipeline->MarkLayoutDirty();
		} else if (fade && Has(w->behavior, WidgetBehaviorFlag::Visible)) {
			an->closing = true;   // stays Visible until the fade-out completes
		} else {
			w->behavior &= ~WidgetBehaviorFlag::Visible;
			if (m_pipeline) m_pipeline->MarkLayoutDirty();
		}
	}

	inline void Context::SetHoverable(ECS::EntityId e, bool b) {
		if (auto* w = m_ctx.Get<Widget>(e)) {
			if (b) w->behavior |= WidgetBehaviorFlag::Hoverable;
			else   w->behavior &= ~WidgetBehaviorFlag::Hoverable;
		}
	}

	inline void Context::SetSelectable(ECS::EntityId e, bool b) {
		if (auto* w = m_ctx.Get<Widget>(e)) {
			if (b) w->behavior |= WidgetBehaviorFlag::Selectable;
			else   w->behavior &= ~WidgetBehaviorFlag::Selectable;
		}
	}

	inline void Context::SetFocusable(ECS::EntityId e, bool b) {
		if (auto* w = m_ctx.Get<Widget>(e)) {
			if (b) w->behavior |= WidgetBehaviorFlag::Focusable;
			else   w->behavior &= ~WidgetBehaviorFlag::Focusable;
		}
	}

	inline void Context::SetDispatchEvent(ECS::EntityId e, bool b) {
		if (auto* w = m_ctx.Get<Widget>(e)) {
			if (b) w->behavior |= WidgetBehaviorFlag::DispatchEvent;
			else   w->behavior &= ~WidgetBehaviorFlag::DispatchEvent;
		}
	}

	inline void Context::MarkLayoutDirty() { if (m_pipeline) m_pipeline->MarkLayoutDirty(); }

	inline void Context::SetScrollableX(ECS::EntityId e, bool b) {
		if (auto* w = m_ctx.Get<Widget>(e)) {
			if (b) w->behavior |= WidgetBehaviorFlag::ScrollableX;
			else   w->behavior &= ~WidgetBehaviorFlag::ScrollableX;
		}
	}
	inline void Context::SetScrollableY(ECS::EntityId e, bool b) {
		if (auto* w = m_ctx.Get<Widget>(e)) {
			if (b) w->behavior |= WidgetBehaviorFlag::ScrollableY;
			else   w->behavior &= ~WidgetBehaviorFlag::ScrollableY;
		}
	}
	inline void Context::SetScrollable(ECS::EntityId e, bool bx, bool by) { SetScrollableX(e, bx); SetScrollableY(e, by); }

	inline void Context::SetAutoScrollableX(ECS::EntityId e, bool b) {
		if (auto* w = m_ctx.Get<Widget>(e)) {
			if (b) w->behavior |= WidgetBehaviorFlag::AutoScrollableX;
			else   w->behavior &= ~WidgetBehaviorFlag::AutoScrollableX;
		}
	}
	inline void Context::SetAutoScrollableY(ECS::EntityId e, bool b) {
		if (auto* w = m_ctx.Get<Widget>(e)) {
			if (b) w->behavior |= WidgetBehaviorFlag::AutoScrollableY;
			else   w->behavior &= ~WidgetBehaviorFlag::AutoScrollableY;
		}
	}
	inline void Context::SetAutoScrollable(ECS::EntityId e, bool bx, bool by) { SetAutoScrollableX(e, bx); SetAutoScrollableY(e, by); }

	inline void Context::SetScrollbarThickness(ECS::EntityId e, float t) {
		if (auto* ss = m_ctx.Get<ScrollbarStyle>(e)) ss->thickness = t;
		else m_ctx.Add<ScrollbarStyle>(e).thickness = t;
	}

	inline void Context::SetScrollOffset(ECS::EntityId e, float off) {
		if (auto* lp = m_ctx.Get<LayoutProps>(e)) lp->scrollY = off;
	}

	template <typename T>
	inline void Context::SetSliderMarkers(ECS::EntityId e, std::vector<T> markers) {
		if (auto* sc = m_ctx.Get<SliderConfig>(e)) {
			sc->markers.clear();
			for (auto v : markers) sc->markers.push_back((float)v);
		}
	}

	// ── ListBox ──────────────────────────────────────────────────────────────────

	inline void Context::SetListBoxItems(ECS::EntityId e, std::vector<std::string> items) {
		m_factory->RebuildListBoxItems(e, std::move(items));
		if (m_pipeline) m_pipeline->MarkLayoutDirty();
	}

	inline void Context::SetListBoxSelection(ECS::EntityId e, int idx) {
		if (auto* ilv = m_ctx.Get<ItemListView>(e)) ilv->selectedIndex = idx;
	}

	inline void Context::SetListBoxReorderable(ECS::EntityId e, bool v) {
		if (auto* lb = m_ctx.Get<ListBoxData>(e)) lb->reorderable = v;
	}

	inline void Context::SetListBoxOnReorder(ECS::EntityId e, std::function<void(int, int)> cb) {
		if (auto* c = m_ctx.Get<ItemCbs>(e)) c->onItemReorder = std::move(cb);
	}

	// ── ComboBox ─────────────────────────────────────────────────────────────────

	inline void Context::SetComboBoxItems(ECS::EntityId e, std::vector<std::string> items) {
		auto* ilv = m_ctx.Get<ItemListView>(e);
		int sel = ilv ? ilv->selectedIndex : 0;
		m_factory->RebuildComboBoxItems(e, std::move(items), sel);
		if (m_pipeline) m_pipeline->MarkLayoutDirty();
	}

	inline void Context::SetComboBoxSelection(ECS::EntityId e, int idx) {
		auto* ilv = m_ctx.Get<ItemListView>(e);
		if (!ilv) return;
		ilv->selectedIndex = idx;
		// Update toggle button label
		auto* cbd = m_ctx.Get<ComboBoxData>(e);
		if (cbd && cbd->toggleButton != ECS::NullEntity) {
			if (idx >= 0 && idx < (int)ilv->items.size()) {
				if (auto* te = m_ctx.Get<TextEdit>(cbd->toggleButton))
					te->text = ilv->items[idx];
			}
		}
	}

	// ── Expander ─────────────────────────────────────────────────────────────────

	inline void Context::SetExpanderExpanded(ECS::EntityId e, bool expanded) {
		auto* ed = m_ctx.Get<ExpanderData>(e);
		if (!ed) return;
		ed->expanded = expanded;
		ed->animT    = expanded ? 1.f : 0.f;
		if (ed->contentEntity != ECS::NullEntity)
			SetVisible(ed->contentEntity, expanded);
	}

	inline void Context::OnExpanderToggle(ECS::EntityId e, std::function<void(bool)> cb) {
		if (auto* c = m_ctx.Get<ValueCbs>(e)) c->onToggle = std::move(cb);
	}

	// ── TabView ───────────────────────────────────────────────────────────────────

	inline void Context::AddTabViewTab(ECS::EntityId e, std::string_view label, bool closable) {
		m_factory->AddTabViewTab(e, label, closable);
		if (m_pipeline) m_pipeline->MarkLayoutDirty();
	}

	inline void Context::SetActiveTab(ECS::EntityId e, int index) {
		auto* tvd = m_ctx.Get<TabViewData>(e);
		if (!tvd) return;
		for (int i = 0; i < (int)tvd->tabs.size(); ++i) {
			bool active = (i == index);
			if (tvd->tabs[i].tabContent != ECS::NullEntity)
				SetVisible(tvd->tabs[i].tabContent, active);
		}
		tvd->activeTab = index;
		if (tvd->onTabChange) tvd->onTabChange(index);
		if (m_pipeline) m_pipeline->MarkLayoutDirty();
	}

	// ── Tooltip ───────────────────────────────────────────────────────────────────

	inline void Context::EnsureTooltip(ECS::EntityId e, std::string_view text, float delay) {
		m_factory->EnsureTooltip(e, text, delay);
	}

	inline void Context::SetTooltipText(ECS::EntityId e, std::string_view t) {
		if (auto* td = m_ctx.Get<TooltipData>(e)) {
			td->text = std::string(t);
			if (td->labelEntity != ECS::NullEntity)
				if (auto* te = m_ctx.Get<TextEdit>(td->labelEntity))
					te->text = td->text;
		}
	}

	inline void Context::SetTooltipDelay(ECS::EntityId e, float delay) {
		if (auto* td = m_ctx.Get<TooltipData>(e)) td->delay = delay;
	}

	inline void Context::SetTooltipVisible(ECS::EntityId e, bool v) {
		if (auto* td = m_ctx.Get<TooltipData>(e)) {
			td->visible = v;
			if (td->entity != ECS::NullEntity) SetVisible(td->entity, v);
		}
	}

	inline void Context::RemoveTooltip(ECS::EntityId e) {
		if (auto* td = m_ctx.Get<TooltipData>(e)) {
			if (td->entity != ECS::NullEntity && m_ctx.IsAlive(td->entity))
				m_ctx.DestroyEntity(td->entity);
		}
		m_ctx.Remove<TooltipData>(e);
	}

	// ── Graph ─────────────────────────────────────────────────────────────────────

	inline void Context::SetGraphData(ECS::EntityId e, std::vector<float> data) {
		if (auto* gd = m_ctx.Get<GraphData>(e)) gd->data = std::move(data);
	}

	inline void Context::SetGraphRange(ECS::EntityId e, float minV, float maxV) {
		if (auto* gd = m_ctx.Get<GraphData>(e)) { gd->minVal = minV; gd->maxVal = maxV; }
	}

	inline void Context::SetGraphXRange(ECS::EntityId e, float xMin, float xMax) {
		if (auto* gd = m_ctx.Get<GraphData>(e)) { gd->xMin = xMin; gd->xMax = xMax; }
	}

	// ── ColorPicker ───────────────────────────────────────────────────────────────

	inline void Context::SetPickedColor(ECS::EntityId e, SDL::Color c) {
		if (auto* cp = m_ctx.Get<ColorPickerState>(e)) cp->currentColor = c;
	}

	inline SDL::Color Context::GetPickedColor(ECS::EntityId e) const {
		if (auto* cp = m_ctx.Get<ColorPickerState>(e)) return cp->currentColor;
		return {0, 0, 0, 255};
	}

	// ── Popup ─────────────────────────────────────────────────────────────────────

	inline void Context::SetPopupOpen(ECS::EntityId e, bool open) {
		if (auto* ps = m_ctx.Get<PopupState>(e)) ps->open = open;
		if (auto* w = m_ctx.Get<Widget>(e)) {
			w->dirty |= DirtyFlag::All;
			if (open) w->behavior |=  WidgetBehaviorFlag::Visible;
			else      w->behavior &= ~WidgetBehaviorFlag::Visible;
		}
	}

	inline void Context::SetPopupTitle(ECS::EntityId e, std::string_view title) {
		if (auto* pd = m_ctx.Get<PopupConfig>(e)) pd->title = std::string(title);
	}

	inline void Context::AddPopupHeaderButton(ECS::EntityId e, std::string_view iconKey, std::function<void()> cb) {
		if (auto* pd = m_ctx.Get<PopupConfig>(e))
			pd->headerButtons.push_back({std::string(iconKey), std::move(cb)});
	}

	// ── Tree ──────────────────────────────────────────────────────────────────────

	inline void Context::AddTreeNode(ECS::EntityId e, const TreeNodeData& node) {
		auto* td = m_ctx.Get<TreeData>(e);
		if (!td) return;
		int idx = (int)td->nodes.size();
		td->nodes.push_back(node);
		m_factory->AddTreeNodeRow(e, node, idx);
		if (m_pipeline) m_pipeline->MarkLayoutDirty();
	}

	inline void Context::ClearTreeNodes(ECS::EntityId e) {
		if (auto* td = m_ctx.Get<TreeData>(e)) td->nodes.clear();
		m_factory->ClearTreeRows(e);
		if (m_pipeline) m_pipeline->MarkLayoutDirty();
	}

	inline int Context::GetTreeSelection(ECS::EntityId e) const {
		if (auto* td = m_ctx.Get<TreeData>(e)) return td->selectedIndex;
		return -1;
	}

	// ── MenuBar ───────────────────────────────────────────────────────────────────

	inline void Context::AddMenuBarMenu(ECS::EntityId e, MenuBarMenu menu) {
		m_factory->AddMenuBarMenu(e, std::move(menu));
		if (m_pipeline) m_pipeline->MarkLayoutDirty();
	}

	inline void Context::CloseMenuBar(ECS::EntityId e) {
		if (auto* mb = m_ctx.Get<MenuBarData>(e)) {
			mb->openMenu = -1;
			if (mb->overlay != ECS::NullEntity) SetVisible(mb->overlay, false);
		}
	}

	// ── Backgrounds & skins ───────────────────────────────────────────────────────

	inline void Context::SetBgGradient(ECS::EntityId e, BgGradient grad) {
		if (auto* bg = m_ctx.Get<BackgroundStyle>(e))
			bg->gradient = grad;
	}

	inline void Context::RemoveBgGradient(ECS::EntityId e) {
		if (auto* bg = m_ctx.Get<BackgroundStyle>(e)) bg->gradient.reset();
	}

	inline void Context::SetTileset(ECS::EntityId e, TilesetData ts) {
		if (auto* existing = m_ctx.Get<TilesetData>(e)) *existing = ts;
		else m_ctx.Add<TilesetData>(e, ts);
	}

	inline void Context::RemoveTileset(ECS::EntityId e) { m_ctx.Remove<TilesetData>(e); }

	// ── Image ─────────────────────────────────────────────────────────────────────

	inline void Context::SetImageKey(ECS::EntityId e, std::string_view key, ImageFit fit) {
		if (auto* id = m_ctx.Get<ImageData>(e)) { id->key = std::string(key); id->fit = fit; }
		else m_ctx.Add<ImageData>(e, ImageData{std::string(key), fit});
	}

	// ── Grid layout ───────────────────────────────────────────────────────────────

	inline LayoutGridProps& Context::EnsureGridProps(ECS::EntityId e) {
		if (auto* gp = m_ctx.Get<LayoutGridProps>(e)) return *gp;
		return m_ctx.Add<LayoutGridProps>(e);
	}

	inline void Context::SetGridCols        (ECS::EntityId e, int n)       { EnsureGridProps(e).columns  = n; if (m_pipeline) m_pipeline->MarkLayoutDirty(e); }
	inline void Context::SetGridRows        (ECS::EntityId e, int n)       { EnsureGridProps(e).rows     = n; if (m_pipeline) m_pipeline->MarkLayoutDirty(e); }
	inline void Context::SetGridColSizing   (ECS::EntityId e, GridSizing s){ EnsureGridProps(e).colSizing = s; }
	inline void Context::SetGridRowSizing   (ECS::EntityId e, GridSizing s){ EnsureGridProps(e).rowSizing = s; }
	inline void Context::SetGridLines       (ECS::EntityId e, GridLines l) { EnsureGridProps(e).lines     = l; }
	inline void Context::SetGridLineColor   (ECS::EntityId e, SDL::Color c){ EnsureGridProps(e).lineColor = c; }
	inline void Context::SetGridLineThickness(ECS::EntityId e, float t)    { EnsureGridProps(e).lineThickness = t; }

	inline void Context::SetGridCell(ECS::EntityId e, int col, int row, int colSpan, int rowSpan) {
		if (auto* gc = m_ctx.Get<GridCell>(e)) { gc->col = col; gc->row = row; gc->colSpan = colSpan; gc->rowSpan = rowSpan; }
		else m_ctx.Add<GridCell>(e, GridCell{col, row, colSpan, rowSpan});
	}

	// ── TextArea ─────────────────────────────────────────────────────────────────

	inline void Context::SetTextAreaContent(ECS::EntityId e, std::string_view text) {
		if (auto* te = m_ctx.Get<TextEdit>(e)) te->text = std::string(text);
	}
	inline void Context::SetTextAreaHighlightColor([[maybe_unused]] ECS::EntityId e, [[maybe_unused]] SDL::Color c) {}
	inline void Context::SetTextAreaTabSize(ECS::EntityId e, int sz) {
		if (auto* te = m_ctx.Get<TextEdit>(e)) te->tabSize = sz;
	}
	inline void Context::AddTextAreaSpan(ECS::EntityId e, int start, int end, TextSpanStyle style) {
		if (auto* ts = m_ctx.Get<TextSpans>(e)) ts->Add(start, end, style);
	}
	inline void Context::ClearTextAreaSpans(ECS::EntityId e) {
		if (auto* ts = m_ctx.Get<TextSpans>(e)) ts->Clear();
	}
	inline void Context::SetTextAreaReadOnly(ECS::EntityId e, bool ro) {
		if (auto* te = m_ctx.Get<TextEdit>(e)) te->readOnly = ro;
	}
	inline const std::string& Context::GetTextAreaContent(ECS::EntityId e) const {
		static std::string empty;
		if (auto* te = m_ctx.Get<TextEdit>(e)) return te->text;
		return empty;
	}
	inline bool Context::GetTextAreaReadOnly(ECS::EntityId e) const {
		if (auto* te = m_ctx.Get<TextEdit>(e)) return te->readOnly;
		return false;
	}

	// ── Canvas ────────────────────────────────────────────────────────────────────

	inline void Context::OnEventCanvas (ECS::EntityId e, std::function<void(SDL::Event&)> cb) {
		if (auto* cd = m_ctx.Get<CanvasData>(e)) cd->eventCb = std::move(cb);
	}
	inline void Context::OnUpdateCanvas(ECS::EntityId e, std::function<void(float)> cb) {
		if (auto* cd = m_ctx.Get<CanvasData>(e)) cd->updateCb = std::move(cb);
	}
	inline void Context::OnRenderCanvas(ECS::EntityId e, std::function<void(RendererRef, FRect)> cb) {
		if (auto* cd = m_ctx.Get<CanvasData>(e)) cd->renderCb = std::move(cb);
	}

	// ── Callbacks ─────────────────────────────────────────────────────────────────

	inline void Context::OnPress      (ECS::EntityId e, std::function<void(SDL::MouseButton)> cb) { if (auto* c = m_ctx.Get<PointerCbs>(e)) c->onPress       = std::move(cb); }
	inline void Context::OnRelease    (ECS::EntityId e, std::function<void(SDL::MouseButton)> cb) { if (auto* c = m_ctx.Get<PointerCbs>(e)) c->onRelease     = std::move(cb); }
	inline void Context::OnClick      (ECS::EntityId e, std::function<void(SDL::MouseButton)> cb) { if (auto* c = m_ctx.Get<PointerCbs>(e)) c->onClick       = std::move(cb); }
	inline void Context::OnDoubleClick(ECS::EntityId e, std::function<void(SDL::MouseButton)> cb) { if (auto* c = m_ctx.Get<PointerCbs>(e)) c->onDoubleClick = std::move(cb); }
	inline void Context::OnMultiClick (ECS::EntityId e, std::function<void(SDL::MouseButton, int)> cb) { if (auto* c = m_ctx.Get<PointerCbs>(e)) c->onMultiClick  = std::move(cb); }
	inline void Context::OnMouseEnter (ECS::EntityId e, std::function<void()> cb) { if (auto* c = m_ctx.Get<PointerCbs>(e)) c->onMouseEnter = std::move(cb); }
	inline void Context::OnMouseLeave (ECS::EntityId e, std::function<void()> cb) { if (auto* c = m_ctx.Get<PointerCbs>(e)) c->onMouseLeave = std::move(cb); }
	inline void Context::OnFocusGain  (ECS::EntityId e, std::function<void()> cb) { if (auto* c = m_ctx.Get<FocusCbs>(e))   c->onFocusGain  = std::move(cb); }
	inline void Context::OnFocusLose  (ECS::EntityId e, std::function<void()> cb) { if (auto* c = m_ctx.Get<FocusCbs>(e))   c->onFocusLose  = std::move(cb); }
	inline void Context::OnDrag       (ECS::EntityId e, std::function<void(SDL::FPoint)> cb) { if (auto* c = m_ctx.Get<PointerCbs>(e)) c->onDrag = std::move(cb); }
	inline void Context::OnTouchFingerEvent(ECS::EntityId e, std::function<void(const SDL::TouchFingerEvent&)> cb) { if (auto* c = m_ctx.Get<PointerCbs>(e)) c->onTouchFinger = std::move(cb); }

	inline void Context::OnChange(ECS::EntityId e, std::function<void(float)> cb) {
		if (auto* c = m_ctx.Get<ValueCbs>(e)) c->onChange = std::move(cb);
	}

	template <typename T>
	inline void Context::OnChange(ECS::EntityId e, std::function<void(T)> cb) {
		if (auto* c = m_ctx.Get<ValueCbs>(e))
			c->onChange = [cb = std::move(cb)](float v) { cb(static_cast<T>(v)); };
	}

	inline void Context::OnColorChange(ECS::EntityId e, std::function<void(SDL::Color)> cb) {
		if (auto* c = m_ctx.Get<ValueCbs>(e)) c->onColorChange = std::move(cb);
	}

	inline void Context::OnTextChange(ECS::EntityId e, std::function<void(const std::string&)> cb) {
		if (auto* c = m_ctx.Get<ValueCbs>(e)) c->onTextChange = std::move(cb);
	}

	inline void Context::OnToggle(ECS::EntityId e, std::function<void(bool)> cb) {
		if (auto* c = m_ctx.Get<ValueCbs>(e)) c->onToggle = std::move(cb);
	}

	inline void Context::OnScroll(ECS::EntityId e, std::function<void(float)> cb) {
		if (auto* c = m_ctx.Get<ValueCbs>(e)) c->onScroll = std::move(cb);
	}

	inline void Context::OnScrollChange(ECS::EntityId e, std::function<void(SDL::FPoint, SDL::FPoint)> cb) {
		if (auto* c = m_ctx.Get<ValueCbs>(e)) c->onScrollChange = std::move(cb);
	}

	inline void Context::OnTreeSelect(ECS::EntityId e, std::function<void(int, bool)> cb) {
		if (auto* c = m_ctx.Get<ItemCbs>(e)) c->onTreeSelect = std::move(cb);
	}

	// ── Getters ───────────────────────────────────────────────────────────────────

	inline const std::string& Context::GetText(ECS::EntityId e) const {
		static std::string empty;
		if (auto* te = m_ctx.Get<TextEdit>(e)) return te->text;
		return empty;
	}

	inline int   Context::GetListBoxSelection(ECS::EntityId e) const {
		if (auto* ilv = m_ctx.Get<ItemListView>(e)) return ilv->selectedIndex;
		return -1;
	}

	inline float Context::GetScrollOffset(ECS::EntityId e) const {
		if (auto* lp = m_ctx.Get<LayoutProps>(e)) return lp->scrollY;
		return 0.f;
	}

	inline FRect Context::GetScreenRect(ECS::EntityId e) const {
		if (auto* cr = m_ctx.Get<ComputedRect>(e)) return cr->absolute;
		return {};
	}

	inline bool Context::IsChecked   (ECS::EntityId e) const {
		if (auto* t = m_ctx.Get<ToggleState>(e)) return t->checked;
		if (auto* r = m_ctx.Get<RadioState>(e))  return r->checked;
		return false;
	}
	inline bool Context::IsEnabled   (ECS::EntityId e) const { if (auto* w = m_ctx.Get<Widget>(e)) return Has(w->behavior, WidgetBehaviorFlag::Enable);         return false; }
	inline bool Context::IsVisible   (ECS::EntityId e) const { if (auto* w = m_ctx.Get<Widget>(e)) return Has(w->behavior, WidgetBehaviorFlag::Visible);         return false; }
	inline bool Context::IsHoverable (ECS::EntityId e) const { if (auto* w = m_ctx.Get<Widget>(e)) return Has(w->behavior, WidgetBehaviorFlag::Hoverable);       return false; }
	inline bool Context::IsSelectable(ECS::EntityId e) const { if (auto* w = m_ctx.Get<Widget>(e)) return Has(w->behavior, WidgetBehaviorFlag::Selectable);      return false; }
	inline bool Context::IsFocusable (ECS::EntityId e) const { if (auto* w = m_ctx.Get<Widget>(e)) return Has(w->behavior, WidgetBehaviorFlag::Focusable);       return false; }
	inline bool Context::IsScrollableX(ECS::EntityId e) const { if (auto* w = m_ctx.Get<Widget>(e)) return Has(w->behavior, WidgetBehaviorFlag::ScrollableX|WidgetBehaviorFlag::AutoScrollableX); return false; }
	inline bool Context::IsScrollableY(ECS::EntityId e) const { if (auto* w = m_ctx.Get<Widget>(e)) return Has(w->behavior, WidgetBehaviorFlag::ScrollableY|WidgetBehaviorFlag::AutoScrollableY); return false; }

	inline bool Context::IsHovered(ECS::EntityId e) const {
		if (auto* w = m_ctx.Get<Widget>(e)) return Has(w->states, WidgetStateFlag::Hovered);
		return false;
	}
	inline bool Context::IsFocused(ECS::EntityId e) const {
		if (auto* w = m_ctx.Get<Widget>(e)) return Has(w->states, WidgetStateFlag::Focused);
		return false;
	}
	inline bool Context::IsPressed(ECS::EntityId e) const {
		if (auto* w = m_ctx.Get<Widget>(e)) return Has(w->states, WidgetStateFlag::Pressed);
		return false;
	}

	inline void Context::FocusNext() {
		if (m_pipeline) m_pipeline->AdvanceFocus(ECS::NullEntity, true);
	}
	inline void Context::FocusPrev() {
		if (m_pipeline) m_pipeline->AdvanceFocus(ECS::NullEntity, false);
	}

	inline void Context::StartTextInput() {
		if (m_renderer) {
			auto win = m_renderer.GetWindow();
			if (win) win.StartTextInput();
		}
	}
	inline void Context::StopTextInput() {
		if (m_renderer) {
			auto win = m_renderer.GetWindow();
			if (win) win.StopTextInput();
		}
	}

	// ── Texture ───────────────────────────────────────────────────────────────────

	inline SDL::TextureRef Context::_EnsureTexture(const std::string& key, const std::string& path) {
		if (key.empty()) return nullptr;
		auto h = m_pool.Get<SDL::Texture>(key);
		if (h) return *h.get();
		SDL::Texture wrapper;
		try {
			wrapper = SDL::Texture(m_renderer, path.empty() ? key : path);
		} catch (...) {
			SDL::LogWarn(SDL::LOG_CATEGORY_APPLICATION,
			             std::format("UI::Context: cannot open texture '{}'", path.empty() ? key : path));
			return nullptr;
		}
		m_pool.Add<SDL::Texture>(key, std::move(wrapper));
		auto h2 = m_pool.Get<SDL::Texture>(key);
		return h2 ? SDL::TextureRef(*h2.get()) : nullptr;
	}

	// ── Font ──────────────────────────────────────────────────────────────────────

	inline SDL::RendererTextEngine* Context::_EnsureEngine() {
		if (!m_engine.has_value()) {
			try { m_engine.emplace(m_renderer); } catch (...) {
				SDL::LogWarn(SDL::LOG_CATEGORY_APPLICATION, "UI::Context: failed to create RendererTextEngine");
				return nullptr;
			}
		}
		return &m_engine.value();
	}

	inline SDL::FontRef Context::_EnsureFont(const std::string& key, float ptsize, const std::string& path) {
		if (key.empty() || ptsize <= 0.f) return nullptr;
		auto h = m_pool.Get<SDL::Font>(key);
		if (h) {
			if (h->GetSize() != ptsize) h->SetSize(ptsize);
			return *h.get();
		}
		SDL::Font wrapper;
		try {
			wrapper = SDL::Font(path.empty() ? key : path, ptsize);
		} catch (...) {
			SDL::LogWarn(SDL::LOG_CATEGORY_APPLICATION,
			             std::format("UI::Context: cannot open font '{}' at {:.2f}pt", path.empty() ? key : path, ptsize));
			return nullptr;
		}
		m_pool.Add<SDL::Font>(key, std::move(wrapper));
		auto h2 = m_pool.Get<SDL::Font>(key);
		if (h2) {
			if (h2->GetSize() != ptsize) h2->SetSize(ptsize);
			return SDL::FontRef(*h2.get());
		}
		return nullptr;
	}

	inline SDL::TextRef Context::_EnsureText(ECS::EntityId e, SDL::FontRef font, const std::string& text) {
		if (!font || text.empty()) return nullptr;
		auto* engine = _EnsureEngine();
		if (!engine) return nullptr;
		auto* cache = m_ctx.Get<TextCache>(e);
		if (!cache) cache = &m_ctx.Add<TextCache>(e);
		if (cache->text) {
			if (cache->text.GetFont().Get() != font.Get())
				cache->text = engine->CreateText(font, text);
			else
				cache->text.SetString(text);
		} else {
			try {
				cache->text = engine->CreateText(font, text);
			} catch (const std::exception& ex) {
				SDL::LogWarn(SDL::LOG_CATEGORY_APPLICATION,
				             std::format("UI::Context: Failed to create text for entity {}: {}", e, ex.what()));
				return nullptr;
			}
		}
		return SDL::TextRef(cache->text.Get());
	}

	inline Context::ResolvedFont Context::_ResolveFont(ECS::EntityId e) {
		for (ECS::EntityId cur = e; cur != ECS::NullEntity && m_ctx.IsAlive(cur);) {
			auto* ts = m_ctx.Get<TextStyle>(cur);
			if (!ts) break;
			switch (ts->usedFont) {
				case FontType::Debug:
					return {"", ts->fontSize > 0.f ? ts->fontSize : m_defaultFontSize, true};
				case FontType::Default:
					if (!m_defaultFontPath.empty()) return {m_defaultFontPath, m_defaultFontSize, false};
					return {"", m_defaultFontSize, m_usedDebugFontPerDefault};
				case FontType::Self:
					if (!ts->fontKey.empty() && ts->fontSize > 0.f)
						return {ts->fontKey, ts->fontSize, false};
					break;
				case FontType::Root: {
					ECS::EntityId root = cur;
					while (true) {
						auto* p = m_ctx.Get<Parent>(root);
						if (!p || p->id == ECS::NullEntity) break;
						root = p->id;
					}
					if (root != cur) {
						auto* rs = m_ctx.Get<TextStyle>(root);
						if (rs && rs->usedFont == FontType::Self && !rs->fontKey.empty() && rs->fontSize > 0.f)
							return {rs->fontKey, rs->fontSize, false};
					}
					if (!m_defaultFontPath.empty()) return {m_defaultFontPath, m_defaultFontSize, false};
					return {"", m_defaultFontSize, m_usedDebugFontPerDefault};
				}
				case FontType::Inherited: break;
			}
			auto* p = m_ctx.Get<Parent>(cur);
			if (!p || p->id == ECS::NullEntity) break;
			cur = p->id;
		}
		if (!m_defaultFontPath.empty()) return {m_defaultFontPath, m_defaultFontSize, false};
		return {"", m_defaultFontSize, m_usedDebugFontPerDefault};
	}

	// ── Audio ─────────────────────────────────────────────────────────────────────

	inline SDL::AudioRef Context::_EnsureAudio(const std::string& key, const std::string& path) {
		if (key.empty()) return nullptr;
		auto h = m_pool.Get<SDL::Audio>(key);
		if (h) return *h.get();
		SDL::Audio wrapper;
		try {
			wrapper = SDL::Audio(m_mixer, path.empty() ? key : path, false);
		} catch (...) {
			SDL::LogWarn(SDL::LOG_CATEGORY_APPLICATION,
			             std::format("UI::Context: cannot open audio '{}'", path.empty() ? key : path));
			return nullptr;
		}
		m_pool.Add<SDL::Audio>(key, std::move(wrapper));
		auto h2 = m_pool.Get<SDL::Audio>(key);
		return h2 ? SDL::AudioRef(*h2.get()) : nullptr;
	}

	inline void Context::_PlayAudio(SDL::AudioRef audio) {
		if (m_mixer) m_mixer.PlayAudio(audio);
	}

	// ── Resource loading ──────────────────────────────────────────────────────────

	inline void Context::LoadTexture(std::string_view key, std::string_view path) { _EnsureTexture(std::string(key), std::string(path)); }
	inline void Context::LoadFont   (std::string_view key, std::string_view path) { _EnsureFont(std::string(key), m_defaultFontSize, std::string(path)); }
	inline void Context::LoadAudio  (std::string_view key, std::string_view path) { _EnsureAudio(std::string(key), std::string(path)); }

	inline SDL::TextRef Context::EnsureText(ECS::EntityId e, SDL::FontRef font, const std::string& text) { return _EnsureText(e, font, text); }
	inline SDL::FontRef Context::EnsureFont(const std::string& key, float ptsize, const std::string& path) { return _EnsureFont(key, ptsize, path); }
	inline Context::ResolvedFont Context::ResolveFont(ECS::EntityId e) { return _ResolveFont(e); }

	// ==================================================================================
	// Deferred implementations for LayoutSystem::_TextWidth/_TextHeight
	// and RenderSystem::_DrawText
	// (defined here to ensure Context is fully defined)
	// ==================================================================================

	inline float LayoutSystem::_TextWidth(const std::string& text, ECS::EntityId e) {
		if (text.empty()) return 0.f;
		// Handle multi-line: return width of widest line
		float maxW = 0.f;
		size_t start = 0;
		while (true) {
			size_t nl = text.find('\n', start);
			std::string line = text.substr(start, nl == std::string::npos ? nl : nl - start);
			float lw = 0.f;
			auto rf   = m_sys.ResolveFont(e);
			auto font = m_sys.EnsureFont(rf.key, rf.size);
			if (font) {
				int measured_width = 0;
				size_t measured_length = 0;
				font.MeasureString(line, 10000, &measured_width, &measured_length);
				lw = (float)measured_width;
			} else {
				lw = (float)line.size() * rf.size * 0.6f;
			}
			if (lw > maxW) maxW = lw;
			if (nl == std::string::npos) break;
			start = nl + 1;
		}
		return maxW;
	}

	inline float LayoutSystem::_TextHeight(ECS::EntityId e) {
		auto rf   = m_sys.ResolveFont(e);
		auto font = m_sys.EnsureFont(rf.key, rf.size);
		float lineH = font ? (float)font.GetHeight() : rf.size;
		// Count lines in the widget's text
		int lines = 1;
		if (auto* te = m_sys.GetCtx().Get<TextEdit>(e))
			lines = te->LineCount();
		return lineH * (float)lines;
	}

	inline void RenderSystem::_DrawText(ECS::EntityId e, const std::string& text,
	                                      const FRect& rect,
	                                      TextHAlign hAlign, TextVAlign vAlign) {
		if (text.empty()) return;

		auto rf   = m_sys.ResolveFont(e);
		auto font = m_sys.EnsureFont(rf.key, rf.size);
		if (!font) return;

		auto textRef = m_sys.EnsureText(e, font, text);
		if (!textRef) return;

		auto sz = textRef.GetSize();
		FPoint pos = {rect.x, rect.y};

		switch (hAlign) {
			case TextHAlign::Left:   pos.x = rect.x;                        break;
			case TextHAlign::Center: pos.x = rect.x + (rect.w - sz.x)*0.5f; break;
			case TextHAlign::Right:  pos.x = rect.x + rect.w - sz.x;        break;
		}
		switch (vAlign) {
			case TextVAlign::Top:    pos.y = rect.y;                        break;
			case TextVAlign::Center: pos.y = rect.y + (rect.h - sz.y)*0.5f; break;
			case TextVAlign::Bottom: pos.y = rect.y + rect.h - sz.y;        break;
		}
		textRef.DrawRenderer(pos);
	}

} // namespace SDL::UI

// Include builder types after all declarations and implementations
#include "UIBuilder.h"

// Layout shortcuts and remaining builder-returning factory methods
namespace SDL::UI {

	inline ContainerBuilder Context::Column(float gap, float pad, float marg) {
		ECS::EntityId e = MakeContainer();
		auto& sp = GetSpacing(e); sp.gap = gap; sp.padding = SDL::FBox(pad); sp.margin = SDL::FBox(marg);
		return ContainerBuilder{*this, e};
	}

	inline ContainerBuilder Context::Row(float gap, float pad, float marg) {
		ECS::EntityId e = MakeContainer();
		GetLayout(e).layout = Layout::InLine;
		auto& sp = GetSpacing(e); sp.gap = gap; sp.padding = SDL::FBox(pad); sp.margin = SDL::FBox(marg);
		return ContainerBuilder{*this, e};
	}

	inline ContainerBuilder Context::Card(float gap, float marg) {
		ECS::EntityId e = MakeContainer();
		auto& sp = GetSpacing(e); sp.gap = gap; sp.margin = SDL::FBox(marg);
		return ContainerBuilder{*this, e};
	}

	inline ContainerBuilder Context::Stack(float gap, float pad, float marg) {
		ECS::EntityId e = MakeContainer();
		GetLayout(e).layout = Layout::Stack;
		auto& sp = GetSpacing(e); sp.gap = gap; sp.padding = SDL::FBox(pad); sp.margin = SDL::FBox(marg);
		return ContainerBuilder{*this, e};
	}

	inline ContainerBuilder Context::ScrollView(float gap) {
		auto b = Column(gap, 0.f);
		b.SetAutoScrollable(false, true).Padding(0);
		return b;
	}

	inline ContainerBuilder Context::Grid(int columns, float gap, float pad) {
		ECS::EntityId e = MakeContainer();
		GetLayout(e).layout = Layout::InGrid;
		SetGridCols(e, columns);
		auto& sp = GetSpacing(e); sp.gap = gap; sp.padding = SDL::FBox(pad);
		return ContainerBuilder{*this, e};
	}

	inline Builder Context::SectionTitle(std::string_view text, SDL::Color color) {
		ECS::EntityId e = MakeLabel(text);
		if (auto* ts = m_ctx.Get<TextStyle>(e)) ts->color = color;
		return Builder{*this, e};
	}

	template <typename T>
	inline SliderBuilder Context::Slider(NumericValue<T> v, Orientation o) {
		return SliderBuilder{*this, MakeSlider(std::move(v), o)};
	}

	template <typename T>
	inline ProgressBuilder Context::Progress(NumericValue<T> v) {
		return ProgressBuilder{*this, MakeProgress(std::move(v))};
	}

	template <typename T>
	inline KnobBuilder Context::Knob(NumericValue<T> v, KnobShape shape) {
		return KnobBuilder{*this, MakeKnob(std::move(v), shape)};
	}

	template <typename T>
	inline InputBuilder Context::InputValue(NumericValue<T> v) {
		return InputBuilder{*this, MakeInputValue(std::move(v))};
	}

	inline Builder Context::GetBuilder(ECS::EntityId e) {
		return Builder{*this, e};
	}

} // namespace SDL::UI
