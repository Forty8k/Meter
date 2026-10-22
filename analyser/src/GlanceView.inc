// meterDARK Analyser: the LIBRARY OVERVIEW (included by MainGui.cpp, inside its anonymous namespace).
// The top ~40% is the selected track: its name, a large overall tone bar with the grade and the overlay lines (peak, bass position, slew), separate weight /
// peaks / slew tone bars, the technical details, and play / stop with a playhead you can click to seek. The bottom ~60% is a three-column list: name, a small
// tone strip, and a sortable grade. Stacked together the strips show trends across the library. It reads only the lean records (see LeanRecord.h); the audio is
// opened from its stored file path just for playing. The full table stays one click away.

class GlanceView : public juce::Component, public juce::TableListBoxModel, private juce::Timer, private juce::TableHeaderComponent::Listener {
public:
    struct Item { juce::String id, name, grade, path, song, source, hay; juce::String bpm, musicalKey; double key = 0, dur = 0, sr = 0; int group = -1, groupSize = 1; bool best = false, shared = false; mdark::LeanRecord rec; };
    std::function<std::shared_ptr<mdark::Library>()> getLibrary;
    std::function<std::shared_ptr<mdark::TrackStore>()> getStore;   //the library in memory: every list here comes from it, a changed track updates just its own row
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
        {   //the three default columns, and the rest of the full table's columns, hidden until you add them (right-click the header)
            const int hidden = juce::TableHeaderComponent::defaultFlags & ~juce::TableHeaderComponent::visible;
            auto& h = table.getHeader();
            h.addColumn("Track", 1, 420, 120, 2000, juce::TableHeaderComponent::defaultFlags & ~juce::TableHeaderComponent::appearsOnColumnMenu);
            h.addColumn("Tone", 2, 320, 80, 1200, juce::TableHeaderComponent::visible | juce::TableHeaderComponent::resizable | juce::TableHeaderComponent::appearsOnColumnMenu);
            h.addColumn("Grade", 3, 110, 60, 300, juce::TableHeaderComponent::defaultFlags);
            const struct {const char* name; int w;} extra[] = {{"Artist", 150}, {"Album", 150}, {"Year", 56}, {"BPM", 56}, {"Key", 80}, {"LUFS", 60}, {"Peak dB", 66}, {"True peak", 70}, {"Length", 62}, {"Rate Hz", 70}, {"Bits", 44}, {"Source", 100}, {"Format", 90}};
            for (int i = 0; i < 13; ++i) h.addColumn(extra[i].name, 4 + i, extra[i].w, 40, 600, hidden);
            h.setSortColumnId(3, true);
            h.addListener(this);
        }
        for (auto* b : {&playButton, &linesButton, &buildButton, &compareButton, &shareButton, &sharedButton}) {addAndMakeVisible(b); styleButton(*b);}
        shareButton.onClick = [this] { shareMenu(); };
        sharedButton.setTooltip("Records other people (or your other computer) shared with you in packs: shown in the list, marked (shared), or hidden. The Library's filters never hide them.");
        sharedButton.onClick = [this] { showShared = !showShared; saveOverviewSettings(); updateSharedButton(); loaded = false; };
        updateSharedButton();
        compareButton.setClickingTogglesState(true); compareButton.setColour(juce::TextButton::buttonOnColourId, kSelect);
        compareButton.onClick = [this] { compareMode = compareButton.getToggleState(); repaint(); };
        addAndMakeVisible(sourceBox);
        sourceBox.setTextWhenNothingSelected("audio source");
        for (int i = 0; i < 8; ++i) sourceBox.addItem(kSources[i], i + 1);
        sourceBox.setColour(juce::ComboBox::backgroundColourId, kPanel); sourceBox.setColour(juce::ComboBox::textColourId, kText); sourceBox.setColour(juce::ComboBox::outlineColourId, kPanel);
        sourceBox.onChange = [this] { setSourceOfSelected(sourceBox.getText()); };
        linesButton.setClickingTogglesState(true); linesButton.setToggleState(true, juce::dontSendNotification); //on by default: the lines sit inside the small bars
        linesButton.setColour(juce::TextButton::buttonOnColourId, kSelect);
        linesButton.onClick = [this] { repaint(); };
        playButton.onClick = [this] { togglePlay(); };
        buildButton.onClick = [this] { buildRecords(); };
        addAndMakeVisible(status);
        status.setColour(juce::Label::textColourId, kDim);
        addAndMakeVisible(search);
        search.setTextToShowWhenEmpty("Search name, artist, genre, key ...", kDim);
        search.setColour(juce::TextEditor::backgroundColourId, kPanel);
        search.setColour(juce::TextEditor::textColourId, kText);
        search.setColour(juce::TextEditor::outlineColourId, kPanel);
        search.onTextChange = [this] { resort(); repaint(); };
        startTimer(80);
    }
    ~GlanceView() override {
        stopTimer(); table.getHeader().removeListener(this);
        if (devices != nullptr) { devices->removeAudioCallback(&player); }
        player.setSource(nullptr); transport.setSource(nullptr); readAhead.stopThread(2000);
        pool.removeAllJobs(true, 10000);
    }

    void reloadNow() { //synchronous (for the automated picture)
        auto store = getStore ? getStore() : nullptr;
        if (store == nullptr) return;
        loadedFor = store.get(); loadedSeq = store->seq();
        items = gather(store, getAllowed ? getAllowed() : nullptr, showShared); loaded = true; restoreColumns();
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
        shareButton.setBounds(bar.removeFromLeft(120)); bar.removeFromLeft(6);
        sharedButton.setBounds(bar.removeFromLeft(118)); bar.removeFromLeft(10);
        bar.removeFromRight(146); //the Library tab's toggle button sits over the top-right corner
        search.setBounds(bar.removeFromRight(juce::jlimit(150, 260, bar.getWidth()/3))); bar.removeFromRight(8);
        status.setBounds(bar);
        const int topH = juce::jmax(372, (int)(r.getHeight()*(r.getHeight() > 900 ? 0.58f : 0.44f)));
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
        const float ui = juce::jlimit(1.0f, 1.7f, (float)getHeight()/780.0f); //the bars and spacing grow with the window (1.0 at the normal size) ...
        const float uT = juce::jmin(ui, 1.25f), lg = juce::jmin(ui, 1.1f); //... the text grows less, and the legend stays small, so the details block has room
        // head row: name (left), who leads at the playhead / mouse (centre, large, yellow), grade (right)
        auto head = a.removeFromTop(juce::roundToInt(30*ui));
        {
            auto gradeArea = head.removeFromRight(juce::roundToInt(210*ui));
            g.setColour(kAccent); g.setFont(juce::Font(26.0f*uT, juce::Font::bold));
            g.drawText(it->grade, gradeArea, juce::Justification::centredRight);
            const int nameW = juce::roundToInt(head.getWidth()*0.38f);
            g.setColour(kText); g.setFont(juce::Font(20.0f*uT, juce::Font::bold));
            g.drawText(it->name, head.removeFromLeft(nameW), juce::Justification::centredLeft, true);
            double frac = hoverFrac;
            if (transportReady) { const double len = transport.getLengthInSeconds(); if (len > 0) frac = transport.getCurrentPosition()/len; }
            if (frac >= 0 && rec.columns > 0) {
                const int c = juce::jlimit(0, rec.columns - 1, (int)(frac*rec.columns));
                const double secs = frac*juce::String(rec.get("length_s")).getDoubleValue();
                g.setColour(kAccent); g.setFont(juce::Font(22.0f*uT, juce::Font::bold));
                g.drawText(juce::String((int)secs/60) + ":" + juce::String::formatted("%02d", (int)secs % 60) + "   " + whoLeads(rec, c), head, juce::Justification::centredLeft, true);
            }
        }
        // technical details
        {
            juce::StringArray d;
            auto num = [&](const char* k, const char* fmt) { const juce::String v = juce::String(rec.get(k)); return v.isEmpty() ? juce::String("-") : juce::String::formatted(fmt, v.getDoubleValue()); };
            const juce::String bits = juce::String(rec.get("bits")), src = juce::String(rec.get("source"));
            const int secs = juce::roundToInt(juce::String(rec.get("length_s")).getDoubleValue());
            d.add("Length " + juce::String(secs/60) + ":" + juce::String::formatted("%02d", secs % 60));
            d.add(num("sample_rate", "%.0f Hz") + " / " + (bits.isEmpty() ? juce::String("-") : bits + " bit"));
            d.add("Peak " + num("peak_db", "%.1f dBFS")); d.add("True peak " + num("true_peak_db", "%.1f dBTP")); d.add("LUFS " + num("lufs", "%.1f"));
            d.add("Meter " + juce::String(rec.get("meter_version")));
            d.add("Source " + (src.isEmpty() ? juce::String("not set") : src));
            g.setColour(kText); g.setFont(juce::Font(13.0f*uT));
            g.drawFittedText(d.joinIntoString("      "), a.removeFromTop(juce::roundToInt(20*uT)), juce::Justification::centredLeft, 1);
        }
        a.removeFromTop(juce::roundToInt(4*ui));
        // the large overall tone bar: the three small bars under it share its exact left edge and width, so a point in time is one x position in all four
        //the big bar gives way to the details block: what is needed under it (3 small bars, legend, ~7 lines of facts) is reserved first
        const int factsWant = juce::roundToInt(6*uT) + 7*juce::roundToInt(17*uT);
        const int belowBar = juce::roundToInt(4*ui) + 3*juce::roundToInt(41*ui) + juce::roundToInt(6*ui) + juce::roundToInt(92*lg) + juce::roundToInt(6*ui);
        const int barH = juce::jlimit(40, 150, juce::jmin((int)(a.getHeight()*0.30f), a.getHeight() - belowBar - factsWant));
        bigBar = a.removeFromTop(barH);
        drawBar(g, bigBar, rec, 0);
        a.removeFromTop(juce::roundToInt(4*ui));
        // the three separate meters: the label sits above each bar, the line (when "Overlay lines" is on) lives inside its own bar
        static const char* names[3] = {"tone", "punch", "detail"};
        const int smallH = juce::roundToInt(24*ui);
        for (int k = 1; k <= 3; ++k) {
            auto labelRow = a.removeFromTop(juce::roundToInt(14*ui));
            const int chip = juce::roundToInt(8*ui);
            g.setColour(meterColour(k - 1)); g.fillRect(bigBar.getX(), labelRow.getY() + (labelRow.getHeight() - chip)/2, chip, chip);
            g.setColour(kDim); g.setFont(juce::Font(12.0f*uT));
            g.drawText(names[k - 1], bigBar.getX() + chip + 5, labelRow.getY(), 120, labelRow.getHeight(), juce::Justification::centredLeft);
            auto row = a.removeFromTop(smallH).withX(bigBar.getX()).withWidth(bigBar.getWidth());
            smallBars[k - 1] = row;
            drawBar(g, row, rec, k);
            if (linesButton.getToggleState()) drawLine(g, row, k == 1 ? rec.lineBass : k == 2 ? rec.linePeak : rec.lineSlew, meterColour(k - 1).interpolatedWith(juce::Colours::white, 0.45f));
            a.removeFromTop(juce::roundToInt(3*ui));
        }
        // the playhead (the big bar only)
        {
            double frac = -1;
            if (transportReady) { const double len = transport.getLengthInSeconds(); if (len > 0) frac = transport.getCurrentPosition()/len; }
            if (frac >= 0) {
                const float x = (float)bigBar.getX() + (float)frac*(float)bigBar.getWidth();
                g.setColour(juce::Colours::white); g.fillRect(x - 1.0f, (float)bigBar.getY() - 2.0f, 2.0f, (float)bigBar.getHeight() + 4.0f);
            }
        }
        a.removeFromTop(juce::roundToInt(6*ui));
        // the legend: how to read the bars
        legendArea = a.removeFromTop(juce::roundToInt(92*lg));
        paintLegend(g, legendArea, lg);
        // everything else we know about the track (only when there is room)
        a.removeFromTop(juce::roundToInt(6*ui));
        if (a.getHeight() >= juce::roundToInt(6*uT) + 3*juce::roundToInt(17*uT)) paintFacts(g, a, *it, uT); //as many lines as fit (3 or more)
    }

    //---------------------------------------------------------------- the details block (what the Full table's double-click shows), cached per track
    std::vector<std::pair<juce::String, juce::String>> facts; juce::String factsFor; uint64_t factsSeq = ~0ull; const void* factsStore = nullptr;
    std::map<juce::String, mdark::Table> factsGrades; std::map<juce::String, std::map<juce::String, size_t>> factsGradeRow;
    std::map<juce::String, std::map<juce::String, juce::String>> factsUser;
    void buildFacts(const Item& it) {
        auto store = getStore ? getStore() : nullptr;
        facts.clear(); factsFor = it.id;
        if (store == nullptr) return;
        if (factsStore != store.get() || factsSeq != store->seq()) {
            factsStore = store.get(); factsSeq = store->seq();
            factsGrades.clear(); factsGradeRow.clear();
            for (auto& v : mdark::Library::versionIds()) {
                mdark::Table t = store->readings(v);
                auto& m = factsGradeRow[v]; for (size_t r = 0; r < t.rows.size(); ++r) m[t.get(r, "id")] = r;
                factsGrades[v] = std::move(t);
            }
            factsUser = store->userValues();
        }
        mdark::Table idx; { juce::StringArray r; if (store->indexRow(it.id, r, &idx.header)) idx.rows.push_back(r); }
        const int row = idx.rows.empty() ? -1 : 0;
        auto fromIndex = [&](const char* k) { return row >= 0 ? idx.get((size_t)row, k) : juce::String(); };
        auto field = [&](const char* k) { auto u = factsUser.find(it.id); if (u != factsUser.end()) { auto v = u->second.find(k); if (v != u->second.end() && v->second.isNotEmpty()) return v->second; } juce::String x = fromIndex(k); return x.isNotEmpty() ? x : juce::String(it.rec.get(k)); };
        auto add = [&](const juce::String& label, const juce::String& v) { if (v.isNotEmpty()) facts.emplace_back(label, v); };
        add("Artist", field("artist")); add("Album", field("album")); add("Year", field("year")); add("Genre", field("genre"));
        { juce::String b = it.bpm; if (b.isNotEmpty() && row >= 0) b += "  (confidence " + fromIndex("bpm_confidence") + ")"; add("BPM", b); }
        { juce::String k = it.musicalKey; if (k.isNotEmpty() && row >= 0) k += "  (confidence " + fromIndex("key_confidence") + ")"; add("Key", k); }
        if (row >= 0) {
            add("Format", fromIndex("format") + (fromIndex("lossy") == "yes" ? "  (LOSSY)" : "") + ", " + fromIndex("channels") + " ch");
            add("Added", fromIndex("analysed").upToFirstOccurrenceOf("T", false, false));
        }
        for (auto& v : mdark::Library::versionIds()) {
            auto gt = factsGrades.find(v); auto rt = factsGradeRow.find(v);
            if (gt == factsGrades.end() || rt == factsGradeRow.end()) continue;
            auto r = rt->second.find(it.id); if (r == rt->second.end()) continue;
            const auto& t = gt->second; const size_t gr = r->second;
            juce::String name = v; for (auto& mv : mdark::meterVersions()) if (mv.id == v.toStdString()) name = juce::String(mv.version);
            add("Meter " + name, t.get(gr, "grade") + "   authority / power / detail  " + t.get(gr, "authority_value").substring(0, 6) + " / " + t.get(gr, "power_value").substring(0, 6) + " / " + t.get(gr, "detail_value").substring(0, 6)
                                 + "   letter " + t.get(gr, "letter_value").substring(0, 6) + "   bonus " + t.get(gr, "bonus_value"));
            add("   directions", t.get(gr, "directions_main") + " most of the loud parts, '" + t.get(gr, "directions_end") + "' at the end");
        }
    }
    void paintFacts(juce::Graphics& g, juce::Rectangle<int> area, const Item& it, float ui) {
        if (factsFor != it.id) buildFacts(it);
        if (facts.empty()) return;
        g.setColour(kDim.withAlpha(0.4f)); g.fillRect(area.getX(), area.getY(), area.getWidth(), 1);
        area.removeFromTop(juce::roundToInt(6*ui));
        const float fs = 12.5f*ui; const int lineH = juce::roundToInt(17*ui);
        // short facts go in columns; the per-version grade lines are long and take the full width underneath
        std::vector<std::pair<juce::String, juce::String>> shortF, longF;
        for (auto& f : facts) (f.second.length() > 60 || f.first.startsWith("Meter") || f.first.startsWith("   ") ? longF : shortF).push_back(f);
        const int cols = area.getWidth() > 1100 ? 3 : 2, colW = area.getWidth()/cols, labW = juce::roundToInt(64*ui);
        const int perCol = (int)((shortF.size() + (size_t)cols - 1)/(size_t)cols);
        g.setFont(juce::Font(fs));
        for (size_t i = 0; i < shortF.size(); ++i) {
            const int c = (int)i/juce::jmax(1, perCol), r = (int)i % juce::jmax(1, perCol);
            const int x = area.getX() + c*colW, y = area.getY() + r*lineH;
            if (y + lineH > area.getBottom()) continue;
            g.setColour(kDim); g.drawText(shortF[i].first, x, y, labW, lineH, juce::Justification::centredLeft, true);
            g.setColour(kText); g.drawText(shortF[i].second, x + labW + 4, y, colW - labW - 12, lineH, juce::Justification::centredLeft, true);
        }
        int y = area.getY() + perCol*lineH + juce::roundToInt(6*ui);
        for (auto& f : longF) {
            if (y + lineH > area.getBottom()) break;
            const bool dir = f.first.startsWith("   ");
            g.setColour(dir ? kDim : kText.withAlpha(0.95f));
            g.drawText((dir ? juce::String("      ") : f.first + "   ") + f.second, area.getX(), y, area.getWidth(), lineH, juce::Justification::centredLeft, true);
            y += lineH;
        }
    }

    static juce::Colour meterColour(int k) { static const juce::Colour c[3] = {juce::Colour(70, 205, 100), juce::Colour(80, 140, 255), juce::Colour(240, 85, 75)}; return c[k]; }
    //which meter leads at a column: one word group. A meter leads when it is clearly ahead; two are named when they are nearly level; none = balanced
    static juce::String whoLeads(const mdark::LeanRecord& rec, int c) {
        const double d[3] = {rec.devWeight[(size_t)c]/127.0, rec.devPeaks[(size_t)c]/127.0, rec.devSlew[(size_t)c]/127.0};
        static const char* n[3] = {"tone", "punch", "detail"};
        int best = 0; for (int i = 1; i < 3; ++i) if (d[i] > d[best]) best = i;
        if (d[best] < 0.10) return "balanced";
        juce::StringArray lead; lead.add(n[best]);
        for (int i = 0; i < 3; ++i) if (i != best && d[i] > 0.10 && d[best] - d[i] < 0.06) lead.add(n[i]);
        return lead.joinIntoString(" + ") + " lead" + (lead.size() == 1 ? "s" : "");
    }
    static const char* legendText(int i) {
        static const char* t[5] = {
            "GREEN means TONE leads. The body or weight of the track carries this section.",
            "BLUE means PUNCH leads. The peaks, the hits and impact, are out in front. It doesn't mean squashed.",
            "RED means DETAIL leads. The slews, the brightness and fine high detail, are out in front.",
            "White in the big bar, with all three small bars dark, means balanced: nothing leads.",
            "So a lit bar says \"this is what dominates here\", not \"this is wrong\"."};
        return t[i];
    }
    void paintLegend(juce::Graphics& g, juce::Rectangle<int> r, float ui) {
        g.setFont(juce::Font(12.5f*ui));
        const int colW = r.getWidth()/2 - 10, rowH = juce::roundToInt(34*ui), chip = juce::roundToInt(9*ui);
        for (int i = 0; i < 4; ++i) {
            const int cx = r.getX() + (i % 2)*(colW + 20), cy = r.getY() + (i/2)*rowH;
            if (i < 3) g.setColour(meterColour(i)); else g.setColour(juce::Colours::white);
            g.fillRect(cx, cy + juce::roundToInt(3*ui), chip, chip);
            g.setColour(kText.withAlpha(0.9f));
            g.drawFittedText(legendText(i), cx + chip + 6, cy, colW - chip - 6, rowH - 2, juce::Justification::topLeft, 2, 1.0f);
        }
        g.setColour(kDim); g.setFont(juce::Font(12.5f*ui, juce::Font::italic));
        g.drawText(legendText(4), r.getX(), r.getY() + 2*rowH + 2, r.getWidth(), juce::roundToInt(16*ui), juce::Justification::centredLeft);
    }
    void mouseMove(const juce::MouseEvent& e) override {
        const bool over = bigBar.getWidth() > 0 && e.y >= bigBar.getY() && e.y <= smallBars[2].getBottom() && e.x >= bigBar.getX() && e.x <= bigBar.getRight();
        const double f = over ? juce::jlimit(0.0, 1.0, (double)(e.x - bigBar.getX())/(double)bigBar.getWidth()) : -1.0;
        if (f != hoverFrac) { hoverFrac = f; repaint(topArea); }
    }
    void mouseExit(const juce::MouseEvent&) override { if (hoverFrac >= 0) { hoverFrac = -1; repaint(topArea); } }

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
        if (bigBar.getWidth() > 0 && e.y >= bigBar.getY() - 4 && e.y <= smallBars[2].getBottom() + 4) {
            const double f = juce::jlimit(0.0, 1.0, (double)(e.x - bigBar.getX())/(double)juce::jmax(1, bigBar.getWidth()));
            if (transportReady) { transport.setPosition(transport.getLengthInSeconds()*f); if (!transport.isPlaying()) transport.start(); repaint(); }
            else startPlayback(f); //a click anywhere on the bars starts playing from there
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
        if (col >= 4) { g.drawText(cellText(it, col), 6, 0, w - 12, h, isNumericCol(col) ? juce::Justification::centredRight : juce::Justification::centredLeft, true); return; }
        juce::String t = col == 1 ? it.name : it.grade;
        if (col == 1 && it.groupSize > 1) t << "   (" << it.groupSize << " versions" << (it.source.isNotEmpty() ? ", " + it.source : juce::String()) << ")";
        if (col == 3 && it.best) t << "  best";
        g.setColour(col == 3 && it.best ? kAccent : kText);
        g.drawText(t, 6, 0, w - 12, h, juce::Justification::centredLeft, true);
    }
    static bool isNumericCol(int col) { return col == 6 || col == 7 || (col >= 9 && col <= 14); }
    static juce::String cellText(const Item& it, int col) {
        const auto& r = it.rec;
        auto f1 = [&](const char* k) { const juce::String v = juce::String(r.get(k)); return v.isEmpty() ? juce::String() : juce::String(v.getDoubleValue(), 1); };
        switch (col) {
            case 4: return juce::String(r.get("artist")); case 5: return juce::String(r.get("album")); case 6: return juce::String(r.get("year"));
            case 7: return it.bpm; case 8: return it.musicalKey; case 9: return f1("lufs"); case 10: return f1("peak_db"); case 11: return f1("true_peak_db");
            case 12: { const int s = juce::roundToInt(juce::String(r.get("length_s")).getDoubleValue()); return s > 0 ? juce::String(s/60) + ":" + juce::String::formatted("%02d", s % 60) : juce::String(); }
            case 13: return juce::String(r.get("sample_rate")); case 14: return juce::String(r.get("bits")); case 15: return it.source; case 16: return juce::String(r.get("format"));
            default: return {};
        }
    }
    static double cellNumber(const Item& it, int col) {
        const auto& r = it.rec;
        switch (col) {
            case 6: return juce::String(r.get("year")).getDoubleValue(); case 7: return it.bpm.getDoubleValue();
            case 9: return juce::String(r.get("lufs")).getDoubleValue(); case 10: return juce::String(r.get("peak_db")).getDoubleValue(); case 11: return juce::String(r.get("true_peak_db")).getDoubleValue();
            case 12: return juce::String(r.get("length_s")).getDoubleValue(); case 13: return juce::String(r.get("sample_rate")).getDoubleValue(); case 14: return juce::String(r.get("bits")).getDoubleValue();
            default: return 0;
        }
    }
    void sortOrderChanged(int col, bool fwd) override { sortCol = col; sortFwd = fwd; resort(); }
    //remember which columns you added, their order and widths (per library)
    void tableColumnsChanged(juce::TableHeaderComponent*) override { saveColumns(); }
    void tableColumnsResized(juce::TableHeaderComponent*) override { saveColumns(); }
    void tableSortOrderChanged(juce::TableHeaderComponent*) override {}
    juce::File overviewColumnsFile() const { auto lib = getLibrary ? getLibrary() : nullptr; return lib ? lib->folder.getChildFile("overview-columns.txt") : juce::File(); }
    void saveColumns() { if (!restoredColumns) return; const auto f = overviewColumnsFile(); if (f != juce::File()) f.replaceWithText(table.getHeader().toString()); }
    void restoreColumns() {
        if (restoredColumns) return;
        const auto f = overviewColumnsFile();
        if (f == juce::File()) return;
        restoredColumns = true;
        if (f.existsAsFile()) { const juce::String t = f.loadFileAsString(); if (t.isNotEmpty()) table.getHeader().restoreFromString(t); }
    }
    void selectedRowsChanged(int row) override {
        if (row < 0 || row >= (int)order.size()) return;
        selectedId = items[order[(size_t)row]].id;
        stopPlayback();
        if (const Item* it = current()) { sourceBox.setText(it->source, juce::dontSendNotification); compareButton.setEnabled(it->groupSize > 1); if (it->groupSize < 2) {compareMode = false; compareButton.setToggleState(false, juce::dontSendNotification);} }
        repaint();
    }

