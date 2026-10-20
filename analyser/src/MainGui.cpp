// meterDARK Analyser: the window app.
//  "Add tracks" - drop audio files or folders and each track is captured into the library.
//  "Library"    - browse everything in the library: sort by any column, search, fix tags, correct BPM/key,
//                 add your own columns (streams, sales, notes ...), import values from a CSV, remove tracks.
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include "Engine.h"
#include "LabHints.h"
#include "LabScore.h"
#include "GradeRank.h"
#include "TagFill.h"
#include <thread>
#include <set>
#include <map>
#ifndef MDARK_BUILD_NUMBER
#define MDARK_BUILD_NUMBER 0
#endif

namespace {

const juce::Colour kBg(24, 27, 33), kPanel(34, 38, 46), kRowAlt(29, 33, 40), kText(220, 224, 232), kDim(140, 146, 158),
                   kAccent(255, 225, 40), kSelect(60, 70, 92), kYours(120, 200, 255);

juce::File defaultLibrary() {
    const auto docs = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory);
    return docs.getChildFile("meterDARK Library");
}

void styleButton(juce::Button& b)
{
    b.setColour(juce::TextButton::buttonColourId, kPanel);
    b.setColour(juce::TextButton::textColourOffId, kText);
}

//=============================================================================================== worker thread
class Worker : public juce::Thread {
public:
    std::function<void(const juce::String&)> log;                      //called on the message thread
    std::function<void(int, int, double, const juce::String&)> status; //done, total, current progress, current file
    explicit Worker(juce::Component& ownerComp) : juce::Thread("analyser"), owner(&ownerComp) {}
    ~Worker() override { stopThread(6000); }
    void setLibrary(std::shared_ptr<mdark::Library> l) { const juce::ScopedLock sl(lock); library = std::move(l); needUpgrade = true; notify(); }
    void startRescan() { { const juce::ScopedLock sl(lock); needRescan = true; } notify(); }
    void add(const juce::Array<juce::File>& files) {
        { const juce::ScopedLock sl(lock); for (auto& f : files) queue.add(f); total += files.size(); }
        notify();
    }
    void run() override {
        while (!threadShouldExit()) {
            juce::File next; std::shared_ptr<mdark::Library> lib; bool upgrade = false, rescan = false;
            {
                const juce::ScopedLock sl(lock);
                lib = library; upgrade = needUpgrade; needUpgrade = false; rescan = needRescan && !upgrade; if (rescan) needRescan = false;
                if (!upgrade && !rescan && queue.size() > 0) next = queue.removeAndReturn(0);
            }
            if (lib == nullptr) {wait(-1); continue;}
            if (rescan) { //every track scanned by an older analyser is scanned again from its file; each finished track is kept at once
                const double t0 = juce::Time::getMillisecondCounterHiRes();
                const int want = lib->tracksNeedingRescan();
                postLog("Rescanning " + juce::String(want) + " tracks. You can close the app at any time; the next rescan carries on with the tracks not done yet.");
                const int n = lib->rescanLibrary([this] { return threadShouldExit(); },
                    [this, t0](int d, int t, const juce::String& f) {
                        const double secs = (juce::Time::getMillisecondCounterHiRes() - t0)/1000.0;
                        const double left = d > 0 ? secs/d*(t - d) : 0.0;
                        juce::String eta = d >= 3 ? "   about " + (left >= 5400 ? juce::String(left/3600.0, 1) + " hours" : juce::String(juce::roundToInt(left/60.0)) + " min") + " left" : juce::String();
                        post(d, t, 0.0, "rescanning " + f + eta);
                    },
                    [this](const juce::String& m) { postLog(m); });
                postLog("Rescan finished: " + juce::String(n) + " tracks scanned again" + (lib->lastNoAudio > 0 ? ", " + juce::String(lib->lastNoAudio) + " left as they were (file not found or changed)" : juce::String()) + ".");
                post(0, 0, 0.0, {});
                continue;
            }
            if (upgrade) { //tracks scanned by an earlier version get the newer values worked out from their saved data
                const int n = lib->upgradeOldRows([this] { return threadShouldExit(); },
                    [this](int d, int t, const juce::String& f) { post(d, t, 0.0, "updating the library for the newest Meter: " + f); });
                if (n > 0) postLog("Updated " + juce::String(n) + " tracks already in the library (grades from every Meter version, BPM)");
                if (lib->lastNoAudio > 0) postLog(juce::String(lib->lastNoAudio) + " tracks could not get the newest Meter's grades because their audio file was moved or deleted. Add them again to update them.");
                post(0, 0, 0.0, {});
                continue;
            }
            if (next == juce::File()) {post(done, total, 0.0, {}); wait(-1); continue;}
            const juce::String name = next.getFileName();
            post(done, total, 0.0, name);
            const auto r = lib->analyse(next, [this, name](double p) { post(done, total, p, name); }, [this] { return threadShouldExit(); });
            ++done;
            postLog((r.status == mdark::AnalyseOutcome::Added ? "added      " : r.status == mdark::AnalyseOutcome::Updated ? "updated    "
                     : r.status == mdark::AnalyseOutcome::AlreadyThere ? "skipped    " : "NOT ADDED  ") + name + "   (" + r.message + ")");
        }
    }
private:
    void post(int d, int t, double p, const juce::String& n) {
        juce::MessageManager::callAsync([this, sp = owner, d, t, p, n] { if (sp != nullptr && status) status(d, t, p, n); });
    }
    void postLog(const juce::String& line) {
        juce::MessageManager::callAsync([this, sp = owner, line] { if (sp != nullptr && log) log(line); });
    }
    juce::CriticalSection lock;
    juce::Array<juce::File> queue;
    std::shared_ptr<mdark::Library> library;
    bool needUpgrade = false, needRescan = false;
    std::atomic<int> done{0}, total{0};
    juce::Component::SafePointer<juce::Component> owner; //the worker lives inside its owner, so a live owner means a live worker
};

//=============================================================================================== "Add tracks"
class AddPanel : public juce::Component, public juce::FileDragAndDropTarget {
public:
    std::function<void(const juce::Array<juce::File>&)> onFiles;
    std::function<void()> onOpenFolder, onChangeFolder, onRescan;
    AddPanel() {
        for (auto* b : {&addButton, &openButton, &changeButton, &rescanButton}) {addAndMakeVisible(b); styleButton(*b);}
        rescanButton.onClick = [this] { if (onRescan) onRescan(); };
        addChildComponent(updateButton); styleButton(updateButton);
        updateButton.setColour(juce::TextButton::buttonColourId, kAccent.withAlpha(0.25f));
        addButton.onClick = [this] { chooseFiles(); };
        openButton.onClick = [this] { if (onOpenFolder) onOpenFolder(); };
        changeButton.onClick = [this] { if (onChangeFolder) onChangeFolder(); };
        addAndMakeVisible(progressBar);
        progressBar.setColour(juce::ProgressBar::foregroundColourId, kAccent.withAlpha(0.8f));
        progressBar.setColour(juce::ProgressBar::backgroundColourId, kPanel);
        progressBar.setPercentageDisplay(false);
        addAndMakeVisible(logBox);
        logBox.setMultiLine(true); logBox.setReadOnly(true); logBox.setCaretVisible(false); logBox.setScrollbarsShown(true);
        logBox.setColour(juce::TextEditor::backgroundColourId, kPanel);
        logBox.setColour(juce::TextEditor::textColourId, kText);
        logBox.setColour(juce::TextEditor::outlineColourId, kPanel);
        logBox.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 13.0f, juce::Font::plain));
    }
    void log(const juce::String& l) { logBox.moveCaretToEnd(); logBox.insertTextAtCaret(l + "\n"); }
    void showUpdate(const juce::String& title, const juce::URL& page) {
        updateButton.setButtonText("New version available: " + title + "  -  click to download");
        updateButton.onClick = [page] { page.launchInDefaultBrowser(); };
        updateButton.setVisible(true);
        resized();
    }
    void setStatus(int d, int t, double p, const juce::String& n) {
        done = d; total = t; progress = t > 0 ? juce::jlimit(0.0, 1.0, (d + p) / (double)t) : 0.0; current = n; repaint();
    }
    void paint(juce::Graphics& g) override {
        g.fillAll(kBg);
        auto drop = dropArea();
        g.setColour(dragOver ? kAccent.withAlpha(0.15f) : kPanel);
        g.fillRoundedRectangle(drop.toFloat(), 10.0f);
        g.setColour(dragOver ? kAccent : kDim);
        const float dash[2] = {8.0f, 6.0f};
        juce::Path p; p.addRoundedRectangle(drop.toFloat().reduced(1.0f), 10.0f);
        juce::Path d; juce::PathStrokeType(1.5f).createDashedStroke(d, p, dash, 2); g.fillPath(d);
        g.setColour(kText);
        g.setFont(juce::Font(22.0f));
        const int cy = drop.getCentreY();
        g.drawText("Drop audio files or folders here", drop.getX(), cy - 34, drop.getWidth(), 30, juce::Justification::centred);
        g.setColour(kDim);
        g.setFont(juce::Font(14.0f));
        g.drawText("WAV, AIFF, FLAC (lossless is best). Each track is captured once and added to your library.",
                   drop.getX(), cy + 6, drop.getWidth(), 22, juce::Justification::centred);
        g.setColour(kText);
        juce::String st = total == 0 ? juce::String("Nothing added yet")
                        : (done < total ? "Working: " + juce::String(done + 1) + " of " + juce::String(total) + "   " + current
                                        : "Done: " + juce::String(total) + " file" + (total == 1 ? "" : "s"));
        g.drawText(st, statusArea(), juce::Justification::centredLeft);
    }
    void resized() override {
        auto r = getLocalBounds().reduced(16);
        r.removeFromTop(dropArea().getHeight() + 12);
        auto buttons = r.removeFromTop(30);
        addButton.setBounds(buttons.removeFromLeft(140)); buttons.removeFromLeft(8);
        openButton.setBounds(buttons.removeFromLeft(160)); buttons.removeFromLeft(8);
        changeButton.setBounds(buttons.removeFromLeft(170)); buttons.removeFromLeft(8);
        rescanButton.setBounds(buttons.removeFromLeft(150)); buttons.removeFromLeft(8);
        if (updateButton.isVisible()) updateButton.setBounds(buttons);
        r.removeFromTop(10);
        r.removeFromTop(22);
        progressBar.setBounds(r.removeFromTop(14));
        r.removeFromTop(10);
        logBox.setBounds(r);
    }
    bool isInterestedInFileDrag(const juce::StringArray&) override { return true; }
    void fileDragEnter(const juce::StringArray&, int, int) override { dragOver = true; repaint(); }
    void fileDragExit(const juce::StringArray&) override { dragOver = false; repaint(); }
    void filesDropped(const juce::StringArray& files, int, int) override {
        dragOver = false; repaint();
        juce::Array<juce::File> list;
        for (auto& f : files) mdark::Library::collectAudioFiles(juce::File(f), list);
        queue(list, files.size());
    }
