// meterDARK Analyser: the LAB tab (included by MainGui.cpp, inside its anonymous namespace).
//  Hints   - meterDARK's light/heavy, headroom/squashed and dark/bright hints, simulated across the whole library as you move
//            the settings. Shows where each track's readings sit and how often each hint would show. Export sends the
//            settings to meterDARK.
//  Scoring - an experimental copy of the newest original Meter's scoring with its constants as settings. Re-score the
//            library to see which grades and rankings move. It never changes the Library tab's real grades.

class LabView : public juce::Component, private juce::Timer {
public:
    std::function<std::shared_ptr<f8k::Library>()> getLibrary;

    LabView() {
        for (auto* b : {&hintsMode, &scoreMode}) {
            addAndMakeVisible(b); styleButton(*b); b->setClickingTogglesState(true); b->setRadioGroupId(77);
            b->setColour(juce::TextButton::buttonOnColourId, kSelect); b->setColour(juce::TextButton::textColourOnId, kAccent);
        }
        hintsMode.setToggleState(true, juce::dontSendNotification);
        hintsMode.onClick = [this] { setMode(0); };
        scoreMode.onClick = [this] { setMode(1); };
        addAndMakeVisible(settingsView);
        settingsView.setViewedComponent(&settingsPanel, false);
        settingsView.setScrollBarsShown(true, false);
        for (auto* b : {&fromLibrary, &resetButton, &exportButton, &rescoreButton, &saveButton}) {addAndMakeVisible(b); styleButton(*b);}
        addAndMakeVisible(percentBox);
        percentBox.addItem("10% each side", 10); percentBox.addItem("15% each side", 15); percentBox.addItem("20% each side", 20); percentBox.addItem("25% each side", 25);
        percentBox.setSelectedId(15, juce::dontSendNotification);
        percentBox.setColour(juce::ComboBox::backgroundColourId, kPanel); percentBox.setColour(juce::ComboBox::textColourId, kText); percentBox.setColour(juce::ComboBox::outlineColourId, kPanel);
        fromLibrary.onClick = [this] { setFromLibrary(percentBox.getSelectedId()/100.0); };
        resetButton.onClick = [this] { if (mode == 0) hints = f8k::HintSettings(); else score = f8k::ScoreParams(); paintEvery = 1; buildSettings(); recompute(); };
        exportButton.onClick = [this] { exportToMeter(); };
        rescoreButton.onClick = [this] { rescore(); };
        saveButton.onClick = [this] { saveScoreSettings(); };
        addAndMakeVisible(status);
        status.setColour(juce::Label::textColourId, kDim);
        addAndMakeVisible(table);
        table.setModel(&model);
        table.setColour(juce::ListBox::backgroundColourId, kBg);
        table.setRowHeight(21);
        table.getHeader().setColour(juce::TableHeaderComponent::backgroundColourId, kPanel);
        table.getHeader().setColour(juce::TableHeaderComponent::textColourId, kText);
        table.getHeader().setColour(juce::TableHeaderComponent::outlineColourId, kBg);
        addAndMakeVisible(dist);
        model.owner = this; dist.owner = this;
        buildSettings();
        setMode(0);
        startTimer(200);
    }
    ~LabView() override { stopTimer(); pool.removeAllJobs(true, 10000); }

