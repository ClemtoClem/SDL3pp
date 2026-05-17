/**
 * @file 10_file_explorer.cpp
 * @brief Advanced File Explorer — Windows/Ubuntu-style file browser built with SDL3pp_ui.
 *
 * Layout:
 *   ┌─────────────────────────────────────────────────────────────────────┐
 *   │  [<] [>] [^] [H] [R]  │ /current/path/...          │ [Search...]   │
 *   ├──────────────┬──────────────────────────────────────────────────────┤
 *   │  Quick       │  Name                           Size       Type      │
 *   │  Access      │  ─────────────────────────────────────────────────── │
 *   │  ─────────   │  > Documents                              Folder     │
 *   │  Home        │  > Downloads                              Folder     │
 *   │  Desktop     │    file.txt                       4.2 KB  Text       │
 *   │  Documents   │    photo.png                      2.1 MB  Image      │
 *   │  Downloads   │                                                       │
 *   │  Music       │                                                       │
 *   │  Pictures    │                                                       │
 *   │  Videos      │                                                       │
 *   │  Filesystem  │                                                       │
 *   │  /           │                                                       │
 *   ├──────────────┴──────────────────────────────────────────────────────┤
 *   │  42 items  —  file.txt (4.2 KB · Text · 2024-01-13 10:25)          │
 *   └─────────────────────────────────────────────────────────────────────┘
 *
 * Controls:
 *   - Click the address bar and press Enter to navigate to a typed path.
 *   - Click an item to select it (details appear in the status bar).
 *   - Double-click a folder to navigate into it.
 *   - Double-click a file to open it with the system default application.
 *   - Click a location in the sidebar to navigate there directly.
 *   - [<] / [>] : back / forward in history (also Alt+Left / Alt+Right).
 *   - [^]  : up one level (also Backspace when address bar is not focused).
 *   - [H]  : home directory.
 *   - [R] / F5 : reload current directory.
 *   - Search box: filter items by name (case-insensitive).
 *   - "Show hidden" toggle: include or exclude dotfiles.
 */

#define SDL3PP_MAIN_USE_CALLBACKS 1
#include <SDL3pp/SDL3pp.h>
#include <SDL3pp/SDL3pp_main.h>
#include <SDL3pp/SDL3pp_filesystem.h>
#include <SDL3pp/SDL3pp_ui.h>

#include <algorithm>
#include <cstring>
#include <ctime>
#include <format>
#include <string>
#include <string_view>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// Palette
// ─────────────────────────────────────────────────────────────────────────────

namespace pal {
    constexpr SDL::Color BG      = { 18,  20,  28, 255};
    constexpr SDL::Color PANEL   = { 22,  24,  34, 255};
    constexpr SDL::Color TOOLBAR = { 16,  18,  26, 255};
    constexpr SDL::Color CARD    = { 28,  30,  44, 255};
    constexpr SDL::Color HDR_ROW = { 16,  18,  26, 255};
    constexpr SDL::Color ACCENT  = { 58, 120, 210, 255};
    constexpr SDL::Color TEXT    = {215, 218, 228, 255};
    constexpr SDL::Color GREY    = {110, 120, 140, 255};
    constexpr SDL::Color BORDER  = { 40,  44,  62, 255};
    constexpr SDL::Color DIR_TXT = { 90, 150, 230, 255}; // folder name text
    constexpr SDL::Color WHITE   = {255, 255, 255, 255};
    constexpr SDL::Color SELECT  = { 52,  95, 185, 255}; // selection highlight
    constexpr SDL::Color FILL    = { 70, 130, 210, 255}; // list fill / thumb
}

// ─────────────────────────────────────────────────────────────────────────────
// File entry
// ─────────────────────────────────────────────────────────────────────────────

static std::string FileExt(std::string_view name) {
    auto pos = name.rfind('.');
    if (pos == std::string_view::npos || pos == 0) return {};
    std::string ext(name.substr(pos + 1));
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    return ext;
}

struct FileEntry {
    std::string name;
    std::string path;
    bool        isDir   = false;
    Uint64      size    = 0;
    time_t      modTime = 0;

    std::string SizeStr() const {
        if (isDir) return "";
        if (size < 1024)             return std::format("{} B",    size);
        if (size < 1024*1024)        return std::format("{:.1f} KB", size / 1024.f);
        if (size < 1024LL*1024*1024) return std::format("{:.1f} MB", size / (1024.f*1024.f));
        return std::format("{:.1f} GB", size / (1024.f*1024.f*1024.f));
    }