private:
    juce::Rectangle<int> dropArea() const { return getLocalBounds().reduced(16).removeFromTop(170); }
    juce::Rectangle<int> statusArea() const { auto r = getLocalBounds().reduced(16); r.removeFromTop(170 + 12 + 30 + 10); return r.removeFromTop(22); }
    void queue(const juce::Array<juce::File>& list, int dropped) {
        if (list.isEmpty()) {log("No audio files found in what was added (" + juce::String(dropped) + " item(s))"); return;}
        log("Queued " + juce::String(list.size()) + " file(s)");
        if (onFiles) onFiles(list);
    }
    void chooseFiles() {
        chooser = std::make_unique<juce::FileChooser>("Add audio files", juce::File(), "*.wav;*.wave;*.flac;*.aif;*.aiff;*.ogg;*.mp3");
        chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::canSelectMultipleItems,
            [this](const juce::FileChooser& fc) {
                juce::Array<juce::File> list; for (auto& f : fc.getResults()) mdark::Library::collectAudioFiles(f, list);
                if (fc.getResults().size()) queue(list, fc.getResults().size());
            });
    }
    juce::TextButton addButton{"Add files..."}, openButton{"Open library folder"}, changeButton{"Change library folder..."}, rescanButton{"Rescan library..."}, updateButton;
    double progress = 0.0;
    juce::ProgressBar progressBar{progress};
    juce::TextEditor logBox;
    std::unique_ptr<juce::FileChooser> chooser;
    int done = 0, total = 0;
    juce::String current;
    bool dragOver = false;
};

//=============================================================================================== "Library"
enum class Kind { Text, Number, Duration, Letter, SubGrade, Bonus, Grade, Bpm, Key };

struct Column {
    juce::String key;      //the user.csv key for editable columns, else a name for the column
    juce::String base;     //the index.csv column it shows
    juce::String header;
    Kind kind = Kind::Text;
    bool editable = false, custom = false;
    int width = 80;
    juce::String sortBase; //index.csv column to sort by (if different from base)
};

#include "Filters.inc"

//FILL MISSING TAGS: folder names, then MusicBrainz and Discogs. Only empty fields; your own edits only; everything is logged so it can be undone.
class TagFillPanel : public juce::Component, private juce::Timer {
public:
    std::function<std::shared_ptr<mdark::Library>()> getLibrary;
    TagFillPanel() {
        setSize(520, 330);
        for (auto* t : {&foldersTick, &mbTick, &dcTick}) { addAndMakeVisible(t); t->setColour(juce::ToggleButton::textColourId, kText); t->setColour(juce::ToggleButton::tickColourId, kAccent); }
        foldersTick.setToggleState(true, juce::dontSendNotification); mbTick.setToggleState(true, juce::dontSendNotification);
        foldersTick.setButtonText("Folder names  (Rock/Foo Fighters/Album (1997)/...: genre, artist, album, year; instant, offline)");
        mbTick.setButtonText("MusicBrainz  (free, no account: release year and genre; about 1 track per second)");
        dcTick.setButtonText("Discogs  (needs your free personal token: genre and year; about 1 track per second)");
        addAndMakeVisible(tokenLabel); tokenLabel.setText("Discogs token:", juce::dontSendNotification); tokenLabel.setColour(juce::Label::textColourId, kDim);
        addAndMakeVisible(token); token.setColour(juce::TextEditor::backgroundColourId, kPanel); token.setColour(juce::TextEditor::textColourId, kText); token.setColour(juce::TextEditor::outlineColourId, kPanel);
        token.setTextToShowWhenEmpty("paste it here (discogs.com > Settings > Developers > Generate token)", kDim);
        token.setText(tokenFile().existsAsFile() ? tokenFile().loadFileAsString().trim() : juce::String(), false);
        for (auto* b : {&startButton, &stopButton, &undoButton, &closeButton}) { addAndMakeVisible(b); styleButton(*b); }
        stopButton.setEnabled(false);
        startButton.onClick = [this] { start(); };
        stopButton.onClick = [this] { stopFlag = true; stopButton.setEnabled(false); };
        undoButton.onClick = [this] { if (auto lib = getLibrary ? getLibrary() : nullptr) { const int n = mdark::tagfill::undoLast(*lib); status.setText("Undone: " + juce::String(n) + " values removed again.", juce::dontSendNotification); } };
        closeButton.onClick = [this] { if (auto* w = findParentComponentOfClass<juce::DialogWindow>()) w->exitModalState(0); };
        addAndMakeVisible(status); status.setColour(juce::Label::textColourId, kText); status.setJustificationType(juce::Justification::topLeft);
        status.setText("Fills only EMPTY fields, into your own edits (the audio files are never touched). Folder names win over the online lookups. "
                       "Online lookups take about a second per track and can be stopped and resumed; answers are kept, so a second run is quick.", juce::dontSendNotification);
    }
    ~TagFillPanel() override { stopTimer(); stopFlag = true; if (worker.joinable()) worker.join(); }
    void resized() override {
        auto r = getLocalBounds().reduced(14);
        foldersTick.setBounds(r.removeFromTop(24)); mbTick.setBounds(r.removeFromTop(24)); dcTick.setBounds(r.removeFromTop(24));
        auto t = r.removeFromTop(28); tokenLabel.setBounds(t.removeFromLeft(100)); token.setBounds(t);
        r.removeFromTop(8);
        auto b = r.removeFromBottom(30);
        startButton.setBounds(b.removeFromLeft(110)); b.removeFromLeft(6); stopButton.setBounds(b.removeFromLeft(80)); b.removeFromLeft(6); undoButton.setBounds(b.removeFromLeft(130)); closeButton.setBounds(b.removeFromRight(90));
        r.removeFromBottom(8);
        status.setBounds(r);
    }
private:
    static juce::File tokenFile() { return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("meterDARK Analyser").getChildFile("discogs-token.txt"); }
    void start() {
        auto lib = getLibrary ? getLibrary() : nullptr;
        if (lib == nullptr || running) return;
        mdark::tagfill::Options o; o.folders = foldersTick.getToggleState(); o.musicBrainz = mbTick.getToggleState(); o.discogs = dcTick.getToggleState() && token.getText().trim().isNotEmpty(); o.discogsToken = token.getText().trim();
        if (dcTick.getToggleState() && o.discogsToken.isEmpty()) { status.setText("Paste your Discogs token first, or untick Discogs.", juce::dontSendNotification); return; }
        if (o.discogsToken.isNotEmpty()) { tokenFile().getParentDirectory().createDirectory(); tokenFile().replaceWithText(o.discogsToken); }
        running = true; stopFlag = false; doneFlag = false; startButton.setEnabled(false); stopButton.setEnabled(true); undoButton.setEnabled(false);
        if (worker.joinable()) worker.join();
        worker = std::thread([this, lib, o] {
            const auto sum = mdark::tagfill::fill(*lib, o, [this](int d, int t, const juce::String& m) { const juce::ScopedLock sl(lock); progressText = juce::String(d) + " of " + juce::String(t) + "   " + m; },
                                                  [this] { return stopFlag.load(); });
            const juce::ScopedLock sl(lock);
            result = "Filled: " + juce::String(sum.genres) + " genres, " + juce::String(sum.years) + " years, " + juce::String(sum.artists) + " artists, " + juce::String(sum.albums) + " albums."
                     + (sum.lookups > 0 ? "  (" + juce::String(sum.lookups) + " online lookups, " + juce::String(sum.failures) + " failed)" : juce::String()) + (stopFlag ? "  Stopped: press Start to carry on." : "") + "\nNothing was overwritten. \"Undo last fill\" removes these values again.";
            doneFlag = true;
        });
        startTimer(300);
    }
    void timerCallback() override {
        const juce::ScopedLock sl(lock);
        if (doneFlag) { stopTimer(); running = false; startButton.setEnabled(true); stopButton.setEnabled(false); undoButton.setEnabled(true); status.setText(result, juce::dontSendNotification); }
        else status.setText(progressText, juce::dontSendNotification);
    }
    juce::ToggleButton foldersTick, mbTick, dcTick; juce::Label tokenLabel, status; juce::TextEditor token;
    juce::TextButton startButton{"Start"}, stopButton{"Stop"}, undoButton{"Undo last fill"}, closeButton{"Close"};
    std::thread worker; std::atomic<bool> stopFlag{false}; bool running = false, doneFlag = false; juce::CriticalSection lock; juce::String progressText, result;
};

class LibraryView : public juce::Component, public juce::TableListBoxModel, private juce::Timer {
public:
    std::function<std::shared_ptr<mdark::Library>()> getLibrary;

    //For the Lab: the level mode chosen here, and the ids of the tracks the Lab should use (null = all of them).
    std::shared_ptr<const std::set<juce::String>> labIds() const {
        if (!filters.labUses) return nullptr;
        return std::make_shared<const std::set<juce::String>>(labSet());
    }
    int filterGeneration() const { return filterGen; } //goes up when the filters or the level mode change

