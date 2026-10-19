// meterDARK Analyser: the LAB tab (included by MainGui.cpp, inside its anonymous namespace).
//  Hints   - meterDARK's thin/full, headroom/squashed and dark/bright hints, simulated across the whole library as you move
//            the settings. Shows where each track's readings sit and how often each hint would show. Export sends the
//            settings to meterDARK.
//  Scoring - an experimental copy of the newest original Meter's scoring with its constants as settings. Re-score the
//            library to see which grades and rankings move. It never changes the Library tab's real grades.

class LabView : public juce::Component, private juce::Timer {
public:
    std::function<std::shared_ptr<f8k::Library>()> getLibrary;
    std::function<f8k::LevelMode()> getLevel;                                      //the Library tab's level mode (original / normalised / mix)
    std::function<std::shared_ptr<const std::set<juce::String>>()> getAllowed;      //the tracks to use (null = all of them)
    std::function<int()> getFilterGen;                                             //goes up when the filters or the level mode change

    //kind: 0 = hintLAB (the hints only), 1 = gradeLAB (the original scoring experiment only), -1 = both (old layout)
    explicit LabView(int labKind = -1) : kind(labKind) {
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
        for (auto* b : {&fromLibrary, &resetButton, &exportButton, &rescoreButton, &saveButton, &levelTestButton, &lowEndButton}) {addAndMakeVisible(b); styleButton(*b);}
        addAndMakeVisible(percentBox);
        percentBox.addItem("10% each side", 10); percentBox.addItem("15% each side", 15); percentBox.addItem("20% each side", 20); percentBox.addItem("25% each side", 25);
        percentBox.setSelectedId(15, juce::dontSendNotification);
        percentBox.setColour(juce::ComboBox::backgroundColourId, kPanel); percentBox.setColour(juce::ComboBox::textColourId, kText); percentBox.setColour(juce::ComboBox::outlineColourId, kPanel);
        fromLibrary.onClick = [this] { setFromLibrary(percentBox.getSelectedId()/100.0); };
        resetButton.onClick = [this] { if (mode == 0) hints = f8k::HintSettings(); else score = f8k::ScoreParams(); paintEvery = 1; buildSettings(); recompute(); };
        exportButton.onClick = [this] { exportToMeter(); };
        levelTestButton.onClick = [this] { runLevelTest(); };
        lowEndButton.onClick = [this] { measureLowEnd(); };
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
        addAndMakeVisible(dist); addAndMakeVisible(scatter);
        model.owner = this; dist.owner = this; scatter.owner = this;
        addAndMakeVisible(blocksToggle); styleButton(blocksToggle);
        blocksToggle.setClickingTogglesState(true); blocksToggle.setColour(juce::TextButton::buttonOnColourId, kSelect);
        blocksToggle.setTooltip("Regrade from each track's lean record (its packed replay block) instead of the stored window data. This is what a shared pack gives you.");
        blocksToggle.onClick = [this] { useBlocks = blocksToggle.getToggleState(); loaded = false; };
        if (kind == 0) {scoreMode.setVisible(false); blocksToggle.setVisible(false); hintsMode.setButtonText("hintLAB");}
        if (kind == 1) {hintsMode.setVisible(false); scoreMode.setButtonText("gradeLAB: original scoring (experiment)");}
        buildSettings();
        setMode(kind == 1 ? 1 : 0);
        startTimer(200);
    }
    ~LabView() override { stopTimer(); pool.removeAllJobs(true, 10000); }