    std::string TypeStr() const {
        if (isDir) return "Folder";
        std::string e = FileExt(name);
        if (e.empty())                                                    return "File";
        if (e=="txt"||e=="md"||e=="rst"||e=="log")                        return "Text";
        if (e=="cpp"||e=="c"||e=="h"||e=="hpp"||e=="py"||e=="js"||
            e=="ts"||e=="rs"||e=="go"||e=="java"||e=="sh"||e=="lua")      return "Source";
        if (e=="png"||e=="jpg"||e=="jpeg"||e=="bmp"||
            e=="gif"||e=="svg"||e=="webp"||e=="ico")                      return "Image";
        if (e=="mp3"||e=="wav"||e=="ogg"||e=="flac"||e=="aac")            return "Audio";
        if (e=="mp4"||e=="avi"||e=="mkv"||e=="mov"||e=="webm")            return "Video";
        if (e=="pdf")                                                      return "PDF";
        if (e=="zip"||e=="tar"||e=="gz"||e=="xz"||
            e=="bz2"||e=="7z"||e=="rar")                                  return "Archive";
        if (e=="exe"||e=="so"||e=="dll"||e=="a"||e=="o")                  return "Binary";
        if (e=="json"||e=="xml"||e=="yaml"||e=="toml"||
            e=="ini"||e=="cfg"||e=="conf")                                return "Config";
        return e;
    }

    std::string DateStr() const {
        if (modTime == 0) return "";
        struct tm* lt = std::localtime(&modTime);
        char buf[20];
        std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", lt);
        return buf;
    }

