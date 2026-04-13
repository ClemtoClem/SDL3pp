/*
 * Hexadecimal editor — rewritten with SDL3pp_ui.
 *
 * Layout:
 *   ┌──────────────────────────────────────────────────┐
 *   │  Toolbar: [Open] [Save] [Go to addr…] [Search…]  │
 *   ├──────────────────────────────────────────────────┤
 *   │                  HEX CANVAS                      │
 *   │  (address col | hex bytes | ASCII col | scrollbar│
 *   ├──────────────────────────────────────────────────┤
 *   │  Status bar: filename + cursor info              │
 *   └──────────────────────────────────────────────────┘
 *
 * The TextArea widget (SDL3pp_ui) is used for:
 *   • "Go to address" input      → single-line mode
 *   • "Search bytes" input       → single-line mode
 *   • Side-panel rich text note  → multi-line, bold/italic/color spans demo
 *
 * The hex/ascii view is rendered via a Canvas widget so the existing
 * low-level rendering code is preserved but now lives inside the UI tree.
 *
 * Demonstrates:
 *   - SDL3pp_ui layout (Column / Row / Canvas / Input / TextArea / Button / Label)
 *   - TextArea: selection, highlight color, copy/paste, drag-drop text
 *   - Rich text spans (bold, italic, color)
 *   - File open/save dialogs
 *   - Drop file/text
 */
#define SDL3PP_MAIN_USE_CALLBACKS 1
#include <SDL3pp/SDL3pp.h>
#include <SDL3pp/SDL3pp_ui.h>
#include <SDL3pp/SDL3pp_main.h>

using namespace std::literals;
using namespace SDL::UI;

// ──────────────────────────────────────────────────────────────────────────────
// Palette
// ──────────────────────────────────────────────────────────────────────────────
namespace pal {
  constexpr SDL::Color background    {30,  30,  38,  255};
  constexpr SDL::Color text          {255, 255, 180, 255};
  constexpr SDL::Color hex_text      {255, 255, 255, 197};
  constexpr SDL::Color placeholder   {128, 128, 128, 255};
  constexpr SDL::Color selected_bg   {0,   80,  200, 100};
  constexpr SDL::Color selected_text {255, 255, 180, 255};
  constexpr SDL::Color title_bg      {20,  20,  28,  255};
  constexpr SDL::Color title_text    {255, 255, 255, 255};
  constexpr SDL::Color cursor_color  {255, 255, 255, 255};
  constexpr SDL::Color mirror_cursor {80,  80,  80,  80};
  constexpr SDL::Color toolbar_bg    {26,  26,  36,  255};
  constexpr SDL::Color statusbar_bg  {18,  18,  26,  255};
  constexpr SDL::Color accent        {70,  130, 210, 255};
}

constexpr char         placeholder_ch{'.'}; // ASCII fallback glyph
constexpr char         hexDigits[]{"0123456789ABCDEF"};
constexpr SDL::Nanoseconds kCursorHalfPeriod = 500ms;

// ──────────────────────────────────────────────────────────────────────────────
// HexView — document + cursor state (plain data, no rendering deps)
// ──────────────────────────────────────────────────────────────────────────────
struct HexView {
    std::string  content{"Hello SDL3pp_ui!"};
    Uint64       address = 0;          // first visible row start address
    std::string  lastFilename;

    SDL::Point   cursor{0, 0};        // (col, row) within the visible screen
    bool         cursorOnRight = false;
    bool         cursorShown   = true;

    SDL::Nanoseconds cursorCounter{};
    SDL::Nanoseconds delta{};

    // layout cache (computed each frame from canvas size)
    float  charW  = 8.f, charH = 8.f;
    float  spaceW = 4.f;
    float  headerH = 0.f;
    float  addressAreaW = 0.f;
    float  hexAreaX     = 0.f,  hexAreaW = 0.f;
    float  asciiAreaX   = 0.f,  asciiAreaW = 0.f;
    float  scrollBarX   = 0.f,  scrollBarW = 0.f;
    Uint64 addressRowsCount = 1;
    int    rowsInScreen     = 1;
    int    addressDigits    = 4;
    bool   longAddressBar   = false;
    SDL::FRect barRect{};