    LibraryView() {
        addAndMakeVisible(table);
        table.setModel(this);
        table.setColour(juce::ListBox::backgroundColourId, kBg);
        table.setColour(juce::ListBox::outlineColourId, kPanel);
        table.setOutlineThickness(1);
        table.setRowHeight(22);
        table.getHeader().setColour(juce::TableHeaderComponent::backgroundColourId, kPanel);
        table.getHeader().setColour(juce::TableHeaderComponent::textColourId, kText);
        table.getHeader().setColour(juce::TableHeaderComponent::outlineColourId, kBg);
        table.getHeader().setColour(juce::TableHeaderComponent::highlightColourId, kSelect);
        table.getHeader().setStretchToFitActive(false);
        table.getViewport()->setScrollBarsShown(true, true);
        addAndMakeVisible(search);
        search.setTextToShowWhenEmpty("Search name, artist, genre, key ...", kDim);
        search.setColour(juce::TextEditor::backgroundColourId, kPanel);
        search.setColour(juce::TextEditor::textColourId, kText);
        search.setColour(juce::TextEditor::outlineColourId, kPanel);
        search.onTextChange = [this] { applyFilter(); };
        for (auto* b : {&addColumnButton, &columnsButton, &importButton, &showButton, &removeButton}) {addAndMakeVisible(b); styleButton(*b);}
        addColumnButton.onClick = [this] { addColumnDialog(); };
        columnsButton.onClick = [this] { columnsMenu(); };
        importButton.onClick = [this] { importDialog(); };
        showButton.onClick = [this] { if (auto* r = selectedRow()) revealTrack(*r); };
        removeButton.onClick = [this] { removeSelected(); };
        for (auto* l : {&gradeLabel, &compareLabel}) {addAndMakeVisible(l); l->setColour(juce::Label::textColourId, kDim); l->setJustificationType(juce::Justification::centredRight);}
        for (auto* b : {&gradeBox, &compareBox}) {
            addAndMakeVisible(b);
            b->setColour(juce::ComboBox::backgroundColourId, kPanel); b->setColour(juce::ComboBox::textColourId, kText); b->setColour(juce::ComboBox::outlineColourId, kPanel);
        }
        gradeBox.onChange = [this] { const int i = gradeBox.getSelectedId() - 1; const auto v = mdark::Library::versionIds(); if (i >= 0 && i < (int)v.size() && v[(size_t)i] != gradeVersion) {gradeVersion = v[(size_t)i]; saveVersions(); rebuildColumns(); applyFilter();} };
        compareBox.onChange = [this] { const int i = compareBox.getSelectedId() - 2; const auto v = mdark::Library::versionIds(); const juce::String nv = (i >= 0 && i < (int)v.size()) ? v[(size_t)i] : juce::String(); if (nv != compareVersion) {compareVersion = nv; saveVersions(); rebuildColumns(); applyFilter();} };
        addAndMakeVisible(filterButton); styleButton(filterButton);
        filterButton.onClick = [this] {openFilters();};
        addAndMakeVisible(tagFillButton); styleButton(tagFillButton);
        tagFillButton.setTooltip("Fills empty artist, album, year and genre from your folder names and, if you like, MusicBrainz / Discogs. Only your own edits are written; it can be undone.");
        tagFillButton.onClick = [this] {
            auto* p = new TagFillPanel(); p->getLibrary = getLibrary;
            juce::DialogWindow::LaunchOptions o; o.content.setOwned(p); o.dialogTitle = "Fill missing tags"; o.dialogBackgroundColour = kBg; o.escapeKeyTriggersCloseButton = true;
            o.useNativeTitleBar = true; o.resizable = false; o.componentToCentreAround = this; o.launchAsync();
        };
        addAndMakeVisible(applyButton); styleButton(applyButton);
        applyButton.setClickingTogglesState(false);
        applyButton.setTooltip("On: this table shows the same tracks as the Overview and the LABs (your filters, the exclusions and one copy of each song). Off: every track.");
        applyButton.onClick = [this] {
            tableUsesFilters = !tableUsesFilters;
            if (auto lib = getLibrary ? getLibrary() : nullptr) lib->folder.getChildFile("fulltable-filters.txt").replaceWithText(tableUsesFilters ? "1" : "0");
            applyFilter();
        };
        for (auto* b : {&dupButton, &howButton}) {addAndMakeVisible(b); styleButton(*b);}
        dupButton.onClick = [this] { dupOnly = !dupOnly; applyFilter(); };
        howButton.onClick = [this] { showHowToRead(); };
        addAndMakeVisible(countLabel);
        countLabel.setColour(juce::Label::textColourId, kDim);
        countLabel.setJustificationType(juce::Justification::centredRight);
        table.getHeader().addListener(&headerWatcher);
        startTimer(700);
    }

    ~LibraryView() override { table.getHeader().removeListener(&headerWatcher); if (filterWindow != nullptr) delete filterWindow.getComponent(); }

    void reload() {
        auto lib = getLibrary ? getLibrary() : nullptr;
        if (lib == nullptr) return;
        lastChange = lib->changeCount();
        const juce::String selectedId = selectedRow() ? selectedRow()->id : juce::String();
        idx = lib->index();
        user = lib->userValues();
        if (rateLoadedFor != lib.get()) { //a different library: its filters
            rateLoadedFor = lib.get();
            filters.load(lib->folder);
            const auto tf = lib->folder.getChildFile("fulltable-filters.txt");
            tableUsesFilters = !tf.existsAsFile() || tf.loadFileAsString().trim() != "0";
        }
        refreshVersionBoxes();
        grades.clear(); gradeRow.clear();
        for (auto& v : mdark::Library::versionIds()) {
            grades[v] = readingsAt96k(*lib, v);
            auto& m = gradeRow[v];
            for (size_t r = 0; r < grades[v].rows.size(); ++r) m[grades[v].get(r, "id")] = r;
        }
        findDuplicates();
        custom = lib->customColumns();
        rebuildColumns();
        all.clear();
        for (size_t r = 0; r < idx.rows.size(); ++r) all.push_back({idx.get(r, "id"), r});
        applyFilter();
        if (selectedId.isNotEmpty())
            for (int i = 0; i < (int)shown.size(); ++i) if (shown[(size_t)i].id == selectedId) table.selectRow(i);
    }

    //---- TableListBoxModel
    int getNumRows() override { return (int)shown.size(); }
    void paintRowBackground(juce::Graphics& g, int row, int, int, bool selected) override {
        g.fillAll(selected ? kSelect : (row % 2 ? kRowAlt : kBg));
        if (row >= 0 && row < (int)shown.size() && dupOf.count(shown[(size_t)row].id)) g.fillAll(juce::Colour(150, 30, 30).withAlpha(selected ? 0.45f : 0.35f));
    }
    void cellClicked(int row, int, const juce::MouseEvent& e) override {
        if (!e.mods.isPopupMenu() || row < 0 || row >= (int)shown.size()) return;
        table.selectRow(row);
        const RowRef r = shown[(size_t)row];
        juce::PopupMenu m;
        m.addItem(1, "Details...");
        m.addItem(2, "Show file");
        if (dupOf.count(r.id)) m.addItem(3, "Not a duplicate");
        else if (userValue(r.id, "not_duplicate") == "1") m.addItem(4, "Check for duplicates again");
        m.addSeparator();
        m.addItem(5, "Remove from library...");
        m.showMenuAsync(juce::PopupMenu::Options(), [this, r](int id) {
            if (id == 1) showDetails(r);
            if (id == 2) revealTrack(r);
            if (id == 3 || id == 4) if (auto lib = getLibrary()) {lib->setUserValue(r.id, "not_duplicate", id == 3 ? "1" : ""); reload();}
            if (id == 5) removeSelected();
        });
    }
    void paintCell(juce::Graphics& g, int row, int colId, int width, int height, bool) override {
        if (row < 0 || row >= (int)shown.size() || colId < 1 || colId > (int)cols.size()) return;
        const Column& c = cols[(size_t)colId - 1];
        bool yours = false, unsure = false;
        const juce::String text = display(shown[(size_t)row], c, yours, unsure);
        g.setColour(yours ? kYours : (unsure ? kDim : kText));
        g.setFont(juce::Font(14.0f));
        const bool right = c.kind == Kind::Number || c.kind == Kind::Duration || c.kind == Kind::Bpm;
        const bool centre = c.kind == Kind::SubGrade || c.kind == Kind::Letter || c.kind == Kind::Bonus;
        g.drawText(text, 6, 0, width - 12, height, centre ? juce::Justification::centred : (right ? juce::Justification::centredRight : juce::Justification::centredLeft), true);
    }
    juce::Component* refreshComponentForCell(int row, int colId, bool, juce::Component* existing) override {
        if (colId < 1 || colId > (int)cols.size() || !cols[(size_t)colId - 1].editable) {delete existing; return nullptr;}
        auto* cell = dynamic_cast<EditCell*>(existing);
        if (cell == nullptr) {delete existing; cell = new EditCell(*this);}
        cell->set(row, colId);
        return cell;
    }
    void sortOrderChanged(int colId, bool forwards) override { sortCol = colId; sortForwards = forwards; sortShown(); table.updateContent(); repaint(); saveLayout(); }
    void columnsChangedOrResized() { saveLayout(); }
    void saveLayout() {
        if (restoringLayout || colKeys.isEmpty()) return;
        auto lib = getLibrary ? getLibrary() : nullptr;
        if (lib == nullptr) return;
        const juce::File f = lib->folder.getChildFile("view.json");
        juce::var j = juce::JSON::parse(f);
        if (!j.isObject()) j = juce::var(new juce::DynamicObject());
        j.getDynamicObject()->setProperty("keys", colKeys.joinIntoString("|")); j.getDynamicObject()->setProperty("layout", table.getHeader().toString());
        f.replaceWithText(juce::JSON::toString(j));
    }
    void cellDoubleClicked(int row, int colId, const juce::MouseEvent&) override {
        if (colId >= 1 && colId <= (int)cols.size() && cols[(size_t)colId - 1].editable) return;
        if (row >= 0 && row < (int)shown.size()) showDetails(shown[(size_t)row]);
    }
    void selectedRowsChanged(int) override { const bool any = selectedRow() != nullptr; showButton.setEnabled(any); removeButton.setEnabled(any); }

