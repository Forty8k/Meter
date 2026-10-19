// meterDARK Analyser: the LIBRARY OVERVIEW (included by MainGui.cpp, inside its anonymous namespace).
// The top ~40% is the selected track: its name, a large overall tone bar with the grade and the overlay lines (peak, bass position, slew), separate weight /
// peaks / slew tone bars, the technical details, and play / stop with a playhead you can click to seek. The bottom ~60% is a three-column list: name, a small
// tone strip, and a sortable grade. Stacked together the strips show trends across the library. It reads only the lean records (see LeanRecord.h); the audio is
// opened from its stored file path just for playing. The full table stays one click away.

class GlanceView : public juce::Component, public juce::TableListBoxModel, private juce::Timer {
public:
    struct Item { juce::String id, name, grade, path, song, source; double key = 0, dur = 0, sr = 0; int group = -1, groupSize = 1; bool best = false, shared = false; f8k::LeanRecord rec; };
    std::function<std::shared_ptr<f8k::Library>()> getLibrary;
    std::function<std::shared_ptr<const std::set<juce::String>>()> getAllowed;
    std::function<int()> getFilterGen;

    GlanceView() {
        formats.registerBasicFormats();
        addAndMakeVisible(table);
        table.setModel(this);
        table.setColour(juce::ListBox::backgroundColourId, kBg);
        table.setRowHeight(20);
        table.getHeader().setColour(juce::TableHeaderComponent::backgroundColourId, kPanel);
        table.getHeader().setColour(juce::TableHeaderComponent::textColourId, kText);
        table.getHeader().setColour(juce::TableHeaderComponent::outlineColourId, kBg);
        table.getHeader().addColumn("Track", 1, 420, 120, 2000, juce::TableHeaderComponent::defaultFlags);
        table.getHeader().addColumn("Tone", 2, 320, 80, 1200, juce::TableHeaderComponent::notSortable);
        table.getHeader().addColumn("Grade", 3, 110, 60, 300, juce::TableHeaderComponent::defaultFlags);
        table.getHeader().setSortColumnId(3, true);
        for (auto* b : {&playButton, &linesButton, &buildButton, &compareButton, &shareButton}) {addAndMakeVisible(b); styleButton(*b);}
        shareButton.onClick = [this] { shareMenu(); };
        compareButton.setClickingTogglesState(true); compareButton.setColour(juce::TextButton::buttonOnColourId, kSelect);
        compareButton.onClick = [this] { compareMode = compareButton.getToggleState(); repaint(); };
        addAndMakeVisible(sourceBox);
        sourceBox.setTextWhenNothingSelected("audio source");
        for (int i = 0; i < 8; ++i) sourceBox.addItem(kSources[i], i + 1);
        sourceBox.setColour(juce::ComboBox::backgroundColourId, kPanel); sourceBox.setColour(juce::ComboBox::textColourId, kText); sourceBox.setColour(juce::ComboBox::outlineColourId, kPanel);
        sourceBox.onChange = [this] { setSourceOfSelected(sourceBox.getText()); };
        linesButton.setClickingTogglesState(true); linesButton.setToggleState(true, juce::dontSendNotification);
        linesButton.setColour(juce::TextButton::buttonOnColourId, kSelect);
        linesButton.onClick = [this] { repaint(); };
        playButton.onClick = [this] { togglePlay(); };
        buildButton.onClick = [this] { buildRecords(); };
        addAndMakeVisible(status);
        status.setColour(juce::Label::textColourId, kDim);
        startTimer(250);
    }
    ~GlanceView() override {
        stopTimer();
        if (devices != nullptr) { devices->removeAudioCallback(&player); }
        player.setSource(nullptr); transport.setSource(nullptr); readAhead.stopThread(2000);
        pool.removeAllJobs(true, 10000);
    }

    void reloadNow() { //synchronous (for the automated picture)
        auto lib = getLibrary ? getLibrary() : nullptr;
        if (lib == nullptr) return;
        loadedFor = lib.get(); loadedChange = lib->changeCount();
        items = gather(lib, getAllowed ? getAllowed() : nullptr); loaded = true;
        status.setText(juce::String((int)items.size()) + " tracks", juce::dontSendNotification);
        resort(); repaint();
    }