    // scrollbar drag
    std::optional<SDL::FPoint> scrollDragStart;
    float scrollDragAddrAtStart = 0.f;

    // ── helpers ───────────────────────────────────────────────────────────────
    [[nodiscard]] Uint64 addressRow() const noexcept { return address >> 4; }
    [[nodiscard]] SDL::Point globalCursor() const noexcept {
        return {cursor.x, cursor.y + (int)addressRow()};
    }

    void loadFile(std::string path) {
        auto data = SDL::LoadFile(path);
        if (!data) return;
        address = 0; cursor = {0, 0};
        content.clear();
        content.reserve(data.size());
        std::ranges::copy(data, std::back_inserter(content));
        lastFilename = std::move(path);
    }
    void saveFile(std::string path) {
        try { SDL::SaveFile(path, content); lastFilename = std::move(path); }
        catch (std::exception &e) { SDL::Log("Save error: {}", e.what()); }
    }

    std::string formatByte(Uint8 b) const {
        return {hexDigits[b >> 4], hexDigits[b & 0xF]};
    }
    std::string formatAddress(Uint64 addr) const {
        int w = longAddressBar ? 15 : 7;
        char buf[16]; buf[w] = 0;
        for (int i = 0; i < w; ++i) {
            if (i < addressDigits) {
                buf[w - 1 - i] = hexDigits[addr % 16]; addr /= 16;
            } else buf[w - 1 - i] = ' ';
        }
        return buf;
    }

    void enterNibble(int nibble) {
        size_t idx = address + cursor.y * 16 + cursor.x;
        if (idx < content.size()) {
            char &ch = content[idx];
            if (cursorOnRight) { ch = (ch & 0xF0) | nibble; cursorOnRight = false; cursor.x++; }
            else               { ch = (ch & 0x0F) | (nibble << 4); cursorOnRight = true; }
        } else {
            content.push_back((char)(nibble << 4));
        }
    }

    // Update layout metrics for a given canvas size.
    void updateLayout(SDL::FPoint canvasSz) {
        addressDigits = 0;
        for (Uint64 a = content.size(); a; a >>= 4, ++addressDigits);
        longAddressBar = addressDigits > 8;
        if (addressDigits <= 4) { addressDigits = 3; longAddressBar = false; }
        else                   { if (!longAddressBar) --addressDigits; }

        float cols = (longAddressBar ? 16.f : 8.f) + 16.f * 5.f / 2.f + 16.f;
        charW = charH = canvasSz.x / cols;
        spaceW = charW / 2.f;

        addressAreaW = charW * (longAddressBar ? 15.f : 7.f) + spaceW;
        hexAreaX     = addressAreaW;
        hexAreaW     = 32.f * charW + 16.f * spaceW;
        asciiAreaX   = hexAreaX + hexAreaW;
        asciiAreaW   = charW * 16.f;
        scrollBarX   = asciiAreaX + asciiAreaW;
        scrollBarW   = spaceW;
        headerH      = charH + spaceW;

        rowsInScreen = SDL::Max(1, (int)((canvasSz.y - headerH) / charH));
        addressRowsCount = (content.size() >> 4) + 1;
    }

    // Clamp cursor within valid bounds.
    void clampCursor() {
        if (cursor.x < 0) {
            if (cursor.y > 0 || address > 0) { cursor.x = 15; cursor.y--; cursorOnRight = true; }
            else                              { cursor.x = 0; cursorOnRight = false; }
        } else if (cursor.x >= 16) {
            cursor.x = 0; cursor.y++; cursorOnRight = false;
        }
        if (cursor.y < 0) { cursor.y = 0; if (address >= 16) address -= 16; }
        else if (cursor.y >= rowsInScreen) { cursor.y = rowsInScreen - 1; address += 16; }

        if (address > content.size()) address = (addressRowsCount - 1) << 4;
        if (addressRow() + rowsInScreen > addressRowsCount)
            rowsInScreen = (int)(addressRowsCount - addressRow());
        if (rowsInScreen < 1) rowsInScreen = 1;
        if (cursor.y >= rowsInScreen) cursor.y = rowsInScreen - 1;

        if ((Uint64)globalCursor().y == addressRowsCount - 1) {
            Uint64 off = content.size() % 16;
            if (off > 0 && (Uint64)cursor.x >= off) {
                cursor.x = (int)off; cursorOnRight = false;
            }
        }
    }