    void resized() override {
        auto r = getLocalBounds().reduced(12);
        auto top = r.removeFromTop(28);
        hintsMode.setBounds(top.removeFromLeft(150)); top.removeFromLeft(6);
        scoreMode.setBounds(top.removeFromLeft(210)); top.removeFromLeft(16);
        status.setBounds(top);
        r.removeFromTop(8);
        auto left = r.removeFromLeft(400);
        auto buttons = left.removeFromBottom(mode == 0 ? 66 : 66);
        auto b1 = buttons.removeFromTop(30), b2 = buttons.removeFromBottom(30);
        if (mode == 0) {
            fromLibrary.setBounds(b1.removeFromLeft(220)); b1.removeFromLeft(6); percentBox.setBounds(b1);
            resetButton.setBounds(b2.removeFromLeft(196)); b2.removeFromLeft(8); exportButton.setBounds(b2);
        } else {
            rescoreButton.setBounds(b1.removeFromLeft(196)); b1.removeFromLeft(8); saveButton.setBounds(b1);
            resetButton.setBounds(b2.removeFromLeft(196));
        }
        left.removeFromBottom(6);
        settingsView.setBounds(left);
        settingsPanel.setSize(left.getWidth() - 14, settingsPanel.getHeight());
        layoutSettings();
        r.removeFromLeft(12);
        if (mode == 0) {dist.setVisible(true); dist.setBounds(r.removeFromTop(150)); r.removeFromTop(8);} else dist.setVisible(false);
        table.setBounds(r);
    }
    void paint(juce::Graphics& g) override { g.fillAll(kBg); }

private:
    //---------------------------------------------------------------- data
    struct Track { juce::String id, file, grade, gradeBonus; double letterValue = 0; std::vector<std::string> names; std::vector<float> core; std::vector<std::string> names2; std::vector<float> core2; double sr = 44100; };
    std::vector<Track> tracks;          //loaded once per library (both the 0.5.5 data for the hints and the newest version's data for scoring)
    bool loading = false, loaded = false; const void* loadedFor = nullptr; int loadedChange = -1;
    std::vector<f8k::HintTrackResult> hintResults;
    struct ScoreRow { juce::String grade, bonus; double letterValue = 0; bool done = false; };
    std::vector<ScoreRow> scoreResults;
    int origRank(size_t i) const { return i < origRanks.size() ? origRanks[i] : 0; }
    std::vector<int> origRanks, labRanks;
    juce::CriticalSection dataLock;
    juce::ThreadPool pool{juce::jmax(1, (int)std::thread::hardware_concurrency() - 1)};
    std::atomic<int> generation{0};
    std::atomic<int> scoreDone{0}; int scoreTotal = 0; bool scoring = false;

    f8k::HintSettings hints;
    f8k::ScoreParams score;
    int paintEvery = 1;
    int mode = 0;

    void timerCallback() override {
        handlePending();
        auto lib = getLibrary ? getLibrary() : nullptr;
        if (lib == nullptr) return;
        if (!loading && !scoring && (!loaded || lib.get() != loadedFor || (isShowing() && lib->changeCount() != loadedChange))) load(lib);
        if (scoring) {
            status.setText("Re-scoring with the Lab settings: " + juce::String(scoreDone.load()) + " of " + juce::String(scoreTotal) + " tracks ...", juce::dontSendNotification);
            if (scoreDone.load() >= scoreTotal) {scoring = false; finishRescore();}
        }
    }

    void load(std::shared_ptr<f8k::Library> lib) {
        loading = true;
        loadedFor = lib.get(); loadedChange = lib->changeCount();
        status.setText("Loading the library's window data ...", juce::dontSendNotification);
        juce::Component::SafePointer<LabView> sp(this);
        pool.addJob([this, sp, lib] {
            std::vector<Track> t;
            const auto idx = lib->index();
            const auto vids = f8k::Library::versionIds();
            const juce::String newest = juce::String(f8k::tunableVersionId());
            const auto g = lib->grades(newest);
            for (size_t r = 0; r < idx.rows.size(); ++r) {
                Track tr; tr.id = idx.get(r, "id"); tr.file = idx.get(r, "file"); tr.sr = idx.get(r, "sample_rate").getDoubleValue();
                const int gr = g.findRow("id", tr.id);
                if (gr >= 0) {tr.grade = g.get((size_t)gr, "grade"); tr.gradeBonus = g.get((size_t)gr, "bonus"); tr.letterValue = g.get((size_t)gr, "letter_value").getDoubleValue();}
                lib->readCore(tr.id, vids.front(), tr.names, tr.core);
                lib->readCore(tr.id, newest, tr.names2, tr.core2);
                if (!tr.core.empty() || !tr.core2.empty()) t.push_back(std::move(tr));
            }
            juce::MessageManager::callAsync([this, sp, t = std::move(t)]() mutable {
                if (sp == nullptr) return;
                {const juce::ScopedLock sl(dataLock); tracks = std::move(t);}
                loading = false; loaded = true;
                scoreResults.assign(tracks.size(), {}); origRanks = ranks(false);
                recompute();
            });
        });
    }