    void paint(juce::Graphics& g) override {
        g.fillAll(kBg);
        g.setColour(kDim);
        g.setFont(juce::Font(12.5f));
        g.drawFittedText("Click a column title to sort, click again to reverse. Double-click artist, title, album, year, genre, BPM, key or one of your columns to edit it"
                   " (clear it to go back to the original). Double-click anything else for the track's details.   Blue = your value.   Grey with ? = a less certain estimate.",
                   hintArea(), juce::Justification::centredLeft, 2);
    }
    void resized() override {
        auto r = getLocalBounds().reduced(12);
        auto top = r.removeFromTop(28);
        search.setBounds(top.removeFromLeft(280)); top.removeFromLeft(8);
        addColumnButton.setBounds(top.removeFromLeft(110)); top.removeFromLeft(6);
        columnsButton.setBounds(top.removeFromLeft(120)); top.removeFromLeft(6);
        importButton.setBounds(top.removeFromLeft(110)); top.removeFromLeft(6);
        showButton.setBounds(top.removeFromLeft(90)); top.removeFromLeft(6);
        removeButton.setBounds(top.removeFromLeft(90)); top.removeFromLeft(6);
        countLabel.setBounds(top);
        r.removeFromTop(6);
        auto row2 = r.removeFromTop(26);
        gradeLabel.setBounds(row2.removeFromLeft(84)); gradeBox.setBounds(row2.removeFromLeft(170)); row2.removeFromLeft(4);
        compareLabel.setBounds(row2.removeFromLeft(96)); compareBox.setBounds(row2.removeFromLeft(170)); row2.removeFromLeft(4);
        dupButton.setBounds(row2.removeFromLeft(190)); row2.removeFromLeft(6);
        howButton.setBounds(row2.removeFromLeft(150));
        r.removeFromTop(6);
        auto row3 = r.removeFromTop(26);
        filterButton.setBounds(row3.removeFromLeft(220)); row3.removeFromLeft(6);
        applyButton.setBounds(row3.removeFromLeft(150)); row3.removeFromLeft(6);
        tagFillButton.setBounds(row3.removeFromLeft(180));
        r.removeFromTop(6);
        hint = r.removeFromBottom(34);
        table.setBounds(r);
    }

private:
    struct RowRef { juce::String id; size_t row; };

    //---- an editable cell: shows the value, double-click to type a new one (empty = back to the measured/tag value)
    struct EditCell : public juce::Label {
        LibraryView& owner; int row = -1, colId = 0;
        explicit EditCell(LibraryView& o) : owner(o) {
            setEditable(false, true, false);
            setColour(juce::Label::textWhenEditingColourId, kText);
            setColour(juce::Label::backgroundWhenEditingColourId, kPanel);
            setColour(juce::TextEditor::highlightColourId, kSelect);
            setFont(juce::Font(14.0f));
            onTextChange = [this] { owner.edited(row, colId, getText()); };
        }
        void set(int r, int c) {
            row = r; colId = c;
            bool yours = false, unsure = false;
            const Column& col = owner.cols[(size_t)c - 1];
            setText(owner.display(owner.shown[(size_t)r], col, yours, unsure), juce::dontSendNotification);
            setColour(juce::Label::textColourId, yours ? kYours : (unsure ? kDim : kText));
            setJustificationType((col.kind == Kind::Number || col.kind == Kind::Bpm) ? juce::Justification::centredRight : juce::Justification::centredLeft);
        }
        void mouseDown(const juce::MouseEvent& e) override { owner.table.selectRowsBasedOnModifierKeys(row, e.mods, false); juce::Label::mouseDown(e); }
        void editorShown(juce::TextEditor* ed) override { //start from your value, or empty for "type to override"
            const Column& col = owner.cols[(size_t)colId - 1];
            ed->setText(owner.userValue(owner.shown[(size_t)row].id, col.key), false);
            ed->selectAll();
        }
    };

    juce::Rectangle<int> hintArea() const { return hint; }

    void timerCallback() override {
        auto lib = getLibrary ? getLibrary() : nullptr;
        if (lib != nullptr && (lib.get() != lastLib || lib->changeCount() != lastChange)) {lastLib = lib.get(); reload();}
    }

    void rebuildColumns() {
        std::vector<Column> c;
        auto add = [&](juce::String key, juce::String base, juce::String header, Kind k, bool ed, int w, juce::String sortBase = {}) {
            Column col; col.key = key; col.base = base; col.header = header; col.kind = k; col.editable = ed; col.width = w; col.sortBase = sortBase;
            c.push_back(col);
        };
        add("file", "file", "Track", Kind::Text, false, 230);
        add("artist", "artist", "Artist", Kind::Text, true, 130);
        add("title", "title", "Title", Kind::Text, true, 150);
        add("grade", "g:grade", "Grade", Kind::Grade, false, 80);
        if (compareVersion.isNotEmpty()) add("cgrade", "c:grade", "Grade " + shortName(compareVersion), Kind::Grade, false, 90);
        add("letter", "g:letter", "Letter", Kind::Letter, false, 52, "g:letter_value");
        add("bonus", "g:bonus", "Bonus", Kind::Bonus, false, 52, "g:bonus_value");
        add("authority", "g:authority", "Authority", Kind::SubGrade, false, 70, "g:authority_value");
        add("power", "g:power", "Power", Kind::SubGrade, false, 56, "g:power_value");
        add("detail", "g:detail", "Detail", Kind::SubGrade, false, 56, "g:detail_value");
        add("bpm", "bpm_est", "BPM", Kind::Bpm, true, 56);
        add("key", "key_est", "Key", Kind::Key, true, 84);
        add("lufs", "lufs_integrated", "LUFS", Kind::Number, false, 56);
        add("peak", "peak_dbfs", "Peak dB", Kind::Number, false, 62);
        add("tp", "true_peak_dbtp", "True pk", Kind::Number, false, 62);
        add("len", "duration_s", "Length", Kind::Duration, false, 60);
        for (auto& cc : custom) {
            Column col; col.key = cc.key; col.header = cc.name; col.kind = cc.numeric ? Kind::Number : Kind::Text; col.editable = true; col.custom = true; col.width = 100;
            c.push_back(col);
        }
        add("album", "album", "Album", Kind::Text, true, 120);
        add("year", "year", "Year", Kind::Number, true, 50);
        add("genre", "genre", "Genre", Kind::Text, true, 90);
        add("source", "source", "Source", Kind::Text, true, 90); //where this copy came from: Tidal, Spotify, Apple Music, vinyl, CD, HDtracks, Qobuz, other
        add("fmt", "format", "Format", Kind::Text, false, 110);
        add("dir", "g:directions_main", "Directions", Kind::Text, false, 150);
        add("added", "analysed", "Added", Kind::Text, false, 130);
        //keep the user's column widths and sort if the set of columns did not change
        juce::StringArray keys; for (auto& x : c) keys.add(x.key);
        if (keys == colKeys) {cols = c; return;}
        const juce::String sortKey = (sortCol >= 1 && sortCol <= (int)cols.size()) ? cols[(size_t)sortCol - 1].key : juce::String("grade");
        cols = c; colKeys = keys;
        auto& h = table.getHeader();
        h.removeAllColumns();
        for (int i = 0; i < (int)cols.size(); ++i) h.addColumn(cols[(size_t)i].header, i + 1, cols[(size_t)i].width, 30, 800, juce::TableHeaderComponent::defaultFlags);
        restoringLayout = true;
        bool restored = false;
        if (auto lib = getLibrary ? getLibrary() : nullptr) { //the widths, order and sort you left the columns in, if the columns are the same
            const juce::var v = juce::JSON::parse(lib->folder.getChildFile("view.json"));
            if (v.getProperty("keys", "").toString() == keys.joinIntoString("|")) {h.restoreFromString(v.getProperty("layout", "").toString()); restored = h.getSortColumnId() > 0;}
        }
        if (restored) {sortCol = h.getSortColumnId(); sortForwards = h.isSortedForwards();}
        else {
            sortCol = keys.indexOf(sortKey) + 1;
            if (sortCol < 1) {sortCol = keys.indexOf("grade") + 1; sortForwards = true;}
            h.setSortColumnId(sortCol, sortForwards);
        }
        restoringLayout = false;
    }

    //a Meter version's grades with the audio converted to 96 kHz; a track not rescanned yet (no 96 kHz reading) shows its own readings
    static mdark::Table readingsAt96k(mdark::Library& lib, const juce::String& vid) {
        mdark::Table t = lib.grades(vid, gradeRate);
        if (t.header.isEmpty()) return lib.grades(vid);
        const mdark::Table nat = lib.grades(vid);
        std::set<juce::String> have;
        for (size_t i = 0; i < t.rows.size(); ++i) have.insert(t.get(i, "id"));
        for (size_t i = 0; i < nat.rows.size(); ++i) {
            const juce::String id = nat.get(i, "id");
            if (have.count(id)) continue;
            juce::StringArray cells; for (auto& h : t.header) cells.add(nat.get(i, h));
            t.rows.push_back(cells);
        }
        return t;
    }

    //a value for a row: "g:x" = column x of the chosen Meter version's grades, "c:x" = of the compared version, else index.csv
    juce::String field(const RowRef& r, const juce::String& name) const {
        if (name.startsWith("g:") || name.startsWith("c:")) {
            const juce::String vid = name.startsWith("g:") ? gradeVersion : compareVersion;
            auto gt = grades.find(vid); auto rt = gradeRow.find(vid);
            if (gt == grades.end() || rt == gradeRow.end()) return {};
            auto it = rt->second.find(r.id);
            return it == rt->second.end() ? juce::String() : gt->second.get(it->second, name.substring(2));
        }
        return idx.get(r.row, name);
    }

    juce::String userValue(const juce::String& id, const juce::String& key) const {
        auto it = user.find(id);
        if (it == user.end()) return {};
        auto jt = it->second.find(key);
        return jt == it->second.end() ? juce::String() : jt->second;
    }