    // Tick the cursor blink counter.
    void tickCursor(SDL::Nanoseconds dt) {
        cursorCounter += dt;
        if (cursorCounter > kCursorHalfPeriod) {
            cursorShown = !cursorShown;
            cursorCounter -= kCursorHalfPeriod;
        }
    }

    std::string statusLine(SDL::FPoint sz) const {
        Uint64 off = address + cursor.y * 16 + cursor.x;
        if (sz.x <= 0.f)
            return std::format("  Offset: {:06X}  |  File: {} bytes", off, content.size());
        return std::format("  Offset: {:06X}  |  File: {} bytes  |  {:.0f}x{:.0f}",
                           off, content.size(), sz.x, sz.y);
    }
};

// ──────────────────────────────────────────────────────────────────────────────
// Hex canvas renderer
// ──────────────────────────────────────────────────────────────────────────────
struct HexRenderer {
    SDL::Texture charTable;
    SDL::FPoint  sourceCharSz{ SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE,
                                SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE };

    void init(SDL::RendererRef r) {
        char ascii[127 - 32 + 1];
        for (int i = 32; i < 127; i++) ascii[i - 32] = (char)i;
        ascii[127 - 32] = 0;

        auto surf = SDL::Surface(SDL::Point{SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE * (127 - 32),
                                            SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE},
                                 SDL::PIXELFORMAT_RGBA32);
        SDL::Renderer buf{SDL::Renderer(surf)};
        buf.SetDrawColor(SDL::Color{});
        buf.RenderClear();
        buf.SetDrawColor(SDL::Color{255, 255, 255});
        buf.RenderDebugText({0, 0}, ascii);
        buf.Present();

        charTable = SDL::Texture{r, surf};
        charTable.SetScaleMode(SDL::SCALEMODE_NEAREST);
    }

    void setColor(SDL::RendererRef, SDL::Color c) { charTable.SetMod(c); }

    void putChar(SDL::RendererRef r, SDL::FPoint p, char ch, SDL::FPoint csz) {
        if (ch < 32 || ch >= 127) ch = '.';
        SDL::Point src{SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE * (ch - 32), 0};
        r.RenderTexture(charTable, SDL::FRect{src, sourceCharSz}, SDL::FRect{p, csz});
    }

    void putString(SDL::RendererRef r, SDL::FPoint p, std::string_view s, SDL::FPoint csz) {
        for (char ch : s) { putChar(r, p, ch, csz); p.x += csz.x; }
    }

