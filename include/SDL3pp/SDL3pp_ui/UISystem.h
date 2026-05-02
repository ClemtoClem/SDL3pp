#pragma once

#include "UIComponents.h"
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

	/// @brief Concept matching arithmetic types (excluding @c bool) usable as the
	///        value type of a numeric Slider, Knob, Progress, or Input widget.
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
	struct RadioButtonBuilder;
	struct ImageBuilder;
	struct BadgeBuilder;
	struct CanvasBuilder;
	struct SeparatorBuilder;
	struct ComboBoxBuilder;
	struct TreeBuilder;
	struct ColorPickerBuilder;
	struct PopupBuilder;
	struct TabViewBuilder;
	struct ExpanderBuilder;
	struct MenuBarBuilder;

	class UIFactory;
	class UILayoutSystem;
	class UIEventSystem;
	class UIRenderSystem;

	// ==================================================================================
	// System — public façade orchestrating all UI sub-systems
	// ==================================================================================

	class System {
	public:
		// ── Construction ─────────────────────────────────────────────────────
		System(ECS::Context& ctx, RendererRef r, MixerRef m, ResourcePool& pool);
		~System();

		System(const System&)            = delete;
		System& operator=(const System&) = delete;

		// ── Pipeline ─────────────────────────────────────────────────────────
		void ProcessEvent(const SDL::Event& ev);
		void Iterate(float dt);

		// ── Engine accessors ─────────────────────────────────────────────────
		[[nodiscard]] ECS::Context& GetCtx()  noexcept { return m_ctx; }
		[[nodiscard]] ResourcePool& GetPool() noexcept { return m_pool; }
		[[nodiscard]] RendererRef   GetRenderer() noexcept { return m_renderer; }
		[[nodiscard]] MixerRef      GetMixer()    noexcept { return m_mixer; }

		// ── Default font ─────────────────────────────────────────────────────
		void                              SetDefaultFont(std::string_view path, float ptsize);
		void                              UseDebugFont(float ptsize);
		[[nodiscard]] const std::string&  GetDefaultFontPath() const noexcept;
		[[nodiscard]] float               GetDefaultFontSize() const noexcept;

		// ── Tree management ──────────────────────────────────────────────────
		void                          SetRoot(ECS::EntityId e);
		[[nodiscard]] ECS::EntityId   GetRootId() const noexcept { return m_root; }
		void                          AppendChild(ECS::EntityId parent, ECS::EntityId child);
		void                          RemoveChild(ECS::EntityId parent, ECS::EntityId child);

		// ──────────────────────────────────────────────────────────────────────
		// Factory — direct entity creation (delegates to UIFactory)
		// ──────────────────────────────────────────────────────────────────────
		ECS::EntityId MakeContainer  (std::string_view n = "Container");
		ECS::EntityId MakeLabel      (std::string_view n, std::string_view text = "");
		ECS::EntityId MakeButton     (std::string_view n, std::string_view text = "");
		ECS::EntityId MakeToggle     (std::string_view n, std::string_view text = "");
		ECS::EntityId MakeRadioButton(std::string_view n, std::string_view group, std::string_view text = "");

		template <typename T>
		ECS::EntityId MakeSlider(std::string_view n,
		                         T mn = T(0), T mx = T(1), T v = T(0), T step = T(0),
		                         Orientation o = Orientation::Horizontal);

		ECS::EntityId MakeScrollBar (std::string_view n, float contentSize = 0.f, float viewSize = 0.f,
		                             Orientation o = Orientation::Vertical);
		ECS::EntityId MakeProgress  (std::string_view n, float v = 0.f, float mx = 1.f);
		ECS::EntityId MakeSeparator (std::string_view n = "sep");
		ECS::EntityId MakeInput     (std::string_view n, std::string_view placeholder = "");

		template <typename T>
		ECS::EntityId MakeInputValue(std::string_view n,
		                             T minValue = T(0), T maxValue = T(100),
		                             T value = T(0), T step = T(1));

		ECS::EntityId MakeInputFiltered(std::string_view n, InputType type, std::string_view placeholder = "");
		ECS::EntityId MakeKnob         (std::string_view n, float mn = 0.f, float mx = 1.f, float v = 0.5f,
		                                KnobShape shape = KnobShape::Arc);
		ECS::EntityId MakeImage        (std::string_view n, std::string_view key = "", ImageFit fit = ImageFit::Contain);

		ECS::EntityId MakeCanvas(std::string_view n,
		                         std::function<void(SDL::Event&)>        eventCb  = nullptr,
		                         std::function<void(float)>              updateCb = nullptr,
		                         std::function<void(RendererRef, FRect)> renderCb = nullptr);

		ECS::EntityId MakeListBox    (std::string_view n, const std::vector<std::string>& items = {});
		ECS::EntityId MakeGraph      (std::string_view n);
		ECS::EntityId MakeTextArea   (std::string_view n, std::string_view text = "", std::string_view placeholder = "");
		ECS::EntityId MakeComboBox   (std::string_view n, const std::vector<std::string>& items = {}, int sel = 0);
		ECS::EntityId MakeTabView    (std::string_view n);
		ECS::EntityId MakeExpander   (std::string_view n, std::string_view label = "", bool expanded = false);
		ECS::EntityId MakeSplitter   (std::string_view n, Orientation o = Orientation::Horizontal, float ratio = 0.5f);
		ECS::EntityId MakeSpinner    (std::string_view n, float speed = 3.f);
		ECS::EntityId MakeBadge      (std::string_view n, std::string_view text = "0");
		ECS::EntityId MakeColorPicker(std::string_view n, ColorPickerPalette palette = ColorPickerPalette::RGB8,
		                              float step = 1.f / 255.f);
		ECS::EntityId MakePopup      (std::string_view n, std::string_view title = "",
		                              bool closable = true, bool draggable = true, bool resizable = false);
		ECS::EntityId MakeTree       (std::string_view n);
		ECS::EntityId MakeMenuBar    (std::string_view n = "menubar");

		// ──────────────────────────────────────────────────────────────────────
		// Factory — Builder-returning (DSL fluent API)
		// ──────────────────────────────────────────────────────────────────────
		ContainerBuilder Container  (std::string_view n = "Container");
		LabelBuilder     Label      (std::string_view n, std::string_view text = "");
		ButtonBuilder    Button     (std::string_view n, std::string_view text = "");
		ToggleBuilder    Toggle     (std::string_view n, std::string_view text = "");
		RadioButtonBuilder Radio      (std::string_view n, std::string_view group, std::string_view text = "");

		template <typename T = float>
		SliderBuilder    Slider     (std::string_view n,
		                             T mn = T(0), T mx = T(1), T v = T(0),
		                             Orientation o = Orientation::Horizontal);

		ScrollBarBuilder ScrollBar  (std::string_view n, float contentSize = 0.f, float viewSize = 0.f,
		                             Orientation o = Orientation::Vertical);
		ProgressBuilder  Progress   (std::string_view n, float v = 0.f, float mx = 1.f);
		SeparatorBuilder Separator  (std::string_view n = "sep");
		InputBuilder     Input      (std::string_view n, std::string_view placeholder = "");

		template <typename T = float>
		InputBuilder     InputValue (std::string_view n,
		                             T minValue = T(0), T maxValue = T(100),
		                             T value = T(0), T step = T(1));

		KnobBuilder      Knob       (std::string_view n, float mn = 0.f, float mx = 1.f, float v = 0.5f,
		                             KnobShape shape = KnobShape::Arc);
		ImageBuilder     ImageWidget(std::string_view n, std::string_view key = "", ImageFit fit = ImageFit::Contain);
		CanvasBuilder    CanvasWidget(std::string_view n,
		                             std::function<void(SDL::Event&)>        eventCb  = nullptr,
		                             std::function<void(float)>              updateCb = nullptr,
		                             std::function<void(RendererRef, FRect)> renderCb = nullptr);

		TextAreaBuilder  TextArea   (std::string_view n, std::string_view text = "", std::string_view placeholder = "");
		ListBoxBuilder   ListBoxWidget(std::string_view n, const std::vector<std::string>& items = {});
		Builder          GradedGraph(std::string_view n);
		ComboBoxBuilder  ComboBox   (std::string_view n, const std::vector<std::string>& items = {}, int sel = 0);
		TabViewBuilder   TabView    (std::string_view n);
		ExpanderBuilder  Expander   (std::string_view n, std::string_view label = "", bool expanded = false);
		SplitterBuilder  Splitter   (std::string_view n, Orientation o = Orientation::Horizontal, float ratio = 0.5f);
		SpinnerBuilder   Spinner    (std::string_view n, float speed = 3.f);
		BadgeBuilder     Badge      (std::string_view n, std::string_view text = "0");
		ColorPickerBuilder ColorPicker(std::string_view n, ColorPickerPalette palette = ColorPickerPalette::RGB8,
		                             float step = 1.f / 255.f);
		PopupBuilder     Popup      (std::string_view n, std::string_view title = "",
		                             bool closable = true, bool draggable = true, bool resizable = false);
		TreeBuilder      Tree       (std::string_view n);
		MenuBarBuilder   MenuBar    (std::string_view n = "menubar");

		// Layout shortcuts
		ContainerBuilder Column     (std::string_view n = "col",   float gap = 4.f, float pad = 8.f, float marg = 0.f);
		ContainerBuilder Row        (std::string_view n = "row",   float gap = 8.f, float pad = 0.f, float marg = 0.f);
		ContainerBuilder Card       (std::string_view n = "card",  float gap = 8.f, float marg = 0.f);
		ContainerBuilder Stack      (std::string_view n = "stack", float gap = 0.f, float pad = 0.f, float marg = 0.f);
		ContainerBuilder ScrollView (std::string_view n,           float gap = 4.f);
		ContainerBuilder Grid       (std::string_view n = "grid",  int columns = 2, float gap = 4.f, float pad = 8.f);
		Builder          SectionTitle(std::string_view text, SDL::Color color = {70, 130, 210, 255});

		/// @brief Wrap an existing entity in a Builder for post-creation styling.
		Builder          GetBuilder (ECS::EntityId e);

		// ──────────────────────────────────────────────────────────────────────
		// Component accessors
		// ──────────────────────────────────────────────────────────────────────
		Style&        GetStyle  (ECS::EntityId e);
		LayoutProps&  GetLayout (ECS::EntityId e);
		TextEdit&     GetContent(ECS::EntityId e);

		// Optional component lookups (return nullptr if absent).
		[[nodiscard]] MenuBarData*     GetMenuBarData    (ECS::EntityId e);
		[[nodiscard]] ColorPickerData* GetColorPickerData(ECS::EntityId e);
		[[nodiscard]] PopupData*       GetPopupData      (ECS::EntityId e);
		[[nodiscard]] TreeData*        GetTreeData       (ECS::EntityId e);
		[[nodiscard]] ListBoxData*     GetListBoxData    (ECS::EntityId e);
		[[nodiscard]] GraphData*       GetGraphData      (ECS::EntityId e);
		[[nodiscard]] TextAreaData*    GetTextAreaData   (ECS::EntityId e);
		[[nodiscard]] TilesetStyle*    GetTilesetStyle   (ECS::EntityId e);

		IconData&     GetOrAddIconData(ECS::EntityId e);

		// ──────────────────────────────────────────────────────────────────────
		// Property setters
		// ──────────────────────────────────────────────────────────────────────

		// ── Universal ────────────────────────────────────────────────────────
		void SetText           (ECS::EntityId e, std::string_view text);

		template <typename T = float>
		void SetValue          (ECS::EntityId e, T v);

		void SetChecked        (ECS::EntityId e, bool b);
		void SetEnabled        (ECS::EntityId e, bool b);
		void SetVisible        (ECS::EntityId e, bool b);
		void SetHoverable      (ECS::EntityId e, bool b);
		void SetSelectable     (ECS::EntityId e, bool b);
		void SetFocusable      (ECS::EntityId e, bool b);
		void SetDispatchEvent  (ECS::EntityId e, bool b);
		void MarkLayoutDirty();

		// ── Scroll ───────────────────────────────────────────────────────────
		void SetScrollableX     (ECS::EntityId e, bool b);
		void SetScrollableY     (ECS::EntityId e, bool b);
		void SetScrollable      (ECS::EntityId e, bool bx, bool by);
		void SetAutoScrollableX (ECS::EntityId e, bool b);
		void SetAutoScrollableY (ECS::EntityId e, bool b);
		void SetAutoScrollable  (ECS::EntityId e, bool bx, bool by);
		void SetScrollbarThickness(ECS::EntityId e, float t);
		void SetScrollOffset    (ECS::EntityId e, float off);

		// ── Slider ───────────────────────────────────────────────────────────
		template <typename T = float>
		void SetSliderMarkers  (ECS::EntityId e, std::vector<T> markers);

		// ── ListBox ──────────────────────────────────────────────────────────
		void SetListBoxItems       (ECS::EntityId e, std::vector<std::string> items);
		void SetListBoxSelection   (ECS::EntityId e, int idx);
		void SetListBoxReorderable (ECS::EntityId e, bool v);
		void SetListBoxOnReorder   (ECS::EntityId e, std::function<void(int, int)> cb);

		// ── ComboBox ─────────────────────────────────────────────────────────
		void SetComboBoxItems      (ECS::EntityId e, std::vector<std::string> items);
		void SetComboBoxSelection  (ECS::EntityId e, int idx);

		// ── Expander ─────────────────────────────────────────────────────────
		void SetExpanderExpanded   (ECS::EntityId e, bool expanded);
		void OnExpanderToggle      (ECS::EntityId e, std::function<void(bool)> cb);

		// ── Knob / Slider ────────────────────────────────────────────────────
		template <typename T>
		void SetKnobMarkers        (ECS::EntityId e, std::vector<T> markers) {
			SetSliderMarkers(e, std::move(markers));
		}

		// ── Tooltip ──────────────────────────────────────────────────────────
		void SetTooltip   (ECS::EntityId e, std::string_view text, float delay = 1.f);
		void RemoveTooltip(ECS::EntityId e);

		// ── Graph ────────────────────────────────────────────────────────────
		void SetGraphData  (ECS::EntityId e, std::vector<float> data);
		void SetGraphRange (ECS::EntityId e, float minV, float maxV);
		void SetGraphXRange(ECS::EntityId e, float xMin, float xMax);

		// ── ColorPicker ──────────────────────────────────────────────────────
		void                       SetPickedColor(ECS::EntityId e, SDL::Color c);
		[[nodiscard]] SDL::Color   GetPickedColor(ECS::EntityId e) const;

		// ── Popup ────────────────────────────────────────────────────────────
		void SetPopupOpen        (ECS::EntityId e, bool open);
		void SetPopupTitle       (ECS::EntityId e, std::string_view title);
		void AddPopupHeaderButton(ECS::EntityId e, std::string_view iconKey, std::function<void()> cb);

		// ── Tree ─────────────────────────────────────────────────────────────
		void                  AddTreeNode    (ECS::EntityId e, const TreeNodeData& node);
		void                  ClearTreeNodes (ECS::EntityId e);
		[[nodiscard]] int     GetTreeSelection(ECS::EntityId e) const;

		// ── Backgrounds & skins ──────────────────────────────────────────────
		void SetBgGradient    (ECS::EntityId e, BgGradient grad);
		void RemoveBgGradient (ECS::EntityId e);
		void SetTilesetStyle  (ECS::EntityId e, TilesetStyle ts);
		void RemoveTilesetStyle(ECS::EntityId e);

		// ── Image ────────────────────────────────────────────────────────────
		void SetImageKey(ECS::EntityId e, std::string_view key, ImageFit fit = ImageFit::Contain);

		// ── Grid layout ──────────────────────────────────────────────────────
		LayoutGridProps& EnsureGridProps    (ECS::EntityId e);
		void             SetGridCols        (ECS::EntityId e, int n);
		void             SetGridRows        (ECS::EntityId e, int n);
		void             SetGridColSizing   (ECS::EntityId e, GridSizing s);
		void             SetGridRowSizing   (ECS::EntityId e, GridSizing s);
		void             SetGridLines       (ECS::EntityId e, GridLines l);
		void             SetGridLineColor   (ECS::EntityId e, SDL::Color c);
		void             SetGridLineThickness(ECS::EntityId e, float t);
		void             SetGridCell        (ECS::EntityId e, int col, int row, int colSpan = 1, int rowSpan = 1);

		// ── TextArea ─────────────────────────────────────────────────────────
		void                      SetTextAreaContent       (ECS::EntityId e, std::string_view text);
		void                      SetTextAreaHighlightColor(ECS::EntityId e, SDL::Color c);
		void                      SetTextAreaTabSize       (ECS::EntityId e, int sz);
		void                      AddTextAreaSpan          (ECS::EntityId e, int start, int end, TextSpanStyle style);
		void                      ClearTextAreaSpans       (ECS::EntityId e);
		void                      SetTextAreaReadOnly      (ECS::EntityId e, bool ro);
		[[nodiscard]] const std::string& GetTextAreaContent(ECS::EntityId e) const;
		[[nodiscard]] bool        GetTextAreaReadOnly      (ECS::EntityId e) const;

		// ── Canvas ───────────────────────────────────────────────────────────
		void OnEventCanvas (ECS::EntityId e, std::function<void(SDL::Event&)> cb);
		void OnUpdateCanvas(ECS::EntityId e, std::function<void(float)> cb);
		void OnRenderCanvas(ECS::EntityId e, std::function<void(RendererRef, FRect)> cb);

		// ── MenuBar ──────────────────────────────────────────────────────────
		void CloseMenuBar(ECS::EntityId e);

		// ──────────────────────────────────────────────────────────────────────
		// Callback registration
		// ──────────────────────────────────────────────────────────────────────
		void OnPress       (ECS::EntityId e, std::function<void(SDL::MouseButton)> cb);
		void OnRelease     (ECS::EntityId e, std::function<void(SDL::MouseButton)> cb);
		void OnClick       (ECS::EntityId e, std::function<void(SDL::MouseButton)> cb);
		void OnDoubleClick (ECS::EntityId e, std::function<void(SDL::MouseButton)> cb);
		void OnMultiClick  (ECS::EntityId e, std::function<void(SDL::MouseButton, int)> cb);
		void OnMouseEnter  (ECS::EntityId e, std::function<void()> cb);
		void OnMouseLeave  (ECS::EntityId e, std::function<void()> cb);
		void OnFocusGain   (ECS::EntityId e, std::function<void()> cb);
		void OnFocusLose  (ECS::EntityId e, std::function<void()> cb);

		template <typename T = float>
		void OnChange      (ECS::EntityId e, std::function<void(T)> cb);

		void OnColorChange (ECS::EntityId e, std::function<void(SDL::Color)> cb);
		void OnTextChange  (ECS::EntityId e, std::function<void(const std::string&)> cb);
		void OnToggle      (ECS::EntityId e, std::function<void(bool)> cb);
		void OnScroll      (ECS::EntityId e, std::function<void(float)> cb);
		void OnScrollChange(ECS::EntityId e, std::function<void(SDL::FPoint, SDL::FPoint)> cb);
		void OnTreeSelect  (ECS::EntityId e, std::function<void(int, bool)> cb);

		// ──────────────────────────────────────────────────────────────────────
		// Getters & queries
		// ──────────────────────────────────────────────────────────────────────
		[[nodiscard]] const std::string& GetText            (ECS::EntityId e) const;

		template <typename T = float>
		[[nodiscard]] T                  GetValue           (ECS::EntityId e) const;

		[[nodiscard]] int                GetListBoxSelection(ECS::EntityId e) const;
		[[nodiscard]] float              GetScrollOffset    (ECS::EntityId e) const;
		[[nodiscard]] FRect              GetScreenRect      (ECS::EntityId e) const;

		[[nodiscard]] bool IsChecked    (ECS::EntityId e) const;
		[[nodiscard]] bool IsEnabled    (ECS::EntityId e) const;
		[[nodiscard]] bool IsVisible    (ECS::EntityId e) const;
		[[nodiscard]] bool IsHoverable  (ECS::EntityId e) const;
		[[nodiscard]] bool IsSelectable (ECS::EntityId e) const;
		[[nodiscard]] bool IsFocusable  (ECS::EntityId e) const;
		[[nodiscard]] bool IsScrollableX(ECS::EntityId e) const;
		[[nodiscard]] bool IsScrollableY(ECS::EntityId e) const;
		[[nodiscard]] bool IsHovered    (ECS::EntityId e) const;
		[[nodiscard]] bool IsFocused    (ECS::EntityId e) const;
		[[nodiscard]] bool IsPressed    (ECS::EntityId e) const;

		// ──────────────────────────────────────────────────────────────────────
		// ──────────────────────────────────────────────────────────────────────
		// Resource pre-loading
		// ──────────────────────────────────────────────────────────────────────
		void LoadTexture(std::string_view key, std::string_view path);
		void LoadFont   (std::string_view key, std::string_view path);
		void LoadAudio  (std::string_view key, std::string_view path);

		// ──────────────────────────────────────────────────────────────────────
		// Resource accessors (public for sub-systems)
		// ──────────────────────────────────────────────────────────────────────
		struct ResolvedFont {
			std::string key;
			float size   = 0.f;
			bool isDebug = false;
		};

		[[nodiscard]] SDL::TextRef  EnsureText(ECS::EntityId e, SDL::FontRef font, const std::string& text);
		[[nodiscard]] SDL::FontRef  EnsureFont(const std::string& key, float ptsize, const std::string& path = "");
		[[nodiscard]] ResolvedFont  ResolveFont(ECS::EntityId e);

	private:
		ECS::Context&  m_ctx;
		RendererRef    m_renderer;
		MixerRef       m_mixer;
		ResourcePool&  m_pool;

		// Owned sub-systems (stored as unique_ptr to keep this header lightweight —
		// callers don't need to see their full definitions to instantiate System).
		std::unique_ptr<UIFactory>       m_factory;
		std::unique_ptr<UILayoutSystem>  m_layout;
		std::unique_ptr<UIEventSystem>   m_events;
		std::unique_ptr<UIRenderSystem>  m_render;

		ECS::EntityId  m_root = ECS::NullEntity;

		std::string m_defaultFontPath;
		float       m_defaultFontSize = 14.f;
		std::optional<SDL::RendererTextEngine> m_engine;
		bool        m_usedDebugFontPerDefault = false;

		// ── Texture ──────────────────────────────────────────────────────────────────
		SDL::TextureRef _EnsureTexture(const std::string &key, const std::string &path = "");
		// ── Font ──────────────────────────────────────────────────────────────────────
		SDL::RendererTextEngine *_EnsureEngine();
		SDL::FontRef _EnsureFont(const std::string &key, float ptsize, const std::string& path = "");
		SDL::TextRef _EnsureText(ECS::EntityId e, SDL::FontRef font, const std::string& text);
		ResolvedFont _ResolveFont(ECS::EntityId e);
		// ── Audio ─────────────────────────────────────────────────────────────────────
		SDL::AudioRef _EnsureAudio(const std::string& key, const std::string& path = "");
		void _PlayAudio(SDL::AudioRef audio);
	};

	// ==================================================================================
	// Implementation: System (inline methods)
	// ==================================================================================

	inline System::System(ECS::Context& ctx, RendererRef r, MixerRef m, ResourcePool& pool)
		: m_ctx(ctx), m_renderer(r), m_mixer(m), m_pool(pool) {
		m_factory = std::make_unique<UIFactory>(*this, ctx);
		m_layout  = std::make_unique<UILayoutSystem>(ctx, *this);
		m_events  = std::make_unique<UIEventSystem>(ctx, *m_layout, *this);
		m_render  = std::make_unique<UIRenderSystem>(ctx, r, pool, *m_layout, *this);
	}

	inline System::~System() = default;

	inline void System::ProcessEvent(const SDL::Event& ev) {
		if (m_events) m_events->Feed(ev);
	}

	inline void System::Iterate(float dt) {
		if (m_root == ECS::NullEntity || !m_ctx.IsAlive(m_root)) return;

		if (m_events) m_events->Process(m_root);
		// Use renderer size if available, else default to 1024×768
		FRect viewport = {0, 0, 1024.f, 768.f};
		if (m_renderer) {
			try {
				auto size = SDL::GetRenderOutputSize(m_renderer);
				viewport.w = (float)size.x;
				viewport.h = (float)size.y;
			} catch (...) {
				// Use default viewport on error
			}
		}
		if (m_layout) m_layout->Process(m_root, viewport);
		if (m_render) m_render->Process(m_root, ECS::NullEntity);
		if (m_render) m_render->ProcessAnimate(dt, m_root);
	}

	inline void System::SetRoot(ECS::EntityId e) {
		m_root = e;
		if (m_layout) m_layout->MarkDirty();
	}

	inline void System::AppendChild(ECS::EntityId parent, ECS::EntityId child) {
		if (m_factory) m_factory->AppendChild(parent, child);
	}

	inline void System::RemoveChild(ECS::EntityId parent, ECS::EntityId child) {
		if (m_factory) m_factory->RemoveChild(parent, child);
	}

	inline void System::SetDefaultFont(std::string_view path, float ptsize) {
		m_defaultFontPath = std::string(path);
		m_defaultFontSize = ptsize;
	}

	inline void System::UseDebugFont(float ptsize) {
		m_defaultFontPath.clear();
		m_defaultFontSize = ptsize;
	}

	inline const std::string& System::GetDefaultFontPath() const noexcept {
		return m_defaultFontPath;
	}

	inline float System::GetDefaultFontSize() const noexcept {
		return m_defaultFontSize;
	}

	// ── Factory direct methods ───────────────────────────────────────────────────────
	inline ECS::EntityId System::MakeContainer(std::string_view n) {
		return m_factory->MakeContainer(n);
	}

	inline ECS::EntityId System::MakeLabel(std::string_view n, std::string_view text) {
		ECS::EntityId e = m_factory->MakeLabel(n, text);
		return e;
	}

	inline ECS::EntityId System::MakeButton(std::string_view n, std::string_view text) {
		return m_factory->MakeButton(n, text);
	}

	inline ECS::EntityId System::MakeToggle(std::string_view n, std::string_view text) {
		return m_factory->MakeToggle(n, text);
	}

	inline ECS::EntityId System::MakeRadioButton(std::string_view n, std::string_view group, std::string_view text) {
		return m_factory->MakeRadioButton(n, group, text);
	}

	inline ECS::EntityId System::MakeSeparator(std::string_view n) {
		return m_factory->MakeSeparator(n);
	}

	inline ECS::EntityId System::MakeInput(std::string_view n, std::string_view placeholder) {
		return m_factory->MakeInput(n, placeholder);
	}

	inline ECS::EntityId System::MakeInputFiltered(std::string_view n, InputType type, std::string_view placeholder) {
		return m_factory->MakeInputFiltered(n, type, placeholder);
	}

	inline ECS::EntityId System::MakeScrollBar(std::string_view n, float contentSize, float viewSize, Orientation o) {
		return m_factory->MakeScrollBar(n, contentSize, viewSize, o);
	}

	inline ECS::EntityId System::MakeImage(std::string_view n, std::string_view key, ImageFit fit) {
		return m_factory->MakeImage(n, key, fit);
	}

	inline ECS::EntityId System::MakeCanvas(std::string_view n,
	                                         std::function<void(SDL::Event&)> eventCb,
	                                         std::function<void(float)> updateCb,
	                                         std::function<void(RendererRef, FRect)> renderCb) {
		return m_factory->MakeCanvas(n, std::move(eventCb), std::move(updateCb), std::move(renderCb));
	}

	inline ECS::EntityId System::MakeListBox(std::string_view n, const std::vector<std::string>& items) {
		return m_factory->MakeListBox(n, items);
	}

	inline ECS::EntityId System::MakeGraph(std::string_view n) {
		return m_factory->MakeGraph(n);
	}

	inline ECS::EntityId System::MakeTextArea(std::string_view n, std::string_view text, std::string_view placeholder) {
		return m_factory->MakeTextArea(n, text, placeholder);
	}

	inline ECS::EntityId System::MakeComboBox(std::string_view n, const std::vector<std::string>& items, int sel) {
		return m_factory->MakeComboBox(n, items, sel);
	}

	inline ECS::EntityId System::MakeTabView(std::string_view n) {
		return m_factory->MakeTabView(n);
	}

	inline ECS::EntityId System::MakeExpander(std::string_view n, std::string_view label, bool expanded) {
		return m_factory->MakeExpander(n, label, expanded);
	}

	inline ECS::EntityId System::MakeSplitter(std::string_view n, Orientation o, float ratio) {
		return m_factory->MakeSplitter(n, o, ratio);
	}

	inline ECS::EntityId System::MakeSpinner(std::string_view n, float speed) {
		return m_factory->MakeSpinner(n, speed);
	}

	inline ECS::EntityId System::MakeBadge(std::string_view n, std::string_view text) {
		return m_factory->MakeBadge(n, text);
	}

	inline ECS::EntityId System::MakeProgress(std::string_view n, float v, float mx) {
		return m_factory->MakeProgress(n, v, mx);
	}

	inline ECS::EntityId System::MakeColorPicker(std::string_view n, ColorPickerPalette palette, float step) {
		return m_factory->MakeColorPicker(n, palette, step);
	}

	inline ECS::EntityId System::MakePopup(std::string_view n, std::string_view title, bool closable, bool draggable, bool resizable) {
		return m_factory->MakePopup(n, title, closable, draggable, resizable);
	}

	inline ECS::EntityId System::MakeTree(std::string_view n) {
		return m_factory->MakeTree(n);
	}

	inline ECS::EntityId System::MakeMenuBar(std::string_view n) {
		return m_factory->MakeMenuBar(n);
	}

	// ── Component accessors ──────────────────────────────────────────────────────────
	inline Style& System::GetStyle(ECS::EntityId e) {
		static Style fallback;
		if (auto *s = m_ctx.Get<Style>(e)) return *s;
		return fallback;
	}

	inline LayoutProps& System::GetLayout(ECS::EntityId e) {
		static LayoutProps fallback;
		if (auto *lp = m_ctx.Get<LayoutProps>(e)) return *lp;
		return fallback;
	}

	inline TextEdit& System::GetContent(ECS::EntityId e) {
		static TextEdit fallback;
		if (auto *te = m_ctx.Get<TextEdit>(e)) return *te;
		return fallback;
	}

	inline MenuBarData* System::GetMenuBarData(ECS::EntityId e) {
		return m_ctx.Get<MenuBarData>(e);
	}

	inline ColorPickerData* System::GetColorPickerData(ECS::EntityId e) {
		return m_ctx.Get<ColorPickerData>(e);
	}

	inline PopupData* System::GetPopupData(ECS::EntityId e) {
		return m_ctx.Get<PopupData>(e);
	}

	inline TreeData* System::GetTreeData(ECS::EntityId e) {
		return m_ctx.Get<TreeData>(e);
	}

	inline ListBoxData* System::GetListBoxData(ECS::EntityId e) {
		return m_ctx.Get<ListBoxData>(e);
	}

	inline GraphData* System::GetGraphData(ECS::EntityId e) {
		return m_ctx.Get<GraphData>(e);
	}

	inline TextAreaData* System::GetTextAreaData(ECS::EntityId e) {
		return m_ctx.Get<TextAreaData>(e);
	}

	inline TilesetStyle* System::GetTilesetStyle(ECS::EntityId e) {
		return m_ctx.Get<TilesetStyle>(e);
	}

	inline IconData& System::GetOrAddIconData(ECS::EntityId e) {
		if (auto *id = m_ctx.Get<IconData>(e)) return *id;
		m_ctx.Add<IconData>(e, IconData{});
		return *m_ctx.Get<IconData>(e);
	}

	// ── Property setters ─────────────────────────────────────────────────────────────
	inline void System::SetText(ECS::EntityId e, std::string_view text) {
		if (auto *te = m_ctx.Get<TextEdit>(e)) {
			te->text = std::string(text);
		}
	}

	inline void System::SetChecked(ECS::EntityId e, bool b) {
		if (auto *tog = m_ctx.Get<ToggleData>(e)) tog->checked = b;
		if (auto *rad = m_ctx.Get<RadioData>(e)) rad->checked = b;
	}

	inline void System::SetEnabled(ECS::EntityId e, bool b) {
		if (auto *w = m_ctx.Get<Widget>(e)) {
			if (b) w->behavior |= BehaviorFlag::Enable;
			else w->behavior &= ~BehaviorFlag::Enable;
		}
	}

	inline void System::SetVisible(ECS::EntityId e, bool b) {
		if (auto *w = m_ctx.Get<Widget>(e)) {
			if (b) w->behavior |= BehaviorFlag::Visible;
			else w->behavior &= ~BehaviorFlag::Visible;
			if (m_layout) m_layout->MarkDirty();
		}
	}

	inline void System::SetHoverable(ECS::EntityId e, bool b) {
		if (auto *w = m_ctx.Get<Widget>(e)) {
			if (b) w->behavior |= BehaviorFlag::Hoverable;
			else w->behavior &= ~BehaviorFlag::Hoverable;
		}
	}

	inline void System::SetSelectable(ECS::EntityId e, bool b) {
		if (auto *w = m_ctx.Get<Widget>(e)) {
			if (b) w->behavior |= BehaviorFlag::Selectable;
			else w->behavior &= ~BehaviorFlag::Selectable;
		}
	}

	inline void System::SetFocusable(ECS::EntityId e, bool b) {
		if (auto *w = m_ctx.Get<Widget>(e)) {
			if (b) w->behavior |= BehaviorFlag::Focusable;
			else w->behavior &= ~BehaviorFlag::Focusable;
		}
	}

	inline void System::SetDispatchEvent(ECS::EntityId e, bool b) {
		if (auto *w = m_ctx.Get<Widget>(e)) {
			if (b) w->behavior |= BehaviorFlag::PropagateEvent;
			else w->behavior &= ~BehaviorFlag::PropagateEvent;
		}
	}

	inline void System::MarkLayoutDirty() {
		if (m_layout) m_layout->MarkDirty();
	}

	inline void System::SetScrollableX(ECS::EntityId e, bool b) {
		if (auto *w = m_ctx.Get<Widget>(e)) {
			if (b) w->behavior |= BehaviorFlag::ScrollableX;
			else w->behavior &= ~BehaviorFlag::ScrollableX;
		}
	}

	inline void System::SetScrollableY(ECS::EntityId e, bool b) {
		if (auto *w = m_ctx.Get<Widget>(e)) {
			if (b) w->behavior |= BehaviorFlag::ScrollableY;
			else w->behavior &= ~BehaviorFlag::ScrollableY;
		}
	}

	inline void System::SetScrollable(ECS::EntityId e, bool bx, bool by) {
		SetScrollableX(e, bx);
		SetScrollableY(e, by);
	}

	inline void System::SetAutoScrollableX(ECS::EntityId e, bool b) {
		if (auto *w = m_ctx.Get<Widget>(e)) {
			if (b) w->behavior |= BehaviorFlag::AutoScrollableX;
			else w->behavior &= ~BehaviorFlag::AutoScrollableX;
		}
	}

	inline void System::SetAutoScrollableY(ECS::EntityId e, bool b) {
		if (auto *w = m_ctx.Get<Widget>(e)) {
			if (b) w->behavior |= BehaviorFlag::AutoScrollableY;
			else w->behavior &= ~BehaviorFlag::AutoScrollableY;
		}
	}

	inline void System::SetAutoScrollable(ECS::EntityId e, bool bx, bool by) {
		SetAutoScrollableX(e, bx);
		SetAutoScrollableY(e, by);
	}

	inline void System::SetScrollbarThickness(ECS::EntityId e, float t) {
		if (auto *lp = m_ctx.Get<LayoutProps>(e)) {
			lp->scrollbarThickness = t;
		}
	}

	inline void System::SetScrollOffset(ECS::EntityId e, float off) {
		if (auto *lp = m_ctx.Get<LayoutProps>(e)) {
			lp->scrollY = off;
		}
	}

	inline void System::SetListBoxItems(ECS::EntityId e, std::vector<std::string> items) {
		if (auto *ilv = m_ctx.Get<ItemListView>(e)) {
			ilv->items = std::move(items);
			if (m_layout) m_layout->MarkDirty(e);
		}
	}

	inline void System::SetListBoxSelection(ECS::EntityId e, int idx) {
		if (auto *ilv = m_ctx.Get<ItemListView>(e)) {
			ilv->selectedIndex = idx;
		}
	}

	inline void System::SetListBoxReorderable(ECS::EntityId e, bool v) {
		if (auto *lb = m_ctx.Get<ListBoxData>(e)) {
			lb->reorderable = v;
		}
	}

	inline void System::SetListBoxOnReorder(ECS::EntityId e, std::function<void(int, int)> cb) {
		if (auto *lb = m_ctx.Get<ListBoxData>(e)) {
			lb->dragActive = false;  // Reset drag state on callback assignment
		}
	}

	inline void System::SetComboBoxItems(ECS::EntityId e, std::vector<std::string> items) {
		if (auto *ilv = m_ctx.Get<ItemListView>(e)) {
			ilv->items = std::move(items);
		}
	}

	inline void System::SetComboBoxSelection(ECS::EntityId e, int idx) {
		if (auto *ilv = m_ctx.Get<ItemListView>(e)) {
			ilv->selectedIndex = idx;
		}
	}

	inline void System::SetExpanderExpanded(ECS::EntityId e, bool expanded) {
		if (auto *ed = m_ctx.Get<ExpanderData>(e)) {
			ed->expanded = expanded;
			ed->animT = expanded ? 1.f : 0.f;
		}
	}

	inline void System::OnExpanderToggle(ECS::EntityId e, std::function<void(bool)> cb) {
		if (auto *c = m_ctx.Get<Callbacks>(e)) {
			c->onToggle = std::move(cb);
		}
	}

	inline void System::SetTooltip(ECS::EntityId e, std::string_view text, float delay) {
		if (auto *ttd = m_ctx.Get<TooltipData>(e)) {
			ttd->text = std::string(text);
			ttd->delay = delay;
		}
	}

	inline void System::RemoveTooltip(ECS::EntityId e) {
		m_ctx.Remove<TooltipData>(e);
	}

	inline void System::SetGraphData(ECS::EntityId e, std::vector<float> data) {
		if (auto *gd = m_ctx.Get<GraphData>(e)) {
			gd->data = std::move(data);
		}
	}

	inline void System::SetGraphRange(ECS::EntityId e, float minV, float maxV) {
		if (auto *gd = m_ctx.Get<GraphData>(e)) {
			gd->minVal = minV;
			gd->maxVal = maxV;
		}
	}

	inline void System::SetGraphXRange(ECS::EntityId e, float xMin, float xMax) {
		if (auto *gd = m_ctx.Get<GraphData>(e)) {
			gd->xMin = xMin;
			gd->xMax = xMax;
		}
	}

	inline void System::SetPickedColor(ECS::EntityId e, SDL::Color c) {
		if (auto *cp = m_ctx.Get<ColorPickerData>(e)) {
			cp->currentColor = c;
		}
	}

	inline SDL::Color System::GetPickedColor(ECS::EntityId e) const {
		if (auto *cp = m_ctx.Get<ColorPickerData>(e)) {
			return cp->currentColor;
		}
		return {0, 0, 0, 255};
	}

	inline void System::SetPopupOpen(ECS::EntityId e, bool open) {
		if (auto *pd = m_ctx.Get<PopupData>(e)) {
			pd->open = open;
		}
	}

	inline void System::SetPopupTitle(ECS::EntityId e, std::string_view title) {
		if (auto *pd = m_ctx.Get<PopupData>(e)) {
			pd->title = std::string(title);
		}
	}

	inline void System::AddPopupHeaderButton(ECS::EntityId e, std::string_view iconKey, std::function<void()> cb) {
		// TODO: Implement
	}

	inline void System::AddTreeNode(ECS::EntityId e, const TreeNodeData& node) {
		if (auto *td = m_ctx.Get<TreeData>(e)) {
			td->nodes.push_back(node);
		}
	}

	inline void System::ClearTreeNodes(ECS::EntityId e) {
		if (auto *td = m_ctx.Get<TreeData>(e)) {
			td->nodes.clear();
		}
	}

	inline int System::GetTreeSelection(ECS::EntityId e) const {
		if (auto *td = m_ctx.Get<TreeData>(e)) {
			return td->selectedIndex;
		}
		return -1;
	}

	inline void System::SetBgGradient(ECS::EntityId e, BgGradient grad) {
		if (auto *bg = m_ctx.Get<BgGradient>(e)) {
			*bg = grad;
		} else {
			m_ctx.Add<BgGradient>(e, grad);
		}
	}

	inline void System::RemoveBgGradient(ECS::EntityId e) {
		m_ctx.Remove<BgGradient>(e);
	}

	inline void System::SetTilesetStyle(ECS::EntityId e, TilesetStyle ts) {
		if (auto *tss = m_ctx.Get<TilesetStyle>(e)) {
			*tss = ts;
		} else {
			m_ctx.Add<TilesetStyle>(e, ts);
		}
	}

	inline void System::RemoveTilesetStyle(ECS::EntityId e) {
		m_ctx.Remove<TilesetStyle>(e);
	}

	inline void System::SetImageKey(ECS::EntityId e, std::string_view key, ImageFit fit) {
		if (auto *id = m_ctx.Get<ImageData>(e)) {
			id->key = std::string(key);
			id->fit = fit;
		}
	}

	inline LayoutGridProps& System::EnsureGridProps(ECS::EntityId e) {
		if (auto *gp = m_ctx.Get<LayoutGridProps>(e)) return *gp;
		m_ctx.Add<LayoutGridProps>(e, LayoutGridProps{});
		return *m_ctx.Get<LayoutGridProps>(e);
	}

	inline void System::SetGridCols(ECS::EntityId e, int n) {
		auto& gp = EnsureGridProps(e);
		gp.columns = n;
		if (m_layout) m_layout->MarkDirty(e);
	}

	inline void System::SetGridRows(ECS::EntityId e, int n) {
		auto& gp = EnsureGridProps(e);
		gp.rows = n;
		if (m_layout) m_layout->MarkDirty(e);
	}

	inline void System::SetGridColSizing(ECS::EntityId e, GridSizing s) {
		auto& gp = EnsureGridProps(e);
		gp.colSizing = s;
	}

	inline void System::SetGridRowSizing(ECS::EntityId e, GridSizing s) {
		auto& gp = EnsureGridProps(e);
		gp.rowSizing = s;
	}

	inline void System::SetGridLines(ECS::EntityId e, GridLines l) {
		auto& gp = EnsureGridProps(e);
		gp.lines = l;
	}

	inline void System::SetGridLineColor(ECS::EntityId e, SDL::Color c) {
		auto& gp = EnsureGridProps(e);
		gp.lineColor = c;
	}

	inline void System::SetGridLineThickness(ECS::EntityId e, float t) {
		auto& gp = EnsureGridProps(e);
		gp.lineThickness = t;
	}

	inline void System::SetGridCell(ECS::EntityId e, int col, int row, int colSpan, int rowSpan) {
		if (auto *gc = m_ctx.Get<GridCell>(e)) {
			gc->col = col;
			gc->row = row;
			gc->colSpan = colSpan;
			gc->rowSpan = rowSpan;
		} else {
			m_ctx.Add<GridCell>(e, GridCell{col, row, colSpan, rowSpan});
		}
	}

	inline void System::SetTextAreaContent(ECS::EntityId e, std::string_view text) {
		if (auto *te = m_ctx.Get<TextEdit>(e)) {
			te->text = std::string(text);
		}
	}

	inline void System::SetTextAreaHighlightColor(ECS::EntityId e, SDL::Color c) {
		// TODO: Implement
	}

	inline void System::SetTextAreaTabSize(ECS::EntityId e, int sz) {
		if (auto *te = m_ctx.Get<TextEdit>(e)) {
			te->tabSize = sz;
		}
	}

	inline void System::AddTextAreaSpan(ECS::EntityId e, int start, int end, TextSpanStyle style) {
		if (auto *ts = m_ctx.Get<TextSpans>(e)) {
			ts->spans.push_back({start, end, style});
		}
	}

	inline void System::ClearTextAreaSpans(ECS::EntityId e) {
		if (auto *ts = m_ctx.Get<TextSpans>(e)) {
			ts->spans.clear();
		}
	}

	inline void System::SetTextAreaReadOnly(ECS::EntityId e, bool ro) {
		if (auto *te = m_ctx.Get<TextEdit>(e)) {
			te->readOnly = ro;
		}
	}

	inline const std::string& System::GetTextAreaContent(ECS::EntityId e) const {
		if (auto *te = m_ctx.Get<TextEdit>(e)) {
			return te->text;
		}
		static std::string empty;
		return empty;
	}

	inline bool System::GetTextAreaReadOnly(ECS::EntityId e) const {
		if (auto *te = m_ctx.Get<TextEdit>(e)) {
			return te->readOnly;
		}
		return false;
	}

	inline void System::OnEventCanvas(ECS::EntityId e, std::function<void(SDL::Event&)> cb) {
		if (auto *cd = m_ctx.Get<CanvasData>(e)) {
			cd->eventCb = std::move(cb);
		}
	}

	inline void System::OnUpdateCanvas(ECS::EntityId e, std::function<void(float)> cb) {
		if (auto *cd = m_ctx.Get<CanvasData>(e)) {
			cd->updateCb = std::move(cb);
		}
	}

	inline void System::OnRenderCanvas(ECS::EntityId e, std::function<void(RendererRef, FRect)> cb) {
		if (auto *cd = m_ctx.Get<CanvasData>(e)) {
			cd->renderCb = std::move(cb);
		}
	}

	inline void System::CloseMenuBar(ECS::EntityId e) {
		if (auto *mb = m_ctx.Get<MenuBarData>(e)) {
			mb->openMenu = -1;
		}
	}

	// ── Callback registration ────────────────────────────────────────────────────────
	inline void System::OnPress(ECS::EntityId e, std::function<void(SDL::MouseButton)> cb) {
		if (auto *c = m_ctx.Get<Callbacks>(e)) c->onPress = cb;
	}

	inline void System::OnRelease(ECS::EntityId e, std::function<void(SDL::MouseButton)> cb) {
		if (auto *c = m_ctx.Get<Callbacks>(e)) c->onRelease = cb;
	}

	inline void System::OnClick(ECS::EntityId e, std::function<void(SDL::MouseButton)> cb) {
		if (auto *c = m_ctx.Get<Callbacks>(e)) c->onClick = cb;
	}

	inline void System::OnDoubleClick(ECS::EntityId e, std::function<void(SDL::MouseButton)> cb) {
		if (auto *c = m_ctx.Get<Callbacks>(e)) c->onDoubleClick = cb;
	}

	inline void System::OnMultiClick(ECS::EntityId e, std::function<void(SDL::MouseButton, int)> cb) {
		if (auto *c = m_ctx.Get<Callbacks>(e)) c->onMultiClick = cb;
	}

	inline void System::OnMouseEnter(ECS::EntityId e, std::function<void()> cb) {
		if (auto *c = m_ctx.Get<Callbacks>(e)) c->onMouseEnter = cb;
	}

	inline void System::OnMouseLeave(ECS::EntityId e, std::function<void()> cb) {
		if (auto *c = m_ctx.Get<Callbacks>(e)) c->onMouseLeave = cb;
	}

	inline void System::OnFocusGain(ECS::EntityId e, std::function<void()> cb) {
		if (auto *c = m_ctx.Get<Callbacks>(e)) c->onFocusGain = cb;
	}

	inline void System::OnFocusLose(ECS::EntityId e, std::function<void()> cb) {
		if (auto *c = m_ctx.Get<Callbacks>(e)) c->onFocusLose = cb;
	}

	inline void System::OnColorChange(ECS::EntityId e, std::function<void(SDL::Color)> cb) {
		if (auto *c = m_ctx.Get<Callbacks>(e)) {
			c->onChange = [cb](float v) { cb({(uint8_t)v, (uint8_t)(v*2), (uint8_t)(v*3), 255}); };
		}
	}

	inline void System::OnTextChange(ECS::EntityId e, std::function<void(const std::string&)> cb) {
		if (auto *c = m_ctx.Get<Callbacks>(e)) c->onTextChange = cb;
	}

	inline void System::OnToggle(ECS::EntityId e, std::function<void(bool)> cb) {
		if (auto *c = m_ctx.Get<Callbacks>(e)) {
			c->onToggle = cb;
		}
	}

	inline void System::OnScroll(ECS::EntityId e, std::function<void(float)> cb) {
		if (auto *c = m_ctx.Get<Callbacks>(e)) c->onScroll = cb;
	}

	inline void System::OnScrollChange(ECS::EntityId e, std::function<void(SDL::FPoint, SDL::FPoint)> cb) {
		if (auto *c = m_ctx.Get<Callbacks>(e)) {
			c->onScroll = [cb](float v) { cb({v, 0}, {0, 0}); };
		}
	}

	inline void System::OnTreeSelect(ECS::EntityId e, std::function<void(int, bool)> cb) {
		if (auto *c = m_ctx.Get<Callbacks>(e)) {
			c->onTreeSelect = cb;
		}
	}

	// ── Getters ──────────────────────────────────────────────────────────────────────
	inline const std::string& System::GetText(ECS::EntityId e) const {
		if (auto *te = m_ctx.Get<TextEdit>(e)) {
			return te->text;
		}
		static std::string empty;
		return empty;
	}

	inline int System::GetListBoxSelection(ECS::EntityId e) const {
		if (auto *ilv = m_ctx.Get<ItemListView>(e)) {
			return ilv->selectedIndex;
		}
		return -1;
	}

	inline float System::GetScrollOffset(ECS::EntityId e) const {
		if (auto *lp = m_ctx.Get<LayoutProps>(e)) {
			return lp->scrollY;
		}
		return 0.f;
	}

	inline FRect System::GetScreenRect(ECS::EntityId e) const {
		if (auto *cr = m_ctx.Get<ComputedRect>(e)) {
			return cr->screen;
		}
		return {};
	}

	inline bool System::IsChecked(ECS::EntityId e) const {
		if (auto *tog = m_ctx.Get<ToggleData>(e)) return tog->checked;
		if (auto *rad = m_ctx.Get<RadioData>(e)) return rad->checked;
		return false;
	}

	inline bool System::IsEnabled(ECS::EntityId e) const {
		if (auto *w = m_ctx.Get<Widget>(e)) return Has(w->behavior, BehaviorFlag::Enable);
		return false;
	}

	inline bool System::IsVisible(ECS::EntityId e) const {
		if (auto *w = m_ctx.Get<Widget>(e)) return Has(w->behavior, BehaviorFlag::Visible);
		return false;
	}

	inline bool System::IsHoverable(ECS::EntityId e) const {
		if (auto *w = m_ctx.Get<Widget>(e)) return Has(w->behavior, BehaviorFlag::Hoverable);
		return false;
	}

	inline bool System::IsSelectable(ECS::EntityId e) const {
		if (auto *w = m_ctx.Get<Widget>(e)) return Has(w->behavior, BehaviorFlag::Selectable);
		return false;
	}

	inline bool System::IsFocusable(ECS::EntityId e) const {
		if (auto *w = m_ctx.Get<Widget>(e)) return Has(w->behavior, BehaviorFlag::Focusable);
		return false;
	}

	inline bool System::IsScrollableX(ECS::EntityId e) const {
		if (auto *w = m_ctx.Get<Widget>(e)) return Has(w->behavior, BehaviorFlag::ScrollableX) || Has(w->behavior, BehaviorFlag::AutoScrollableX);
		return false;
	}

	inline bool System::IsScrollableY(ECS::EntityId e) const {
		if (auto *w = m_ctx.Get<Widget>(e)) return Has(w->behavior, BehaviorFlag::ScrollableY) || Has(w->behavior, BehaviorFlag::AutoScrollableY);
		return false;
	}

	inline bool System::IsHovered(ECS::EntityId e) const {
		if (auto *st = m_ctx.Get<WidgetState>(e)) return st->hovered;
		return false;
	}

	inline bool System::IsFocused(ECS::EntityId e) const {
		if (auto *st = m_ctx.Get<WidgetState>(e)) return st->focused;
		return false;
	}

	inline bool System::IsPressed(ECS::EntityId e) const {
		if (auto *st = m_ctx.Get<WidgetState>(e)) return st->pressed;
		return false;
	}

	// ── Texture ──────────────────────────────────────────────────────────────────
	inline SDL::TextureRef System::_EnsureTexture(const std::string &key, const std::string &path) {
		if (key.empty()) return nullptr;
		auto h = m_pool.Get<SDL::Texture>(key);
		if (h) return *h.get();
		SDL::Texture wrapper;
		try {
			wrapper = SDL::Texture(m_renderer, path.empty() ? key : path);
		} catch (...) {
			SDL::LogWarn(SDL::LOG_CATEGORY_APPLICATION, std::format("UI::System: cannot open texture '{}'", path.empty() ? key : path));
			return nullptr;
		}
		m_pool.Add<SDL::Texture>(key, std::move(wrapper));
		auto h2 = m_pool.Get<SDL::Texture>(key);
		return h2 ? SDL::TextureRef(*h2.get()) : nullptr;
	}

	// ── Font ──────────────────────────────────────────────────────────────────────
	inline SDL::RendererTextEngine *System::_EnsureEngine() {
		if (!m_engine.has_value()) {
			try {
				m_engine.emplace(m_renderer);
			} catch (...) {
				SDL::LogWarn(SDL::LOG_CATEGORY_APPLICATION, "UI::System: failed to create RendererTextEngine");
				return nullptr;
			}
		}
		return &m_engine.value();
	}

	inline SDL::FontRef System::_EnsureFont(const std::string &key, float ptsize, const std::string& path) {
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
			SDL::LogWarn(SDL::LOG_CATEGORY_APPLICATION, std::format("UI::System: cannot open font '{}' at {:.2f}pt", path.empty() ? key : path, ptsize));
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

	inline SDL::TextRef System::_EnsureText(ECS::EntityId e, SDL::FontRef font, const std::string& text) {
		if (!font || text.empty()) return nullptr;
		auto* engine = _EnsureEngine();
		if (!engine) return nullptr;
		auto* cache = m_ctx.Get<TextCache>(e);
		if (!cache) cache = &m_ctx.Add<TextCache>(e);
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

	inline System::ResolvedFont System::_ResolveFont(ECS::EntityId e) {
		for (ECS::EntityId cur = e; cur != ECS::NullEntity && m_ctx.IsAlive(cur);) {
			auto* s = m_ctx.Get<Style>(cur);
			if (!s) break;
			switch (s->usedFont) {
				case FontType::Debug:
					return {"", s->fontSize > 0.f ? s->fontSize : m_defaultFontSize, true};
				case FontType::Default:
					if (!m_defaultFontPath.empty()) return {m_defaultFontPath, m_defaultFontSize, false};
					return {"", m_defaultFontSize, m_usedDebugFontPerDefault};
				case FontType::Self:
					if (!s->fontKey.empty() && s->fontSize > 0.f) return {s->fontKey, s->fontSize, false};
					break;
				case FontType::Root: {
					ECS::EntityId root = cur;
					while (true) {
						auto* p = m_ctx.Get<Parent>(root);
						if (!p || p->id == ECS::NullEntity) break;
						root = p->id;
					}
					if (root != cur) {
						auto* rs = m_ctx.Get<Style>(root);
						if (rs && rs->usedFont == FontType::Self && !rs->fontKey.empty() && rs->fontSize > 0.f)
							return {rs->fontKey, rs->fontSize, false};
					}
					if (!m_defaultFontPath.empty()) return {m_defaultFontPath, m_defaultFontSize, false};
					return {"", m_defaultFontSize, m_usedDebugFontPerDefault};
				}
				case FontType::Inherited:
					break;
			}
			auto* p = m_ctx.Get<Parent>(cur);
			if (!p || p->id == ECS::NullEntity) break;
			cur = p->id;
		}
		if (!m_defaultFontPath.empty()) return {m_defaultFontPath, m_defaultFontSize, false};
		return {"", m_defaultFontSize, m_usedDebugFontPerDefault};
	}

	// ── Audio ─────────────────────────────────────────────────────────────────────
	inline SDL::AudioRef System::_EnsureAudio(const std::string& key, const std::string& path) {
		if (key.empty()) return nullptr;
		auto h = m_pool.Get<SDL::Audio>(key);
		if (h) return *h.get();
		SDL::Audio wrapper;
		try {
			wrapper = SDL::Audio(m_mixer, path.empty() ? key : path, false);
		} catch (...) {
			SDL::LogWarn(SDL::LOG_CATEGORY_APPLICATION, std::format("UI::System: cannot open audio '{}'", path.empty() ? key : path));
			return nullptr;
		}
		m_pool.Add<SDL::Audio>(key, std::move(wrapper));
		auto h2 = m_pool.Get<SDL::Audio>(key);
		return h2 ? SDL::AudioRef(*h2.get()) : nullptr;
	}

	inline void System::_PlayAudio(SDL::AudioRef audio) {
		if (m_mixer) m_mixer.PlayAudio(audio);
	}

	// ── Public resource loading ───────────────────────────────────────────────────
	inline void System::LoadTexture(std::string_view key, std::string_view path) {
		_EnsureTexture(std::string(key), std::string(path));
	}

	inline void System::LoadFont(std::string_view key, std::string_view path) {
		_EnsureFont(std::string(key), m_defaultFontSize, std::string(path));
	}

	inline void System::LoadAudio(std::string_view key, std::string_view path) {
		_EnsureAudio(std::string(key), std::string(path));
	}

	// ── Public accessors for sub-systems ──────────────────────────────────────────
	inline SDL::TextRef System::EnsureText(ECS::EntityId e, SDL::FontRef font, const std::string& text) {
		return _EnsureText(e, font, text);
	}

	inline SDL::FontRef System::EnsureFont(const std::string& key, float ptsize, const std::string& path) {
		return _EnsureFont(key, ptsize, path);
	}

	inline System::ResolvedFont System::ResolveFont(ECS::EntityId e) {
		return _ResolveFont(e);
	}

	// Forward template instantiations for common numeric types
	template <>
	inline void System::SetValue<int>(ECS::EntityId e, int v) {
		if (auto *nv = m_ctx.Get<NumericValue>(e)) {
			nv->val = (double)v;
		}
	}

	template <>
	inline void System::SetValue<float>(ECS::EntityId e, float v) {
		if (auto *nv = m_ctx.Get<NumericValue>(e)) {
			nv->val = (double)v;
		}
	}

	template <>
	inline void System::SetValue<double>(ECS::EntityId e, double v) {
		if (auto *nv = m_ctx.Get<NumericValue>(e)) {
			nv->val = v;
		}
	}

	template <>
	inline int System::GetValue<int>(ECS::EntityId e) const {
		if (auto *nv = m_ctx.Get<NumericValue>(e)) {
			return (int)nv->val;
		}
		return 0;
	}

	template <>
	inline float System::GetValue<float>(ECS::EntityId e) const {
		if (auto *nv = m_ctx.Get<NumericValue>(e)) {
			return (float)nv->val;
		}
		return 0.f;
	}

	template <>
	inline double System::GetValue<double>(ECS::EntityId e) const {
		if (auto *nv = m_ctx.Get<NumericValue>(e)) {
			return nv->val;
		}
		return 0.0;
	}

	template <typename T>
	inline void System::OnChange(ECS::EntityId e, std::function<void(T)> cb) {
		if (!m_ctx.IsAlive(e)) return;
		auto *c = m_ctx.Get<Callbacks>(e);
		if (c) {
			c->onChange = [cb](float v) { cb((T)v); };
		}
	}

	template <typename T>
	inline ECS::EntityId System::MakeSlider(std::string_view n, T mn, T mx, T v, T step, Orientation o) {
		ECS::EntityId e = m_factory->MakeSlider(n, mn, mx, v, step, o);
		return e;
	}

	template <typename T>
	inline ECS::EntityId System::MakeInputValue(std::string_view n, T minValue, T maxValue, T value, T step) {
		ECS::EntityId e = m_factory->MakeInputValue(n, minValue, maxValue, value, step);
		return e;
	}


	template <typename T>
	inline void System::SetSliderMarkers(ECS::EntityId e, std::vector<T> markers) {
		// TODO: Store markers in a component for rendering
	}

	// ==================================================================================
	// Deferred implementations for UILayoutSystem::_TextWidth/Height and UIRenderSystem::_DrawText
	// (defined here to ensure System is fully defined)
	// ==================================================================================

	inline float UILayoutSystem::_TextWidth(const std::string& text, ECS::EntityId e) {
		if (text.empty()) return 0.f;
		auto rf = m_sys.ResolveFont(e);
		auto font = m_sys.EnsureFont(rf.key, rf.size);
		if (font) {
			int measured_width = 0;
			size_t measured_length = 0;
			font.MeasureString(text, 10000, &measured_width, &measured_length);
			return (float)measured_width;
		}
		// Fallback: estimate from font size
		return (float)text.size() * rf.size * 0.6f;
	}

	inline float UILayoutSystem::_TextHeight(ECS::EntityId e) {
		auto rf = m_sys.ResolveFont(e);
		auto font = m_sys.EnsureFont(rf.key, rf.size);
		if (font) return (float)font.GetHeight();
		return rf.size;
	}

	inline void UIRenderSystem::_DrawText(ECS::EntityId e, const std::string& text, const FRect& rect,
	                                      TextHAlign hAlign, TextVAlign vAlign) {
		if (text.empty()) return;

		// Resolve font and get cached text
		auto rf = m_sys.ResolveFont(e);
		auto font = m_sys.EnsureFont(rf.key, rf.size);
		if (!font) return;

		auto textRef = m_sys.EnsureText(e, font, text);
		if (!textRef) return;

		// Compute position based on alignment
		auto sz = textRef.GetSize();
		FPoint pos = {rect.x, rect.y};

		switch (hAlign) {
			case TextHAlign::Left:
				pos.x = rect.x;
				break;
			case TextHAlign::Center:
				pos.x = rect.x + (rect.w - sz.x) * 0.5f;
				break;
			case TextHAlign::Right:
				pos.x = rect.x + rect.w - sz.x;
				break;
		}

		switch (vAlign) {
			case TextVAlign::Top:
				pos.y = rect.y;
				break;
			case TextVAlign::Center:
				pos.y = rect.y + (rect.h - sz.y) * 0.5f;
				break;
			case TextVAlign::Bottom:
				pos.y = rect.y + rect.h - sz.y;
				break;
		}

		// Render text
		textRef.DrawRenderer(pos);
	}

} // namespace SDL::UI

// Include builder types after all declarations and implementations
#include "UIBuilder.h"

// Implement builder-returning helper methods after UIBuilder.h
namespace SDL::UI {

	inline MenuBarBuilder System::MenuBar(std::string_view n) {
		return MenuBarBuilder{*this, MakeMenuBar(n)};
	}

	inline ContainerBuilder System::Column(std::string_view n, float gap, float pad, float marg) {
		return ContainerBuilder{*this, MakeContainer(n)};
	}

	inline ContainerBuilder System::Row(std::string_view n, float gap, float pad, float marg) {
		return ContainerBuilder{*this, MakeContainer(n)};
	}

	inline ContainerBuilder System::Card(std::string_view n, float gap, float marg) {
		return ContainerBuilder{*this, MakeContainer(n)};
	}

	inline ContainerBuilder System::Stack(std::string_view n, float gap, float pad, float marg) {
		return ContainerBuilder{*this, MakeContainer(n)};
	}

	inline ContainerBuilder System::ScrollView(std::string_view n, float gap) {
		return ContainerBuilder{*this, MakeContainer(n)};
	}

	inline ContainerBuilder System::Grid(std::string_view n, int columns, float gap, float pad) {
		return ContainerBuilder{*this, MakeContainer(n)};
	}

	inline Builder System::SectionTitle(std::string_view text, SDL::Color color) {
		return Builder{*this, MakeLabel("section", text)};
	}

	template <typename T>
	inline SliderBuilder System::Slider(std::string_view n, T mn, T mx, T v, Orientation o) {
		ECS::EntityId e = MakeSlider(n, mn, mx, v, T(0), o);
		return SliderBuilder{e, this};
	}

	template <typename T>
	inline InputBuilder System::InputValue(std::string_view n, T minValue, T maxValue, T value, T step) {
		ECS::EntityId e = MakeInputValue(n, minValue, maxValue, value, step);
		return InputBuilder{e, this};
	}

	inline Builder System::GetBuilder(ECS::EntityId e) {
		return Builder{*this, e};
	}

} // namespace SDL::UI