    void resized() override {
        auto r = getLocalBounds();
        auto bar = r.removeFromTop(30).reduced(8, 3);
        playButton.setBounds(bar.removeFromLeft(80)); bar.removeFromLeft(6);
        linesButton.setBounds(bar.removeFromLeft(110)); bar.removeFromLeft(6);
        buildButton.setBounds(bar.removeFromLeft(170)); bar.removeFromLeft(6);
        compareButton.setBounds(bar.removeFromLeft(130)); bar.removeFromLeft(6);
        sourceBox.setBounds(bar.removeFromLeft(130)); bar.removeFromLeft(6);
        shareButton.setBounds(bar.removeFromLeft(120)); bar.removeFromLeft(10);
        status.setBounds(bar.withTrimmedRight(146));
        const int topH = juce::jmax(180, (int)(r.getHeight()*0.40f));
        topArea = r.removeFromTop(topH);
        table.setBounds(r);
    }

    void paint(juce::Graphics& g) override {
        g.fillAll(kBg);
        g.setColour(kPanel); g.fillRect(topArea.reduced(8, 2));
        const Item* it = current();
        auto a = topArea.reduced(18, 8);
        if (it == nullptr) {
            g.setColour(kDim); g.setFont(juce::Font(15.0f));
            g.drawText(items.empty() ? "No lean records yet. Press \"Build records\" (it reads the window data already in your library, not the audio)."
                                     : "Select a track in the list below.", a, juce::Justification::centred);
            return;
        }
        if (compareMode && it->group >= 0) { paintCompare(g, a, *it); return; }
        const auto& rec = it->rec;
        g.setColour(kText); g.setFont(juce::Font(20.0f, juce::Font::bold));
        auto head = a.removeFromTop(28);
        g.drawText(it->name, head.withTrimmedRight(220), juce::Justification::centredLeft, true);
        g.setColour(kAccent); g.setFont(juce::Font(26.0f, juce::Font::bold));
        g.drawText(it->grade, head.removeFromRight(210), juce::Justification::centredRight);
        // the large overall tone bar
        const int barH = juce::jmax(40, (int)(a.getHeight()*0.34f));
        bigBar = a.removeFromTop(barH);
        drawBar(g, bigBar, rec, 0);
        if (linesButton.getToggleState()) drawLines(g, bigBar, rec);
        if (transportReady) { //playhead
            const double len = transport.getLengthInSeconds();
            if (len > 0) { const float x = (float)bigBar.getX() + (float)(transport.getCurrentPosition()/len)*(float)bigBar.getWidth(); g.setColour(juce::Colours::white); g.fillRect(x - 1.0f, (float)bigBar.getY() - 3.0f, 2.0f, (float)bigBar.getHeight() + 6.0f); }
        }
        a.removeFromTop(6);
        // the three separate meters
        const char* names[3] = {"weight", "peaks", "slew"};
        const int smallH = 15;
        for (int k = 1; k <= 3; ++k) {
            auto row = a.removeFromTop(smallH);
            g.setColour(kDim); g.setFont(juce::Font(12.0f));
            g.drawText(names[k - 1], row.removeFromLeft(54), juce::Justification::centredLeft);
            drawBar(g, row.withTrimmedLeft(0).withWidth(bigBar.getWidth() - 54).withX(bigBar.getX() + 54), rec, k);
            a.removeFromTop(3);
        }
        // technical details
        juce::StringArray d;
        auto num = [&](const char* k, const char* fmt) { const juce::String v = juce::String(rec.get(k)); return v.isEmpty() ? juce::String("-") : juce::String::formatted(fmt, v.getDoubleValue()); };
        const juce::String bits = juce::String(rec.get("bits")), src = juce::String(rec.get("source"));
        const int secs = juce::roundToInt(juce::String(rec.get("length_s")).getDoubleValue());
        d.add("Length " + juce::String(secs/60) + ":" + juce::String::formatted("%02d", secs % 60));
        d.add(num("sample_rate", "%.0f Hz") + " / " + (bits.isEmpty() ? juce::String("-") : bits + " bit"));
        d.add("Peak " + num("peak_db", "%.1f dBFS")); d.add("True peak " + num("true_peak_db", "%.1f dBTP")); d.add("LUFS " + num("lufs", "%.1f"));
        d.add("Meter " + juce::String(rec.get("meter_version")));
        d.add("Source " + (src.isEmpty() ? juce::String("not set") : src));
        g.setColour(kText); g.setFont(juce::Font(13.0f));
        g.drawFittedText(d.joinIntoString("      "), a.removeFromTop(34), juce::Justification::topLeft, 2);
    }