    // Draw a complete hex view frame inside the given canvas rect.
    void draw(SDL::RendererRef r, SDL::FRect rect, HexView &v) {
        // Clear background
        r.SetDrawColor(pal::background);
        r.RenderFillRect(rect);

        if (!charTable) return;

        SDL::FPoint csz{v.charW, v.charH};
        const float ox = rect.x; // origin
        const float oy = rect.y;

        // ── Column header background ───────────────────────────────────────
        r.SetDrawColor(pal::title_bg);
        r.RenderFillRect(SDL::FRect{ox, oy, v.scrollBarX, v.headerH});
        // Address gutter background
        r.RenderFillRect(SDL::FRect{ox, oy + v.headerH, v.addressAreaW, rect.h - v.headerH});

        // ── Scrollbar ──────────────────────────────────────────────────────
        r.SetDrawColor(pal::placeholder);
        r.RenderFillRect(SDL::FRect{ox + v.scrollBarX, oy, v.scrollBarW, rect.h});
        v.barRect.x  = ox + v.scrollBarX;
        v.barRect.w  = v.scrollBarW - 1.f;
        v.barRect.h  = SDL::Clamp(rect.h * 2.f / float(v.addressRowsCount), 8.f, rect.h);
        v.barRect.y  = (v.addressRow() > 0)
                         ? oy + (rect.h - v.barRect.h) * float(v.addressRow()) /
                                float(v.addressRowsCount - 1)
                         : oy;
        r.SetDrawColor(pal::title_bg);
        r.RenderFillRect(v.barRect);

        // ── Column labels ──────────────────────────────────────────────────
        setColor(r, pal::placeholder);
        putString(r, {ox + v.hexAreaX,   oy + v.spaceW / 2.f}, "Go to",  csz);
        putString(r, {ox + v.asciiAreaX, oy + v.spaceW / 2.f}, "Search", csz);

        setColor(r, pal::title_text);
        SDL::FPoint hp{ox + v.hexAreaX + v.spaceW, oy + v.spaceW / 2.f};
        for (int i = 0; i < 16; i++, hp.x += v.charW * 2.f + v.spaceW)
            putChar(r, hp, hexDigits[i], csz);

        // ── Address column ─────────────────────────────────────────────────
        setColor(r, pal::title_text);
        SDL::FPoint ap{ox, oy + v.headerH};
        for (Uint64 row = v.address; row < v.content.size() && ap.y < oy + rect.h;
             row += 16, ap.y += v.charH)
            putString(r, ap, v.formatAddress(row >> 4), csz);

        // ── Mirror cursor (highlight current row/col) ──────────────────────
        r.SetDrawColor(pal::mirror_cursor);
        r.RenderFillRect(SDL::FRect{ox + v.hexAreaX,
                          oy + v.headerH + v.cursor.y * v.charH,
                          v.hexAreaW - v.spaceW / 2.f, v.charH});
        r.RenderFillRect(SDL::FRect{ox + v.hexAreaX + v.cursor.x * (v.charW * 2.f + v.spaceW),
                          oy + v.headerH, v.charW * 2.f + v.spaceW / 2.f, rect.h - v.headerH});
        r.RenderFillRect(SDL::FRect{ox + v.asciiAreaX + v.cursor.x * v.charW,
                          oy + v.headerH + v.cursor.y * v.charH, v.charW, v.charH});

        // ── Hex bytes ──────────────────────────────────────────────────────
        setColor(r, pal::hex_text);
        SDL::FPoint bp{0.f, oy + v.headerH};
        for (Uint64 row = v.address; row < v.content.size(); row += 16, bp.y += v.charH) {
            bp.x = ox + v.hexAreaX;
            Uint64 end = SDL::Min(row + 16, (Uint64)v.content.size());
            for (Uint64 c = row; c < end; c++, bp.x += v.charW * 2.f + v.spaceW)
                putString(r, bp, v.formatByte((Uint8)v.content[c]), csz);
            if (bp.y > oy + rect.h) break;
        }

        // ── ASCII ──────────────────────────────────────────────────────────
        SDL::FPoint ac{0.f, oy + v.headerH};
        for (Uint64 row = v.address; row < v.content.size(); row += 16, ac.y += v.charH) {
            ac.x = ox + v.asciiAreaX;
            Uint64 end = SDL::Min(row + 16, (Uint64)v.content.size());
            for (Uint64 c = row; c < end; c++, ac.x += v.charW) {
                char ch = v.content[c];
                if (ch < 32 || ch >= 127) { setColor(r, pal::placeholder); putChar(r, ac, placeholder_ch, csz); }
                else                      { setColor(r, pal::text);         putChar(r, ac, ch, csz); }
            }
            if (ac.y > oy + rect.h) break;
        }

        // ── Cursor caret ───────────────────────────────────────────────────
        if (v.cursorShown) {
            r.SetDrawColor(pal::cursor_color);
            SDL::FRect cr{
                ox + v.hexAreaX + v.cursor.x * (v.charW * 2.f + v.spaceW),
                oy + v.headerH  + v.cursor.y * v.charH + v.charH - 3.f,
                v.charW, 3.f
            };
            if (v.cursorOnRight) cr.x += v.charW;
            r.RenderFillRect(cr);
        }
    }
};

