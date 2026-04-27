/**
 * @file 04_sdl3_office.cpp
 * @brief SDL3pp Office — a LibreOffice-inspired rich-text editor.
 *
 * Features:
 *   - MenuBar with File / Edit / Format / View / Help menus
 *   - Formatting toolbar: Bold, Italic, Underline, Strikethrough, Highlight,
 *     Text colour (via popup colour picker)
 *   - Multi-line TextArea with rich-text spans (bold, italic, underline,
 *     strikethrough, highlight)
 *   - Status bar: file name, cursor position, character count
 *   - Save / Open as XML using SDL3pp DataScripts
 *
 * Keyboard shortcuts (when the editor has focus):
 *   Ctrl+B  Bold        Ctrl+I  Italic      Ctrl+U  Underline
 *   Ctrl+K  Strikethrough  Ctrl+Shift+H  Highlight
 *   Ctrl+N  New         Ctrl+O  Open        Ctrl+S  Save
 *   Ctrl+A  Select All  Ctrl+Space  Clear Formatting
 */

#define SDL3PP_MAIN_USE_CALLBACKS 1
#include <SDL3pp/SDL3pp.h>
#include <SDL3pp/SDL3pp_main.h>
#include <SDL3pp/SDL3pp_dataScripts.h>
#include <SDL3pp/SDL3pp_ui.h>

#include <format>
#include <string>
#include <vector>

using namespace std::literals;

// ─────────────────────────────────────────────────────────────────────────────
// Aliases
// ─────────────────────────────────────────────────────────────────────────────
using MBI = SDL::UI::MenuBarItem;
using MBM = SDL::UI::MenuBarMenu;
using TSS = SDL::UI::TextSpanStyle;

// ─────────────────────────────────────────────────────────────────────────────
// Colour palette
// ─────────────────────────────────────────────────────────────────────────────
namespace pal {
    constexpr SDL::Color BG      = { 14,  16,  22, 255};
    constexpr SDL::Color PANEL   = { 18,  20,  30, 255};
    constexpr SDL::Color EDITOR  = { 20,  22,  34, 255};
    constexpr SDL::Color STATUS  = { 16,  18,  26, 255};
    constexpr SDL::Color ACCENT  = { 55, 130, 220, 255};
    constexpr SDL::Color WHITE   = {215, 218, 228, 255};
    constexpr SDL::Color GREY    = {120, 128, 148, 255};
    constexpr SDL::Color BORDER  = { 42,  46,  68, 255};
}

// ─────────────────────────────────────────────────────────────────────────────
// Resource keys / paths
// ─────────────────────────────────────────────────────────────────────────────
namespace res {
    constexpr const char* FONT      = "font";
    constexpr const char* I_BOLD    = "assets/textures/icons/icon_bold.png";
    constexpr const char* I_ITALIC  = "assets/textures/icons/icon_italic.png";
    constexpr const char* I_UNDER   = "assets/textures/icons/icon_underline.png";
    constexpr const char* I_STRIKE  = "assets/textures/icons/icon_strikethrough.png";
    constexpr const char* I_HL      = "assets/textures/icons/icon_highlight.png";
    constexpr const char* I_TCOLOR  = "assets/textures/icons/icon_text_color.png";
    constexpr const char* I_AL      = "assets/textures/icons/icon_align_left.png";
    constexpr const char* I_AC      = "assets/textures/icons/icon_align_center.png";
    constexpr const char* I_AR      = "assets/textures/icons/icon_align_right.png";
    constexpr const char* I_AJ      = "assets/textures/icons/icon_align_justify.png";
    constexpr const char* I_NEW     = "assets/textures/icons/icon_new.png";
    constexpr const char* I_OPEN    = "assets/textures/icons/icon_open.png";
    constexpr const char* I_SAVE    = "assets/textures/icons/icon_save.png";
    constexpr const char* I_SAVEAS  = "assets/textures/icons/icon_save_as.png";
    constexpr const char* I_UNDO    = "assets/textures/icons/icon_undo.png";
    constexpr const char* I_REDO    = "assets/textures/icons/icon_redo.png";
    constexpr const char* I_CUT     = "assets/textures/icons/icon_cut.png";
    constexpr const char* I_COPY    = "assets/textures/icons/icon_copy.png";
    constexpr const char* I_PASTE   = "assets/textures/icons/icon_paste.png";
    constexpr const char* I_FIND    = "assets/textures/icons/icon_find.png";
    constexpr const char* I_PRINT   = "assets/textures/icons/icon_print.png";
}