    juce::String display(const RowRef& r, const Column& c, bool& yours, bool& unsure) const {
        yours = false; unsure = false;
        if (c.editable) {
            const juce::String u = userValue(r.id, c.key);
            if (u.isNotEmpty()) {yours = true; return u;}
            if (c.custom) return {};
        }
        const juce::String v = field(r, c.base);
        switch (c.kind) {
            case Kind::Duration: {const int s = (int)std::lround(v.getDoubleValue()); return v.isEmpty() ? v : juce::String(s/60) + ":" + juce::String(s%60).paddedLeft('0', 2);}
            case Kind::Number: return (c.base == "lufs_integrated" || c.base == "peak_dbfs" || c.base == "true_peak_dbtp") && v.isNotEmpty() ? juce::String(v.getDoubleValue(), 1) : v;
            case Kind::Bpm: {
                if (v.isEmpty()) return {};
                unsure = idx.get(r.row, "bpm_confidence").getDoubleValue() < 0.3;
                return juce::String(juce::roundToInt(v.getDoubleValue())) + (unsure ? "?" : "");
            }
            case Kind::Key: unsure = v.isNotEmpty() && idx.get(r.row, "key_confidence").getDoubleValue() < 0.05; return v.isEmpty() ? v : v + (unsure ? "?" : "");
            default: return v;
        }
    }

    //sort value: numbers sort as numbers, grades by the value behind them, blanks always last
    double sortNumber(const RowRef& r, const Column& c, bool& blank) const {
        juce::String v;
        if (c.editable && (v = userValue(r.id, c.key)).isNotEmpty()) {}
        else if (c.sortBase.isNotEmpty()) v = field(r, c.sortBase);
        else v = field(r, c.base);
        if (c.kind == Kind::Bonus) { //only 1-80 is shown, lower is better
            if (field(r, c.base).isEmpty()) {blank = true; return 0;}
        }
        v = v.trim().removeCharacters(",");
        blank = v.isEmpty() || !v.containsAnyOf("0123456789");
        return v.getDoubleValue();
    }

    void sortShown() {
        if (sortCol < 1 || sortCol > (int)cols.size()) return;
        const Column c = cols[(size_t)sortCol - 1];
        const bool numeric = c.kind != Kind::Text && c.kind != Kind::Key;
        //grades read best-first when sorted "forwards" (A before B, high authority before low, bonus 1 before 80)
        const bool higherFirst = c.kind == Kind::Grade || c.kind == Kind::Letter || c.kind == Kind::SubGrade;
        std::stable_sort(shown.begin(), shown.end(), [&](const RowRef& a, const RowRef& b) {
            if (c.kind == Kind::Grade) { //Chris's order: the numbered tracks (his "double top 40") first, 1 before 80, then the rest by letter, A first.
                //Ties (the letter value is rarely exactly equal, but many tracks share a bonus number's neighbours) are settled by the bonus number
                //behind the hidden ones, then by authority + power + detail, then by name, so the order never depends on how the list was sorted before.
                const juce::String pa = c.base.substring(0, 2);
                auto key = [&](const RowRef& r, double k[4]) {
                    const bool numbered = field(r, pa + "bonus").isNotEmpty();
                    k[0] = numbered ? 0 : 1;
                    k[1] = numbered ? field(r, pa + "bonus").getDoubleValue() : -field(r, pa + "letter_value").getDoubleValue();
                    const juce::String bv = field(r, pa + "bonus_value");
                    k[2] = bv.isNotEmpty() ? bv.getDoubleValue() : 1.0e9;
                    k[3] = -(field(r, pa + "authority_value").getDoubleValue() + field(r, pa + "power_value").getDoubleValue() + field(r, pa + "detail_value").getDoubleValue());
                };
                const bool gradeA = field(a, pa + "grade").isNotEmpty(), gradeB = field(b, pa + "grade").isNotEmpty();
                if (gradeA != gradeB) return gradeA; //no grade (not scored by this version) always last
                if (!gradeA) return field(a, "file").compareNatural(field(b, "file")) < 0;
                double ka[4], kb[4];
                key(a, ka); key(b, kb);
                for (int i = 0; i < 4; ++i) if (ka[i] != kb[i]) return sortForwards ? ka[i] < kb[i] : ka[i] > kb[i];
                return field(a, "file").compareNatural(field(b, "file")) < 0;
            }
            if (numeric) {
                bool ba = false, bb = false;
                const double va = sortNumber(a, c, ba), vb = sortNumber(b, c, bb);
                if (ba != bb) return bb; //blanks last
                if (ba) return false;
                if (va == vb) return field(a, "file").compareNatural(field(b, "file")) < 0;
                const bool less = higherFirst ? va > vb : va < vb;
                return sortForwards ? less : !less;
            }
            bool y, u;
            const juce::String sa = display(a, c, y, u), sb = display(b, c, y, u);
            if (sa.isEmpty() != sb.isEmpty()) return sb.isEmpty();
            const int cmp = sa.compareNatural(sb);
            if (cmp == 0) return field(a, "file").compareNatural(field(b, "file")) < 0;
            return sortForwards ? cmp < 0 : cmp > 0;
        });
    }

    void applyFilter() {
        const juce::String q = search.getText().trim();
        shown.clear();
        juce::StringArray words; words.addTokens(q, " ", "\"");
        std::set<juce::String> allowed; const bool restrictToLab = tableUsesFilters && filters.labUses;
        if (restrictToLab) allowed = labSet();
        for (auto& r : all) {
            bool ok = true;
            if (words.size()) {
                juce::String hay;
                bool y, u;
                for (auto& c : cols) hay << display(r, c, y, u) << " ";
                for (auto& w : words) if (w.isNotEmpty() && !hay.containsIgnoreCase(w)) ok = false;
            }
            if (ok && dupOnly && !dupOf.count(r.id)) ok = false;
            if (ok && tableUsesFilters && !passesFilters(r)) ok = false;
            if (ok && restrictToLab && !allowed.count(r.id)) ok = false;
            if (ok) shown.push_back(r);
        }
        sortShown();
        table.updateContent();
        table.repaint();
        const size_t nf = filters.current.conditions.size();
        filterButton.setButtonText(nf == 0 ? juce::String("Filters...") : "Filters (" + juce::String((int)nf) + " on)...");
        filterButton.setColour(juce::TextButton::buttonColourId, nf == 0 && !filters.labUses ? kPanel : kSelect);
        applyButton.setButtonText(tableUsesFilters ? "Apply filters: on" : "Apply filters: off");
        applyButton.setColour(juce::TextButton::buttonColourId, tableUsesFilters ? kSelect : kPanel);
        countLabel.setText(juce::String((int)shown.size()) + (shown.size() == all.size() ? juce::String(" tracks") : " of " + juce::String((int)all.size()) + " tracks")
                           + (tableUsesFilters && (nf > 0 || filters.labUses) ? juce::String("  (filters on)") : juce::String()), juce::dontSendNotification);
        selectedRowsChanged(table.getSelectedRow());
        const int nd = (int)dupOf.size();
        dupButton.setVisible(nd > 0 || dupOnly);
        dupButton.setButtonText(dupOnly ? "Show all tracks" : juce::String(nd) + " possible duplicate" + (nd == 1 ? "" : "s"));
        dupButton.setColour(juce::TextButton::buttonColourId, dupOnly ? kSelect : juce::Colour(110, 30, 30));
    }

    juce::String shortName(const juce::String& vid) const {
        if (auto* v = mdark::findMeterVersion(vid.toStdString())) return juce::String(v->version);
        return vid;
    }
    void refreshVersionBoxes() {
        const auto v = mdark::Library::versionIds();
        if (gradeVersion.isEmpty() || std::find(v.begin(), v.end(), gradeVersion) == v.end()) { //the newest version, unless you chose another for this library
            gradeVersion = v.empty() ? juce::String() : v.back(); compareVersion = {};
            if (auto lib = getLibrary ? getLibrary() : nullptr) {
                const juce::var j = juce::JSON::parse(lib->folder.getChildFile("view.json"));
                const juce::String gv = j.getProperty("grades", "").toString(), cv = j.getProperty("compare", "").toString();
                if (std::find(v.begin(), v.end(), gv) != v.end()) gradeVersion = gv;
                if (std::find(v.begin(), v.end(), cv) != v.end()) compareVersion = cv;
            }
        }
        gradeBox.clear(juce::dontSendNotification); compareBox.clear(juce::dontSendNotification);
        compareBox.addItem("nothing", 1);
        for (size_t i = 0; i < v.size(); ++i) {
            const auto* mv = mdark::findMeterVersion(v[i].toStdString());
            const juce::String label = "Meter " + juce::String(mv->version) + "  (" + juce::String(mv->date) + ")";
            gradeBox.addItem(label, (int)i + 1); compareBox.addItem(label, (int)i + 2);
            if (v[i] == gradeVersion) gradeBox.setSelectedId((int)i + 1, juce::dontSendNotification);
            if (v[i] == compareVersion) compareBox.setSelectedId((int)i + 2, juce::dontSendNotification);
        }
        if (compareVersion.isEmpty()) compareBox.setSelectedId(1, juce::dontSendNotification);
    }
    void saveVersions() {
        auto lib = getLibrary ? getLibrary() : nullptr;
        if (lib == nullptr) return;
        const juce::File f = lib->folder.getChildFile("view.json");
        juce::var j = juce::JSON::parse(f);
        if (!j.isObject()) j = juce::var(new juce::DynamicObject());
        j.getDynamicObject()->setProperty("grades", gradeVersion); j.getDynamicObject()->setProperty("compare", compareVersion);;
        f.replaceWithText(juce::JSON::toString(j));
    }