// ──────────────────────────────────────────────────────────────────────────────
// Main application
// ──────────────────────────────────────────────────────────────────────────────
struct Main {
    // ── SDL / UI infrastructure ───────────────────────────────────────────────
    SDL::AppResult Init(Main **m, SDL::AppArgs args) {
        SDL::SetAppMetadata("SDL3pp HexEdit", "2.0", "com.example.hexedit");
        SDL::Init(SDL::INIT_VIDEO);
        *m = new Main(args);
        return SDL::APP_CONTINUE;
    }

    SDL::Point           windowSz{1100, 760};
    SDL::Window          window{"SDL3pp HexEdit", windowSz};
    SDL::Renderer        renderer{window};
    SDL::ECS::World      world;
    SDL::ResourceManager rm;
    SDL::ResourcePool   &pool{*rm.CreatePool("ui")};
    SDL::UI::System      ui{world, renderer, SDL::MixerRef{}, pool};

    HexView     hex;
    HexRenderer hexRen;

    // UI entity handles kept for runtime updates
    SDL::ECS::EntityId eid_status  = SDL::ECS::NullEntity;
    SDL::ECS::EntityId eid_canvas  = SDL::ECS::NullEntity;
    SDL::ECS::EntityId eid_gotoInp = SDL::ECS::NullEntity;
    SDL::ECS::EntityId eid_notes   = SDL::ECS::NullEntity;

    SDL::FRect canvasRect{};      // updated each frame by the canvas callback
    SDL::FPoint canvasSz{};

    // Timing
    SDL::Nanoseconds lastTime{};

    Main(SDL::AppArgs args) {
        window.StartTextInput();

        const std::string basePath = std::string(SDL::GetBasePath()) + "../../../assets/";
        ui.LoadFont("DejaVuSans", basePath + "fonts/DejaVuSans.ttf");
        
        //ui.SetDefaultFont("DejaVuSans", 8.f);
        hexRen.init(renderer);
        if (args.size() > 1) hex.loadFile(std::string{args.back()});
        buildUI();
    }