    //---------------------------------------------------------------- hints
    void recompute() {
        if (mode != 0) {model.refresh(); return;}
        const int gen = ++generation;
        const f8k::HintSettings s = hints;
        juce::Component::SafePointer<LabView> sp(this);
        pool.addJob([this, sp, gen, s] {
            std::vector<f8k::HintTrackResult> res;
            {
                const juce::ScopedLock sl(dataLock);
                for (auto& t : tracks) {
                    if (generation.load() != gen) return; //a newer change is coming
                    res.push_back(t.core.empty() ? f8k::HintTrackResult() : f8k::simulateHints(t.names, t.core, s));
                }
            }
            juce::MessageManager::callAsync([this, sp, gen, res = std::move(res)]() mutable {
                if (sp == nullptr || generation.load() != gen) return;
                hintResults = std::move(res);
                summarise();
                model.refresh(); dist.repaint();
            });
        });
    }
    void summarise() {
        if (mode != 0) return;
        int n = 0, light = 0, heavy = 0, head = 0, squash = 0, dark = 0, bright = 0;
        for (auto& r : hintResults) {
            if (r.audible == 0) continue;
            ++n;
            light += r.light > 0.5; heavy += r.heavy > 0.5; head += r.headroom > 0.5; squash += r.squashed > 0.5; dark += r.dark > 0.5; bright += r.bright > 0.5;
        }
        status.setText(juce::String(n) + " tracks.  Hint showing most of the time:  light " + juce::String(light) + ", heavy " + juce::String(heavy)
                       + "   |   headroom " + juce::String(head) + ", squashed " + juce::String(squash) + "   |   dark " + juce::String(dark) + ", bright " + juce::String(bright),
                       juce::dontSendNotification);
    }
    static double percentile(std::vector<double> v, double p) {
        if (v.empty()) return 0;
        std::sort(v.begin(), v.end());
        const double x = p*(double)(v.size() - 1); const size_t i = (size_t)x; const double f = x - (double)i;
        return i + 1 < v.size() ? v[i]*(1 - f) + v[i + 1]*f : v[i];
    }
    //set each balance point to the middle of the library and each trigger so that about this share of tracks lands beyond it
    void setFromLibrary(double share) {
        std::vector<double> w, d, dk, br;
        for (auto& r : hintResults) if (r.audible > 0) {w.push_back(r.weight); d.push_back(r.dynamics); dk.push_back(r.darkRead); br.push_back(r.brightRead);}
        if (w.size() < 5) {status.setText("Add a few more tracks first (at least 5).", juce::dontSendNotification); return;}
        auto set = [this](const char* k, double v) {const int i = f8k::hintParamIndex(k); const auto& p = f8k::hintParams()[(size_t)i]; hints.v[(size_t)i] = juce::jlimit(p.lo, p.hi, v);};
        const double wm = percentile(w, 0.5), dm = percentile(d, 0.5);
        set("tune9", wm); set("tune0", wm - percentile(w, share)); set("tune1", percentile(w, 1 - share) - wm);
        set("tune11", dm); set("tune2", dm - percentile(d, share)); set("tune3", percentile(d, 1 - share) - dm);
        set("tune4", percentile(dk, share)); set("tune5", percentile(br, 1 - share));
        buildSettings(); recompute();
    }