// ─────────────────────────────────────────────────────────────────────────────
// MenuItem factory helpers
// ─────────────────────────────────────────────────────────────────────────────
static MBI MI(std::string label, std::string shortcut = {}, const char *icon = nullptr,
              std::function<void()> action = nullptr, bool enabled = true) {
    MBI item;
    item.label        = std::move(label);
    item.shortcutText = std::move(shortcut);
    item.iconKey      = icon ? std::string(icon) : std::string{};
    item.action       = std::move(action);
    item.enabled      = enabled;
    return item;
}
static MBI MSep() { return MBI::Sep(); }

// ─────────────────────────────────────────────────────────────────────────────
// Application
// ─────────────────────────────────────────────────────────────────────────────
struct Main {
    // ── SDL handles ───────────────────────────────────────────────────────────
    SDL::Window   window{"SDL3pp Office", {1280, 780}};
    SDL::Renderer renderer{window};

    // ── UI system ─────────────────────────────────────────────────────────────
    SDL::ECS::Context ctx;
    SDL::ResourceManager rm;
    SDL::ResourcePool   *pool = rm.CreatePool("ui");
    SDL::UI::System      ui{ctx, renderer, SDL::MixerRef{}, *pool};
    SDL::FrameTimer      timer{60.f};

    // ── Widget IDs ────────────────────────────────────────────────────────────
    SDL::ECS::EntityId eEditor    = SDL::ECS::NullEntity;
    SDL::ECS::EntityId eStatus    = SDL::ECS::NullEntity;
    SDL::ECS::EntityId eFilePopup = SDL::ECS::NullEntity;
    SDL::ECS::EntityId eFileInput = SDL::ECS::NullEntity;
    SDL::ECS::EntityId eClrPopup  = SDL::ECS::NullEntity;
    SDL::ECS::EntityId eClrPick   = SDL::ECS::NullEntity;

    // ── Document state ────────────────────────────────────────────────────────
    std::string m_filePath;
    bool        m_modified      = false;
    bool        m_isSaveAs      = false;

    // ── SDL::AppResult callbacks ──────────────────────────────────────────────────

	static SDL::AppResult Init(Main** out, SDL::AppArgs args) {
		SDL::LogPriority priority = SDL::LOG_PRIORITY_WARN;
		for (auto arg : args) {
			if      (arg == "--verbose") priority = SDL::LOG_PRIORITY_VERBOSE;
			else if (arg == "--debug")   priority = SDL::LOG_PRIORITY_DEBUG;
			else if (arg == "--info")    priority = SDL::LOG_PRIORITY_INFO;
		}
		SDL::SetLogPriorities(priority);
		SDL::SetAppMetadata("SDL3pp office", "2.0", "com.example.office");
		SDL::Init(SDL::INIT_VIDEO);
		SDL::TTF::Init();
		SDL::MIX::Init();
		*out = new Main();
		return SDL::APP_CONTINUE;
	}
	static void Quit(Main* m, SDL::AppResult) {
		delete m;
		SDL::MIX::Quit();
		SDL::TTF::Quit();
		SDL::Quit();
	}

    // ─────────────────────────────────────────────────────────────────────────
    Main() {
        window.StartTextInput();

        // Load font
        ui.LoadFont(res::FONT, "assets/fonts/DejaVuSans.ttf");
        ui.SetDefaultFont(res::FONT, 14.f);
        _BuildUI();
    }