private:
    std::vector<Item> items; std::vector<size_t> order;
    bool showShared = true; bool scrollToSelected = false; juce::String notice, jumpTo; //notice: the last import / build message, kept next to the count
    bool loaded = false, loading = false, building = false; const void* loadedFor = nullptr; uint64_t loadedSeq = 0; int seenGen = -1; juce::uint32 lastApplyMs = 0;
    juce::String selectedId; int sortCol = 3; bool sortFwd = true; bool restoredColumns = false;
    juce::Rectangle<int> topArea, bigBar, legendArea, smallBars[3]; double hoverFrac = -1;
    juce::TableListBox table;
    juce::TextButton shareButton{"Share packs..."}, sharedButton{"Shared: shown"};
    void updateSharedButton() { sharedButton.setButtonText(showShared ? "Shared: shown" : "Shared: hidden"); }
    //per library: overview-settings.json (only whether shared records show, for now)
    juce::File overviewSettingsFile() const { auto lib = getLibrary ? getLibrary() : nullptr; return lib ? lib->folder.getChildFile("overview-settings.json") : juce::File(); }
    void saveOverviewSettings() { const auto f = overviewSettingsFile(); if (f == juce::File()) return; auto* o = new juce::DynamicObject(); o->setProperty("showShared", showShared); f.replaceWithText(juce::JSON::toString(juce::var(o))); }
    void loadOverviewSettings() { const auto f = overviewSettingsFile(); showShared = true; if (f.existsAsFile()) showShared = (bool)juce::JSON::parse(f).getProperty("showShared", true); updateSharedButton(); }
    std::unique_ptr<juce::FileChooser> chooser;
    juce::TextButton playButton{"Play"}, linesButton{"Overlay lines"}, buildButton{"Build / update records"}, compareButton{"Compare versions"};
    juce::ComboBox sourceBox; bool compareMode = false;
    static constexpr const char* kSources[8] = {"Tidal", "Spotify", "Apple Music", "Vinyl", "CD", "HDtracks", "Qobuz", "Other"};
    void shareMenu() {
        auto lib = getLibrary ? getLibrary() : nullptr;
        if (lib == nullptr) return;
        juce::PopupMenu m;
        m.addItem(1, "Export a display pack, with track names (small: bars, grades, tags)...");
        m.addItem(2, "Export a full pack, with track names (adds the replay blocks so others can regrade)...");
        m.addSeparator();
        m.addItem(11, "Export a display pack, no track names...");
        m.addItem(12, "Export a full pack, no track names...");
        m.addSeparator();
        m.addItem(3, "Import a pack...");
        m.showMenuAsync(juce::PopupMenu::Options(), [this, lib](int r) {
            if (r == 0) return;
            if (r == 3) {
                chooser = std::make_unique<juce::FileChooser>("Choose a meterDARK pack", juce::File(), "*.mdarkpack");
                chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this, lib](const juce::FileChooser& fc) {
                    const juce::File f = fc.getResult();
                    if (f == juce::File()) return;
                    notice = "Importing the pack ..."; resort();
                    juce::Component::SafePointer<GlanceView> sp(this);
                    pool.addJob([this, sp, lib, f] { //a full pack of thousands of tracks takes a moment: not on the window's thread
                        std::vector<juce::String> fresh;
                        const juce::String msg = lib->importPack(f, nullptr, &fresh);
                        juce::MessageManager::callAsync([this, sp, msg, fresh] {
                            if (sp == nullptr) return;
                            notice = msg;
                            if (!fresh.empty()) { jumpTo = fresh.front(); if (!showShared) { showShared = true; saveOverviewSettings(); updateSharedButton(); } }
                            loaded = false; resort();
                        });
                    });
                });
                return;
            }
            const bool full = (r % 10) == 2, names = r < 10;
            chooser = std::make_unique<juce::FileChooser>("Save the pack", juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile(full ? "meterDARK-full.mdarkpack" : "meterDARK-display.mdarkpack"), "*.mdarkpack");
            chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting, [this, lib, full, names](const juce::FileChooser& fc) {
                if (fc.getResult() == juce::File()) return;
                std::vector<juce::String> ids; for (auto& i : items) ids.push_back(i.id);
                const juce::File out = fc.getResult().withFileExtension("mdarkpack");
                const int n = lib->exportPack(out, full, ids, names);
                notice = n < 0 ? juce::String("Could not write the pack.") : juce::String(n) + " records saved (" + juce::String((double)out.getSize()/1024.0, 0) + " KB). "
                         + (names ? "Track names are in it; no folders or paths." : "No track names, folders or paths are in it.");
                resort();
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
    juce::TextEditor search;
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
    static void drawBar(juce::Graphics& g, juce::Rectangle<int> r, const mdark::LeanRecord& rec, int which) {
        if (rec.columns <= 0 || r.getWidth() <= 0) return;
        for (int x = 0; x < r.getWidth(); ++x) {
            const int c = juce::jmin(rec.columns - 1, (int)((int64_t)x*rec.columns/r.getWidth()));
            juce::Colour col;
            if (which == 0) col = juce::Colour(rec.toneRgb[(size_t)c*3], rec.toneRgb[(size_t)c*3 + 1], rec.toneRgb[(size_t)c*3 + 2]);
            else col = devColour(which == 1 ? rec.devWeight[(size_t)c] : which == 2 ? rec.devPeaks[(size_t)c] : rec.devSlew[(size_t)c], which == 1 ? 0 : which == 2 ? 1 : 2);
            g.setColour(col); g.fillRect(r.getX() + x, r.getY(), 1, r.getHeight());
        }
    }
    static void drawLine(juce::Graphics& g, juce::Rectangle<int> r, const std::vector<uint8_t>& v, juce::Colour c) {
        if (v.size() < 2 || r.getWidth() < 2) return;
        juce::Path p;
        for (int x = 0; x < r.getWidth(); ++x) {
            const size_t col = juce::jmin(v.size() - 1, (size_t)((int64_t)x*(int64_t)v.size()/r.getWidth()));
            const float y = (float)r.getBottom() - 1.0f - (float)v[col]/200.0f*(float)(r.getHeight() - 2);
            if (x == 0) p.startNewSubPath((float)r.getX(), y); else p.lineTo((float)(r.getX() + x), y);
        }
        g.setColour(c.withAlpha(0.95f)); g.strokePath(p, juce::PathStrokeType(1.3f));
    }

    //---------------------------------------------------------------- data
    void resort() {
        order.clear();
        juce::StringArray words; words.addTokens(search.getText().trim().toLowerCase(), " ", "\"");
        words.removeEmptyStrings();
        for (size_t i = 0; i < items.size(); ++i) { //the search box: every word must appear (the same rule as the Full table's)
            bool ok = true; for (auto& w : words) if (!items[i].hay.contains(w)) {ok = false; break;}
            if (ok) order.push_back(i);
        }
        int nShared = 0; for (auto& i : items) nShared += i.shared ? 1 : 0;
        const juce::String all = juce::String((int)items.size() - nShared) + " tracks" + (nShared > 0 ? " + " + juce::String(nShared) + " shared" : juce::String());
        status.setText((words.isEmpty() ? all : juce::String((int)order.size()) + " of " + all) + (notice.isNotEmpty() ? "      " + notice : juce::String()), juce::dontSendNotification);
        std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
            if (sortCol == 1) { const int c = items[a].name.compareNatural(items[b].name); return sortFwd ? c < 0 : c > 0; }
            if (sortCol >= 4) {
                if (isNumericCol(sortCol)) { const double x = cellNumber(items[a], sortCol), y = cellNumber(items[b], sortCol); return sortFwd ? x < y : x > y; }
                const int c = cellText(items[a], sortCol).compareNatural(cellText(items[b], sortCol)); return sortFwd ? c < 0 : c > 0;
            }
            const int c = mdark::compareGradeTextDir(items[a].grade.toStdString(), items[b].grade.toStdString(), sortFwd); //grade TEXT, like Finder; ungraded last
            if (c != 0) return c < 0;
            return items[a].name.compareNatural(items[b].name) < 0;
        });
        table.updateContent(); table.repaint();
        const bool scroll = scrollToSelected; scrollToSelected = false;
        for (size_t i = 0; i < order.size(); ++i) if (items[order[i]].id == selectedId) {table.selectRow((int)i, !scroll, true); return;}
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
        for (auto& i : items) { i.group = -1; i.groupSize = 1; i.best = false; }
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
                    {const int c = mdark::compareGradeTextDir(items[i].grade.toStdString(), items[best].grade.toStdString(), true); if (c < 0 || (c == 0 && items[i].sr > items[best].sr)) best = i;} }
                items[best].best = true; ++g;
            }
        }
    }
    //one row of the Overview from the store's memory (the index row, your values and the track's lean record); false = the track has no lean record yet
    static bool makeItem(mdark::TrackStore& store, const mdark::Table& idx, size_t r, const std::map<juce::String, juce::String>* uv, Item& it) {
        const juce::String id = idx.get(r, "id");
        auto rec = store.lean(id);
        if (rec == nullptr) return false;
        it = Item(); it.id = id; it.rec = *rec;
        const juce::String artist = juce::String(it.rec.get("artist")), title = juce::String(it.rec.get("title"));
        it.name = title.isNotEmpty() ? (artist.isNotEmpty() ? artist + " - " + title : title) : juce::File(idx.get(r, "file")).getFileNameWithoutExtension();
        it.grade = juce::String(it.rec.get("grade"));
        const int bonus = juce::String(it.rec.get("bonus_value")).getIntValue();
        const double lv = juce::String(it.rec.get("letter_value")).getDoubleValue();
        it.key = (bonus >= 1 && bonus <= 80) ? (double)bonus : 1000.0 - lv;
        it.path = idx.get(r, "source_path");
        it.bpm = idx.get(r, "bpm_est"); it.musicalKey = idx.get(r, "key_est");
        if (uv) { auto b = uv->find("bpm"); if (b != uv->end() && b->second.isNotEmpty()) it.bpm = b->second; auto k = uv->find("key"); if (k != uv->end() && k->second.isNotEmpty()) it.musicalKey = k->second; }
        it.source = juce::String(it.rec.get("source"));
        it.dur = juce::String(it.rec.get("length_s")).getDoubleValue(); it.sr = juce::String(it.rec.get("sample_rate")).getDoubleValue();
        it.song = songKey(artist, title, idx.get(r, "file"));
        { //what the search box looks in: name, file, artist, title, album, year, genre, BPM, key, source, and any value of yours
            juce::String h = it.name + " " + idx.get(r, "file");
            for (const char* k : {"artist", "title", "album", "year", "genre"}) h << " " << juce::String(it.rec.get(k)) << " " << idx.get(r, k);
            h << " " << it.bpm << " " << it.musicalKey << " " << it.source;
            if (uv) for (auto& kv : *uv) h << " " << kv.second;
            it.hay = h.toLowerCase();
        }
        return true;
    }
    static Item makeSharedItem(const juce::String& id, const mdark::LeanRecord& rec) {
        Item it; it.id = id; it.shared = true; it.rec = rec;
        const juce::String artist = juce::String(it.rec.get("artist")), title = juce::String(it.rec.get("title"));
        it.name = sharedRecordName(rec, id) + "  (shared)";
        it.grade = juce::String(it.rec.get("grade"));
        const int bonus = juce::String(it.rec.get("bonus_value")).getIntValue();
        it.key = (bonus >= 1 && bonus <= 80) ? (double)bonus : 1000.0 - juce::String(it.rec.get("letter_value")).getDoubleValue();
        it.source = juce::String(it.rec.get("source"));
        it.dur = juce::String(it.rec.get("length_s")).getDoubleValue(); it.sr = juce::String(it.rec.get("sample_rate")).getDoubleValue();
        it.song = songKey(artist, title, {});
        it.hay = (it.name + " " + juce::String(it.rec.get("artist")) + " " + juce::String(it.rec.get("title")) + " " + juce::String(it.rec.get("album")) + " " + juce::String(it.rec.get("year")) + " " + juce::String(it.rec.get("genre")) + " " + it.source).toLowerCase();
        return it;
    }
    static std::vector<Item> gather(const std::shared_ptr<mdark::TrackStore>& store, const std::shared_ptr<const std::set<juce::String>>& allowed, bool withShared) {
        std::vector<Item> out;
        const auto idx = store->index();
        const auto users = store->userValues();
        for (size_t r = 0; r < idx.rows.size(); ++r) {
            const juce::String id = idx.get(r, "id");
            if (allowed && !allowed->count(id)) continue;
            auto u = users.find(id);
            Item it;
            if (makeItem(*store, idx, r, u != users.end() ? &u->second : nullptr, it)) out.push_back(std::move(it));
        }
        if (withShared) { //records shared with you (lean-shared): always shown, marked "(shared)". The Library's filters only judge your own tracks, so they leave these alone.
            std::set<juce::String> have; for (size_t r = 0; r < idx.rows.size(); ++r) have.insert(idx.get(r, "id")); //one you measured yourself is shown as yours
            for (auto& id : store->sharedIds()) {
                if (have.count(id)) continue;
                auto rec = store->shared(id);
                if (rec != nullptr) out.push_back(makeSharedItem(id, *rec));
            }
        }
        groupVersions(out);
        return out;
    }
    //One track changed: only its row is made again (or added / dropped); everything else stays as it is. Anything bigger loads the list again (from memory).
    void applyChanges(const std::shared_ptr<mdark::TrackStore>& store) {
        std::vector<mdark::StoreChange> ch; uint64_t now = 0;
        if (!store->changesSince(loadedSeq, ch, now)) { loaded = false; return; }
        const auto allowed = getAllowed ? getAllowed() : nullptr;
        bool any = false;
        for (auto& c : ch) {
            if (c.id.isEmpty()) { loaded = false; return; } //columns, a reset, lean records of many tracks, shared records: the list again (from memory)
            auto at = std::find_if(items.begin(), items.end(), [&](const Item& i) { return i.id == c.id; });
            if (c.kind == mdark::StoreChange::Removed) { if (at != items.end()) { items.erase(at); any = true; } continue; }
            if (allowed && !allowed->count(c.id)) { if (at != items.end()) { items.erase(at); any = true; } continue; }
            mdark::Table one; juce::StringArray row;
            if (!store->indexRow(c.id, row, &one.header)) continue;
            one.rows.push_back(row);
            auto uv = store->userValuesOf(c.id);
            Item it;
            if (makeItem(*store, one, 0, uv.empty() ? nullptr : &uv, it)) { if (at != items.end()) *at = std::move(it); else items.push_back(std::move(it)); any = true; }
            else if (at != items.end()) { items.erase(at); any = true; }
        }
        loadedSeq = now; lastApplyMs = juce::Time::getMillisecondCounter();
        if (!any) return;
        groupVersions(items);
        resort(); repaint();
    }
    void load() {
        auto store = getStore ? getStore() : nullptr;
        if (store == nullptr || loading) return;
        if (loadedFor != store.get()) loadOverviewSettings(); //another library: its own setting
        loading = true; loadedFor = store.get(); loadedSeq = store->seq(); seenGen = getFilterGen ? getFilterGen() : 0;
        const auto allowed = getAllowed ? getAllowed() : nullptr;
        juce::Component::SafePointer<GlanceView> sp(this);
        const bool withShared = showShared;
        pool.addJob([this, sp, store, allowed, withShared] {
            std::vector<Item> out = gather(store, allowed, withShared);
            juce::MessageManager::callAsync([this, sp, out = std::move(out)]() mutable {
                if (sp == nullptr) return;
                items = std::move(out); loading = false; loaded = true; restoreColumns();
                if (jumpTo.isNotEmpty()) { selectedId = jumpTo; jumpTo = {}; scrollToSelected = true; }
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
                notice = "Records: " + juce::String(rep.built) + " built, " + juce::String(rep.kept) + " already there, " + juce::String(rep.noData) + " without window data.";
            });
        });
    }
    void timerCallback() override {
        auto store = getStore ? getStore() : nullptr;
        if (building) status.setText("Building records: " + juce::String(buildDone.load()) + " of " + juce::String(buildTotal.load()) + " ...", juce::dontSendNotification);
        else if (store != nullptr && !loading && isShowing()) {
            if (!loaded || store.get() != loadedFor || (getFilterGen && getFilterGen() != seenGen)) load();
            else if (store->seq() != loadedSeq && juce::Time::getMillisecondCounter() - lastApplyMs > 400) applyChanges(store);
        }
        if (transportReady) {
            if (!transport.isPlaying() && playButton.getButtonText() == "Stop") playButton.setButtonText("Play");
            repaint(topArea); //the playhead and the yellow time / who-leads line follow playback live
        }
    }

    //---------------------------------------------------------------- playing
    void stopPlayback() {
        transport.stop(); transport.setSource(nullptr); readerSource.reset(); transportReady = false;
        playButton.setButtonText("Play");
    }
    void togglePlay() {
        if (transportReady && transport.isPlaying()) { stopPlayback(); repaint(); return; }
        startPlayback(0.0);
    }
    void startPlayback(double startFrac) {
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
        transport.setPosition(transport.getLengthInSeconds()*startFrac); transport.start(); transportReady = true;
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
        fullTable.reserveRight = 146; glance.setBounds(r); fullTable.setBounds(r);
        toggle.setBounds(getWidth() - 138, 4, 130, 24); toggle.toFront(false);
    }
private:
    LibraryView& fullTable;
    juce::TextButton toggle{"Full table"};
    bool fullShown = false;
};