    void exportToMeter() {
        //1. the constants, for meterDARK to read (lab-tuning.xml). 2. the trigger settings, into meterDARK's saved default preset
        const juce::File dir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("meterDARK");
        dir.createDirectory();
        juce::XmlElement lab("meterlabtuning");
        lab.setAttribute("ver", 1);
        for (size_t i = 0; i < f8k::hintParams().size(); ++i) {
            const auto& p = f8k::hintParams()[i];
            if (juce::String(p.key).startsWith("tune")) continue;
            lab.setAttribute(p.key, hints.v[i]);
        }
        lab.setAttribute("exported", juce::Time::getCurrentTime().toISO8601(true));
        const bool ok1 = lab.writeTo(dir.getChildFile("lab-tuning.xml"));
        const juce::File defaults = dir.getChildFile("defaults.xml");
        std::unique_ptr<juce::XmlElement> d(juce::XmlDocument::parse(defaults));
        if (d == nullptr || !d->hasTagName("meterdefaults")) {d = std::make_unique<juce::XmlElement>("meterdefaults"); d->setAttribute("ver", 7);}
        else defaults.copyFileTo(dir.getChildFile("defaults-before-lab.xml"));
        if (d->getIntAttribute("ver", 1) == 6) d->setAttribute("ver", 7); //version 7 has the Detail decay and fill gate (tune6, tune7), which the Lab now sets
        for (size_t i = 0; i < f8k::hintParams().size(); ++i) {
            const auto& p = f8k::hintParams()[i];
            if (juce::String(p.key).startsWith("tune")) d->setAttribute(p.key, hints.v[i]);
        }
        const bool ok2 = d->writeTo(defaults);
        juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon, "Export to meterDARK",
            (ok1 && ok2) ? "Done. meterDARK picks up the reading settings straight away, even while it is open,\n"
                           "and starts new instances with these hint triggers. Projects you saved keep their own triggers until you press\n"
                           "\"factory\" or double-click a setting in the plugin.\n\nYour previous default preset was kept as defaults-before-lab.xml in\n" + dir.getFullPathName()
                         : "Could not write to " + dir.getFullPathName(), "OK", this);
    }

    //---------------------------------------------------------------- scoring
    std::vector<int> ranks(bool lab) const { //Chris's order: numbered first (1 up), then by letter value
        std::vector<size_t> order(tracks.size());
        for (size_t i = 0; i < order.size(); ++i) order[i] = i;
        auto key = [&](size_t i) {
            const juce::String b = lab ? juce::String(scoreResults[i].bonus) : tracks[i].gradeBonus;
            const double lv = lab ? scoreResults[i].letterValue : tracks[i].letterValue;
            return b.isNotEmpty() ? b.getDoubleValue() : 1000.0 - lv;
        };
        std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {return key(a) < key(b);});
        std::vector<int> r(tracks.size());
        for (size_t k = 0; k < order.size(); ++k) r[order[k]] = (int)k + 1;
        return r;
    }
    void rescore() {
        if (scoring || tracks.empty()) return;
        scoring = true; scoreDone = 0; scoreTotal = (int)tracks.size();
        scoreResults.assign(tracks.size(), {});
        const f8k::ScoreParams s = score; const int pe = paintEvery;
        for (size_t i = 0; i < tracks.size(); ++i) {
            pool.addJob([this, i, s, pe] {
                f8k::scoreParams() = s; //this thread's settings
                ScoreRow row;
                {
                    const Track& t = tracks[i];
                    if (!t.core2.empty()) {
                        auto run = f8k::createTunableRun(t.sr, pe);
                        for (size_t w = 0; w + t.names2.size() <= t.core2.size(); w += t.names2.size()) run->feedWindow(t.names2, &t.core2[w]);
                        const auto res = run->result();
                        row.grade = juce::String(res.grade); row.bonus = juce::String(res.bonus); row.letterValue = res.letterValue;
                    }
                    row.done = true;
                }
                scoreResults[i] = row;
                ++scoreDone;
            });
        }
    }
    void finishRescore() {
        labRanks = ranks(true);
        int changed = 0, moved = 0;
        for (size_t i = 0; i < tracks.size(); ++i) {changed += scoreResults[i].grade != tracks[i].grade; moved += labRanks[i] != origRanks[i];}
        status.setText("Lab scoring (Meter " + juce::String(f8k::findMeterVersion(f8k::tunableVersionId())->version) + "):  " + juce::String(changed) + " of "
                       + juce::String((int)tracks.size()) + " grades changed,  " + juce::String(moved) + " tracks moved in the ranking.", juce::dontSendNotification);
        model.refresh();
    }
    void saveScoreSettings() {
        chooser = std::make_unique<juce::FileChooser>("Save the Lab scoring settings", juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("meter-lab-scoring.json"), "*.json");
        chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting, [this](const juce::FileChooser& fc) {
            const auto f = fc.getResult();
            if (f == juce::File()) return;
            auto* o = new juce::DynamicObject();
            o->setProperty("meter_version", juce::String(f8k::tunableVersionId()));
            for (auto& p : f8k::scoreParamInfo()) o->setProperty(p.key, score.*(p.member));
            o->setProperty("redraws_per_window", paintEvery);
            f.replaceWithText(juce::JSON::toString(juce::var(o)));
        });
    }

    //---------------------------------------------------------------- settings panel
    struct Row { juce::Label name; juce::Slider slider; juce::String group; bool header = false; };
    juce::OwnedArray<Row> rows;
    juce::Component settingsPanel;
    juce::Viewport settingsView;

    void buildSettings() {
        rows.clear();
        auto addHeader = [&](const juce::String& t) {
            auto* r = rows.add(new Row()); r->header = true; r->name.setText(t, juce::dontSendNotification);
            r->name.setFont(juce::Font(15.0f, juce::Font::bold)); r->name.setColour(juce::Label::textColourId, kAccent);
            settingsPanel.addAndMakeVisible(r->name);
        };
        auto addSlider = [&](const juce::String& label, double lo, double hi, double val, int dec, const juce::String& unit, const juce::String& help, std::function<void(double)> set, double def) {
            auto* r = rows.add(new Row());
            r->name.setText(label, juce::dontSendNotification); r->name.setColour(juce::Label::textColourId, kText); r->name.setTooltip(help);
            r->slider.setSliderStyle(juce::Slider::LinearHorizontal);
            r->slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 72, 20);
            r->slider.setRange(lo, hi, dec <= 0 ? 1.0 : std::pow(10.0, -dec));
            r->slider.setSkewFactorFromMidPoint(juce::jlimit(lo + (hi - lo)*0.05, hi - (hi - lo)*0.05, def));
            r->slider.setValue(val, juce::dontSendNotification);
            r->slider.setDoubleClickReturnValue(true, def);
            r->slider.setTextValueSuffix(unit);
            r->slider.setTooltip(help + "   (double-click: back to " + juce::String(def, dec) + unit + ")");
            r->slider.setColour(juce::Slider::textBoxTextColourId, kText); r->slider.setColour(juce::Slider::textBoxOutlineColourId, kPanel);
            r->slider.setColour(juce::Slider::trackColourId, kAccent.withAlpha(0.6f)); r->slider.setColour(juce::Slider::backgroundColourId, kPanel);
            r->slider.onValueChange = [this, set, s = &r->slider] { set(s->getValue()); if (mode == 0) startTimer(120), pendingRecompute = true; };
            settingsPanel.addAndMakeVisible(r->name); settingsPanel.addAndMakeVisible(r->slider);
        };
        if (mode == 0) {
            const char* groups[4][2] = {{"weight", "WEIGHT  (light / heavy)"}, {"dynamics", "DYNAMICS  (headroom / squashed)"}, {"detail", "DETAIL  (dark / bright)"}, {"all", "ALL THREE"}};
            for (auto& g : groups) {
                addHeader(g[1]);
                for (size_t i = 0; i < f8k::hintParams().size(); ++i) {
                    const auto& p = f8k::hintParams()[i];
                    if (juce::String(p.group) != g[0]) continue;
                    addSlider(p.label, p.lo, p.hi, hints.v[i], p.decimals, p.unit, p.help, [this, i](double v) {hints.v[i] = v;}, p.def);
                }
            }
        } else {
            addHeader("ORIGINAL METER " + juce::String(f8k::findMeterVersion(f8k::tunableVersionId())->version) + " SCORING (experiment)");
            const auto& used = f8k::tunableSettingsUsed();
            const f8k::ScoreParams defaults;
            for (auto& p : f8k::scoreParamInfo()) {
                if (juce::String(p.key) != "width" && std::find(used.begin(), used.end(), std::string(p.key)) == used.end()) continue;
                auto member = p.member;
                addSlider(p.label, p.lo, p.hi, score.*member, p.decimals, "", p.help, [this, member](double v) {score.*member = v;}, defaults.*member);
            }
            addSlider("redraws per window", 1, 8, paintEvery, 0, "", "1 = real time. Higher mimics a DAW rendering faster than the screen redraws (how Chris's scores change offline)",
                      [this](double v) {paintEvery = (int)v;}, 1);
        }
        layoutSettings();
    }
    bool pendingRecompute = false;
    void layoutSettings() {
        int y = 4; const int w = settingsPanel.getWidth() > 0 ? settingsPanel.getWidth() : 380;
        for (auto* r : rows) {
            if (r->header) {y += 6; r->name.setBounds(0, y, w, 22); y += 24; continue;}
            r->name.setBounds(0, y, 150, 24);
            r->slider.setBounds(150, y, w - 150, 24);
            y += 27;
        }
        settingsPanel.setSize(w, y + 6);
    }
    void setMode(int m) {
        mode = m;
        for (auto* b : {&fromLibrary, &exportButton}) b->setVisible(m == 0);
        percentBox.setVisible(m == 0);
        for (auto* b : {&rescoreButton, &saveButton}) b->setVisible(m == 1);
        resetButton.setButtonText(m == 0 ? "Back to meterDARK's values" : "Back to the original");
        buildSettings();
        model.setColumns(m);
        resized();
        if (m == 0) recompute();
        else status.setText(scoreResults.empty() || !scoreResults[0].done ? juce::String("Move the settings, then press Re-score library (it takes a minute or so). The Library tab's real grades never change.")
                                                                           : status.getText(), juce::dontSendNotification);
    }

    //---------------------------------------------------------------- the results table
    struct Model : public juce::TableListBoxModel {
        LabView* owner = nullptr; int mode = 0; int sortCol = 1; bool fwd = true; std::vector<int> order;
        juce::TableListBox* table() { return &owner->table; }
        void setColumns(int m) {
            mode = m;
            auto& h = owner->table.getHeader();
            h.removeAllColumns();
            if (m == 0) {
                const char* c[] = {"Track", "Grade", "Weight", "Light %", "Heavy %", "Dynamics", "Headroom %", "Squashed %", "Dark rdg", "Dark %", "Bright rdg", "Bright %"};
                for (int i = 0; i < 12; ++i) h.addColumn(c[i], i + 1, i == 0 ? 240 : (i == 1 ? 70 : 72), 30, 600, juce::TableHeaderComponent::defaultFlags);
            } else {
                const char* c[] = {"Track", "Original grade", "Lab grade", "Original rank", "Lab rank", "Moved"};
                for (int i = 0; i < 6; ++i) h.addColumn(c[i], i + 1, i == 0 ? 260 : 100, 30, 600, juce::TableHeaderComponent::defaultFlags);
            }
            sortCol = 1; fwd = true;
            refresh();
        }
        double num(int i, int col) const {
            const auto& o = *owner;
            if (mode == 0) {
                if (i >= (int)o.hintResults.size()) return 0;
                const auto& r = o.hintResults[(size_t)i];
                switch (col) {case 3: return r.weight; case 4: return r.light; case 5: return r.heavy; case 6: return r.dynamics; case 7: return r.headroom;
                              case 8: return r.squashed; case 9: return r.darkRead; case 10: return r.dark; case 11: return r.brightRead; case 12: return r.bright; default: return 0;}
            }
            switch (col) {case 4: return o.origRank((size_t)i); case 5: return i < (int)o.labRanks.size() ? o.labRanks[(size_t)i] : 0;
                          case 6: return i < (int)o.labRanks.size() ? o.origRank((size_t)i) - o.labRanks[(size_t)i] : 0; default: return 0;}
        }
        juce::String text(int i, int col) const {
            const auto& o = *owner;
            if (i >= (int)o.tracks.size()) return {};
            const auto& t = o.tracks[(size_t)i];
            if (col == 1) return t.file;
            if (col == 2) return t.grade;
            if (mode == 0) {
                if (i >= (int)o.hintResults.size() || o.hintResults[(size_t)i].audible == 0) return {};
                const bool pct = col == 4 || col == 5 || col == 7 || col == 8 || col == 10 || col == 12;
                return pct ? juce::String(juce::roundToInt(num(i, col)*100.0)) : juce::String(num(i, col), 2);
            }
            if (col == 3) return i < (int)o.scoreResults.size() && o.scoreResults[(size_t)i].done ? o.scoreResults[(size_t)i].grade : juce::String();
            if (col == 4) return juce::String(o.origRank((size_t)i));
            if (o.labRanks.empty()) return {};
            if (col == 5) return juce::String(o.labRanks[(size_t)i]);
            const int mv = (int)num(i, 6);
            return mv == 0 ? juce::String("-") : (mv > 0 ? "up " + juce::String(mv) : "down " + juce::String(-mv));
        }
        void refresh() {
            const int n = (int)owner->tracks.size();
            order.resize((size_t)n);
            for (int i = 0; i < n; ++i) order[(size_t)i] = i;
            const bool textCol = sortCol == 1 || sortCol == 2 || (mode == 1 && sortCol == 3);
            std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
                if (mode == 1 && (sortCol == 2 || sortCol == 3)) { //grades in Chris's order
                    const double ka = sortCol == 2 ? owner->origRank((size_t)a) : (owner->labRanks.empty() ? 0 : owner->labRanks[(size_t)a]);
                    const double kb = sortCol == 2 ? owner->origRank((size_t)b) : (owner->labRanks.empty() ? 0 : owner->labRanks[(size_t)b]);
                    return fwd ? ka < kb : ka > kb;
                }
                if (textCol) {const int c = text(a, sortCol).compareNatural(text(b, sortCol)); return fwd ? c < 0 : c > 0;}
                const double va = num(a, sortCol), vb = num(b, sortCol);
                return fwd ? va < vb : va > vb;
            });
            owner->table.updateContent(); owner->table.repaint();
        }
        int getNumRows() override { return (int)order.size(); }
        void paintRowBackground(juce::Graphics& g, int row, int, int, bool sel) override { g.fillAll(sel ? kSelect : (row % 2 ? kRowAlt : kBg)); }
        void paintCell(juce::Graphics& g, int row, int col, int w, int h, bool) override {
            if (row < 0 || row >= (int)order.size()) return;
            const int i = order[(size_t)row];
            juce::Colour c = kText;
            if (mode == 0 && col >= 4 && col != 6 && col != 9 && col != 11 && num(i, col) > 0.5) c = kAccent; //that hint shows most of the time
            if (mode == 1 && col == 3 && i < (int)owner->scoreResults.size() && owner->scoreResults[(size_t)i].done && owner->scoreResults[(size_t)i].grade != owner->tracks[(size_t)i].grade) c = kYours;
            g.setColour(c); g.setFont(juce::Font(14.0f));
            g.drawText(text(i, col), 6, 0, w - 12, h, col <= 2 || (mode == 1 && col == 3) ? juce::Justification::centredLeft : juce::Justification::centredRight, true);
        }
        void sortOrderChanged(int col, bool forwards) override { sortCol = col; fwd = forwards; refresh(); }
    } model;
    friend struct Model;

    //---------------------------------------------------------------- where the library's readings sit
    struct Dist : public juce::Component {
        LabView* owner = nullptr;
        void paint(juce::Graphics& g) override {
            g.fillAll(kPanel.darker(0.2f));
            const auto& o = *owner;
            const char* names[4] = {"weight  (light | heavy)", "dynamics  (headroom | squashed)", "dark reading  (dark below the line)", "bright reading  (bright above the line)"};
            const int rowH = getHeight()/4;
            for (int k = 0; k < 4; ++k) {
                auto r = getLocalBounds().withY(k*rowH).withHeight(rowH).reduced(8, 3);
                g.setColour(kDim); g.setFont(juce::Font(12.0f));
                g.drawText(names[k], r.removeFromLeft(220), juce::Justification::centredLeft);
                const float x0 = (float)r.getX(), wd = (float)r.getWidth(), cy = (float)r.getCentreY();
                g.setColour(kDim.withAlpha(0.4f)); g.fillRect(x0, cy, wd, 1.0f);
                auto line = [&](double v, juce::Colour c) {g.setColour(c); g.fillRect(x0 + wd*(float)juce::jlimit(0.0, 1.0, v) - 1.0f, (float)r.getY(), 2.0f, (float)r.getHeight());};
                const auto& h = o.hints;
                if (k == 0) {line(h["tune9"], kDim); line(h["tune9"] - h["tune0"], kYours); line(h["tune9"] + h["tune1"], kAccent);}
                if (k == 1) {line(h["tune11"], kDim); line(h["tune11"] - h["tune2"], kYours); line(h["tune11"] + h["tune3"], kAccent);}
                if (k == 2) line(h["tune4"], kYours);
                if (k == 3) line(h["tune5"], kAccent);
                for (auto& t : o.hintResults) {
                    if (t.audible == 0) continue;
                    const double v = k == 0 ? t.weight : (k == 1 ? t.dynamics : (k == 2 ? t.darkRead : t.brightRead));
                    g.setColour(kText.withAlpha(0.55f));
                    g.fillEllipse(x0 + wd*(float)juce::jlimit(0.0, 1.0, v) - 2.5f, cy - 2.5f, 5.0f, 5.0f);
                }
            }
        }
    } dist;
    friend struct Dist;

    juce::TextButton hintsMode{"meterDARK hints"}, scoreMode{"Original scoring (experiment)"};
    juce::TextButton fromLibrary{"Set triggers from the library"}, resetButton{"Back to meterDARK's values"}, exportButton{"Export to meterDARK"};
    juce::TextButton rescoreButton{"Re-score library"}, saveButton{"Save settings..."};
    juce::ComboBox percentBox;
    juce::Label status;
    juce::TableListBox table;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::TooltipWindow tooltips{this, 600};

    //debounced recompute after a slider moves
    void handlePending() { if (pendingRecompute) {pendingRecompute = false; recompute();} }
public:
    void visibilityChanged() override { if (isVisible()) timerCallback(); }
};