    // ─────────────────────────────────────────────────────────────────────────
    // UI construction
    // ─────────────────────────────────────────────────────────────────────────
    void _BuildUI() {
        // ── File path popup ───────────────────────────────────────────────────
        eFileInput = ui.MakeInput("file_input", "Enter file path…");
        eFilePopup = ui.Popup("file_popup", "File", true, true, false)
            .W(480.f).H(110.f)
            .Fixed(SDL::UI::Value::Ww(50.f) - 240.f,
                   SDL::UI::Value::Wh(50.f) - 55.f)
            .Children(
                ui.Column("fp_col", 8.f, 8.f)
                .W(SDL::UI::Value::Pw(100.f))
                .Children(
                    ui.GetBuilder(eFileInput)
                        .W(SDL::UI::Value::Pw(100.f)).H(28.f),
                    ui.Row("fp_btns", 8.f, 0.f)
                    .W(SDL::UI::Value::Pw(100.f))
                    .Children(
                        ui.Label("fp_sp", "").Grow(100.f),
                        ui.Button("fp_ok", "OK").W(80.f).H(28.f)
                            .BgColor(pal::ACCENT)
                            .Radius(SDL::FCorners(4.f))
                            .OnClick([this]{ _OnFileOK(); }),
                        ui.Button("fp_cancel", "Cancel").W(80.f).H(28.f)
                            .Radius(SDL::FCorners(4.f))
                            .OnClick([this]{
                                ui.SetPopupOpen(eFilePopup, false);
                            })
                    )
                )
            ).Id();

        // ── Colour picker popup ───────────────────────────────────────────────
        eClrPick  = ui.MakeColorPicker("clr_pick");
        eClrPopup = ui.Popup("clr_popup", "Text Colour", true, true, false)
            .W(240.f).H(295.f)
            .Fixed(SDL::UI::Value::Ww(50.f) - 120.f,
                   SDL::UI::Value::Wh(50.f) - 147.f)
            .Children(
                ui.Column("clr_col", 8.f, 8.f)
                .W(SDL::UI::Value::Pw(100.f))
                .Children(
                    ui.GetBuilder(eClrPick)
                        .W(SDL::UI::Value::Pw(100.f)).H(210.f),
                    ui.Row("clr_btns", 8.f, 0.f)
                    .W(SDL::UI::Value::Pw(100.f))
                    .Children(
                        ui.Label("clr_sp","").Grow(100.f),
                        ui.Button("clr_apply","Apply").W(80).H(26)
                            .BgColor(pal::ACCENT)
                            .Radius(SDL::FCorners(4.f))
                            .OnClick([this]{ _ApplyTextColor(); }),
                        ui.Button("clr_cancel","Cancel").W(80).H(26)
                            .Radius(SDL::FCorners(4.f))
                            .OnClick([this]{
                                ui.SetPopupOpen(eClrPopup, false);
                            })
                    )
                )
            ).Id();

        // ── MenuBar ───────────────────────────────────────────────────────────
        auto menubar = ui.MenuBar("mb")
            .W(SDL::UI::Value::Pw(100.f)).H(26.f)
            .WithStyle([](SDL::UI::Style &s){
                s.bgColor        = {18, 20, 30, 255};
                s.bgHoveredColor = {45, 50, 75, 200};
                s.bgCheckedColor = {35, 90, 170, 200};
                s.textColor      = {215, 218, 228, 255};
                s.bdColor        = {42, 46, 68, 255};
                s.radius         = {};
            })
            .Font(res::FONT, 13.f)
            .AddMenu("File", {
                MI("New",       "Ctrl+N",  res::I_NEW,    [this]{ _CmdNew(); }),
                MI("Open…",     "Ctrl+O",  res::I_OPEN,   [this]{ _ShowFilePopup(false); }),
                MSep(),
                MI("Save",      "Ctrl+S",  res::I_SAVE,   [this]{ _CmdSave(); }),
                MI("Save As…",  "",        res::I_SAVEAS, [this]{ _ShowFilePopup(true); }),
                MSep(),
                MI("Print…",    "Ctrl+P",  res::I_PRINT,  nullptr, false),
                MSep(),
                MI("Exit",      "Alt+F4",  nullptr,       [this]{ m_running = false; })
            })
            .AddMenu("Edit", {
                MI("Undo",       "Ctrl+Z",  res::I_UNDO,  nullptr, false),
                MI("Redo",       "Ctrl+Y",  res::I_REDO,  nullptr, false),
                MSep(),
                MI("Cut",        "Ctrl+X",  res::I_CUT,   [this]{ _CmdCutCopyPaste(0); }),
                MI("Copy",       "Ctrl+C",  res::I_COPY,  [this]{ _CmdCutCopyPaste(1); }),
                MI("Paste",      "Ctrl+V",  res::I_PASTE, [this]{ _CmdCutCopyPaste(2); }),
                MSep(),
                MI("Select All", "Ctrl+A",  nullptr,      [this]{ _CmdSelectAll(); }),
                MSep(),
                MI("Find…",      "Ctrl+F",  res::I_FIND,  nullptr, false)
            })
            .AddMenu("Format", {
                MI("Bold",           "Ctrl+B",       res::I_BOLD,   [this]{ _ToggleBold(); }),
                MI("Italic",         "Ctrl+I",       res::I_ITALIC, [this]{ _ToggleItalic(); }),
                MI("Underline",      "Ctrl+U",       res::I_UNDER,  [this]{ _ToggleUnderline(); }),
                MI("Strikethrough",  "Ctrl+K",       res::I_STRIKE, [this]{ _ToggleStrike(); }),
                MI("Highlight",      "Ctrl+Shift+H", res::I_HL,     [this]{ _ToggleHighlight(); }),
                MSep(),
                MI("Text Colour…",   "",             res::I_TCOLOR, [this]{ _ShowClrPopup(); }),
                MSep(),
                MI("Clear Formatting","Ctrl+Space",  nullptr,       [this]{ _ClearFormatting(); })
            })
            .AddMenu("View", {
                MI("Zoom In",    "Ctrl++", nullptr, nullptr, false),
                MI("Zoom Out",   "Ctrl+-", nullptr, nullptr, false),
                MI("Reset Zoom", "Ctrl+0", nullptr, nullptr, false)
            })
            .AddMenu("Help", {
                MI("About SDL3pp Office…", {}, nullptr, []{
                    SDL::Log("SDL3pp Office v1.0 — built with SDL3pp UI.");
                })
            });

        // ── Toolbar ───────────────────────────────────────────────────────────
        auto mkTB = [&](const char *name, const char *icon, const char *tip,
                        std::function<void()> fn, bool enabled = true) {
            auto b = ui.Button(name, "")
                .W(28.f).H(28.f).Padding(3.f)
                .Icon(icon, 3.f)
                .BgColor({0,0,0,0})
                .BgHover({55, 65, 90, 200})
                .BgPress({35, 45, 70, 255})
                .Radius(SDL::FCorners(4.f))
                .Tooltip(tip, 0.6f)
                .Enable(enabled);
            if (fn) b.OnClick(std::move(fn));
            return b;
        };

        auto mkSep = [&](const char *name) {
            return ui.Separator(name)
                .W(1.f).H(22.f)
                .WithStyle([](SDL::UI::Style &s){
                    s.separatorColor = {55, 60, 80, 200};
                    s.borders = {0.f, 1.f, 0.f, 0.f};
                });
        };

        auto toolbar = ui.Row("toolbar", 2.f, 6.f)
            .W(SDL::UI::Value::Pw(100.f)).H(38.f)
            .WithStyle([](SDL::UI::Style &s){
                s.bgColor = {18, 20, 30, 255};
                s.borders = {0.f, 0.f, 0.f, 1.f};
                s.bdColor = {42, 46, 68, 255};
                s.radius  = {};
            })
            .Children(
                mkTB("tb_new",   res::I_NEW,   "New (Ctrl+N)",           [this]{ _CmdNew(); }),
                mkTB("tb_open",  res::I_OPEN,  "Open (Ctrl+O)",          [this]{ _ShowFilePopup(false); }),
                mkTB("tb_save",  res::I_SAVE,  "Save (Ctrl+S)",          [this]{ _CmdSave(); }),
                mkSep("sp1"),
                mkTB("tb_undo",  res::I_UNDO,  "Undo (Ctrl+Z)",          nullptr, false),
                mkTB("tb_redo",  res::I_REDO,  "Redo (Ctrl+Y)",          nullptr, false),
                mkTB("tb_cut",   res::I_CUT,   "Cut (Ctrl+X)",           [this]{ _CmdCutCopyPaste(0); }),
                mkTB("tb_copy",  res::I_COPY,  "Copy (Ctrl+C)",          [this]{ _CmdCutCopyPaste(1); }),
                mkTB("tb_paste", res::I_PASTE, "Paste (Ctrl+V)",         [this]{ _CmdCutCopyPaste(2); }),
                mkSep("sp2"),
                mkTB("tb_bold",  res::I_BOLD,  "Bold (Ctrl+B)",          [this]{ _ToggleBold(); }),
                mkTB("tb_ital",  res::I_ITALIC,"Italic (Ctrl+I)",        [this]{ _ToggleItalic(); }),
                mkTB("tb_ul",    res::I_UNDER, "Underline (Ctrl+U)",     [this]{ _ToggleUnderline(); }),
                mkTB("tb_st",    res::I_STRIKE,"Strikethrough (Ctrl+K)", [this]{ _ToggleStrike(); }),
                mkTB("tb_hl",    res::I_HL,    "Highlight (Ctrl+Shift+H)",[this]{ _ToggleHighlight(); }),
                mkTB("tb_tc",    res::I_TCOLOR,"Text Colour",            [this]{ _ShowClrPopup(); }),
                mkSep("sp3"),
                mkTB("tb_al",    res::I_AL, "Align Left",   nullptr, false),
                mkTB("tb_ac",    res::I_AC, "Align Center", nullptr, false),
                mkTB("tb_ar",    res::I_AR, "Align Right",  nullptr, false),
                mkTB("tb_aj",    res::I_AJ, "Justify",      nullptr, false)
            );

        // ── TextArea (main editor) ────────────────────────────────────────────
        eEditor = ui.TextArea("editor", "", "Start typing your document here…")
            .W(SDL::UI::Value::Pw(100.f))
            .Grow(100.f)
            .WithStyle([](SDL::UI::Style &s){
                s.bgColor        = {20, 22, 34, 255};
                s.bgFocusedColor = {20, 22, 34, 255};
                s.bgHoveredColor = {22, 24, 36, 255};
                s.bdColor        = {42, 46, 68, 255};
                s.bdFocusedColor = {55, 130, 220, 180};
                s.textColor      = {215, 218, 228, 255};
                s.radius         = {};
                s.borders        = {0.f, 0.f, 1.f, 0.f};
            })
            .AutoScrollable(true, true)
            .Padding(14.f)
            .Font(res::FONT, 14.f)
            .OnTextChange([this](const std::string &){ m_modified = true; })
            .Id();

        // ── Status bar ────────────────────────────────────────────────────────
        eStatus = ui.Label("status", "Ln 1, Col 1  |  0 chars  |  Untitled")
            .W(SDL::UI::Value::Pw(100.f)).H(22.f)
            .PaddingH(8.f).PaddingV(2.f)
            .WithStyle([](SDL::UI::Style &s){
                s.bgColor   = {16, 18, 26, 255};
                s.textColor = {110, 120, 145, 255};
                s.borders   = {0.f, 0.f, 1.f, 0.f};
                s.bdColor   = {42, 46, 68, 255};
                s.radius    = {};
            })
            .Id();

        // ── Root ──────────────────────────────────────────────────────────────
        ui.Column("root", 0.f, 0.f)
            .W(SDL::UI::Value::Ww(100.f))
            .H(SDL::UI::Value::Wh(100.f))
            .WithStyle([](SDL::UI::Style &s){
                s.bgColor = {14, 16, 22, 255};
                s.radius  = {};
                s.borders = {};
            })
            .Children(
                menubar,
                toolbar,
                ui.GetBuilder(eEditor),
                ui.GetBuilder(eStatus),
                ui.GetBuilder(eFilePopup),
                ui.GetBuilder(eClrPopup)
            )
            .AsRoot();
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Formatting
    // ─────────────────────────────────────────────────────────────────────────
    void _ApplySpan(std::function<void(TSS &)> mutate) {
        auto *ta = ui.GetTextAreaData(eEditor);
        if (!ta || !ta->HasSelection()) return;
        int s = ta->SelMin(), e = ta->SelMax();
        TSS style;
        if (const TSS *cur = ta->SpanStyleAt(s)) style = *cur;
        mutate(style);
        ta->AddSpan(s, e, style);
        m_modified = true;
    }
    void _ToggleBold()      { _ApplySpan([](TSS &s){ s.bold          = !s.bold; }); }
    void _ToggleItalic()    { _ApplySpan([](TSS &s){ s.italic        = !s.italic; }); }
    void _ToggleUnderline() { _ApplySpan([](TSS &s){ s.underline     = !s.underline; }); }
    void _ToggleStrike()    { _ApplySpan([](TSS &s){ s.strikethrough = !s.strikethrough; }); }
    void _ToggleHighlight() { _ApplySpan([](TSS &s){ s.highlight     = !s.highlight; }); }

    void _ApplyTextColor() {
        SDL::Color c = ui.GetPickedColor(eClrPick);
        _ApplySpan([c](TSS &s){ s.color = c; });
        ui.SetPopupOpen(eClrPopup, false);
    }

    void _ClearFormatting() {
        auto *ta = ui.GetTextAreaData(eEditor);
        if (!ta || !ta->HasSelection()) return;
        ta->AddSpan(ta->SelMin(), ta->SelMax(), TSS{});
        m_modified = true;
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Edit commands
    // ─────────────────────────────────────────────────────────────────────────
    void _CmdSelectAll() {
        auto *ta = ui.GetTextAreaData(eEditor);
        if (ta) ta->SetSelection(0, (int)ta->text.size());
    }

    void _CmdCutCopyPaste(int mode) {
        // mode 0=cut, 1=copy, 2=paste — synthesise keyboard events
        SDL::Keycode key = (mode == 0) ? SDL::KEYCODE_X
                         : (mode == 1) ? SDL::KEYCODE_C
                                       : SDL::KEYCODE_V;
        SDL::Event ev{};
        ev.type = SDL::EVENT_KEY_DOWN;
        ev.key.key = key;
        ev.key.mod = SDL::KMOD_CTRL;
        ui.ProcessEvent(ev);
    }

    // ─────────────────────────────────────────────────────────────────────────
    // File commands
    // ─────────────────────────────────────────────────────────────────────────
    void _CmdNew() {
        auto *ta = ui.GetTextAreaData(eEditor);
        if (!ta) return;
        ta->text.clear();
        ta->ClearSpans();
        ta->cursorPos = 0;
        ta->ClearSelection();
        m_filePath.clear();
        m_modified = false;
    }

    void _CmdSave() {
        if (m_filePath.empty()) { _ShowFilePopup(true); return; }
        _SaveToFile(m_filePath);
    }

    void _ShowFilePopup(bool isSaveAs) {
        m_isSaveAs = isSaveAs;
        ui.SetPopupTitle(eFilePopup, isSaveAs ? "Save As" : "Open File");
        ui.SetPopupOpen(eFilePopup, true);
        // Pre-fill the input with the current path
        auto *cnt = ui.GetECSContext().Get<SDL::UI::Content>(eFileInput);
        if (cnt) { cnt->text = m_filePath; cnt->cursor = (int)cnt->text.size(); }
    }

    void _OnFileOK() {
        auto *cnt = ui.GetECSContext().Get<SDL::UI::Content>(eFileInput);
        if (!cnt || cnt->text.empty()) return;
        std::string path = cnt->text;
        ui.SetPopupOpen(eFilePopup, false);
        if (m_isSaveAs) _SaveToFile(path);
        else            _LoadFromFile(path);
    }

    void _ShowClrPopup() {
        ui.SetPopupOpen(eClrPopup, true);
    }

    // ─────────────────────────────────────────────────────────────────────────
    // XML Save / Load via SDL3pp DataScripts
    // ─────────────────────────────────────────────────────────────────────────
    void _SaveToFile(const std::string &path) {
        auto *ta = ui.GetTextAreaData(eEditor);
        if (!ta) return;

        auto doc  = std::make_shared<SDL::XMLDataDocument>();
        auto root = SDL::ObjectDataNode::Make();
        root->set("text", SDL::StringDataNode::Make(ta->text));

        auto spArr = std::make_shared<SDL::ArrayDataNode>();
        for (const auto &sp : ta->spans) {
            auto sn = SDL::ObjectDataNode::Make();
            sn->set("s",  SDL::S32DataNode::Make(sp.start));
            sn->set("e",  SDL::S32DataNode::Make(sp.end));
            sn->set("b",  SDL::BoolDataNode::Make(sp.style.bold));
            sn->set("i",  SDL::BoolDataNode::Make(sp.style.italic));
            sn->set("u",  SDL::BoolDataNode::Make(sp.style.underline));
            sn->set("sk", SDL::BoolDataNode::Make(sp.style.strikethrough));
            sn->set("hl", SDL::BoolDataNode::Make(sp.style.highlight));
            sn->set("cr", SDL::U8DataNode::Make(sp.style.color.r));
            sn->set("cg", SDL::U8DataNode::Make(sp.style.color.g));
            sn->set("cb", SDL::U8DataNode::Make(sp.style.color.b));
            sn->set("ca", SDL::U8DataNode::Make(sp.style.color.a));
            spArr->add(sn);
        }
        root->set("spans", spArr);
        doc->setRoot(root);

        std::string xml = doc->encode();
        if (auto io = SDL::IOFromFile(path, "w")) {
            io.Write(SDL::SourceBytes{xml.data(), xml.size()});
            m_filePath = path;
            m_modified = false;
        } else {
            SDL::LogWarn(SDL::LOG_CATEGORY_APPLICATION, "Office: cannot write '%s'", path.c_str());
        }
    }

    void _LoadFromFile(const std::string &path) {
        auto io = SDL::IOFromFile(path, "r");
        if (!io) { SDL::LogWarn(SDL::LOG_CATEGORY_APPLICATION, "Office: cannot open '%s'", path.c_str()); return; }

        auto doc = std::make_shared<SDL::XMLDataDocument>();
        if (doc->decode(io)) return;

        auto root = doc->getRoot();
        if (!root) return;

        auto *ta = ui.GetTextAreaData(eEditor);
        if (!ta) return;
        ta->text.clear(); ta->ClearSpans(); ta->cursorPos = 0;

        if (auto tn = std::dynamic_pointer_cast<SDL::StringDataNode>(root->get("text")))
            ta->text = tn->getValue();

        if (auto sa = std::dynamic_pointer_cast<SDL::ArrayDataNode>(root->get("spans"))) {
            for (auto &sn : sa->getValues()) {
                auto sobj = std::dynamic_pointer_cast<SDL::ObjectDataNode>(sn);
                if (!sobj) continue;
                auto getI = [&](const char *k) -> int {
                    if (auto n = std::dynamic_pointer_cast<SDL::S32DataNode>(sobj->get(k))) return n->getValue();
                    return 0;
                };
                auto getB = [&](const char *k) -> bool {
                    if (auto n = std::dynamic_pointer_cast<SDL::BoolDataNode>(sobj->get(k))) return n->getValue();
                    return false;
                };
                auto getU8 = [&](const char *k) -> uint8_t {
                    if (auto n = std::dynamic_pointer_cast<SDL::U8DataNode>(sobj->get(k))) return n->getValue();
                    return 0;
                };
                TSS style;
                style.bold          = getB("b");
                style.italic        = getB("i");
                style.underline     = getB("u");
                style.strikethrough = getB("sk");
                style.highlight     = getB("hl");
                style.color.r       = getU8("cr");
                style.color.g       = getU8("cg");
                style.color.b       = getU8("cb");
                style.color.a       = getU8("ca");
                ta->AddSpan(getI("s"), getI("e"), style);
            }
        }
        m_filePath = path;
        m_modified = false;
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Status update
    // ─────────────────────────────────────────────────────────────────────────
    void _UpdateStatus() {
        auto *ta = ui.GetTextAreaData(eEditor);
        int line = 1, col = 1, nc = 0;
        if (ta) {
            nc   = (int)ta->text.size();
            line = ta->LineOf(ta->cursorPos) + 1;
            col  = ta->ColOf(ta->cursorPos)  + 1;
        }
        std::string fname = m_filePath.empty() ? "Untitled" : m_filePath;
        auto slash = fname.rfind('/');
        if (slash == std::string::npos) slash = fname.rfind('\\');
        if (slash != std::string::npos) fname = fname.substr(slash + 1);

        std::string txt = std::format("Ln {}, Col {}  |  {} chars  |  {}{}",
                                      line, col, nc, fname, m_modified ? " *" : "");
        ui.SetText(eStatus, txt);
        window.SetTitle(std::format("SDL3pp Office — {}{}", fname, m_modified ? " *" : ""));
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Main loop callbacks
    // ─────────────────────────────────────────────────────────────────────────
    bool m_running = true;

    SDL::AppResult Iterate() {
        timer.Begin();
        if (!m_running) return SDL::APP_SUCCESS;
        renderer.SetDrawColor(pal::BG);
        renderer.RenderClear();
        ui.Iterate(0.016f);
        _UpdateStatus();
        renderer.Present();
        timer.End();
        return SDL::APP_CONTINUE;
    }

    SDL::AppResult Event(const SDL::Event &ev) {
        ui.ProcessEvent(ev);
        if (ev.type == SDL::EVENT_QUIT) return SDL::APP_SUCCESS;
        if (ev.type == SDL::EVENT_KEY_DOWN) {
            bool ctrl  = (ev.key.mod & SDL::KMOD_CTRL)  != SDL::KMOD_NONE;
            bool shift = (ev.key.mod & SDL::KMOD_SHIFT) != SDL::KMOD_NONE;
            if (ctrl) {
                switch (ev.key.key) {
                case SDL::KEYCODE_N: _CmdNew();           break;
                case SDL::KEYCODE_O: _ShowFilePopup(false); break;
                case SDL::KEYCODE_S: _CmdSave();          break;
                case SDL::KEYCODE_B: _ToggleBold();       break;
                case SDL::KEYCODE_I: _ToggleItalic();     break;
                case SDL::KEYCODE_U: _ToggleUnderline();  break;
                case SDL::KEYCODE_K: _ToggleStrike();     break;
                case SDL::KEYCODE_H: if (shift) _ToggleHighlight(); break;
                case SDL::KEYCODE_A: _CmdSelectAll();     break;
                case SDL::KEYCODE_SPACE: _ClearFormatting(); break;
                default: break;
                }
            }
        }
        return SDL::APP_CONTINUE;
    }
};

SDL3PP_DEFINE_CALLBACKS(Main)