    //COMPARE VERSIONS: every copy of the song, one under the other, each with its source, grade and levels, so a vinyl original and a remaster can be read against each other
    void paintCompare(juce::Graphics& g, juce::Rectangle<int> a, const Item& sel) {
        std::vector<const Item*> v;
        for (auto& i : items) if (i.group == sel.group) v.push_back(&i);
        std::sort(v.begin(), v.end(), [](const Item* x, const Item* y) { return x->key < y->key; });
        if (v.size() > 6) v.resize(6);
        g.setColour(kText); g.setFont(juce::Font(16.0f, juce::Font::bold));
        g.drawText(sel.name + "  -  " + juce::String((int)v.size()) + " versions, best grade first", a.removeFromTop(22), juce::Justification::centredLeft, true);
        const int rowH = juce::jmax(18, a.getHeight()/(int)juce::jmax<size_t>(1, v.size()));
        for (auto* p : v) {
            auto row = a.removeFromTop(rowH);
            auto label = row.removeFromLeft(300).reduced(0, 1);
            const bool isSel = p->id == sel.id;
            g.setColour(isSel ? kAccent : kText); g.setFont(juce::Font(13.0f, isSel ? juce::Font::bold : juce::Font::plain));
            g.drawText((p->source.isEmpty() ? juce::String("source not set") : p->source) + "   " + p->grade + (p->best ? "  best" : ""), label.removeFromTop(rowH/2 + 1), juce::Justification::centredLeft, true);
            g.setColour(kDim); g.setFont(juce::Font(12.0f));
            g.drawText(juce::String::formatted("%.0f Hz   peak %.1f   LUFS %.1f", p->sr, juce::String(p->rec.get("peak_db")).getDoubleValue(), juce::String(p->rec.get("lufs")).getDoubleValue()), label, juce::Justification::centredLeft, true);
            drawBar(g, row.reduced(4, 2), p->rec, 0);
        }
    }

    void mouseDown(const juce::MouseEvent& e) override {
        if (bigBar.expanded(0, 6).contains(e.getPosition()) && transportReady) {
            const double len = transport.getLengthInSeconds();
            transport.setPosition(len*juce::jlimit(0.0, 1.0, (double)(e.x - bigBar.getX())/(double)juce::jmax(1, bigBar.getWidth())));
            repaint();
        }
    }
    void mouseDrag(const juce::MouseEvent& e) override { mouseDown(e); }