    void buildUI() {
        // ── Toolbar ──────────────────────────────────────────────────────────
        auto toolbar = ui.Row("toolbar", 8.f, 6.f)
            .W(Value::Ww(100.f)).H(38.f)
            .BgColor(pal::toolbar_bg)
            .Borders({0,0,0,1}).BorderColor({50,52,70,255})
            .Children(
                ui.Button("btn_open", "Open").W(70).H(26)
                  .BgColor(pal::accent).Radius(SDL::FCorners{4.f})
                  .OnClick([this]{
                      SDL::ShowOpenFileDialog(
                          [this](auto files, int){ if (files && *files) hex.loadFile(*files); },
                          window, {}, hex.lastFilename);
                  }),
                ui.Button("btn_save", "Save").W(70).H(26)
                  .BgColor({50,140,90,255}).Radius(SDL::FCorners{4.f})
                  .OnClick([this]{
                      SDL::ShowSaveFileDialog(
                          [this](auto files, int){ if (files && *files) hex.saveFile(*files); },
                          window, {}, hex.lastFilename);
                  }),
                ui.Sep("tsep1").W(1).H(24).MarginV(4.f),
                ui.Input("goto_inp", "Go to address…").W(180).H(26)
                  .OnTextChange([this](const std::string &v){
                      Uint64 addr = 0;
                      try { addr = std::stoull(v, nullptr, 16); } catch (...) { return; }
                      addr &= ~0xFULL;
                      if (addr < hex.content.size()) { hex.address = addr; hex.cursor = {0,0}; }
                  }),
                ui.Sep("tsep2").W(1).H(24).MarginV(4.f),
                ui.Label("lbl_file", " ").Grow(1).TextColor({180,180,180,255})
            );
        eid_gotoInp = eid("goto_inp");

        // ── Centre: hex canvas + notes panel ──────────────────────────────
        auto hexCanvas = ui.CanvasWidget("hexcanvas",
            // Event callback: keyboard & mouse in hex area
            [this](SDL::Event &ev){ onHexCanvasEvent(ev); },
            nullptr,
            // Render callback
            [this](SDL::RendererRef r, SDL::FRect rect){
                canvasRect = rect;
                canvasSz   = {rect.w, rect.h};
                hex.updateLayout(canvasSz);
                hex.clampCursor();
                hexRen.draw(r, rect, hex);
            }
        ).Grow(1).W(SDL::UI::Value::Auto());

        eid_canvas = eid("hexcanvas");

        // Notes TextArea — demonstrates rich text API
        auto notes = ui.TextArea("notes",
            "Notes about this file.\n\n"
            "You can write multi-line notes here.\n"
            "  - Tabs work\n"
            "  - Bold / italic spans shown below\n"
            "  - Select text with mouse drag\n"
            "  - Ctrl+C / Ctrl+V / Ctrl+X\n"
            "  - Ctrl+A to select all\n",
            "Type your notes…")
            .W(260.f).Grow(1)
            .BgColor({22,22,32,255}).BorderColor({55,58,80,255})
            .Borders({1,0,0,0})
            .TextAreaHighlightColor({70,130,210,100})
            .TextAreaTabSize(2)
            .OnTextChange([this](const std::string &){
                // Reapply demo spans whenever text changes
                _applyDemoSpans();
            });
        eid_notes = eid("notes");

        // Apply demo rich-text spans once at startup
        _applyDemoSpans();

        auto notesLabel = ui.Label("notes_title", "Notes")
            .TextColor(pal::accent)
            .W(Value::Pw(100)).H(24)
            .Padding({8,4,8,4})
            .BgColor(pal::toolbar_bg)
            .Borders({0,0,0,1})
            .BorderColor({50,52,70,255});

        auto notePanel = ui.Column("notepanel", 0.f, 0.f)
            .W(260.f).Grow(1)
            .BgColor({22,22,32,255})
            .Children(notesLabel, notes);

        auto centre = ui.Row("centre", 0.f, 0.f)
            .W(Value::Ww(100.f)).Grow(1)
            .Children(hexCanvas, notePanel);

        // ── Status bar ────────────────────────────────────────────────────
        auto statusbar = ui.Row("statusbar", 0.f, 0.f)
            .W(Value::Ww(100.f)).H(22.f)
            .BgColor(pal::statusbar_bg)
            .Borders({0,1,0,0}).BorderColor({40,42,60,255})
            .Children(
                ui.Label("status_lbl", "  Ready").Grow(1)
                  .TextColor({160,165,185,255}).Padding({4,3,4,3})
            );
        eid_status = eid("status_lbl");

        // ── Root layout ───────────────────────────────────────────────────
        ui.Column("root", 0.f, 0.f)
          .W(Value::Ww(100.f)).H(Value::Wh(100.f))
          .BgColor(pal::background)
          .Padding(0).Gap(0)
          .Children(toolbar, centre, statusbar)
          .AsRoot();
    }

    // ── Frame pipeline ────────────────────────────────────────────────────────
    SDL::AppResult Iterate() {
        SDL::Nanoseconds now = SDL::GetTicks();
        hex.delta = now - lastTime;
        lastTime  = now;

        hex.tickCursor(hex.delta);

        // Update status label
        if (eid_status != SDL::ECS::NullEntity)
            ui.SetText(eid_status, hex.statusLine(canvasSz));

        renderer.SetDrawColor(pal::background);
        renderer.RenderClear();
        ui.Frame((float)hex.delta.count() * 1e-9f);
        renderer.Present();
        return SDL::APP_CONTINUE;
    }