    void resized() override {
        auto r = getLocalBounds().reduced(12);
        auto top = r.removeFromTop(28);
        if (kind == 1) {scoreMode.setBounds(top.removeFromLeft(290)); top.removeFromLeft(6); blocksToggle.setBounds(top.removeFromLeft(200)); top.removeFromLeft(16);}
        else {hintsMode.setBounds(top.removeFromLeft(150)); top.removeFromLeft(6); if (kind != 0) scoreMode.setBounds(top.removeFromLeft(210)); top.removeFromLeft(16);}
        status.setBounds(top);
        r.removeFromTop(8);
        auto left = r.removeFromLeft(400);
        auto buttons = left.removeFromBottom(mode == 0 ? 100 : 66);
        auto b1 = buttons.removeFromTop(30), b2 = buttons.removeFromBottom(30);
        if (mode == 0) {
            auto b3 = buttons.removeFromTop(34).withTrimmedTop(4); levelTestButton.setBounds(b3.removeFromLeft(150)); b3.removeFromLeft(6); lowEndButton.setBounds(b3);
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
        if (mode == 0) {dist.setVisible(true); dist.setBounds(r.removeFromTop(110)); r.removeFromTop(6); scatter.setVisible(true); scatter.setBounds(r.removeFromTop(200)); r.removeFromTop(8);} else {dist.setVisible(false); scatter.setVisible(false);}
        table.setBounds(r);
    }
    void paint(juce::Graphics& g) override { g.fillAll(kBg); }

private:
    //---------------------------------------------------------------- data
    struct Track { juce::String id, file, grade, gradeBonus; double letterValue = 0; std::vector<std::string> names; std::vector<float> core; std::vector<std::string> names2; std::vector<float> core2; double sr = 44100; };
    std::vector<Track> tracks;          //loaded once per library (both the 0.5.5 data for the hints and the newest version's data for scoring)
    const int kind; bool useBlocks = false; juce::TextButton blocksToggle{"Use replay blocks"};
    bool testing = false;
    bool loading = false, loaded = false; const void* loadedFor = nullptr; int loadedChange = -1;
    std::vector<f8k::HintTrackResult> hintResults;
    std::map<juce::String, double> lowEnd; bool measuring = false; std::atomic<int> lowDone{0}, lowTotal{0}; //real low-end share per track id (-1 = no fine data)
    struct ScoreRow { juce::String grade, bonus; double letterValue = 0; bool done = false; };
    std::vector<ScoreRow> scoreResults;
    int origRank(size_t i) const { return i < origRanks.size() ? origRanks[i] : 0; }
    std::vector<int> origRanks, labRanks;
    juce::CriticalSection dataLock;
    juce::ThreadPool pool{juce::jmax(1, (int)std::thread::hardware_concurrency() - 1)};
    std::atomic<int> generation{0};
    std::atomic<int> scoreDone{0}; int scoreTotal = 0; bool scoring = false;
    std::vector<char> use;                       //1 = the track is used (the Library's filters), same order as tracks
    f8k::LevelMode loadedLevel; int seenGen = -1;
    std::atomic<int> loadDone{0}, loadTotal{0};
    bool inUse(size_t i) const { return i >= use.size() || use[i]; }
    void refreshUse() {
        use.assign(tracks.size(), 1);
        const auto allowed = getAllowed ? getAllowed() : nullptr;
        if (allowed) for (size_t i = 0; i < tracks.size(); ++i) use[i] = allowed->count(tracks[i].id) ? 1 : 0;
    }
    int usedCount() const {int n = 0; for (size_t i = 0; i < tracks.size(); ++i) n += inUse(i); return n;}

    f8k::HintSettings hints;
    f8k::ScoreParams score;
    int paintEvery = 1;
    int mode = 0;

    void timerCallback() override {
        handlePending();
        auto lib = getLibrary ? getLibrary() : nullptr;
        if (lib == nullptr) return;
        const f8k::LevelMode lvNow = getLevel ? getLevel() : f8k::LevelMode();
        if (!loading && !scoring && (!loaded || lib.get() != loadedFor || (isShowing() && (lib->changeCount() != loadedChange || lvNow.key() != loadedLevel.key())))) load(lib);
        else if (loaded && !loading && !scoring && getFilterGen && getFilterGen() != seenGen) { //the Library's filters changed
            seenGen = getFilterGen();
            refreshUse(); origRanks = ranks(false); labRanks.clear(); scoreResults.assign(tracks.size(), {});
            recompute(); model.refresh();
        }
        if (measuring) status.setText("Measuring the real low end: " + juce::String(lowDone.load()) + " of " + juce::String(lowTotal.load()) + " tracks (first time only, it is cached after) ...", juce::dontSendNotification);
        if (loading && loadTotal.load() > 0)
            status.setText("Grading the library in " + juce::String(loadedLevel.mode == 1 ? "normalised" : "mix") + " mode: " + juce::String(loadDone.load()) + " of " + juce::String(loadTotal.load()) + " tracks ...", juce::dontSendNotification);
        if (scoring) {
            status.setText("Re-scoring with the Lab settings: " + juce::String(scoreDone.load()) + " of " + juce::String(scoreTotal) + " tracks ...", juce::dontSendNotification);
            if (scoreDone.load() >= scoreTotal) {scoring = false; finishRescore();}
        }
    }

    void load(std::shared_ptr<f8k::Library> lib) {
        loading = true;
        loadedFor = lib.get(); loadedChange = lib->changeCount();
        const f8k::LevelMode lm = getLevel ? getLevel() : f8k::LevelMode();
        loadedLevel = lm; loadDone = 0; loadTotal = 0;
        status.setText("Loading the library's window data ...", juce::dontSendNotification);
        juce::Component::SafePointer<LabView> sp(this);
        const bool fromBlocks = useBlocks;
        pool.addJob([this, sp, lib, lm, fromBlocks] {
            std::vector<Track> t;
            const auto idx = lib->index();
            const auto vids = f8k::Library::versionIds();
            const juce::String newest = juce::String(f8k::tunableVersionId());
            //everything the Lab reads is at 96 kHz (as the Library shows it); a track not rescanned yet is read as the file has it
            auto g = lm.mode == 0 ? lib->grades(newest, 96000)
                                  : lib->levelGrades(newest, 96000, lm, true, [&] {return sp == nullptr;}, [&](int d, int n) {loadDone = d; loadTotal = n;});
            {
                const auto nat = lib->grades(newest);
                if (g.header.isEmpty()) g = nat;
                else for (size_t i = 0; i < nat.rows.size(); ++i) if (g.findRow("id", nat.get(i, "id")) < 0) {juce::StringArray c; for (auto& h : g.header) c.add(nat.get(i, h)); g.rows.push_back(c);}
            }
            for (size_t r = 0; r < idx.rows.size(); ++r) {
                Track tr; tr.id = idx.get(r, "id"); tr.file = idx.get(r, "file"); tr.sr = idx.get(r, "sample_rate").getDoubleValue();
                const int gr = g.findRow("id", tr.id);
                if (gr >= 0) {tr.grade = g.get((size_t)gr, "grade"); tr.gradeBonus = g.get((size_t)gr, "bonus"); tr.letterValue = g.get((size_t)gr, "letter_value").getDoubleValue();}
                bool gotBlock = false;
                if (fromBlocks) { //the lean record's replay block (what a shared pack contains): no stored window data needed
                    f8k::LeanRecord lr;
                    if (lib->readLean(tr.id, lr) && f8k::unpackWindows(lr.replay, tr.names2, tr.core2)) {gotBlock = true; tr.sr = juce::String(lr.get("window_rate")).getDoubleValue(); if (tr.sr <= 0) tr.sr = 96000.0;}
                }
                if (gotBlock) {}
                else if (!lib->readCoreAt(tr.id, newest, 96000, tr.names2, tr.core2)) lib->readCore(tr.id, newest, tr.names2, tr.core2); //the newest version's windows at 96 kHz
                else tr.sr = 96000.0;
                tr.names = tr.names2; tr.core = tr.core2; //the hints read the same windows (they only need peak, slew and zero)
                if (tr.core.empty()) lib->readCore(tr.id, vids.front(), tr.names, tr.core);
                if (!tr.core.empty() || !tr.core2.empty()) t.push_back(std::move(tr));
            }
            juce::MessageManager::callAsync([this, sp, t = std::move(t)]() mutable {
                if (sp == nullptr) return;
                {const juce::ScopedLock sl(dataLock); tracks = std::move(t);}
                loading = false; loaded = true;
                seenGen = getFilterGen ? getFilterGen() : 0;
                refreshUse();
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
        const std::vector<char> useCopy = use;
        juce::Component::SafePointer<LabView> sp(this);
        pool.addJob([this, sp, gen, s, useCopy] {
            std::vector<f8k::HintTrackResult> res;
            {
                const juce::ScopedLock sl(dataLock);
                for (size_t i = 0; i < tracks.size(); ++i) {
                    const auto& t = tracks[i];
                    if (generation.load() != gen) return; //a newer change is coming
                    const bool used = i >= useCopy.size() || useCopy[i];
                    res.push_back(t.core.empty() || !used ? f8k::HintTrackResult() : f8k::simulateHints(t.names, t.core, s));
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
    //4a: does the weight reading follow the real low end? Reads each track's fine octave bands once (cached), then the scatter plots compare it with the old and new weight readings.
    void measureLowEnd() {
        auto lib = getLibrary ? getLibrary() : nullptr;
        if (lib == nullptr || !loaded || loading || measuring) return;
        measuring = true; lowDone = 0; lowTotal = 0;
        juce::Component::SafePointer<LabView> sp(this);
        pool.addJob([this, sp, lib] {
            auto res = lib->lowEndShares([&] {return sp == nullptr;}, [&](int d, int n) {lowDone = d; lowTotal = n;});
            juce::MessageManager::callAsync([this, sp, res = std::move(res)]() mutable {
                if (sp == nullptr) return;
                lowEnd = std::move(res); measuring = false;
                status.setText("Low end measured. " + scatter.summary(), juce::dontSendNotification);
                scatter.repaint(); model.refresh();
            });
        });
        startTimer(200);
    }
    //LEVEL TEST: the library's tracks replayed 0, 6, 12, 18 and 24 dB quieter through the hint maths with the current settings; shows how far each reading moves (target 0)
    void runLevelTest() {
        if (!loaded || loading || testing) return;
        testing = true;
        status.setText("Level test: replaying every track at five levels ...", juce::dontSendNotification);
        const f8k::HintSettings s = hints;
        juce::Component::SafePointer<LabView> sp(this);
        pool.addJob([this, sp, s] {
            std::vector<f8k::LevelTestRow> rows;
            {
                const juce::ScopedLock sl(dataLock);
                std::vector<f8k::LevelTestTrack> tr;
                for (size_t i = 0; i < tracks.size(); ++i) if (!tracks[i].core.empty() && inUse(i)) tr.push_back({&tracks[i].names, &tracks[i].core});
                rows = f8k::levelTest(tr, s, {0.0, -6.0, -12.0, -18.0, -24.0}, [&] { return sp == nullptr; });
            }
            const juce::String text = juce::String(f8k::levelTestReport(rows)) +
                "\nHow to read it: each number is how far, on average, a reading (or the share of time a hint shows) moves when the same music is played that much quieter. "
                "0 = the hint does not care about level. 'audible' is the share of the track the hints count at that level.\n"
                "The settings used are the ones on the left now. 'level cut limit' lower than 0.8 (try 0.6) also flattens very hot masters.";
            juce::MessageManager::callAsync([this, sp, text] {
                if (sp == nullptr) return;
                testing = false;
                summarise();
                auto* w = new juce::AlertWindow("Level test", {}, juce::MessageBoxIconType::NoIcon, this);
                auto* ed = new juce::TextEditor();
                ed->setMultiLine(true); ed->setReadOnly(true); ed->setText(text); ed->setSize(820, 360);
                ed->setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 13.0f, juce::Font::plain));
                w->addCustomComponent(ed);
                w->addButton("Close", 0, juce::KeyPress(juce::KeyPress::escapeKey));
                w->enterModalState(true, juce::ModalCallbackFunction::create([ed](int) { delete ed; }), true);
            });
        });
    }
    void summarise() {
        if (mode != 0) return;
        int n = 0, thin = 0, full = 0, head = 0, squash = 0, dark = 0, bright = 0;
        for (auto& r : hintResults) {
            if (r.audible == 0) continue;
            ++n;
            thin += r.thin > 0.5; full += r.full > 0.5; head += r.headroom > 0.5; squash += r.squashed > 0.5; dark += r.dark > 0.5; bright += r.bright > 0.5;
        }
        status.setText(juce::String(n) + " tracks" + (usedCount() < (int)tracks.size() ? " (filtered from " + juce::String((int)tracks.size()) + ")" : juce::String()) + ".  Hint showing most of the time:  thin " + juce::String(thin) + ", full " + juce::String(full)
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
        std::vector<double> w, d, dk;
        for (auto& r : hintResults) if (r.audible > 0) {w.push_back(r.weight); d.push_back(r.dynamics); dk.push_back(r.tilt);}
        if (w.size() < 5) {status.setText("Add a few more tracks first (at least 5).", juce::dontSendNotification); return;}
        auto set = [this](const char* k, double v) {const int i = f8k::hintParamIndex(k); const auto& p = f8k::hintParams()[(size_t)i]; hints.v[(size_t)i] = juce::jlimit(p.lo, p.hi, v);};
        const double wm = percentile(w, 0.5), dm = percentile(d, 0.5);
        set("tune9", wm); set("tune0", wm - percentile(w, share)); set("tune1", percentile(w, 1 - share) - wm);
        set("tune11", dm); set("tune2", dm - percentile(d, share)); set("tune3", percentile(d, 1 - share) - dm);
        const double km = percentile(dk, 0.5);
        set("tune18", km); set("tune4", km - percentile(dk, share)); set("tune5", percentile(dk, 1 - share) - km);
        buildSettings(); recompute();
    }

    void exportToMeter() {
        //1. the constants, for meterDARK to read (lab-tuning.xml). 2. the trigger settings, into meterDARK's saved default preset (defaults.xml)
        const int pluginVersion = 11; //the preset / project version meterDARK writes (its PluginProcessor::saveUserDefaults); keep in step with the plugin
        const juce::File dir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("meterDARK");
        dir.createDirectory();

        juce::StringArray changed; //what differs from the factory values, shown to the user afterwards
        for (size_t i = 0; i < f8k::hintParams().size(); ++i) {
            const auto& p = f8k::hintParams()[i];
            if (std::fabs(hints.v[i] - p.def) < 1.0e-9) continue;
            changed.add(juce::String(p.group) + ": " + p.label + " = " + juce::String(hints.v[i], p.decimals) + p.unit + "   (factory " + juce::String(p.def, p.decimals) + p.unit + ")  "
                        + (juce::String(p.key).startsWith("tune") ? "[new instances]" : "[live]"));
        }

        juce::XmlElement lab("meterlabtuning");
        lab.setAttribute("ver", 1);
        for (size_t i = 0; i < f8k::hintParams().size(); ++i) {
            const auto& p = f8k::hintParams()[i];
            if (juce::String(p.key).startsWith("tune")) continue;
            lab.setAttribute(p.key, hints.v[i]);
        }
        lab.setAttribute("exported", juce::Time::getCurrentTime().toISO8601(true));
        const bool ok1 = lab.writeTo(dir.getChildFile("lab-tuning.xml"));

        //The default preset. meterDARK steps an older file forward when it loads it (version 8 added the level settings, 9 turned the 96 kHz rate on), so a file we
        //change must already be in the current form: take those steps here, and write the plugin's current version (a fresh file used to be written as version 7, which
        //made the plugin reset the level settings when it loaded it).
        const juce::File defaults = dir.getChildFile("defaults.xml");
        std::unique_ptr<juce::XmlElement> d(juce::XmlDocument::parse(defaults));
        bool replacedOld = false;
        if (d != nullptr && d->hasTagName("meterdefaults")) {
            defaults.copyFileTo(dir.getChildFile("defaults-before-lab.xml"));
            const int oldVer = d->getIntAttribute("ver", 1);
            if (oldVer < 7) {d = std::make_unique<juce::XmlElement>("meterdefaults"); replacedOld = true;} //too old to carry forward: start from the factory preset (the old file is kept)
            else {
                if (oldVer < 8) {d->setAttribute("tune14", 0.0); d->setAttribute("tune15", 16.0/24.0); d->setAttribute("tune16", 19.0/24.0);} //level mode off, its LUFS defaults
                if (oldVer < 9) d->setAttribute("tune13", 1.0);                                                                                //96 kHz rate on
            }
        } else d = std::make_unique<juce::XmlElement>("meterdefaults");
        d->setAttribute("ver", pluginVersion);
        for (size_t i = 0; i < f8k::hintParams().size(); ++i) {
            const auto& p = f8k::hintParams()[i];
            if (juce::String(p.key).startsWith("tune")) d->setAttribute(p.key, hints.v[i]);
        }
        const bool ok2 = d->writeTo(defaults);

        juce::String msg;
        if (!(ok1 && ok2)) msg = "Could not write to " + dir.getFullPathName();
        else {
            if (changed.isEmpty()) msg << "Every value is a factory value, so meterDARK will read exactly as it did before. Change something in hintLAB first to see a difference.\n\n";
            else {
                msg << juce::String(changed.size()) << (changed.size() == 1 ? " setting differs" : " settings differ") << " from factory:\n";
                for (int i = 0; i < changed.size() && i < 14; ++i) msg << "   " << changed[i] << "\n";
                if (changed.size() > 14) msg << "   ... and " << juce::String(changed.size() - 14) << " more\n";
                msg << "\n";
            }
            msg << "[live] settings are read by meterDARK within a second, even while it is open. They only change the hints, never the grade.\n"
                   "[new instances] settings go into your default preset: a meterDARK you open from now on starts with them. One that is already open, and saved projects,\n"
                   "keep their own until you press \"factory\" or double-click the setting in the plugin's tune panel.\n"
                   "Detail decay and fill gate also change the GRADE (and the * shows) but only when \"grade\" is ticked in the plugin's tune panel.\n";
            if (replacedOld) msg << "\nYour old default preset was from a much older meterDARK, so it was replaced by the factory one with these settings.\n";
            msg << "\nYour previous default preset was kept as defaults-before-lab.xml in\n" + dir.getFullPathName();
        }
        juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon, "Export to meterDARK", msg, "OK", this);
    }

    //---------------------------------------------------------------- scoring
    std::vector<int> ranks(bool lab) const { //Chris's order: numbered first (1 up), then by letter value
        std::vector<size_t> order;
        for (size_t i = 0; i < tracks.size(); ++i) if (inUse(i) && (!lab || scoreResults[i].done)) order.push_back(i);
        auto key = [&](size_t i) {
            const juce::String b = lab ? juce::String(scoreResults[i].bonus) : tracks[i].gradeBonus;
            const double lv = lab ? scoreResults[i].letterValue : tracks[i].letterValue;
            return b.isNotEmpty() ? b.getDoubleValue() : 1000.0 - lv;
        };
        std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {return key(a) < key(b);});
        std::vector<int> r(tracks.size(), 0);
        for (size_t k = 0; k < order.size(); ++k) r[order[k]] = (int)k + 1;
        return r;
    }
    void rescore() {
        if (scoring || tracks.empty()) return;
        refreshUse(); origRanks = ranks(false);
        scoring = true; scoreDone = 0; scoreTotal = usedCount();
        scoreResults.assign(tracks.size(), {});
        if (scoreTotal == 0) {scoring = false; status.setText("No track passes the Library's filters.", juce::dontSendNotification); return;}
        const f8k::ScoreParams s = score; const int pe = paintEvery;
        const f8k::LevelMode lv = getLevel ? getLevel() : f8k::LevelMode();
        for (size_t i = 0; i < tracks.size(); ++i) {
            if (!inUse(i)) continue;
            pool.addJob([this, i, s, pe, lv] {
                f8k::scoreParams() = s; //this thread's settings
                ScoreRow row;
                {
                    const Track& t = tracks[i];
                    if (!t.core2.empty()) {
                        std::vector<float> scaled; const std::vector<float>* src = &t.core2; //the level mode's gain on the stored windows
                        if (lv.mode == 1) {scaled = f8k::scaleWindows(t.names2, t.core2, lv.asLufs - lv.inLufs); src = &scaled;}
                        else if (lv.mode == 2) {scaled = f8k::scaleWindowsEach(t.names2, t.core2, f8k::mixWindowGains(t.names2, t.core2, t.sr)); src = &scaled;}
                        auto run = f8k::createTunableRun(t.sr, pe);
                        for (size_t w = 0; w + t.names2.size() <= src->size(); w += t.names2.size()) run->feedWindow(t.names2, &(*src)[w]);
                        const auto res = run->result();
                        row.grade = juce::String(res.grade) + lv.suffix(); row.bonus = juce::String(res.bonus); row.letterValue = res.letterValue;
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
        for (size_t i = 0; i < tracks.size(); ++i) if (inUse(i)) {changed += scoreResults[i].grade != tracks[i].grade; moved += labRanks[i] != origRanks[i];}
        status.setText("Lab scoring (Meter " + juce::String(f8k::findMeterVersion(f8k::tunableVersionId())->version) + "):  " + juce::String(changed) + " of "
                       + juce::String(usedCount()) + " grades changed,  " + juce::String(moved) + " tracks moved in the ranking.", juce::dontSendNotification);
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
            const char* groups[4][2] = {{"weight", "WEIGHT  (thin / full)"}, {"dynamics", "DYNAMICS  (headroom / squashed)"}, {"detail", "DETAIL  (dark / bright, one gauge)"}, {"all", "ALL THREE"}};
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
        for (auto* b : {&fromLibrary, &exportButton, &levelTestButton, &lowEndButton}) b->setVisible(m == 0);
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
                const char* c[] = {"Track", "Grade", "Weight", "Thin %", "Full %", "Dynamics", "Headroom %", "Squashed %", "Dark-bright", "Dark %", "Bright %", "Old weight", "Low end %"};
                for (int i = 0; i < 13; ++i) h.addColumn(c[i], i + 1, i == 0 ? 240 : (i == 1 ? 70 : 72), 30, 600, juce::TableHeaderComponent::defaultFlags);
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
                switch (col) {case 3: return r.weight; case 4: return r.thin; case 5: return r.full; case 6: return r.dynamics; case 7: return r.headroom;
                              case 8: return r.squashed; case 9: return r.tilt; case 10: return r.dark; case 11: return r.bright; case 12: return r.oldWeight;
                              case 13: {const auto it = o.lowEnd.find(o.tracks[(size_t)i].id); return it == o.lowEnd.end() ? 0.0 : juce::jmax(0.0, it->second);}
                              default: return 0;}
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
                const bool pct = col == 4 || col == 5 || col == 7 || col == 8 || col == 10 || col == 11 || col == 13;
                return pct ? juce::String(juce::roundToInt(num(i, col)*100.0)) : juce::String(num(i, col), 2);
            }
            if (col == 3) return i < (int)o.scoreResults.size() && o.scoreResults[(size_t)i].done ? o.scoreResults[(size_t)i].grade : juce::String();
            if (col == 4) return o.origRank((size_t)i) > 0 ? juce::String(o.origRank((size_t)i)) : juce::String();
            if (o.labRanks.empty() || o.labRanks[(size_t)i] == 0) return {};
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
            if (mode == 0 && col >= 4 && col != 6 && col != 9 && col != 12 && col != 13 && num(i, col) > 0.5) c = kAccent; //that hint shows most of the time
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
            const char* names[3] = {"weight  (thin | full)", "dynamics  (headroom | squashed)", "dark | bright  (balance, dark left)"};
            const int rowH = getHeight()/3;
            for (int k = 0; k < 3; ++k) {
                auto r = getLocalBounds().withY(k*rowH).withHeight(rowH).reduced(8, 3);
                g.setColour(kDim); g.setFont(juce::Font(12.0f));
                g.drawText(names[k], r.removeFromLeft(220), juce::Justification::centredLeft);
                const float x0 = (float)r.getX(), wd = (float)r.getWidth(), cy = (float)r.getCentreY();
                g.setColour(kDim.withAlpha(0.4f)); g.fillRect(x0, cy, wd, 1.0f);
                auto line = [&](double v, juce::Colour c) {g.setColour(c); g.fillRect(x0 + wd*(float)juce::jlimit(0.0, 1.0, v) - 1.0f, (float)r.getY(), 2.0f, (float)r.getHeight());};
                const auto& h = o.hints;
                if (k == 0) {line(h["tune9"], kDim); line(h["tune9"] - h["tune0"], kYours); line(h["tune9"] + h["tune1"], kAccent);}
                if (k == 1) {line(h["tune11"], kDim); line(h["tune11"] - h["tune2"], kYours); line(h["tune11"] + h["tune3"], kAccent);}
                if (k == 2) {line(h["tune18"], kDim); line(h["tune18"] - h["tune4"], kYours); line(h["tune18"] + h["tune5"], kAccent);}
                for (auto& t : o.hintResults) {
                    if (t.audible == 0) continue;
                    const double v = k == 0 ? t.weight : (k == 1 ? t.dynamics : t.tilt);
                    g.setColour(kText.withAlpha(0.55f));
                    g.fillEllipse(x0 + wd*(float)juce::jlimit(0.0, 1.0, v) - 2.5f, cy - 2.5f, 5.0f, 5.0f);
                }
            }
        }
    } dist;
    friend struct Dist;

    //real low-end share (across) against the old and the new weight reading (up). A reading that follows the low end rises from the bottom left to the top right.
    struct Scatter : public juce::Component {
        LabView* owner = nullptr;
        static double pearson(const std::vector<double>& a, const std::vector<double>& b) {
            const size_t n = a.size(); if (n < 3) return 0;
            double ma = 0, mb = 0; for (size_t i = 0; i < n; ++i) {ma += a[i]; mb += b[i];}
            ma /= (double)n; mb /= (double)n;
            double sab = 0, saa = 0, sbb = 0;
            for (size_t i = 0; i < n; ++i) {sab += (a[i] - ma)*(b[i] - mb); saa += (a[i] - ma)*(a[i] - ma); sbb += (b[i] - mb)*(b[i] - mb);}
            return saa > 0 && sbb > 0 ? sab/std::sqrt(saa*sbb) : 0;
        }
        void collect(std::vector<double>& x, std::vector<double>& oldW, std::vector<double>& newW) const {
            const auto& o = *owner;
            for (size_t i = 0; i < o.tracks.size() && i < o.hintResults.size(); ++i) {
                const auto& r = o.hintResults[i]; if (r.audible == 0) continue;
                const auto it = o.lowEnd.find(o.tracks[i].id); if (it == o.lowEnd.end() || it->second < 0) continue;
                x.push_back(it->second); oldW.push_back(r.oldWeight); newW.push_back(r.weight);
            }
        }
        juce::String summary() const {
            std::vector<double> x, a, b; collect(x, a, b);
            if (x.size() < 3) return "Not enough tracks with low-end data.";
            return "Correlation with the real low end (1 = perfect, 0 = none): old weight " + juce::String(pearson(x, a), 2) + ", new weight " + juce::String(pearson(x, b), 2) + "  (" + juce::String((int)x.size()) + " tracks).";
        }
        void paint(juce::Graphics& g) override {
            g.fillAll(kPanel.darker(0.2f));
            std::vector<double> x, a, b; collect(x, a, b);
            if (x.size() < 3) {g.setColour(kDim); g.setFont(juce::Font(13.0f)); g.drawText("Press \"Measure low end\" to compare the weight readings with the real low-end energy of each track.", getLocalBounds().reduced(10), juce::Justification::centred); return;}
            double xmax = 0; for (double v : x) xmax = juce::jmax(xmax, v);
            xmax = juce::jmax(0.05, xmax);
            auto area = getLocalBounds().reduced(8, 6);
            for (int k = 0; k < 2; ++k) {
                auto r = area.removeFromLeft(area.getWidth()/(2 - k)).reduced(6, 0);
                const auto& y = k == 0 ? a : b;
                g.setColour(kDim); g.setFont(juce::Font(12.0f));
                g.drawText(juce::String(k == 0 ? "OLD weight" : "NEW weight (thin-full)") + "  r = " + juce::String(pearson(x, y), 2) + "      across: real low end, up: reading", r.removeFromTop(16), juce::Justification::centredLeft);
                g.setColour(kDim.withAlpha(0.5f)); g.drawRect(r.toFloat(), 1.0f);
                double ymax = 0; for (double v : y) ymax = juce::jmax(ymax, v);
                ymax = juce::jmax(0.05, k == 1 ? 1.0 : ymax);
                g.setColour((k == 0 ? kDim : kAccent).withAlpha(0.6f));
                for (size_t i = 0; i < x.size(); ++i)
                    g.fillEllipse((float)r.getX() + (float)(x[i]/xmax)*(float)r.getWidth() - 2, (float)r.getBottom() - (float)(y[i]/ymax)*(float)r.getHeight() - 2, 4.0f, 4.0f);
            }
        }
    } scatter;
    friend struct Scatter;

    juce::TextButton hintsMode{"meterDARK hints"}, scoreMode{"Original scoring (experiment)"};
    juce::TextButton lowEndButton{"Measure low end"};
    juce::TextButton fromLibrary{"Set triggers from the library"}, resetButton{"Back to meterDARK's values"}, exportButton{"Export to meterDARK"};
    juce::TextButton rescoreButton{"Re-score library"}, saveButton{"Save settings..."}, levelTestButton{"Level test"};
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