    //POSSIBLE DUPLICATES: the same name (ignoring case, the extension and bracketed bits like "(2012 Remaster)") and within 2 s
    //of the same length, or near-identical measurements (length, loudness and peaks) whatever the name.
    void findDuplicates() {
        dupOf.clear(); dupPairs.clear();
        auto key = [](juce::String f) {
            f = juce::File::createFileWithoutCheckingPath("/" + f).getFileNameWithoutExtension().toLowerCase();
            juce::String o; int depth = 0;
            for (auto ch : f) {if (ch == '(' || ch == '[') ++depth; else if ((ch == ')' || ch == ']') && depth > 0) --depth; else if (depth == 0 && juce::CharacterFunctions::isLetterOrDigit(ch)) o << juce::String::charToString(ch);}
            return o;
        };
        const size_t n = idx.rows.size();
        std::vector<juce::String> keys(n), ids(n), files(n);
        std::vector<double> dur(n), lufs(n), pk(n), tp(n);
        std::vector<bool> skip(n);
        for (size_t r = 0; r < n; ++r) {
            ids[r] = idx.get(r, "id"); files[r] = idx.get(r, "file"); keys[r] = key(files[r]);
            dur[r] = idx.get(r, "duration_s").getDoubleValue(); lufs[r] = idx.get(r, "lufs_integrated").getDoubleValue();
            pk[r] = idx.get(r, "peak_dbfs").getDoubleValue(); tp[r] = idx.get(r, "true_peak_dbtp").getDoubleValue();
            skip[r] = userValue(ids[r], "not_duplicate") == "1";
        }
        for (size_t a = 0; a < n; ++a) for (size_t b = a + 1; b < n; ++b) {
            if (skip[a] || skip[b]) continue;
            const bool sameName = keys[a].isNotEmpty() && keys[a] == keys[b] && std::fabs(dur[a] - dur[b]) <= 2.0;
            const bool sameSound = std::fabs(dur[a] - dur[b]) <= 1.0 && std::fabs(lufs[a] - lufs[b]) <= 0.1 && std::fabs(pk[a] - pk[b]) <= 0.1 && std::fabs(tp[a] - tp[b]) <= 0.1;
            if (!sameName && !sameSound) continue;
            dupPairs.push_back({a, b});
            auto addTo = [&](size_t x, size_t y) {juce::String& s2 = dupOf[ids[x]]; if (s2.isNotEmpty()) s2 << ", "; s2 << files[y];};
            addTo(a, b); addTo(b, a);
        }
    }

    //---- filters
    std::vector<FilterField> filterFields() const {
        std::vector<FilterField> f;
        f.push_back({"folder", "Folder (where the file is)", 0});
        f.push_back({"rate", "Sample rate (Hz)", 1});
        for (auto& c : cols) {
            if (c.key == "dir" || c.key == "added") continue;
            int type = 0;
            if (c.kind == Kind::Number || c.kind == Kind::Duration || c.kind == Kind::Bpm) type = 1;
            else if (c.kind == Kind::SubGrade) type = 3; //ranked letters (see GradeRank.h)
            else if (c.kind == Kind::Letter) type = 4;
            else if (c.kind == Kind::Bonus) type = 5;
            juce::String label = c.header;
            if (c.kind == Kind::Duration) label = "Length (seconds, or m:ss)";
            f.push_back({c.key, label, type});
        }
        return f;
    }
    juce::String filterValue(const RowRef& r, const juce::String& key) const {
        if (key == "folder") return juce::File(idx.get(r.row, "source_path")).getParentDirectory().getFullPathName();
        if (key == "rate") return idx.get(r.row, "sample_rate");
        if (key == "letter" || key == "authority" || key == "power" || key == "detail") return field(r, "g:" + key); //the grade part as shown (a letter), ranked by the filter
        for (auto& c : cols) if (c.key == key) {
            bool y, u;
            if (c.editable) {const juce::String uv = userValue(r.id, c.key); if (uv.isNotEmpty()) return uv;}
            if (c.kind == Kind::Bonus) return field(r, c.base);
            if (c.sortBase.isNotEmpty()) return field(r, c.sortBase);
            if (c.kind == Kind::Duration) return field(r, c.base);
            return display(r, c, y, u);
        }
        return {};
    }
    bool passesFilters(const RowRef& r) const {
        if (filters.current.conditions.empty()) return true;
        const auto fields = filterFieldsCached();
        for (auto& c : filters.current.conditions) {
            int type = 0;
            for (auto& f : fields) if (f.key == c.field) type = f.type;
            if (!conditionPasses(c, type, filterValue(r, c.field))) return false;
        }
        return true;
    }
    const std::vector<FilterField>& filterFieldsCached() const {
        if (fieldCacheCols != cols.size() || fieldCache.empty()) {fieldCache = filterFields(); fieldCacheCols = cols.size();}
        return fieldCache;
    }
    mutable std::vector<FilterField> fieldCache; mutable size_t fieldCacheCols = 0;

    //the tracks the Lab uses: the filter, the default exclusions, one copy of each song
    std::set<juce::String> labSet() const {
        std::vector<size_t> keep; //indexes into `all`
        for (size_t i = 0; i < all.size(); ++i) {
            const RowRef& r = all[i];
            if (!passesFilters(r)) continue;
            const double dur = idx.get(r.row, "duration_s").getDoubleValue();
            const juce::String lufs = idx.get(r.row, "lufs_integrated");
            if (filters.current.excShort && dur < 60.0) continue;
            if (filters.current.excLong && dur > 600.0) continue;
            if (filters.current.excQuiet && lufs.isNotEmpty() && lufs.getDoubleValue() < -25.0) continue;
            if (filters.current.excNoGrade && field(r, "g:grade").isEmpty()) continue;
            keep.push_back(i);
        }
        std::set<juce::String> out;
        std::vector<char> drop(all.size(), 0);
        if (filters.current.oneCopy) {
            //groups of the same song (rows linked by dupPairs), keep the best of each group
            std::vector<size_t> parent(idx.rows.size()); for (size_t i = 0; i < parent.size(); ++i) parent[i] = i;
            std::function<size_t(size_t)> find = [&](size_t x) {while (parent[x] != x) {parent[x] = parent[parent[x]]; x = parent[x];} return x;};
            for (auto& p : dupPairs) parent[find(p.first)] = find(p.second);
            std::map<size_t, size_t> best; //group -> index into `all` of the copy to keep
            auto better = [&](size_t a, size_t b) { //is `a` a better copy than `b`?
                const double ra = idx.get(all[a].row, "sample_rate").getDoubleValue(), rb = idx.get(all[b].row, "sample_rate").getDoubleValue();
                if (ra != rb) return ra > rb;
                return idx.get(all[a].row, "file").compareIgnoreCase(idx.get(all[b].row, "file")) < 0;
            };
            for (size_t i : keep) {
                const size_t g = find(all[i].row);
                auto it = best.find(g);
                if (it == best.end()) best[g] = i; else if (better(i, it->second)) it->second = i;
            }
            for (size_t i : keep) if (best[find(all[i].row)] != i) drop[i] = 1;
        }
        for (size_t i : keep) if (!drop[i]) out.insert(all[i].id);
        return out;
    }
    juce::String filterCounts() const {
        return juce::String((int)shown.size()) + " of " + juce::String((int)all.size()) + " tracks in the list.   hintLAB would use " + juce::String((int)labSet().size()) + " tracks"
               + (filters.labUses ? "." : " (switched off: it uses every track).");
    }
    void filtersChanged() {
        if (auto lib = getLibrary ? getLibrary() : nullptr) filters.save(lib->folder);
        ++filterGen;
        applyFilter();
    }
    void openFilters() {
        if (filterWindow != nullptr) {filterWindow->toFront(true); return;}
        auto* p = new FilterPanel();
        p->store = &filters; p->fields = filterFieldsCached();
        p->valuesFor = [this](const juce::String& key) {
            std::set<juce::String> vals;
            for (auto& r : all) {const juce::String v = filterValue(r, key); if (v.isNotEmpty()) vals.insert(v);}
            juce::StringArray a; for (auto& v : vals) a.add(v);
            return a;
        };
        p->counts = [this] {return filterCounts();};
        p->changed = [this] {filtersChanged();};
        p->attach();
        juce::DialogWindow::LaunchOptions o;
        o.content.setOwned(p); o.dialogTitle = "Filters"; o.dialogBackgroundColour = kBg; o.escapeKeyTriggersCloseButton = true;
        o.useNativeTitleBar = true; o.resizable = false; o.componentToCentreAround = this;
        filterWindow = o.launchAsync();
    }