    SDL::AppResult Event(const SDL::Event &ev) {
        ui.ProcessEvent(ev);
        switch (ev.type) {
        case SDL::EVENT_QUIT: return SDL::APP_SUCCESS;
        case SDL::EVENT_DROP_TEXT:
            // Drop text into hex content
            hex.content = ev.drop.data;
            hex.address = 0; hex.cursor = {0,0};
            break;
        case SDL::EVENT_DROP_FILE:
            hex.loadFile(ev.drop.data);
            break;
        case SDL::EVENT_WINDOW_RESIZED:
            windowSz = {ev.window.data1, ev.window.data2};
            break;
        default: break;
        }
        return SDL::APP_CONTINUE;
    }

    // ── Hex canvas keyboard/mouse events ─────────────────────────────────────
    void onHexCanvasEvent(SDL::Event &ev) {
        switch (ev.type) {
        case SDL::EVENT_KEY_DOWN:
            hexKeyDown(ev.key);
            break;
        case SDL::EVENT_MOUSE_BUTTON_DOWN:
            if (ev.button.button == SDL::BUTTON_LEFT) hexMouseDown(ev.button);
            break;
        case SDL::EVENT_MOUSE_BUTTON_UP:
            if (hex.scrollDragStart) hex.scrollDragStart.reset();
            break;
        case SDL::EVENT_MOUSE_MOTION:
            if (hex.scrollDragStart) hexScrollDrag(ev.motion);
            break;
        default: break;
        }
    }

    void hexKeyDown(const SDL::KeyboardEvent &ev) {
        bool ctrl  = ev.mod & SDL::KMOD_CTRL;
        bool shift = ev.mod & SDL::KMOD_SHIFT;
        switch (ev.key) {
        case SDL::KEYCODE_UP:
            if (shift && !ctrl) { if (hex.address > (Uint64)(hex.rowsInScreen<<4)) hex.address -= hex.rowsInScreen<<4; else hex.address = 0; }
            else if (ctrl) hex.cursor.y = 0;
            else           hex.cursor.y--;
            break;
        case SDL::KEYCODE_DOWN:
            if (shift && !ctrl) hex.address += hex.rowsInScreen << 4;
            else if (ctrl) hex.cursor.y = hex.rowsInScreen - 1;
            else           hex.cursor.y++;
            break;
        case SDL::KEYCODE_LEFT:
            if (ctrl) { hex.cursor.x = 0; hex.cursorOnRight = false; }
            else if (hex.cursorOnRight) hex.cursorOnRight = false;
            else { hex.cursor.x--; hex.cursorOnRight = true; }
            break;
        case SDL::KEYCODE_RIGHT:
            if (ctrl) { hex.cursor.x = 15; hex.cursorOnRight = true; }
            else if (!hex.cursorOnRight) hex.cursorOnRight = true;
            else { hex.cursorOnRight = false; hex.cursor.x++; }
            break;
        case SDL::KEYCODE_PAGEUP:
            if (!ctrl) { if (hex.address > (Uint64)(hex.rowsInScreen<<4)) hex.address -= hex.rowsInScreen<<4; else hex.address = 0; }
            break;
        case SDL::KEYCODE_PAGEDOWN:
            if (!ctrl) hex.address += hex.rowsInScreen << 4;
            break;
        case SDL::KEYCODE_HOME:
            hex.cursor = {0,0}; hex.cursorOnRight = false;
            if (ctrl) hex.address = 0;
            break;
        case SDL::KEYCODE_END:
            hex.cursor = {15, hex.rowsInScreen - 1}; hex.cursorOnRight = true;
            if (ctrl) hex.address = (hex.addressRowsCount - hex.rowsInScreen) << 4;
            break;
        case SDL::KEYCODE_S:
            if (ctrl) SDL::ShowSaveFileDialog([this](auto f, int){ if(f&&*f) hex.saveFile(*f); }, window, {}, hex.lastFilename);
            break;
        case SDL::KEYCODE_O:
            if (ctrl) SDL::ShowOpenFileDialog([this](auto f, int){ if(f&&*f) hex.loadFile(*f); }, window, {}, hex.lastFilename);
            break;
        default:
            if (ev.key >= SDL::KEYCODE_0 && ev.key <= SDL::KEYCODE_9) hex.enterNibble(ev.key - (int)SDL::KEYCODE_0);
            else if (ev.key >= SDL::KEYCODE_A && ev.key <= SDL::KEYCODE_F) hex.enterNibble(ev.key - (int)SDL::KEYCODE_A + 10);
            else if (ev.key == SDL::KEYCODE_KP_0) hex.enterNibble(0);
            else if (ev.key >= SDL::KEYCODE_KP_1 && ev.key <= SDL::KEYCODE_KP_9) hex.enterNibble(ev.key - (int)SDL::KEYCODE_KP_1 + 1);
            break;
        }
    }