    // Display string for the ListBox.
    // Name is padded so size+type roughly align regardless of proportional font.
    std::string ListStr() const {
        const std::string icon = isDir ? "> " : "  ";
        std::string n = name;
        if (n.size() > 46) n = n.substr(0, 43) + "...";
        while ((int)n.size() < 46) n += ' ';
        if (isDir) return icon + n + "Folder";
        std::string s = SizeStr();
        while ((int)s.size() < 10) s += ' ';
        return icon + n + s + "  " + TypeStr();
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Path helpers
// ─────────────────────────────────────────────────────────────────────────────

static std::string StripTrailingSlash(std::string path) {
    if (path.size() > 1 && path.back() == '/') path.pop_back();
    return path;
}

static std::string ParentPath(const std::string& path) {
    if (path == "/" || path.empty()) return "/";
    std::string p = StripTrailingSlash(path);
    auto pos = p.rfind('/');
    if (pos == std::string::npos || pos == 0) return "/";
    return p.substr(0, pos);
}

// ─────────────────────────────────────────────────────────────────────────────
// Main application
// ─────────────────────────────────────────────────────────────────────────────

struct Main {
    // ── SDL / UI ──────────────────────────────────────────────────────────────

    SDL::Window      window  {SDL::CreateWindowAndRenderer(
                                "SDL3pp — File Explorer", {1280, 800},
                                SDL_WINDOW_RESIZABLE, nullptr)};
    SDL::RendererRef renderer{window.GetRenderer()};

    SDL::ResourceManager   rm;
    SDL::ResourcePool&     pool{*rm.CreatePool("ui")};
    SDL::ECS::Context      ecs;
    SDL::UI::System        ui{ecs, renderer, {}, pool};
    SDL::FrameTimer        timer{60.f};

    // ── UI entity IDs ─────────────────────────────────────────────────────────

    SDL::ECS::EntityId id_btnBack     = SDL::ECS::NullEntity;
    SDL::ECS::EntityId id_btnFwd      = SDL::ECS::NullEntity;
    SDL::ECS::EntityId id_btnUp       = SDL::ECS::NullEntity;
    SDL::ECS::EntityId id_addrInput   = SDL::ECS::NullEntity;
    SDL::ECS::EntityId id_searchInput = SDL::ECS::NullEntity;
    SDL::ECS::EntityId id_sideTree    = SDL::ECS::NullEntity;
    SDL::ECS::EntityId id_fileList    = SDL::ECS::NullEntity;
    SDL::ECS::EntityId id_statusLabel = SDL::ECS::NullEntity;

    // ── Application state ─────────────────────────────────────────────────────

    std::string              m_currentPath;
    std::vector<FileEntry>   m_entries;
    std::vector<int>         m_filtered;
    std::vector<std::string> m_history;
    int                      m_histPos     = -1;
    std::string              m_searchFilter;
    bool                     m_showHidden  = false;
static SDL::AppResult Init(Main** out, SDL::AppArgs args) {
		SDL::LogPriority priority = SDL::LOG_PRIORITY_WARN;
		for (auto arg : args) {
			if      (arg == "--verbose") priority = SDL::LOG_PRIORITY_VERBOSE;
			else if (arg == "--debug")   priority = SDL::LOG_PRIORITY_DEBUG;
			else if (arg == "--info")    priority = SDL::LOG_PRIORITY_INFO;
		}
		SDL::SetLogPriorities(priority);
		SDL::SetAppMetadata("SDL3pp File Explorer", "1.0", "com.example.file_explorer");
		SDL::Init(SDL::INIT_VIDEO);
		SDL::TTF::Init();
		*out = new Main();
		return SDL::APP_CONTINUE;
	}

	static void Quit(Main* m, SDL::AppResult) {
		delete m;
		SDL::TTF::Quit();
		SDL::Quit();
	}

    // ── Constructor ───────────────────────────────────────────────────────────

    Main() {
        window.StartTextInput();

        // Load TTF font so all widgets render with it
        std::string assets = std::string(SDL::GetBasePath()) + "../../../assets/";
        ui.LoadFont("font", assets + "fonts/DejaVuSans.ttf");
        ui.SetDefaultFont("font", 13.f);

        // Start in the user's home directory
        const char* home = SDL::GetUserFolder(SDL::FOLDER_HOME);
        m_currentPath = home ? StripTrailingSlash(home) : "/";

        _BuildUI();
        _BuildSidebarLocations();
        _Navigate(m_currentPath);
        timer.Begin();
    }

    ~Main() { pool.Release(); }

    // ── SDL callbacks ─────────────────────────────────────────────────────────

    SDL::AppResult Iterate() {
        const float dt = timer.GetDelta();
        pool.Update();
        renderer.SetDrawColor(pal::BG);
        renderer.RenderClear();
        ui.Iterate(dt);
        renderer.Present();
        timer.End();
        timer.Begin();
        return SDL::APP_CONTINUE;
    }

    SDL::AppResult Event(const SDL::Event& ev) {
        if (ev.type == SDL::EVENT_QUIT) return SDL::APP_SUCCESS;
        if (ev.type == SDL::EVENT_KEY_DOWN) {
            auto key = ev.key.key;
            if (key == SDL::KEYCODE_ESCAPE) return SDL::APP_SUCCESS;
            if (key == SDL::KEYCODE_F5) { _Reload(); return SDL::APP_CONTINUE; }
            if ((key == SDL::KEYCODE_RETURN || key == SDL::KEYCODE_RETURN2)
                && ui.IsFocused(id_addrInput)) {
                _Navigate(ui.GetText(id_addrInput));
                return SDL::APP_CONTINUE;
            }
            if (key == SDL::KEYCODE_BACKSPACE
                && !ui.IsFocused(id_addrInput)
                && !ui.IsFocused(id_searchInput)) {
                _GoUp();
                return SDL::APP_CONTINUE;
            }
            if (key == SDL::KEYCODE_LEFT  && (ev.key.mod & SDL::KMOD_ALT)) { _GoBack();    return SDL::APP_CONTINUE; }
            if (key == SDL::KEYCODE_RIGHT && (ev.key.mod & SDL::KMOD_ALT)) { _GoForward(); return SDL::APP_CONTINUE; }
        }
        ui.ProcessEvent(ev);
        return SDL::APP_CONTINUE;
    }

    // ── Navigation ────────────────────────────────────────────────────────────

    void _Navigate(const std::string& rawPath, bool pushHistory = true) {
        std::string path = StripTrailingSlash(rawPath.empty() ? "/" : rawPath);
        try {
            SDL::PathInfo info = SDL::GetPathInfo(path);
            if (info.type != SDL::PATHTYPE_DIRECTORY) return;
        } catch (...) { return; }

        if (pushHistory) {
            if (m_histPos < (int)m_history.size() - 1)
                m_history.erase(m_history.begin() + m_histPos + 1, m_history.end());
            m_history.push_back(path);
            m_histPos = (int)m_history.size() - 1;
        }
        m_currentPath = path;
        _LoadDirectory(path);
        ui.SetText(id_addrInput, path);
        _UpdateNavButtons();
    }

    void _GoBack()    { if (m_histPos > 0)                                { --m_histPos; _Navigate(m_history[m_histPos], false); } }
    void _GoForward() { if (m_histPos < (int)m_history.size() - 1)        { ++m_histPos; _Navigate(m_history[m_histPos], false); } }
    void _GoUp()      { std::string p = ParentPath(m_currentPath); if (p != m_currentPath) _Navigate(p); }

    void _GoHome() {
        const char* home = SDL::GetUserFolder(SDL::FOLDER_HOME);
        if (home) _Navigate(StripTrailingSlash(home));
    }

    void _Reload() { _Navigate(m_currentPath, false); }

    void _UpdateNavButtons() {
        ui.SetEnable(id_btnBack, m_histPos > 0);
        ui.SetEnable(id_btnFwd,  m_histPos < (int)m_history.size() - 1);
        ui.SetEnable(id_btnUp,   ParentPath(m_currentPath) != m_currentPath);
    }

    // ── Directory loading ─────────────────────────────────────────────────────

    void _LoadDirectory(const std::string& path) {
        m_entries.clear();
        try {
            SDL::EnumerateDirectory(path, [&](const char* dir, const char* fname) {
                if (!m_showHidden && fname[0] == '.') return SDL::ENUM_CONTINUE;
                FileEntry e;
                e.name = fname;
                e.path = SDL::JoinPath(dir, fname);
                try {
                    SDL::PathInfo info = SDL::GetPathInfo(e.path);
                    e.isDir   = (info.type == SDL::PATHTYPE_DIRECTORY);
                    e.size    = info.size;
                    e.modTime = (time_t)(info.modify_time / 1000000000LL);
                } catch (...) {}
                m_entries.push_back(std::move(e));
                return SDL::ENUM_CONTINUE;
            });
        } catch (...) {}

        std::sort(m_entries.begin(), m_entries.end(), [](const FileEntry& a, const FileEntry& b) {
            if (a.isDir != b.isDir) return a.isDir > b.isDir;
            std::string an = a.name, bn = b.name;
            std::transform(an.begin(), an.end(), an.begin(), ::tolower);
            std::transform(bn.begin(), bn.end(), bn.begin(), ::tolower);
            return an < bn;
        });
        _ApplyFilter();
    }

    void _ApplyFilter() {
        m_filtered.clear();
        std::string f = m_searchFilter;
        std::transform(f.begin(), f.end(), f.begin(), ::tolower);
        for (int i = 0; i < (int)m_entries.size(); ++i) {
            if (f.empty()) {
                m_filtered.push_back(i);
            } else {
                std::string n = m_entries[i].name;
                std::transform(n.begin(), n.end(), n.begin(), ::tolower);
                if (n.find(f) != std::string::npos) m_filtered.push_back(i);
            }
        }
        _RefreshFileList();
    }

    // Build display strings + per-item styles and push them to the ListBox
    void _RefreshFileList() {
        std::vector<std::string>              items;
        std::vector<SDL::UI::ListBoxItemStyle> styles;
        items.reserve(m_filtered.size());
        styles.reserve(m_filtered.size());

        for (int idx : m_filtered) {
            const FileEntry& e = m_entries[idx];
            items.push_back(e.ListStr());

            SDL::UI::ListBoxItemStyle st;
            if (e.isDir) {
                // Directories: blue text, slightly tinted background
                st.textColor = pal::DIR_TXT;
                st.bgColor   = {30, 40, 64, 60}; // subtle blue tint
            }
            // Files keep default (alpha=0 → use widget Style)
            styles.push_back(st);
        }

        ui.SetListBoxItems(id_fileList, std::move(items), std::move(styles));
        _UpdateStatus();
    }

    void _UpdateStatus() {
        int n   = (int)m_filtered.size();
        std::string s = std::format("{} item{}", n, n != 1 ? "s" : "");
        int sel = ui.GetListBoxSelection(id_fileList);
        if (sel >= 0 && sel < n) {
            auto& e = m_entries[m_filtered[sel]];
            s += "  —  " + e.name;
            if (!e.isDir) s += "  (" + e.SizeStr() + "  ·  " + e.TypeStr() + ")";
            if (!e.DateStr().empty()) s += "  ·  " + e.DateStr();
        }
        ui.SetText(id_statusLabel, s);
    }

    void _ActivateSelected() {
        int sel = ui.GetListBoxSelection(id_fileList);
        if (sel < 0 || sel >= (int)m_filtered.size()) return;
        auto& e = m_entries[m_filtered[sel]];
        if (e.isDir)
            _Navigate(e.path);
        else
            system(("xdg-open \"" + e.path + "\" 2>/dev/null &").c_str());
    }

    // ── Sidebar Quick Access locations ────────────────────────────────────────

    void _BuildSidebarLocations() {
        ui.ClearTreeNodes(id_sideTree);

        ui.AddTreeNode(id_sideTree, {"Quick Access", "", 0, false, false});

        struct Place { const char* label; SDL::Folder folder; };
        static constexpr Place kPlaces[] = {
            {"Home",      SDL::FOLDER_HOME},
            {"Desktop",   SDL::FOLDER_DESKTOP},
            {"Documents", SDL::FOLDER_DOCUMENTS},
            {"Downloads", SDL::FOLDER_DOWNLOADS},
            {"Music",     SDL::FOLDER_MUSIC},
            {"Pictures",  SDL::FOLDER_PICTURES},
            {"Videos",    SDL::FOLDER_VIDEOS},
        };
        for (auto& p : kPlaces) {
            const char* path = SDL::GetUserFolder(p.folder);
            if (!path) continue;
            ui.AddTreeNode(id_sideTree, {p.label, path, 1, false, false});
        }

        ui.AddTreeNode(id_sideTree, {"Filesystem", "", 0, false, false});
        ui.AddTreeNode(id_sideTree, {"/",          "/", 1, false, false});

        ui.MarkLayoutDirty();
    }

    // ── UI construction ───────────────────────────────────────────────────────

    void _BuildUI() {
        ui.Column("root")
            .BgColor(pal::BG)
            .Borders(SDL::FBox(0.f))
            .Children(
                _BuildToolbar(),
                _BuildBody(),
                _BuildStatusBar()
            )
            .AsRoot();
    }

    SDL::ECS::EntityId _BuildToolbar() {
        auto row = ui.Row("toolbar", 4.f, 0.f)
            .W(SDL::UI::Value::Ww(100.f)).H(44.f)
            .PaddingH(8.f).PaddingV(7.f)
            .BgColor(pal::TOOLBAR)
            .Borders(SDL::FBox(0.f, 0.f, 0.f, 1.f)).BdColor(pal::BORDER);

        id_btnBack = ui.Button("btn_back", "<")
            .W(30.f).H(30.f).BgColor(pal::CARD)
            .Tooltip("Back (Alt+Left)")
            .OnClick([this]{ _GoBack(); });
        row.Child(id_btnBack);

        id_btnFwd = ui.Button("btn_fwd", ">")
            .W(30.f).H(30.f).BgColor(pal::CARD)
            .Tooltip("Forward (Alt+Right)")
            .OnClick([this]{ _GoForward(); });
        row.Child(id_btnFwd);

        id_btnUp = ui.Button("btn_up", "^")
            .W(30.f).H(30.f).BgColor(pal::CARD)
            .Tooltip("Up one level (Backspace)")
            .OnClick([this]{ _GoUp(); });
        row.Child(id_btnUp);

        row.Child(ui.Button("btn_home", "H")
            .W(30.f).H(30.f).BgColor(pal::CARD)
            .Tooltip("Home directory")
            .OnClick([this]{ _GoHome(); }));

        row.Child(ui.Button("btn_reload", "R")
            .W(30.f).H(30.f).BgColor(pal::CARD)
            .Tooltip("Reload (F5)")
            .OnClick([this]{ _Reload(); }));

        row.Child(ui.Separator("sep_addr")
            .W(1.f).H(20.f)
            .WithStyle([](SDL::UI::Style& s){ s.separatorColor = {50,54,74,255}; }));

        id_addrInput = ui.Input("addr_input", "")
            .W(SDL::UI::Value::Grow(100.f)).H(30.f)
            .BgColor(pal::CARD);
        row.Child(id_addrInput);

        row.Child(ui.Separator("sep_search")
            .W(1.f).H(20.f)
            .WithStyle([](SDL::UI::Style& s){ s.separatorColor = {50,54,74,255}; }));

        id_searchInput = ui.Input("search_input", "Search...")
            .W(180.f).H(30.f)
            .BgColor(pal::CARD)
            .OnTextChange([this](const std::string& t) {
                m_searchFilter = t;
                _ApplyFilter();
            });
        row.Child(id_searchInput);

        return row;
    }

    SDL::ECS::EntityId _BuildBody() {
        auto body = ui.Row("body", 0.f, 0.f)
            .W(SDL::UI::Value::Ww(100.f))
            .GrowH(1.f)
            .BgColor(pal::BG)
            .Borders(SDL::FBox(0.f));

        body.Child(_BuildSidebar());
        body.Child(ui.Separator("body_sep")
            .W(1.f).GrowH(1.f)
            .WithStyle([](SDL::UI::Style& s){ s.separatorColor = {40,44,62,255}; }));
        body.Child(_BuildContent());
        return body;
    }

    SDL::ECS::EntityId _BuildSidebar() {
        auto panel = ui.Column("sidebar", 0.f, 0.f)
            .W(190.f).GrowH(1.f)
            .BgColor(pal::PANEL)
            .Borders(SDL::FBox(0.f));

        id_sideTree = ui.Tree("side_tree")
            .W(SDL::UI::Value::Pw(100.f)).GrowH(1.f)
            .BgColor(pal::PANEL)
            .Borders(SDL::FBox(0.f))
            .TreeItemHeight(26.f)
            .TreeIndent(12.f)
            .OnTreeSelect([this](int idx, bool) {
                auto* td = ui.GetTreeData(id_sideTree);
                if (!td || idx < 0 || idx >= (int)td->nodes.size()) return;
                const std::string& p = td->nodes[idx].iconKey;
                if (!p.empty()) _Navigate(StripTrailingSlash(p));
            });
        panel.Child(id_sideTree);
        return panel;
    }

    SDL::ECS::EntityId _BuildContent() {
        auto col = ui.Column("content", 0.f, 0.f)
            .GrowW(1.f).GrowH(1.f)
            .BgColor(pal::BG)
            .Borders(SDL::FBox(0.f));

        // Column header row (decorative labels)
        col.Child(_BuildColumnHeaders());

        // File list — ListBox: TTF font, native scroll, per-item text/bg colors
        id_fileList = ui.ListBoxWidget("file_list", {})
            .GrowW(1.f).GrowH(1.f)
            .BgColor(pal::BG)
            .BgHoveredColor(pal::BG)
            .BgCheckedColor(pal::SELECT)
            .TextColor(pal::TEXT)
            .FillColor(pal::FILL)
            .Borders(SDL::FBox(0.f, 1.f, 0.f, 0.f)).BdColor(pal::BORDER)
            .WithLayout([](SDL::UI::LayoutProps& lp) {
                lp.padding = {4.f, 2.f, 4.f, 2.f};
            })
            .OnClick([this] { _UpdateStatus(); })
            .OnDoubleClick([this] { _ActivateSelected(); });
        col.Child(id_fileList);

        return col;
    }

    SDL::ECS::EntityId _BuildColumnHeaders() {
        auto hdr = ui.Row("col_hdr", 0.f, 0.f)
            .GrowW(1.f).H(26.f)
            .PaddingH(8.f).PaddingV(4.f)
            .BgColor(pal::HDR_ROW)
            .Borders(SDL::FBox(0.f, 0.f, 0.f, 1.f)).BdColor(pal::BORDER);

        hdr.Child(ui.Label("hdr_name", "Name")
            .GrowW(1.f).TextColor(pal::GREY));
        hdr.Child(ui.Label("hdr_size", "Size")
            .W(90.f).TextColor(pal::GREY));
        hdr.Child(ui.Label("hdr_type", "Type")
            .W(70.f).TextColor(pal::GREY));

        return hdr;
    }

    SDL::ECS::EntityId _BuildStatusBar() {
        auto bar = ui.Row("statusbar", 8.f, 0.f)
            .W(SDL::UI::Value::Ww(100.f)).H(28.f)
            .PaddingH(10.f).PaddingV(5.f)
            .BgColor(pal::TOOLBAR)
            .Borders(SDL::FBox(1.f, 0.f, 0.f, 0.f)).BdColor(pal::BORDER);

        id_statusLabel = ui.Label("status_lbl", "")
            .GrowW(1.f)
            .TextColor(pal::GREY);
        bar.Child(id_statusLabel);

        bar.Child(ui.Separator("sb_sep")
            .W(1.f).H(16.f)
            .WithStyle([](SDL::UI::Style& s){ s.separatorColor = {50,54,74,255}; }));

        bar.Child(ui.Toggle("toggle_hidden", "Show hidden")
            .H(18.f).TextColor(pal::GREY)
            .OnToggle([this](bool v) {
                m_showHidden = v;
                _Reload();
            }));

        return bar;
    }
};

SDL3PP_DEFINE_CALLBACKS(Main)