    void showHowToRead() {
        const juce::String t =
            "A grade like  30-Abbd  reads left to right:\n\n"
            "30-   THE NUMBER: Chris's 'double top 40'. Only tracks that make it into the top 80 get one; 1 is best.\n"
            "      No number means the track is outside it: compare those by their letter.\n\n"
            "A     THE LETTER: how well BALANCED the three meters are with each other. A is best, Z is worst.\n\n"
            "bbd   AUTHORITY, POWER, DETAIL: HOW MUCH of each the track has (lows, peaks, highs), each on its own scale:\n"
            "      lowercase s, r, q ... b, a  =  little to plenty      then  capitals A, B, C ...  =  exceptionally strong\n"
            "      (authority and power go up to J, detail up to K; detail starts lower, at v). Capitals are not a warning.\n\n"
            "How they fit together: big capitals are fine if all three are big together. What pulls the LETTER down is one of\n"
            "the three lagging behind the others, so that is the one to look at.\n\n"
            "Examples from a real library:\n"
            "  AbAA   strong across the board and in proportion: letter A\n"
            "  DBdB   huge authority and detail, but power lags at d: letter drops to D\n"
            "  76-Aghi   modest everywhere, but evenly matched: still an A, and in the top 80\n\n"
            "Best possible: 1-AJJK.   Worst: Zssv.\n\n"
            "Directions shows what the meter's own direction line said most of the time during the loud parts of the track.\n"
            "Sorting by Grade follows Chris's order: numbered tracks first (1 to 80), then the rest by letter, A first.";
        auto* w = new juce::AlertWindow("How to read a grade", {}, juce::MessageBoxIconType::NoIcon, this);
        auto* ed = new juce::TextEditor();
        ed->setMultiLine(true); ed->setReadOnly(true); ed->setText(t); ed->setSize(720, 420);
        ed->setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 13.0f, juce::Font::plain));
        w->addCustomComponent(ed);
        w->addButton("Close", 0, juce::KeyPress(juce::KeyPress::escapeKey));
        w->enterModalState(true, juce::ModalCallbackFunction::create([ed](int) { delete ed; }), true);
    }

    const RowRef* selectedRow() const {
        const int r = table.getSelectedRow();
        return (r >= 0 && r < (int)shown.size()) ? &shown[(size_t)r] : nullptr;
    }

    void edited(int row, int colId, const juce::String& text) {
        if (row < 0 || row >= (int)shown.size() || colId < 1 || colId > (int)cols.size()) return;
        auto lib = getLibrary ? getLibrary() : nullptr;
        if (lib == nullptr) return;
        const Column& c = cols[(size_t)colId - 1];
        juce::String v = text.trim();
        if (!c.custom && v == idx.get(shown[(size_t)row].row, c.base)) v = {}; //typing the measured value back = no override
        lib->setUserValue(shown[(size_t)row].id, c.key, v);
        reload();
    }

    void revealTrack(const RowRef& r) {
        const juce::File src(idx.get(r.row, "source_path"));
        if (src.existsAsFile()) src.revealToUser();
        else if (auto lib = getLibrary ? getLibrary() : nullptr) lib->folder.getChildFile("tracks").getChildFile(r.id + ".meta.json").revealToUser();
    }

    void showDetails(const RowRef& r) {
        juce::String t;
        bool y, u;
        for (auto& c : cols) {const auto v = display(r, c, y, u); if (v.isNotEmpty()) t << c.header << ":  " << v << (y ? "   (yours)" : "") << "\n";}
        for (auto& v : mdark::Library::versionIds()) {
            auto gt = grades.find(v); auto rt = gradeRow.find(v);
            if (gt == grades.end() || rt == gradeRow.end()) continue;
            auto it = rt->second.find(r.id);
            if (it == rt->second.end()) {t << "\nMeter " << shortName(v) << ":  not scored yet"; continue;}
            const auto& g = gt->second; const size_t gr = it->second;
            t << "\nMeter " << shortName(v) << ":  " << g.get(gr, "grade") << "    authority / power / detail values " << g.get(gr, "authority_value") << " / "
              << g.get(gr, "power_value") << " / " << g.get(gr, "detail_value") << "    letter value " << g.get(gr, "letter_value") << "    bonus value " << g.get(gr, "bonus_value")
              << "\n      directions: " << g.get(gr, "directions_main") << " most of the loud parts, '" << g.get(gr, "directions_end") << "' at the end";
        }
        if (dupOf.count(r.id)) t << "\n\nPOSSIBLE DUPLICATE of: " << dupOf.at(r.id);
        t << "\nBPM estimate:  " << idx.get(r.row, "bpm_est") << "  (confidence " << idx.get(r.row, "bpm_confidence") << ")";
        t << "\nkey estimate:  " << idx.get(r.row, "key_est") << "  (confidence " << idx.get(r.row, "key_confidence") << ")";
        t << "\n" << idx.get(r.row, "sample_rate") << " Hz, " << idx.get(r.row, "bits") << " bit, " << idx.get(r.row, "channels") << " ch" << (idx.get(r.row, "lossy") == "yes" ? ", LOSSY" : "");
        t << "\ncapture version " << idx.get(r.row, "capture_version") << ",  id " << r.id;
        t << "\n" << idx.get(r.row, "source_path");
        auto* w = new juce::AlertWindow(idx.get(r.row, "file"), {}, juce::MessageBoxIconType::NoIcon, this);
        auto* ed = new juce::TextEditor();
        ed->setMultiLine(true); ed->setReadOnly(true); ed->setText(t); ed->setSize(560, 380);
        ed->setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 13.0f, juce::Font::plain));
        w->addCustomComponent(ed);
        w->addButton("Show file", 1);
        if (dupOf.count(r.id)) w->addButton("Not a duplicate", 2);
        w->addButton("Close", 0, juce::KeyPress(juce::KeyPress::escapeKey));
        const RowRef copy = r;
        w->enterModalState(true, juce::ModalCallbackFunction::create([this, copy, ed](int result) {
            if (result == 1) revealTrack(copy);
            if (result == 2) if (auto lib = getLibrary()) {lib->setUserValue(copy.id, "not_duplicate", "1"); reload();}
            delete ed;
        }), true);
    }

    void addColumnDialog() {
        auto* w = new juce::AlertWindow("Add a column", "A column of your own, for example Streams, Sales, Chart peak, Notes.", juce::MessageBoxIconType::NoIcon, this);
        w->addTextEditor("name", "", "Column name");
        w->addComboBox("type", {"Numbers (sorts as numbers)", "Text"}, "Holds");
        w->addButton("Add", 1, juce::KeyPress(juce::KeyPress::returnKey)); w->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
        w->enterModalState(true, juce::ModalCallbackFunction::create([this, w](int result) {
            if (result != 1) return;
            const auto name = w->getTextEditorContents("name").trim();
            if (name.isEmpty()) return;
            if (auto lib = getLibrary()) {lib->addCustomColumn(name, w->getComboBoxComponent("type")->getSelectedItemIndex() == 0); reload();}
        }), true);
    }

    void columnsMenu() {
        juce::PopupMenu m;
        if (custom.empty()) m.addItem(-1, "No columns of yours yet (use Add column)", false);
        for (int i = 0; i < (int)custom.size(); ++i) {
            juce::PopupMenu sub;
            sub.addItem(1000 + i, "Rename...");
            sub.addItem(2000 + i, "Remove (deletes its values)");
            m.addSubMenu(custom[(size_t)i].name + (custom[(size_t)i].numeric ? "  (numbers)" : "  (text)"), sub);
        }
        m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&columnsButton), [this](int id) {
            if (id >= 1000 && id < 1000 + (int)custom.size()) renameDialog(custom[(size_t)id - 1000]);
            if (id >= 2000 && id < 2000 + (int)custom.size()) removeColumnDialog(custom[(size_t)id - 2000]);
        });
    }
    void renameDialog(const mdark::CustomColumn c) {
        auto* w = new juce::AlertWindow("Rename column", {}, juce::MessageBoxIconType::NoIcon, this);
        w->addTextEditor("name", c.name, "New name");
        w->addButton("Rename", 1, juce::KeyPress(juce::KeyPress::returnKey)); w->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
        w->enterModalState(true, juce::ModalCallbackFunction::create([this, w, c](int result) {
            const auto name = w->getTextEditorContents("name").trim();
            if (result == 1 && name.isNotEmpty()) if (auto lib = getLibrary()) {lib->renameCustomColumn(c.key, name); reload();}
        }), true);
    }
    void removeColumnDialog(const mdark::CustomColumn c) {
        juce::AlertWindow::showOkCancelBox(juce::MessageBoxIconType::WarningIcon, "Remove column", "Remove \"" + c.name + "\" and all the values in it?", "Remove", "Cancel", this,
            juce::ModalCallbackFunction::create([this, c](int result) { if (result == 1) if (auto lib = getLibrary()) {lib->removeCustomColumn(c.key); reload();} }));
    }
    void importDialog() {
        chooser = std::make_unique<juce::FileChooser>("Import values from a CSV (needs a \"file\" column)", juce::File(), "*.csv;*.txt");
        chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this](const juce::FileChooser& fc) {
            const auto f = fc.getResult();
            if (!f.existsAsFile()) return;
            if (auto lib = getLibrary()) {
                const auto report = lib->importCsv(f);
                reload();
                juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon, "Import", report, "OK", this);
            }
        });
    }
    void removeSelected() {
        const auto* r = selectedRow();
        if (r == nullptr) return;
        const juce::String id = r->id, name = idx.get(r->row, "file");
        juce::AlertWindow::showOkCancelBox(juce::MessageBoxIconType::WarningIcon, "Remove from library",
            "Remove \"" + name + "\" from the library? Its captured data and your values for it are deleted. The audio file itself is not touched.", "Remove", "Cancel", this,
            juce::ModalCallbackFunction::create([this, id](int result) { if (result == 1) if (auto lib = getLibrary()) {lib->removeTrack(id); reload();} }));
    }

    juce::TableListBox table;
    juce::TextEditor search;
    juce::TextButton addColumnButton{"Add column..."}, columnsButton{"Your columns..."}, importButton{"Import CSV..."}, showButton{"Show file"}, removeButton{"Remove..."};
    juce::Label countLabel;
    juce::Rectangle<int> hint;
    std::unique_ptr<juce::FileChooser> chooser;
    mdark::Table idx;
    std::map<juce::String, mdark::Table> grades;                       //Meter version -> its grades
    std::map<juce::String, std::map<juce::String, size_t>> gradeRow; //Meter version -> track id -> row in its grades
    juce::String gradeVersion, compareVersion;
    std::map<juce::String, juce::String> dupOf;                      //track id -> the file names it may duplicate
    bool dupOnly = false;
    juce::ComboBox gradeBox, compareBox;
    juce::TextButton filterButton{"Filters..."}, applyButton{"Apply filters"}, tagFillButton{"Fill missing tags..."};
    bool tableUsesFilters = true; //the Full table shows the same tracks as the Overview and the LABs (the filters, the exclusions, one copy of each song)
    const mdark::LevelMode level;                                        //always original: the files are graded as they are
    FilterStore filters;
    int filterGen = 0;
    std::vector<std::pair<size_t, size_t>> dupPairs;                   //rows of index.csv that may be the same song
    juce::Component::SafePointer<juce::DialogWindow> filterWindow;
    static constexpr int gradeRate = 96000; //every track is shown as read after conversion to 96 kHz (the meterDARK plugin's default anchor rate); a track not rescanned yet shows its own readings
    const void* rateLoadedFor = nullptr;
    juce::Label gradeLabel{{}, "Grades from"}, compareLabel{{}, "compare with"};
    juce::TextButton dupButton, howButton{"How to read a grade"};
    std::map<juce::String, std::map<juce::String, juce::String>> user;
    std::vector<mdark::CustomColumn> custom;
    std::vector<Column> cols;
    juce::StringArray colKeys;
    std::vector<RowRef> all, shown;
    int sortCol = 0; bool sortForwards = true;
    int lastChange = -1; const void* lastLib = nullptr;
    bool restoringLayout = false;
    struct HeaderWatcher : public juce::TableHeaderComponent::Listener {
        LibraryView& v; explicit HeaderWatcher(LibraryView& o) : v(o) {}
        void tableColumnsChanged(juce::TableHeaderComponent*) override { v.columnsChangedOrResized(); }
        void tableColumnsResized(juce::TableHeaderComponent*) override { v.columnsChangedOrResized(); }
        void tableSortOrderChanged(juce::TableHeaderComponent*) override {}
    } headerWatcher{*this};
};