    void hexMouseDown(const SDL::MouseButtonEvent &ev) {
        SDL::FPoint p{ev.x, ev.y};
        float sbX = canvasRect.x + hex.scrollBarX;
        if (p.x >= sbX && p.x < sbX + hex.scrollBarW) {
            if (p.InRect(hex.barRect)) {
                hex.scrollDragStart  = p;
                hex.scrollDragAddrAtStart = (float)hex.address;
            } else if (p.y > hex.barRect.y) {
                hex.address += hex.rowsInScreen << 4;
            } else if (hex.addressRow() >= (Uint64)hex.rowsInScreen) {
                hex.address -= hex.rowsInScreen << 4;
            } else {
                hex.address = 0;
            }
        }
    }

    void hexScrollDrag(const SDL::MouseMotionEvent &ev) {
        if (!hex.scrollDragStart) return;
        float offset = ((float)ev.y - hex.scrollDragStart->y) * hex.addressRowsCount / canvasSz.y;
        if (std::abs(offset) < 1.f) return;
        hex.scrollDragStart = {(float)ev.x, (float)ev.y};
        if (offset < 0) {
            Uint64 d = (Uint64)SDL::Ceil(-offset * 16.f);
            hex.address = (d > hex.address) ? 0 : hex.address - d;
        } else {
            Uint64 d = (Uint64)SDL::Ceil(offset * 16.f);
            hex.address = (hex.content.size() - hex.address <= d)
                              ? (hex.addressRowsCount - 1) << 4
                              : hex.address + d;
        }
    }

    // ── Helpers ───────────────────────────────────────────────────────────────

    SDL::ECS::EntityId eid(const std::string &name) {
        SDL::ECS::EntityId found = SDL::ECS::NullEntity;
        world.Each<Widget>([&](SDL::ECS::EntityId e, Widget &w){
            if (w.name == name) found = e;
        });
        return found;
    }

    void _applyDemoSpans() {
        if (eid_notes == SDL::ECS::NullEntity) return;
        auto *ta = ui.GetTextAreaData(eid_notes);
        if (!ta) return;
        ta->ClearSpans();
        // Colorize "Notes about this file." in accent blue
        const std::string &txt = ta->text;
        auto findSpan = [&](std::string_view what) -> std::pair<int,int> {
            auto pos = txt.find(what);
            if (pos == std::string::npos) return {-1,-1};
            return {(int)pos, (int)(pos + what.size())};
        };
        // Bold + accent color for "Notes about this file."
        {
            auto [s,e] = findSpan("Notes about this file.");
            if (s >= 0) ta->AddSpan(s, e, {.bold=true, .color={70,130,210,255}});
        }
        // Italic + green for "multi-line notes"
        {
            auto [s,e] = findSpan("multi-line notes");
            if (s >= 0) ta->AddSpan(s, e, {.italic=true, .color={80,200,110,255}});
        }
        // Orange for keyboard shortcuts
        for (std::string_view kw : {"Ctrl+C", "Ctrl+V", "Ctrl+X", "Ctrl+A"}) {
            auto [s,e] = findSpan(kw);
            if (s >= 0) ta->AddSpan(s, e, {.bold=true, .color={220,150,50,255}});
        }
    }
};

SDL3PP_DEFINE_CALLBACKS(Main)