    //---------------------------------------------------------------- list
    int getNumRows() override { return (int)order.size(); }
    void paintRowBackground(juce::Graphics& g, int row, int, int, bool sel) override { g.fillAll(sel ? kSelect : (row % 2 ? kRowAlt : kBg)); }
    void paintCell(juce::Graphics& g, int row, int col, int w, int h, bool) override {
        if (row < 0 || row >= (int)order.size()) return;
        const Item& it = items[order[(size_t)row]];
        if (col == 2) { drawBar(g, juce::Rectangle<int>(4, 3, w - 8, h - 6), it.rec, 0); return; }
        g.setColour(kText); g.setFont(juce::Font(14.0f));
        juce::String t = col == 1 ? it.name : it.grade;
        if (col == 1 && it.groupSize > 1) t << "   (" << it.groupSize << " versions" << (it.source.isNotEmpty() ? ", " + it.source : juce::String()) << ")";
        if (col == 3 && it.best) t << "  best";
        g.setColour(col == 3 && it.best ? kAccent : kText);
        g.drawText(t, 6, 0, w - 12, h, juce::Justification::centredLeft, true);
    }
    void sortOrderChanged(int col, bool fwd) override { sortCol = col; sortFwd = fwd; resort(); }
    void selectedRowsChanged(int row) override {
        if (row < 0 || row >= (int)order.size()) return;
        selectedId = items[order[(size_t)row]].id;
        stopPlayback();
        if (const Item* it = current()) { sourceBox.setText(it->source, juce::dontSendNotification); compareButton.setEnabled(it->groupSize > 1); if (it->groupSize < 2) {compareMode = false; compareButton.setToggleState(false, juce::dontSendNotification);} }
        repaint();
    }

private:
    std::vector<Item> items; std::vector<size_t> order;
    bool loaded = false, loading = false, building = false; const void* loadedFor = nullptr; int loadedChange = -1, seenGen = -1;
    juce::String selectedId; int sortCol = 3; bool sortFwd = true;
    juce::Rectangle<int> topArea, bigBar;
    juce::TableListBox table;
    juce::TextButton shareButton{"Share packs..."};
    std::unique_ptr<juce::FileChooser> chooser;
    juce::TextButton playButton{"Play"}, linesButton{"Overlay lines"}, buildButton{"Build / update records"}, compareButton{"Compare versions"};
    juce::ComboBox sourceBox; bool compareMode = false;
    static constexpr const char* kSources[8] = {"Tidal", "Spotify", "Apple Music", "Vinyl", "CD", "HDtracks", "Qobuz", "Other"};
    void shareMenu() {
        auto lib = getLibrary ? getLibrary() : nullptr;
        if (lib == nullptr) return;
        juce::PopupMenu m;
        m.addItem(1, "Export a display pack (small: bars, grades, tags)...");
        m.addItem(2, "Export a full pack (adds the replay blocks so others can regrade)...");
        m.addItem(3, "Import a pack...");
        m.showMenuAsync(juce::PopupMenu::Options(), [this, lib](int r) {
            if (r == 0) return;
            if (r == 3) {
                chooser = std::make_unique<juce::FileChooser>("Choose a meterDARK pack", juce::File(), "*.f8kpack");
                chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this, lib](const juce::FileChooser& fc) {
                    if (fc.getResult() == juce::File()) return;
                    status.setText(lib->importPack(fc.getResult()), juce::dontSendNotification); loaded = false;
                });
                return;
            }
            chooser = std::make_unique<juce::FileChooser>("Save the pack", juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile(r == 1 ? "meterDARK-display.f8kpack" : "meterDARK-full.f8kpack"), "*.f8kpack");
            chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting, [this, lib, r](const juce::FileChooser& fc) {
                if (fc.getResult() == juce::File()) return;
                std::vector<juce::String> ids; for (auto& i : items) ids.push_back(i.id);
                const int n = lib->exportPack(fc.getResult().withFileExtension("f8kpack"), r == 2, ids);
                status.setText(n < 0 ? juce::String("Could not write the pack.") : juce::String(n) + " records saved (" + juce::String((double)fc.getResult().withFileExtension("f8kpack").getSize()/1024.0, 0) + " KB). No file names or paths are in it.", juce::dontSendNotification);
            });
        });
    }
    void setSourceOfSelected(const juce::String& v) {
        auto lib = getLibrary ? getLibrary() : nullptr;
        if (lib == nullptr || selectedId.isEmpty()) return;
        lib->setUserValue(selectedId, "source", v);              // yours: it stays in user.csv
        lib->setLeanMeta(selectedId, {{"source", v.toStdString()}});
        for (auto& i : items) if (i.id == selectedId) { i.source = v; i.rec.set("source", v.toStdString()); }
        table.repaint(); repaint();
    }
    juce::Label status;
    juce::ThreadPool pool{1};
    std::atomic<int> buildDone{0}, buildTotal{0};

    // audio, created only when you press Play
    juce::AudioFormatManager formats;
    std::unique_ptr<juce::AudioDeviceManager> devices;
    juce::AudioSourcePlayer player; juce::AudioTransportSource transport;
    std::unique_ptr<juce::AudioFormatReaderSource> readerSource;
    bool transportReady = false;
    juce::TimeSliceThread readAhead{"audio read-ahead"};

    const Item* current() const { for (auto& i : items) if (i.id == selectedId) return &i; return nullptr; }

    static juce::Colour devColour(int dev, int meter) { //a meter lights up in its own colour (zero crosses green, peaks blue, slews red) when it leads the other two
        static const juce::Colour base(20, 24, 31), glow[3] = {juce::Colour(70, 205, 100), juce::Colour(80, 140, 255), juce::Colour(240, 85, 75)};
        const float amount = juce::jmin(juce::jmax((float)dev/127.0f, 0.0f)*2.0f, 0.9f);
        return base.interpolatedWith(glow[meter], amount*0.78f);
    }
    // which = 0: overall tone; 1 weight, 2 peaks, 3 slew
    static void drawBar(juce::Graphics& g, juce::Rectangle<int> r, const f8k::LeanRecord& rec, int which) {
        if (rec.columns <= 0 || r.getWidth() <= 0) return;
        for (int x = 0; x < r.getWidth(); ++x) {
            const int c = juce::jmin(rec.columns - 1, (int)((int64_t)x*rec.columns/r.getWidth()));
            juce::Colour col;
            if (which == 0) col = juce::Colour(rec.toneRgb[(size_t)c*3], rec.toneRgb[(size_t)c*3 + 1], rec.toneRgb[(size_t)c*3 + 2]);
            else col = devColour(which == 1 ? rec.devWeight[(size_t)c] : which == 2 ? rec.devPeaks[(size_t)c] : rec.devSlew[(size_t)c], which == 1 ? 0 : which == 2 ? 1 : 2);
            g.setColour(col); g.fillRect(r.getX() + x, r.getY(), 1, r.getHeight());
        }
    }
    static void drawLines(juce::Graphics& g, juce::Rectangle<int> r, const f8k::LeanRecord& rec) {
        if (rec.columns < 2) return;
        auto line = [&](const std::vector<uint8_t>& v, juce::Colour c) {
            juce::Path p;
            for (int x = 0; x < r.getWidth(); ++x) {
                const int col = juce::jmin(rec.columns - 1, (int)((int64_t)x*rec.columns/r.getWidth()));
                const float y = (float)r.getBottom() - (float)v[(size_t)col]/200.0f*(float)r.getHeight();
                if (x == 0) p.startNewSubPath((float)r.getX(), y); else p.lineTo((float)(r.getX() + x), y);
            }
            g.setColour(c.withAlpha(0.85f)); g.strokePath(p, juce::PathStrokeType(1.2f));
        };
        line(rec.linePeak, juce::Colour(255, 255, 255)); line(rec.lineBass, juce::Colour(60, 220, 110)); line(rec.lineSlew, juce::Colour(255, 90, 80));
    }

    //---------------------------------------------------------------- data
    void resort() {
        order.resize(items.size());
        for (size_t i = 0; i < items.size(); ++i) order[i] = i;
        std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
            if (sortCol == 1) { const int c = items[a].name.compareNatural(items[b].name); return sortFwd ? c < 0 : c > 0; }
            return sortFwd ? items[a].key < items[b].key : items[a].key > items[b].key; //grade: Chris's order (numbered bonus first, then by letter value)
        });
        table.updateContent(); table.repaint();
        for (size_t i = 0; i < order.size(); ++i) if (items[order[i]].id == selectedId) {table.selectRow((int)i, true, false); return;}
        if (!order.empty() && current() == nullptr) table.selectRow(0);
    }
    //the same song: artist + title (ignoring case, punctuation and bracketed bits like "(2012 Remaster)"), or the file name when there are no tags; and about the same length
    static juce::String songKey(const juce::String& artist, const juce::String& title, const juce::String& file) {
        auto norm = [](juce::String f) {
            f = f.toLowerCase(); juce::String o; int depth = 0;
            for (auto ch : f) {if (ch == '(' || ch == '[') ++depth; else if ((ch == ')' || ch == ']') && depth > 0) --depth; else if (depth == 0 && juce::CharacterFunctions::isLetterOrDigit(ch)) o << juce::String::charToString(ch);}
            return o;
        };
        if (title.isNotEmpty()) return norm(artist) + "|" + norm(title);
        return "|" + norm(juce::File::createFileWithoutCheckingPath("/" + file).getFileNameWithoutExtension());
    }
    //groups the versions of a song and marks the best one (the best grade; on a tie the higher sample rate, then the louder-headroom peak)
    static void groupVersions(std::vector<Item>& items) {
        std::map<juce::String, std::vector<size_t>> bySong;
        for (size_t i = 0; i < items.size(); ++i) if (items[i].song != "|") bySong[items[i].song].push_back(i);
        int g = 0;
        for (auto& kv : bySong) {
            auto v = kv.second; std::sort(v.begin(), v.end(), [&](size_t a, size_t b) { return items[a].dur < items[b].dur; });
            std::vector<std::vector<size_t>> clusters;
            for (size_t i : v) { if (!clusters.empty() && items[i].dur <= items[clusters.back().front()].dur*1.2 + 2.0) clusters.back().push_back(i); else clusters.push_back({i}); }
            for (auto& cl : clusters) {
                if (cl.size() < 2) continue;
                size_t best = cl.front();
                for (size_t i : cl) { items[i].group = g; items[i].groupSize = (int)cl.size();
                    if (items[i].key < items[best].key || (items[i].key == items[best].key && items[i].sr > items[best].sr)) best = i; }
                items[best].best = true; ++g;
            }
        }
    }
    static std::vector<Item> gather(const std::shared_ptr<f8k::Library>& lib, const std::shared_ptr<const std::set<juce::String>>& allowed) {
        std::vector<Item> out;
            const auto idx = lib->index();
        for (size_t r = 0; r < idx.rows.size(); ++r) {
            const juce::String id = idx.get(r, "id");
            if (allowed && !allowed->count(id)) continue;
            Item it; it.id = id;
            if (!lib->readLean(id, it.rec)) continue;
            const juce::String artist = juce::String(it.rec.get("artist")), title = juce::String(it.rec.get("title"));
            it.name = title.isNotEmpty() ? (artist.isNotEmpty() ? artist + " - " + title : title) : juce::File(idx.get(r, "file")).getFileNameWithoutExtension();
            it.grade = juce::String(it.rec.get("grade"));
            const int bonus = juce::String(it.rec.get("bonus_value")).getIntValue();
            const double lv = juce::String(it.rec.get("letter_value")).getDoubleValue();
            it.key = (bonus >= 1 && bonus <= 80) ? (double)bonus : 1000.0 - lv;
            it.path = idx.get(r, "source_path");
            it.source = juce::String(it.rec.get("source"));
            it.dur = juce::String(it.rec.get("length_s")).getDoubleValue(); it.sr = juce::String(it.rec.get("sample_rate")).getDoubleValue();
            it.song = songKey(artist, title, idx.get(r, "file"));
            out.push_back(std::move(it));
        }
        if (!allowed) { //records other people shared with you (lean-shared): shown too, marked as shared. Not when the Library's filters are in use, since they can only judge your own tracks.
            std::set<juce::String> have; for (auto& i : out) have.insert(i.id);
            for (auto& id : lib->sharedIds()) {
                if (have.count(id)) continue;
                Item it; it.id = id; it.shared = true;
                if (!lib->readShared(id, it.rec)) continue;
                const juce::String artist = juce::String(it.rec.get("artist")), title = juce::String(it.rec.get("title"));
                it.name = (title.isNotEmpty() ? (artist.isNotEmpty() ? artist + " - " + title : title) : id) + "  (shared)";
                it.grade = juce::String(it.rec.get("grade"));
                const int bonus = juce::String(it.rec.get("bonus_value")).getIntValue();
                it.key = (bonus >= 1 && bonus <= 80) ? (double)bonus : 1000.0 - juce::String(it.rec.get("letter_value")).getDoubleValue();
                it.source = juce::String(it.rec.get("source"));
                it.dur = juce::String(it.rec.get("length_s")).getDoubleValue(); it.sr = juce::String(it.rec.get("sample_rate")).getDoubleValue();
                it.song = songKey(artist, title, {});
                out.push_back(std::move(it));
            }
        }
        groupVersions(out);
        return out;
    }
    void load() {
        auto lib = getLibrary ? getLibrary() : nullptr;
        if (lib == nullptr || loading) return;
        loading = true; loadedFor = lib.get(); loadedChange = lib->changeCount(); seenGen = getFilterGen ? getFilterGen() : 0;
        const auto allowed = getAllowed ? getAllowed() : nullptr;
        juce::Component::SafePointer<GlanceView> sp(this);
        pool.addJob([this, sp, lib, allowed] {
            std::vector<Item> out = gather(lib, allowed);
            juce::MessageManager::callAsync([this, sp, out = std::move(out)]() mutable {
                if (sp == nullptr) return;
                items = std::move(out); loading = false; loaded = true;
                status.setText(juce::String((int)items.size()) + " tracks", juce::dontSendNotification);
                resort(); repaint();
            });
        });
    }
    void buildRecords() {
        auto lib = getLibrary ? getLibrary() : nullptr;
        if (lib == nullptr || building) return;
        building = true; buildDone = 0; buildTotal = 0;
        juce::Component::SafePointer<GlanceView> sp(this);
        pool.addJob([this, sp, lib] {
            const auto rep = lib->buildLeanRecords(false, false, [&] {return sp == nullptr;}, [&](int d, int n) {buildDone = d; buildTotal = n;});
            juce::MessageManager::callAsync([this, sp, rep] {
                if (sp == nullptr) return;
                building = false; loaded = false;
                status.setText("Records: " + juce::String(rep.built) + " built, " + juce::String(rep.kept) + " already there, " + juce::String(rep.noData) + " without window data.", juce::dontSendNotification);
            });
        });
    }
    void timerCallback() override {
        auto lib = getLibrary ? getLibrary() : nullptr;
        if (building) status.setText("Building records: " + juce::String(buildDone.load()) + " of " + juce::String(buildTotal.load()) + " ...", juce::dontSendNotification);
        else if (lib != nullptr && !loading && isShowing() && (!loaded || lib.get() != loadedFor || lib->changeCount() != loadedChange || (getFilterGen && getFilterGen() != seenGen))) load();
        if (transportReady) {
            if (!transport.isPlaying() && playButton.getButtonText() == "Stop") playButton.setButtonText("Play");
            repaint(bigBar.expanded(0, 8));
        }
    }

    //---------------------------------------------------------------- playing
    void stopPlayback() {
        transport.stop(); transport.setSource(nullptr); readerSource.reset(); transportReady = false;
        playButton.setButtonText("Play");
    }
    void togglePlay() {
        if (transportReady && transport.isPlaying()) { stopPlayback(); repaint(); return; }
        const Item* it = current();
        if (it == nullptr) return;
        const juce::File f(it->path);
        if (!f.existsAsFile()) { status.setText("The audio file is not at its stored path any more: " + it->path, juce::dontSendNotification); return; }
        std::unique_ptr<juce::AudioFormatReader> rd(formats.createReaderFor(f));
        if (rd == nullptr) { status.setText("Could not open that audio file.", juce::dontSendNotification); return; }
        if (devices == nullptr) {
            devices = std::make_unique<juce::AudioDeviceManager>();
            const auto err = devices->initialiseWithDefaultDevices(0, 2);
            if (err.isNotEmpty()) { status.setText("No audio output: " + err, juce::dontSendNotification); devices.reset(); return; }
            player.setSource(&transport); devices->addAudioCallback(&player);
        }
        const double sr = rd->sampleRate;
        readerSource = std::make_unique<juce::AudioFormatReaderSource>(rd.release(), true);
        readAhead.startThread(); transport.setSource(readerSource.get(), 32768, &readAhead, sr);
        transport.setPosition(0.0); transport.start(); transportReady = true;
        playButton.setButtonText("Stop");
    }
};

// The Library tab: the overview, with the full table one click away.
class LibraryTab : public juce::Component {
public:
    LibraryTab(LibraryView& full) : fullTable(full) {
        addAndMakeVisible(glance); addChildComponent(fullTable);
        addAndMakeVisible(toggle); styleButton(toggle);
        toggle.onClick = [this] { showFull(!fullShown); };
    }
    GlanceView glance;
    void showFull(bool f) {
        fullShown = f;
        glance.setVisible(!f); fullTable.setVisible(f);
        toggle.setButtonText(f ? "Back to overview" : "Full table");
        resized();
    }
    void resized() override {
        auto r = getLocalBounds();
        auto bar = r.removeFromTop(0);
        (void)bar;
        glance.setBounds(r); fullTable.setBounds(r);
        toggle.setBounds(getWidth() - 138, 4, 130, 24); toggle.toFront(false);
    }
private:
    LibraryView& fullTable;
    juce::TextButton toggle{"Full table"};
    bool fullShown = false;
};