//=============================================================================================== "Lab"
#include "LabView.inc"
#include "GlanceView.inc"

//=============================================================================================== the window
class MainComponent : public juce::Component {
public:
    explicit MainComponent(const juce::File& overrideFolder = {}) {
        juce::PropertiesFile::Options o; o.applicationName = "meterDARK Analyser"; o.filenameSuffix = ".settings"; o.folderName = "meterDARK";
        settings = std::make_unique<juce::PropertiesFile>(o);
        setFolder(overrideFolder != juce::File() ? overrideFolder : juce::File(settings->getValue("library", defaultLibrary().getFullPathName())));
        worker.log = [this](const juce::String& l) { addPanel.log(l); };
        worker.status = [this](int d, int t, double p, const juce::String& n) { addPanel.setStatus(d, t, p, n); };
        worker.startThread();
        addPanel.onFiles = [this](const juce::Array<juce::File>& f) { worker.add(f); };
        addPanel.onOpenFolder = [this] { folder.createDirectory(); folder.startAsProcess(); };
        addPanel.onChangeFolder = [this] { chooseLibrary(); };
        addPanel.onRescan = [this] {
            const int n = library != nullptr ? library->tracksNeedingRescan() : 0;
            if (n == 0) {juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon, "Rescan library", "Every track in the library was scanned by this version already.", "OK", this); return;}
            juce::AlertWindow::showOkCancelBox(juce::MessageBoxIconType::QuestionIcon, "Rescan library",
                juce::String(n) + " tracks were scanned by an older version. Rescanning reads each one's audio file again (from where it was when you added it) and adds the readings "
                "at 44.1 kHz and 96 kHz. Your own values (artist, BPM, your columns) are not touched. A track whose file cannot be found is left as it is.\n\n"
                "It runs on several cores and keeps every finished track straight away, so you can close the app and carry on later. Start now?", "Rescan", "Cancel", this,
                juce::ModalCallbackFunction::create([this](int r) { if (r == 1) worker.startRescan(); }));
        };
        libraryView.getLibrary = [this] { return library; };
        labView.getLibrary = [this] { return library; };
        labView.getAllowed = [this] { return libraryView.labIds(); };
        labView.getFilterGen = [this] { return libraryView.filterGeneration(); };
        tabs.addTab("Add tracks", kBg, &addPanel, false);
        libraryTab.glance.getLibrary = [this] { return library; };
        libraryTab.glance.getAllowed = [this] { return libraryView.labIds(); };
        libraryTab.glance.getFilterGen = [this] { return libraryView.filterGeneration(); };
        tabs.addTab("Library", kBg, &libraryTab, false);
        tabs.addTab("hintLAB", kBg, &labView, false);
        gradeLabView.getLibrary = [this] { return library; };
        gradeLabView.getAllowed = [this] { return libraryView.labIds(); };
        gradeLabView.getFilterGen = [this] { return libraryView.filterGeneration(); };
        tabs.addTab("gradeLAB", kBg, &gradeLabView, false);
        tabs.setColour(juce::TabbedComponent::outlineColourId, kBg);
        tabs.setColour(juce::TabbedButtonBar::tabTextColourId, kDim);
        tabs.setColour(juce::TabbedButtonBar::frontTextColourId, kText);
        tabs.setTabBarDepth(32);
        tabs.setCurrentTabIndex(0);
        addAndMakeVisible(tabs);
        setSize(1100, 680);
        checkForUpdate();
    }
    //Each analyser build on GitHub is published as a release "analyser-b<build>". If a newer one exists (for example one that
    //adds a new original Meter version), say so on the Add tracks tab. Nothing is downloaded without you clicking.
    void checkForUpdate() {
        if (MDARK_BUILD_NUMBER <= 0) return; //a local build
        juce::Component::SafePointer<MainComponent> sp(this);
        juce::Thread::launch([sp] {
            auto in = juce::URL("https://api.github.com/repos/Forty8k/Meter/releases?per_page=30")
                          .createInputStream(juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress).withConnectionTimeoutMs(8000)
                                             .withExtraHeaders("Accept: application/vnd.github+json\r\nUser-Agent: meterDARK-Analyser"));
            if (in == nullptr) return;
            const juce::var j = juce::JSON::parse(in->readEntireStreamAsString());
            int best = 0; juce::String title, page;
            if (auto* arr = j.getArray())
                for (auto& r : *arr) {
                    const juce::String tag = r.getProperty("tag_name", "").toString();
                    if (!tag.startsWith("analyser-b")) continue;
                    const int b = tag.fromFirstOccurrenceOf("analyser-b", false, false).getIntValue();
                    if (b > best) {best = b; title = r.getProperty("name", tag).toString(); page = r.getProperty("html_url", "").toString();}
                }
            if (best <= MDARK_BUILD_NUMBER || page.isEmpty()) return;
            juce::MessageManager::callAsync([sp, title, page] { if (sp != nullptr) sp->addPanel.showUpdate(title, juce::URL(page)); });
        });
    }
    ~MainComponent() override { worker.signalThreadShouldExit(); worker.notify(); }
    void paint(juce::Graphics& g) override { g.fillAll(kBg); }
    void resized() override { tabs.setBounds(getLocalBounds()); }
    //for automated checks: pictures of both tabs
    void snapshot(const juce::File& outDir) {
        outDir.createDirectory();
        const char* names[4] = {"tab-add.png", "tab-library.png", "tab-lab.png", "tab-table.png"};
        if (library != nullptr) library->buildLeanRecords(false, false);
        for (int t = 0; t < 4; ++t) {
            tabs.setCurrentTabIndex(t == 3 ? 1 : t);
            if (t == 1 || t == 3) libraryView.reload();
            libraryTab.showFull(t == 3);
            if (t == 1) { libraryTab.glance.reloadNow(); }
            const auto img = createComponentSnapshot(getLocalBounds(), true, 1.0f);
            juce::FileOutputStream os(outDir.getChildFile(names[t]));
            if (os.openedOk()) {os.setPosition(0); os.truncate(); juce::PNGImageFormat().writeImageToStream(img, os);}
        }
    }
    std::shared_ptr<mdark::Library> getLibrary() const { return library; }
    void showLabSoon() { tabs.setCurrentTabIndex(2); } //so the Lab loads its data before the pictures
private:
    void setFolder(const juce::File& f) {
        folder = f;
        library = std::make_shared<mdark::Library>(folder);
        worker.setLibrary(library);
        addPanel.log("Library: " + folder.getFullPathName());
    }
    void chooseLibrary() {
        chooser = std::make_unique<juce::FileChooser>("Choose the library folder", folder);
        chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories, [this](const juce::FileChooser& fc) {
            const auto f = fc.getResult();
            if (f == juce::File()) return;
            settings->setValue("library", f.getFullPathName()); settings->saveIfNeeded();
            setFolder(f);
        });
    }
    std::unique_ptr<juce::PropertiesFile> settings;
    juce::File folder;
    std::shared_ptr<mdark::Library> library;
    AddPanel addPanel;
    LibraryView libraryView;
    LibraryTab libraryTab{libraryView};
    LabView labView{0};
    LabView gradeLabView{1};
    Worker worker{*this};
    juce::TabbedComponent tabs{juce::TabbedButtonBar::TabsAtTop};
    std::unique_ptr<juce::FileChooser> chooser;
};

class AnalyserApp : public juce::JUCEApplication {
public:
    const juce::String getApplicationName() override { return "meterDARK Analyser"; }
    const juce::String getApplicationVersion() override { return "1.2.0 (build " + juce::String(MDARK_BUILD_NUMBER) + ")"; }
    bool moreThanOneInstanceAllowed() override { return true; }
    void initialise(const juce::String& commandLine) override {
        juce::LookAndFeel::getDefaultLookAndFeel().setColour(juce::AlertWindow::backgroundColourId, kPanel);
        juce::LookAndFeel::getDefaultLookAndFeel().setColour(juce::AlertWindow::textColourId, kText);
        juce::LookAndFeel::getDefaultLookAndFeel().setColour(juce::PopupMenu::backgroundColourId, kPanel);
        juce::StringArray args; args.addTokens(commandLine, true); args.trim(); args.removeEmptyStrings();
        const int si = args.indexOf("--snapshot");
        if (si >= 0 && si + 2 < args.size()) { //--snapshot <library folder> <output folder>: draw both tabs to PNG files and quit (for automated checks)
            const juce::File lib(args[si+1].unquoted()), out(args[si+2].unquoted());
            snap = std::make_unique<MainComponent>(lib);
            if (auto l = snap->getLibrary()) { //show a column of yours too
                l->upgradeOldRows();
                if (l->customColumns().empty()) {
                    const auto key = l->addCustomColumn("Streams", true);
                    auto t = l->index();
                    for (size_t r = 0; r < t.rows.size(); ++r) l->setUserValue(t.get(r, "id"), key, juce::String(1000 * (int)(r + 1) * 37 % 9000));
                    if (!t.rows.empty()) l->setUserValue(t.get(0, "id"), "artist", "Edited Artist");
                }
            }
            snap->showLabSoon();
            juce::Timer::callAfterDelay(4000, [this, out] { snap->snapshot(out); snap.reset(); quit(); });
            return;
        }
        window = std::make_unique<Window>();
    }
    void shutdown() override { snap.reset(); window.reset(); }
    void systemRequestedQuit() override { quit(); }
private:
    struct Window : public juce::DocumentWindow {
        Window() : juce::DocumentWindow("meterDARK Analyser", kBg, juce::DocumentWindow::allButtons) {
            setUsingNativeTitleBar(true);
            setContentOwned(new MainComponent(), true);
            setResizable(true, false);
            setResizeLimits(760, 460, 4000, 3000);
            centreWithSize(getWidth(), getHeight());
            setVisible(true);
        }
        void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }
    };
    std::unique_ptr<Window> window;
    std::unique_ptr<MainComponent> snap;
};

} // namespace

START_JUCE_APPLICATION(AnalyserApp)
